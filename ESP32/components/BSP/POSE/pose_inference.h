#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "pose_classifier.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pose_inference_init(uint8_t team_id, uint8_t stable_frames);
esp_err_t pose_inference_run_jpeg(const uint8_t *jpeg_data, size_t jpeg_size,
                                  pose_result_t *out);
void pose_inference_deinit(void);

#ifdef __cplusplus
}
#endif
