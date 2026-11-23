#include "competition_controller.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define COMPETITION_TOTAL_LIMIT_US  (15ULL * 60ULL * 1000000ULL)
#define COMPETITION_STAGE_LIMIT_US  (5ULL * 60ULL * 1000000ULL)

static const char *TAG = "COMPETITION";
static SemaphoreHandle_t s_mutex;
static competition_status_t s_status;
static int64_t s_total_start_us;
static int64_t s_stage_start_us;
static bool s_visual_armed;
static bool s_initialized;

static bool lock_controller(void)
{
    return s_mutex != NULL &&
           xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void unlock_controller(void)
{
    xSemaphoreGive(s_mutex);
}

static bool stage_is_valid(uint8_t stage)
{
    return stage < 3U;
}

static void refresh_timeout_locked(void)
{
    if (s_status.state == COMPETITION_IDLE ||
        s_status.state == COMPETITION_COMPLETE ||
        s_status.state == COMPETITION_ABORTED ||
        s_status.state == COMPETITION_FAULT) {
        return;
    }

    const int64_t now = esp_timer_get_time();
    if (s_total_start_us > 0 && now - s_total_start_us >= COMPETITION_TOTAL_LIMIT_US) {
        s_status.state = COMPETITION_ABORTED;
        ESP_LOGW(TAG, "Total competition time exceeded");
        return;
    }
    if (s_stage_start_us > 0 && now - s_stage_start_us >= COMPETITION_STAGE_LIMIT_US) {
        s_status.state = COMPETITION_ABORTED;
        ESP_LOGW(TAG, "Stage time exceeded: stage=%u",
                 (unsigned)s_status.current_stage);
    }
}

static void update_elapsed_locked(void)
{
    const int64_t now = esp_timer_get_time();
    if (s_total_start_us > 0) {
        s_status.total_elapsed_ms = (uint32_t)((now - s_total_start_us) / 1000LL);
    }
    if (s_stage_start_us > 0) {
        s_status.stage_elapsed_ms = (uint32_t)((now - s_stage_start_us) / 1000LL);
    }
}

static esp_err_t accept_recognition_locked(uint8_t stage, uint8_t id)
{
    refresh_timeout_locked();
    if (s_status.state != COMPETITION_RUNNING_RECOGNITION ||
        s_status.current_stage != stage || id == 0xFFU) {
        return ESP_ERR_INVALID_STATE;
    }
    s_status.state = COMPETITION_DISPATCH_ACTION;
    ESP_LOGI(TAG, "Recognition accepted: stage=%u id=%u",
             (unsigned)stage, (unsigned)id);
    return ESP_OK;
}

esp_err_t competition_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = COMPETITION_IDLE;
    s_status.current_stage = 0xFFU;
    s_status.current_item = 0U;
    s_status.last_music_id = 0xFFU;
    s_total_start_us = 0;
    s_stage_start_us = 0;
    s_visual_armed = false;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t competition_on_prepare_command(void)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_status.state != COMPETITION_IDLE) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t now = esp_timer_get_time();
    s_total_start_us = now;
    s_status.total_elapsed_ms = 0;
    s_status.stage_elapsed_ms = 0;
    s_status.current_stage = 0xFFU;
    s_status.current_item = 0U;
    s_status.last_music_id = 0xFFU;
    memset(s_status.stage_seen, 0, sizeof(s_status.stage_seen));
    s_visual_armed = false;
    s_status.state = COMPETITION_READY_FOR_STAGE;
    unlock_controller();
    ESP_LOGI(TAG, "Competition prepared; waiting for stage command");
    return ESP_OK;
}

esp_err_t competition_on_stage_command(uint8_t stage_event_id)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();

    const bool stage_running =
        s_status.state == COMPETITION_RUNNING_RECOGNITION ||
        s_status.state == COMPETITION_MUSIC_WAIT_COMMAND ||
        s_status.state == COMPETITION_MUSIC_RECOGNITION ||
        s_status.state == COMPETITION_MUSIC_ANNOUNCE_DELAY ||
        s_status.state == COMPETITION_MUSIC_PERFORMANCE;
    if ((!stage_running && s_status.state != COMPETITION_READY_FOR_STAGE) ||
        !stage_is_valid(stage_event_id) || s_status.stage_seen[stage_event_id]) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }

    s_status.current_stage = stage_event_id;
    s_status.current_item = 0U;
    s_status.last_music_id = 0xFFU;
    s_status.stage_seen[stage_event_id] = true;
    s_stage_start_us = esp_timer_get_time();
    // Entering the visual stage does not arm recognition. The separate
    // SU-03T 0x11 command must explicitly arm one visual recognition.
    s_visual_armed = false;
    s_status.state = stage_event_id == COMPETITION_STAGE_MUSIC
                         ? COMPETITION_MUSIC_WAIT_COMMAND
                         : COMPETITION_RUNNING_RECOGNITION;
    unlock_controller();
    ESP_LOGI(TAG, "Stage started: %u", (unsigned)stage_event_id);
    return ESP_OK;
}

