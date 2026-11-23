#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef esp_err_t (*web_pose_action_callback_t)(uint8_t visual_message_id);

esp_err_t web_set_pose_action_callback(web_pose_action_callback_t callback);
esp_err_t web_start(void);
esp_err_t web_stop(void);
