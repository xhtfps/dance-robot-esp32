#include "audio_i2s.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define AUDIO_I2S_PORT       I2S_NUM_0
#define AUDIO_I2S_PIN_BCLK   GPIO_NUM_40
#define AUDIO_I2S_PIN_WS     GPIO_NUM_39
#define AUDIO_I2S_PIN_DIN    GPIO_NUM_38
#define AUDIO_I2S_MIC_SLOT   0U /* INMP441 L/R is tied to GND: left slot */
static const char *TAG = "AUDIO_I2S";
static i2s_chan_handle_t s_rx_channel;
static int32_t s_pcm_raw[AUDIO_I2S_DMA_FRAMES * 2U];
static bool s_initialized;

esp_err_t audio_i2s_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 6;
    channel_config.dma_frame_num = AUDIO_I2S_DMA_FRAMES;
    esp_err_t err = i2s_new_channel(&channel_config, NULL, &s_rx_channel);

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_I2S_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_PIN_BCLK,
            .ws = AUDIO_I2S_PIN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = AUDIO_I2S_PIN_DIN,
            .invert_flags = {0},
        },
    };
    if (err == ESP_OK) {
        err = i2s_channel_init_std_mode(s_rx_channel, &std_config);
    }
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_rx_channel);
    }
    if (err != ESP_OK) {
        if (s_rx_channel != NULL) {
            i2s_channel_disable(s_rx_channel);
        }
        if (s_rx_channel != NULL) {
            i2s_del_channel(s_rx_channel);
            s_rx_channel = NULL;
        }
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "I2S RX ready: rate=%u BCLK=%d WS=%d DIN=%d",
             AUDIO_I2S_SAMPLE_RATE_HZ, AUDIO_I2S_PIN_BCLK, AUDIO_I2S_PIN_WS,
             AUDIO_I2S_PIN_DIN);
    return ESP_OK;
}

esp_err_t audio_i2s_read_raw(int32_t *samples, size_t sample_capacity,
                             size_t *samples_read, uint32_t timeout_ms)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (samples == NULL || samples_read == NULL || sample_capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_rx_channel, samples,
                                     sample_capacity * sizeof(int32_t), &bytes_read,
                                     pdMS_TO_TICKS(timeout_ms));
    *samples_read = bytes_read / sizeof(int32_t);
    return err;
}

esp_err_t audio_i2s_read_pcm16(int16_t *samples, size_t sample_capacity,
                               size_t *samples_read, uint32_t timeout_ms)
{
    if (samples == NULL || samples_read == NULL || sample_capacity == 0U ||
        sample_capacity > AUDIO_I2S_DMA_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t raw_count = 0;
    esp_err_t err = audio_i2s_read_raw(s_pcm_raw, sample_capacity * 2U,
                                       &raw_count, timeout_ms);
    if (err != ESP_OK) {
        *samples_read = 0;
        return err;
    }

    const size_t frame_count = raw_count / 2U;
    for (size_t i = 0; i < frame_count; ++i) {
        /* INMP441 drives only the slot selected by its L/R pin. Averaging the
         * active slot with the silent slot costs 6 dB and hurts real-room SNR. */
        samples[i] = (int16_t)(s_pcm_raw[i * 2U + AUDIO_I2S_MIC_SLOT] >> 16);
    }
    *samples_read = frame_count;
    return ESP_OK;
}
