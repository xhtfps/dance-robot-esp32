#include "audio_i2s.h"
#include "arduino_command_map.h"
#include "camera.h"
#include "competition_controller.h"
#include "led.h"
#include "music_recognizer.h"
#include "pose_inference.h"
#include "serial_bridge.h"
#include "web.h"
#include "wifi.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

enum {
    VISION_MODE_PC = 0,
    VISION_MODE_LOCAL,
};

#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define BOOT_BUTTON_POLL_MS 10U

static volatile bool s_local_pose_stop_requested;
static TaskHandle_t s_local_pose_task_handle;
static int s_vision_mode = VISION_MODE_PC;
static bool s_pc_pose_started;
static volatile uint8_t s_pending_arduino_command;
static volatile bool s_waiting_arduino_ack;
static volatile uint32_t s_arduino_ack_count;
static volatile int64_t s_arduino_action_started_us;
static volatile uint32_t s_pending_ack_timeout_ms;
static volatile uint8_t s_pending_action_kind;

static esp_err_t stop_pc_pose(void);
static esp_err_t dispatch_arduino_command(uint8_t arduino_byte,
                                          uint8_t action_kind);
static esp_err_t dispatch_visual_action(uint8_t visual_message_id);
static bool visual_message_id_from_pose(pose_id_t pose_id,
                                        uint8_t *visual_message_id);
static void on_music_recognizer_done(
    const music_recognition_result_t *result, void *context);
static void music_dance_delay_task(void *context);

#define SU03_TASK1_COMMAND 0x44U
#define SU03_TASK2_COMMAND 0x55U
#define SU03_TASK3_COMMAND 0x66U
#define SU03_LEAR_EYES_COMMAND 0x22U
#define SU03_LEAR_MUSIC_COMMAND 0x33U
#define ARDUINO_ACTION_ERROR 0xFEU
#define MUSIC_DANCE_DELAY_MS 5000U
#define ARDUINO_OFFICIAL_DANCE_COMMAND 0x44U
#define ARDUINO_CUSTOM_DANCE_COMMAND 0x45U

enum {
    ARDUINO_ACTION_STANDARD = 0,
    ARDUINO_ACTION_MUSIC_DANCE,
};

static void on_su03_stage_command(uint8_t command)
{
    if (command == SU03_LEAR_EYES_COMMAND) {
        const esp_err_t err = competition_on_visual_arm_command();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Visual arm command 0x22 rejected: %s",
                     esp_err_to_name(err));
        }
        return;
    }

    if (command == SU03_LEAR_MUSIC_COMMAND) {
        const esp_err_t err = competition_on_music_recognition_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Music recognition command 0x33 rejected: %s",
                     esp_err_to_name(err));
            return;
        }
        const esp_err_t start_err = music_recognizer_start();
        if (start_err != ESP_OK) {
            ESP_LOGE(TAG, "Cannot start music recognizer: %s",
                     esp_err_to_name(start_err));
            const esp_err_t reset_err =
                competition_on_music_recognition_failed();
            if (reset_err != ESP_OK) {
                ESP_LOGW(TAG, "Cannot restore music retry state: %s",
                         esp_err_to_name(reset_err));
            }
        }
        return;
    }

    uint8_t stage = 0xFFU;
    if (command == SU03_TASK1_COMMAND) {
        stage = COMPETITION_STAGE_VOICE;
    } else if (command == SU03_TASK2_COMMAND) {
        stage = COMPETITION_STAGE_VISION;
    } else if (command == SU03_TASK3_COMMAND) {
        stage = COMPETITION_STAGE_MUSIC;
    } else {
        return;
    }

    competition_status_t status = {0};
    if (competition_get_status(&status) == ESP_OK &&
        status.state == COMPETITION_IDLE) {
        competition_on_prepare_command();
    }
    const esp_err_t err = competition_on_stage_command(stage);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Stage command 0x%02X rejected: %s",
                 (unsigned)command, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "SU-03T selected stage=%u", (unsigned)stage);
}