esp_err_t competition_on_visual_arm_command(void)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();
    if (s_status.state != COMPETITION_RUNNING_RECOGNITION ||
        s_status.current_stage != COMPETITION_STAGE_VISION) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }

    s_visual_armed = true;
    unlock_controller();
    ESP_LOGI(TAG, "Visual recognition armed by 0x11");
    return ESP_OK;
}

esp_err_t competition_on_music_recognition_start(void)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();
    if (s_status.state != COMPETITION_MUSIC_WAIT_COMMAND ||
        s_status.current_stage != COMPETITION_STAGE_MUSIC) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }

    s_status.last_music_id = 0xFFU;
    s_status.state = COMPETITION_MUSIC_RECOGNITION;
    unlock_controller();
    ESP_LOGI(TAG, "Music recognition started");
    return ESP_OK;
}

esp_err_t competition_on_voice_recognized(uint8_t id)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = accept_recognition_locked(COMPETITION_STAGE_VOICE, id);
    unlock_controller();
    return err;
}

esp_err_t competition_on_visual_recognized(uint8_t id)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();
    if (s_status.state != COMPETITION_RUNNING_RECOGNITION ||
        s_status.current_stage != COMPETITION_STAGE_VISION ||
        id == 0xFFU || !s_visual_armed) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }
    s_visual_armed = false;
    s_status.state = COMPETITION_DISPATCH_ACTION;
    ESP_LOGI(TAG, "Visual recognition accepted: id=%u", (unsigned)id);
    const esp_err_t err = ESP_OK;
    unlock_controller();
    return err;
}

esp_err_t competition_on_music_recognized(uint8_t id)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();
    if (s_status.state != COMPETITION_MUSIC_RECOGNITION ||
        id < 1U || id > 6U) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }
    s_status.last_music_id = id;
    s_status.state = COMPETITION_MUSIC_ANNOUNCE_DELAY;
    ESP_LOGI(TAG, "Music recognized: id=%u", (unsigned)id);
    unlock_controller();
    return ESP_OK;
}

esp_err_t competition_on_music_recognition_failed(void)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();
    if (s_status.state != COMPETITION_MUSIC_RECOGNITION ||
        s_status.current_stage != COMPETITION_STAGE_MUSIC) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }
    s_status.last_music_id = 0xFFU;
    s_status.state = COMPETITION_MUSIC_WAIT_COMMAND;
    unlock_controller();
    ESP_LOGW(TAG, "Music recognition failed; waiting for retry command");
    return ESP_OK;
}

esp_err_t competition_on_music_announcement_failed(void)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();
    if (s_status.state != COMPETITION_MUSIC_ANNOUNCE_DELAY ||
        s_status.current_stage != COMPETITION_STAGE_MUSIC) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }

    s_status.last_music_id = 0xFFU;
    s_status.state = COMPETITION_MUSIC_WAIT_COMMAND;
    unlock_controller();
    ESP_LOGW(TAG, "Music announcement failed; waiting for retry command");
    return ESP_OK;
}

esp_err_t competition_on_music_performance_start(uint8_t music_id)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();
    if (s_status.state != COMPETITION_MUSIC_ANNOUNCE_DELAY ||
        s_status.current_stage != COMPETITION_STAGE_MUSIC ||
        music_id != s_status.last_music_id) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }

    s_status.state = COMPETITION_MUSIC_PERFORMANCE;
    unlock_controller();
    ESP_LOGI(TAG, "Music performance started: music_id=%u",
             (unsigned)music_id);
    return ESP_OK;
}

esp_err_t competition_on_music_performance_failed(void)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_status.state != COMPETITION_MUSIC_PERFORMANCE) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }

    s_status.last_music_id = 0xFFU;
    s_status.state = COMPETITION_MUSIC_WAIT_COMMAND;
    unlock_controller();
    return ESP_OK;
}

esp_err_t competition_on_action_done(bool success)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();
    if (s_status.state != COMPETITION_DISPATCH_ACTION &&
        s_status.state != COMPETITION_WAIT_ACTION_DONE) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }
    if (!success) {
        s_status.state = COMPETITION_RUNNING_RECOGNITION;
        unlock_controller();
        return ESP_OK;
    }

    s_status.current_item++;
    s_status.state = COMPETITION_RUNNING_RECOGNITION;
    unlock_controller();
    return ESP_OK;
}

esp_err_t competition_on_music_performance_done(void)
{
    if (!s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();
    if (s_status.state != COMPETITION_MUSIC_PERFORMANCE) {
        unlock_controller();
        return ESP_ERR_INVALID_STATE;
    }
    s_status.current_item++;
    s_status.state = s_status.last_music_id == 6U
                         ? COMPETITION_COMPLETE
                         : COMPETITION_MUSIC_WAIT_COMMAND;
    unlock_controller();
    return ESP_OK;
}

esp_err_t competition_get_status(competition_status_t *out)
{
    if (out == NULL || !s_initialized || !lock_controller()) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_timeout_locked();
    update_elapsed_locked();
    memcpy(out, &s_status, sizeof(*out));
    unlock_controller();
    return ESP_OK;
}
