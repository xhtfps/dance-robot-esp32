#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_I2S_SAMPLE_RATE_HZ 16000U
#define AUDIO_I2S_SLOT_BITS      32U
#define AUDIO_I2S_DMA_FRAMES     320U

esp_err_t audio_i2s_init(void);
esp_err_t audio_i2s_read_raw(int32_t *samples, size_t sample_capacity,
                             size_t *samples_read, uint32_t timeout_ms);
esp_err_t audio_i2s_read_pcm16(int16_t *samples, size_t sample_capacity,
                               size_t *samples_read, uint32_t timeout_ms);