static esp_err_t dispatch_arduino_command(uint8_t arduino_byte,
                                          uint8_t action_kind)
{
    if (s_waiting_arduino_ack) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t timeout_ms =
        arduino_command_ack_timeout_ms(arduino_byte);
    if (timeout_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    s_pending_arduino_command = arduino_byte;
    s_pending_action_kind = action_kind;
    s_arduino_action_started_us = esp_timer_get_time();
    s_pending_ack_timeout_ms = timeout_ms;
    s_waiting_arduino_ack = true;

    const esp_err_t err = serial_bridge_send_to_arduino(arduino_byte);
    if (err != ESP_OK) {
        s_waiting_arduino_ack = false;
        return err;
    }

    return ESP_OK;
}

static void music_dance_delay_task(void *context)
{
    const uint8_t music_id = (uint8_t)(uintptr_t)context;
    vTaskDelay(pdMS_TO_TICKS(MUSIC_DANCE_DELAY_MS));

    const esp_err_t start_err =
        competition_on_music_performance_start(music_id);
    if (start_err == ESP_OK) {
        const uint8_t command = music_id <= 5U
                                    ? ARDUINO_OFFICIAL_DANCE_COMMAND
                                    : ARDUINO_CUSTOM_DANCE_COMMAND;
        const esp_err_t dispatch_err = dispatch_arduino_command(
            command, ARDUINO_ACTION_MUSIC_DANCE);
        if (dispatch_err != ESP_OK) {
            ESP_LOGE(TAG,
                     "Cannot dispatch automatic dance for music id=%u: %s",
                     (unsigned)music_id, esp_err_to_name(dispatch_err));
            competition_on_music_performance_failed();
        }
    } else {
        ESP_LOGW(TAG, "Music delay expired outside ready state: id=%u error=%s",
                 (unsigned)music_id, esp_err_to_name(start_err));
    }

    vTaskDelete(NULL);
}

static void on_music_recognizer_done(
    const music_recognition_result_t *result, void *context)
{
    (void)context;
    if (result == NULL || result->status != ESP_OK ||
        result->music_id < 1U || result->music_id > 6U) {
        const esp_err_t reset_err = competition_on_music_recognition_failed();
        if (reset_err != ESP_OK) {
            ESP_LOGW(TAG, "Music failure result rejected by state: %s",
                     esp_err_to_name(reset_err));
        }
        return;
    }

    const esp_err_t accepted =
        competition_on_music_recognized(result->music_id);
    if (accepted != ESP_OK) {
        ESP_LOGW(TAG, "Music result rejected by state: id=%u error=%s",
                 (unsigned)result->music_id, esp_err_to_name(accepted));
        return;
    }

    const uint8_t message_id = (uint8_t)(6U + result->music_id);
    const esp_err_t speech_err =
        serial_bridge_send_to_su03_message(message_id);
    if (speech_err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot request music speech: message_id=%u error=%s",
                 (unsigned)message_id, esp_err_to_name(speech_err));
        competition_on_music_announcement_failed();
        return;
    }

    ESP_LOGI(TAG,
             "Music recognized: id=%u confidence=%.3f speech=%u; dance in %u ms",
             (unsigned)result->music_id, result->confidence,
             (unsigned)message_id, (unsigned)MUSIC_DANCE_DELAY_MS);
    const BaseType_t created = xTaskCreate(
        music_dance_delay_task, "music_delay", 3072,
        (void *)(uintptr_t)result->music_id, 3, NULL);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Cannot create music delay task");
        competition_on_music_announcement_failed();
    }
}

static esp_err_t dispatch_visual_action(uint8_t visual_message_id)
{
    uint8_t arduino_byte = 0U;
    if (!arduino_command_from_visual_message(visual_message_id,
                                             &arduino_byte)) {
        ESP_LOGW(TAG, "No Arduino action mapping for visual message id=%u",
                 (unsigned)visual_message_id);
        return ESP_ERR_NOT_FOUND;
    }

    const esp_err_t err = dispatch_arduino_command(
        arduino_byte, ARDUINO_ACTION_STANDARD);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Visual action dispatched: visual_id=%u Arduino=0x%02X",
                 (unsigned)visual_message_id, (unsigned)arduino_byte);
    }
    return err;
}

static bool visual_message_id_from_pose(pose_id_t pose_id,
                                        uint8_t *visual_message_id)
{
    if (visual_message_id == NULL) {
        return false;
    }

    switch (pose_id) {
    case POSE_BIG_STAND:
        *visual_message_id = 1U;
        return true;
    case POSE_DOUBLE_HANDS:
        *visual_message_id = 2U;
        return true;
    case POSE_SQUAT:
        *visual_message_id = 3U;
        return true;
    case POSE_BOW_STEP:
        *visual_message_id = 4U;
        return true;
    case POSE_HANDS_ON_HIPS:
        *visual_message_id = 5U;
        return true;
    case POSE_SALUTE:
        *visual_message_id = 6U;
        return true;
    default:
        return false;
    }
}

