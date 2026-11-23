#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_err_t status;
    uint8_t music_id;
    float confidence;
} music_recognition_result_t;

typedef void (*music_recognizer_done_cb_t)(
    const music_recognition_result_t *result, void *context);

esp_err_t music_recognizer_init(music_recognizer_done_cb_t callback,
                                void *context);
esp_err_t music_recognizer_start(void);
esp_err_t music_recognizer_stop(void);
esp_err_t music_recognizer_get_last(uint8_t *music_id, float *confidence);

#ifdef __cplusplus
}
#endif
