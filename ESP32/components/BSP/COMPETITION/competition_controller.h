#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COMPETITION_STAGE_VOICE = 0,
    COMPETITION_STAGE_VISION = 1,
    COMPETITION_STAGE_MUSIC = 2,
} competition_stage_t;

typedef enum {
    COMPETITION_IDLE,
    COMPETITION_READY_FOR_STAGE,
    COMPETITION_RUNNING_RECOGNITION,
    COMPETITION_DISPATCH_ACTION,
    COMPETITION_WAIT_ACTION_DONE,
    COMPETITION_MUSIC_WAIT_COMMAND,
    COMPETITION_MUSIC_RECOGNITION,
    COMPETITION_MUSIC_ANNOUNCE_DELAY,
    COMPETITION_MUSIC_PERFORMANCE,
    COMPETITION_COMPLETE,
    COMPETITION_ABORTED,
    COMPETITION_FAULT,
} competition_state_t;

typedef struct {
    competition_state_t state;
    uint8_t current_stage;
    uint32_t current_item;
    uint32_t total_elapsed_ms;
    uint32_t stage_elapsed_ms;
    uint8_t last_music_id;
    bool stage_seen[3];
} competition_status_t;

esp_err_t competition_init(void);
esp_err_t competition_on_prepare_command(void);
esp_err_t competition_on_stage_command(uint8_t stage_event_id);
esp_err_t competition_on_visual_arm_command(void);
esp_err_t competition_on_voice_recognized(uint8_t id);
esp_err_t competition_on_visual_recognized(uint8_t id);
esp_err_t competition_on_music_recognition_start(void);
esp_err_t competition_on_music_recognized(uint8_t id);
esp_err_t competition_on_music_recognition_failed(void);
esp_err_t competition_on_music_announcement_failed(void);
esp_err_t competition_on_music_performance_start(uint8_t music_id);
esp_err_t competition_on_music_performance_failed(void);
esp_err_t competition_on_action_done(bool success);
esp_err_t competition_on_music_performance_done(void);
esp_err_t competition_get_status(competition_status_t *out);

#ifdef __cplusplus
}
#endif