static void serial_log_byte(const char *source, uint8_t byte)
{
    ESP_LOGI(TAG, "%s byte: 0x%02X (%u)", source, (unsigned)byte,
             (unsigned)byte);
}

static void on_su03_byte(uint8_t byte)
{
    serial_log_byte("SU-03T RX", byte);
    on_su03_stage_command(byte);

    uint8_t arduino_byte = 0;
    if (!arduino_command_from_su03_action(byte, &arduino_byte)) {
        return;
    }

    if (s_waiting_arduino_ack) {
        ESP_LOGW(TAG, "Ignoring SU-03T action while Arduino action is pending: 0x%02X",
                 (unsigned)byte);
        return;
    }

    const esp_err_t accepted = competition_on_voice_recognized(byte);
    if (accepted != ESP_OK) {
        ESP_LOGW(TAG, "Voice event not accepted by current competition state: %s",
                 esp_err_to_name(accepted));
        return;
    }

    const esp_err_t err = dispatch_arduino_command(
        arduino_byte, ARDUINO_ACTION_STANDARD);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot forward Arduino command 0x%02X from SU-03T 0x%02X: %s",
                 (unsigned)arduino_byte, (unsigned)byte, esp_err_to_name(err));
        competition_on_action_done(false);
        return;
    }

    ESP_LOGI(TAG, "Forwarded Arduino command: 0x%02X from SU-03T 0x%02X; waiting for completion",
             (unsigned)arduino_byte, (unsigned)byte);
}

static void on_su03_message(uint8_t message_id)
{
    ESP_LOGI(TAG, "SU-03T framed message: id=%u", (unsigned)message_id);
}

static void on_arduino_byte(uint8_t byte)
{
    if (byte == ARDUINO_ACTION_ERROR) {
        if (!s_waiting_arduino_ack) {
            ESP_LOGW(TAG, "Arduino reported an action error without a pending action");
            return;
        }
        s_waiting_arduino_ack = false;
        ESP_LOGW(TAG, "Arduino reported action failure: command=0x%02X",
                 (unsigned)s_pending_arduino_command);
        if (s_pending_action_kind == ARDUINO_ACTION_MUSIC_DANCE) {
            competition_on_music_performance_failed();
        } else {
            competition_on_action_done(false);
        }
        return;
    }

    if (!arduino_command_is_known(byte)) {
        ESP_LOGW(TAG, "Arduino ACK unknown: 0x%02X (%u)",
                 (unsigned)byte, (unsigned)byte);
        return;
    }

    if (s_waiting_arduino_ack && byte == s_pending_arduino_command) {
        s_waiting_arduino_ack = false;
        ++s_arduino_ack_count;
        ESP_LOGI(TAG, "Arduino action complete: command=0x%02X ('%c'), count=%u",
                 (unsigned)byte, (char)byte, (unsigned)s_arduino_ack_count);
        if (s_pending_action_kind == ARDUINO_ACTION_MUSIC_DANCE) {
            competition_on_music_performance_done();
        } else {
            competition_on_action_done(true);
        }
        return;
    }

    if (s_waiting_arduino_ack) {
        ESP_LOGW(TAG, "Arduino ACK MISMATCH: received=0x%02X expected=0x%02X",
                 (unsigned)byte, (unsigned)s_pending_arduino_command);
    }
}

static void arduino_action_watchdog_task(void *context)
{
    (void)context;

    while (true) {
        if (s_waiting_arduino_ack) {
            const int64_t elapsed_us = esp_timer_get_time() -
                                       s_arduino_action_started_us;
            if (elapsed_us >=
                (int64_t)s_pending_ack_timeout_ms * 1000LL) {
                s_waiting_arduino_ack = false;
                ESP_LOGW(TAG, "Arduino action timeout: command=0x%02X",
                         (unsigned)s_pending_arduino_command);
                if (s_pending_action_kind == ARDUINO_ACTION_MUSIC_DANCE) {
                    competition_on_music_performance_failed();
                } else {
                    competition_on_action_done(false);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void init_competition_serial(void)
{
    ESP_ERROR_CHECK(competition_init());
    ESP_ERROR_CHECK(web_set_pose_action_callback(dispatch_visual_action));

    const serial_bridge_callbacks_t callbacks = {
        .on_su03_byte = on_su03_byte,
        .on_su03_message = on_su03_message,
        .on_arduino_byte = on_arduino_byte,
    };
    ESP_ERROR_CHECK(serial_bridge_init(&callbacks));
}

static void local_pose_task(void *context)
{
    (void)context;
    while (!s_local_pose_stop_requested) {
        camera_fb_t *frame = camera_get_frame();
        if (frame != NULL) {
            pose_result_t result;
            esp_err_t err = pose_inference_run_jpeg(frame->buf, frame->len, &result);
            camera_return_frame(frame);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Local pose inference failed: %s", esp_err_to_name(err));
            } else if (result.stable && !s_waiting_arduino_ack) {
                uint8_t visual_message_id = 0U;
                if (visual_message_id_from_pose(result.stable_pose_id,
                                                &visual_message_id) &&
                    competition_on_visual_recognized(visual_message_id) == ESP_OK) {
                    const bool speech_sent =
                        serial_bridge_send_to_su03_message(visual_message_id) == ESP_OK;
                    if (!speech_sent) {
                        ESP_LOGW(TAG, "Cannot send local visual speech: id=%u",
                                 (unsigned)visual_message_id);
                    }

                    const esp_err_t action_err =
                        dispatch_visual_action(visual_message_id);
                    if (action_err != ESP_OK) {
                        ESP_LOGW(TAG, "Local visual action dispatch failed: %s",
                                 esp_err_to_name(action_err));
                        competition_on_action_done(false);
                    }
                }
            }
        }
        if (!s_local_pose_stop_requested) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    s_local_pose_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t start_local_pose(void)
{
    if (s_local_pose_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_pc_pose_started) {
        esp_err_t stop_err = stop_pc_pose();
        if (stop_err != ESP_OK) {
            return stop_err;
        }
    }

    esp_err_t err = camera_init();
    if (err != ESP_OK) {
        return err;
    }

    err = pose_inference_init(CONFIG_POSE_TEAM_ID, CONFIG_POSE_STABLE_FRAMES);
    if (err != ESP_OK) {
        camera_deinit();
        return err;
    }

    s_local_pose_stop_requested = false;
    BaseType_t created = xTaskCreatePinnedToCore(local_pose_task, "local_pose",
                                                 8192, NULL, 4,
                                                 &s_local_pose_task_handle, 1);
    if (created != pdPASS) {
        pose_inference_deinit();
        camera_deinit();
        s_local_pose_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_vision_mode = VISION_MODE_LOCAL;
    led_set_preset_color(LED_COLOR_GREEN);
    ESP_LOGI(TAG, "Vision mode: LOCAL; team=%d; press BOOT for PC mode",
             CONFIG_POSE_TEAM_ID);
    return ESP_OK;
}

static esp_err_t stop_local_pose(void)
{
    if (s_local_pose_task_handle == NULL) {
        s_vision_mode = VISION_MODE_PC;
        return ESP_OK;
    }

    s_local_pose_stop_requested = true;
    while (s_local_pose_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    pose_inference_deinit();
    esp_err_t err = camera_deinit();
    s_vision_mode = VISION_MODE_PC;
    led_set_preset_color(LED_COLOR_BLUE);
    ESP_LOGI(TAG, "Vision mode: PC; local camera and model stopped");
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

static esp_err_t start_pc_pose(void)
{
    if (s_pc_pose_started) {
        return ESP_OK;
    }

    esp_err_t err = camera_init();
    if (err != ESP_OK) {
        return err;
    }

    err = wifi_start();
    if (err != ESP_OK) {
        camera_deinit();
        return err;
    }

    err = web_start();
    if (err != ESP_OK) {
        wifi_stop();
        camera_deinit();
        return err;
    }

    s_pc_pose_started = true;
    s_vision_mode = VISION_MODE_PC;
    led_set_preset_color(LED_COLOR_BLUE);
    ESP_LOGI(TAG, "Vision mode: PC; stream=http://192.168.4.1:81/stream; press BOOT for local pose");
    return ESP_OK;
}

static esp_err_t stop_pc_pose(void)
{
    if (!s_pc_pose_started) {
        return ESP_OK;
    }

    esp_err_t err = web_stop();
    esp_err_t wifi_err = wifi_stop();
    esp_err_t camera_err = camera_deinit();
    s_pc_pose_started = false;
    s_vision_mode = VISION_MODE_PC;
    if (err != ESP_OK) {
        return err;
    }
    if (wifi_err != ESP_OK) {
        return wifi_err;
    }
    return camera_err == ESP_ERR_INVALID_STATE ? ESP_OK : camera_err;
}

static void toggle_vision_mode(void)
{
    if (s_vision_mode == VISION_MODE_PC) {
        esp_err_t err = start_local_pose();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Cannot enter local vision mode: %s",
                     esp_err_to_name(err));
            if (!s_pc_pose_started) {
                esp_err_t restart_err = start_pc_pose();
                if (restart_err != ESP_OK) {
                    ESP_LOGE(TAG, "Cannot restore PC vision mode: %s",
                             esp_err_to_name(restart_err));
                }
            }
            led_set_preset_color(LED_COLOR_BLUE);
        }
        return;
    }

    esp_err_t err = stop_local_pose();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot stop local vision mode: %s",
                 esp_err_to_name(err));
    }
    err = start_pc_pose();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start PC vision mode: %s", esp_err_to_name(err));
    }
}

static void boot_button_task(void *context)
{
    (void)context;

    int last_level = gpio_get_level(BOOT_BUTTON_GPIO);
    int stable_level = last_level;
    unsigned stable_samples = 0U;
    bool armed = stable_level != 0;

    while (true) {
        int level = gpio_get_level(BOOT_BUTTON_GPIO);
        if (level != last_level) {
            last_level = level;
            stable_samples = 0U;
        } else if (stable_samples < 3U) {
            ++stable_samples;
        }

        if (stable_samples >= 3U && stable_level != last_level) {
            stable_level = last_level;
            if (stable_level != 0) {
                armed = true;
            } else if (armed) {
                armed = false;
                toggle_vision_mode();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BOOT_BUTTON_POLL_MS));
    }
}

static void init_boot_button(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
}

#if CONFIG_POSE_PERIPHERAL_SELF_TEST
static void run_audio_self_test(void)
{
    static int16_t samples[320];
    size_t samples_read = 0;
    esp_err_t err = audio_i2s_read_pcm16(samples, 320U, &samples_read, 1000U);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "INMP441 capture test failed: %s", esp_err_to_name(err));
        return;
    }
    int32_t peak = 0;
    uint64_t magnitude_sum = 0U;
    size_t nonzero_samples = 0U;
    size_t clipped_samples = 0U;
    for (size_t i = 0; i < samples_read; ++i) {
        int32_t magnitude = samples[i];
        if (magnitude < 0) {
            magnitude = -magnitude;
        }
        magnitude_sum += (uint32_t)magnitude;
        if (magnitude != 0) {
            ++nonzero_samples;
        }
        if (magnitude >= 32760) {
            ++clipped_samples;
        }
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    const uint32_t mean_abs = samples_read == 0U
                                  ? 0U
                                  : (uint32_t)(magnitude_sum / samples_read);
    ESP_LOGI(TAG,
             "INMP441 capture test: samples=%u peak=%ld mean_abs=%u nonzero=%u clipped=%u",
             (unsigned)samples_read, (long)peak, (unsigned)mean_abs,
             (unsigned)nonzero_samples, (unsigned)clipped_samples);
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Reset reason=%d free_heap=%u", (int)esp_reset_reason(),
             (unsigned)esp_get_free_heap_size());

    led_init();
    ESP_ERROR_CHECK(audio_i2s_init());
    ESP_ERROR_CHECK(music_recognizer_init(on_music_recognizer_done, NULL));
    init_competition_serial();
#if CONFIG_POSE_PERIPHERAL_SELF_TEST
    run_audio_self_test();
#endif

    ESP_ERROR_CHECK(wifi_init());
    esp_err_t pc_err = start_pc_pose();
    if (pc_err != ESP_OK) {
        ESP_LOGE(TAG, "PC camera/Wi-Fi mode failed: %s", esp_err_to_name(pc_err));
        led_set_preset_color(LED_COLOR_BLUE);
    }

    init_boot_button();
    if (pc_err != ESP_OK) {
        ESP_LOGI(TAG, "Vision mode unavailable; press BOOT to retry");
    }

    BaseType_t created = xTaskCreate(boot_button_task, "boot_mode",
                                     3072, NULL, 3, NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    created = xTaskCreate(arduino_action_watchdog_task, "action_watchdog",
                          3072, NULL, 3, NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
