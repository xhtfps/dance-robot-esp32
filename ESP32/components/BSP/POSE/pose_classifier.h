#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pose_contract.h"

#define POSE_CLASSIFIER_MAX_STABLE_FRAMES 8U

typedef struct {
    float x;
    float y;
    float score;
} pose_keypoint_t;

typedef struct {
    pose_id_t pose_id;
    float confidence;
    bool stable;
    pose_id_t stable_pose_id;
} pose_result_t;

typedef struct {
    uint8_t team_id;
    uint8_t stable_frames;
    uint8_t history_count;
    pose_id_t history[POSE_CLASSIFIER_MAX_STABLE_FRAMES];
    pose_id_t stable_pose_id;
    bool initialized;
} pose_classifier_t;

#ifdef __cplusplus
extern "C" {
#endif

bool pose_classifier_init(pose_classifier_t *self,
                          uint8_t team_id, uint8_t stable_frames);
void pose_classifier_reset(pose_classifier_t *self);
bool pose_classifier_update(pose_classifier_t *self,
                            const pose_keypoint_t points[POSE_KEYPOINT_COUNT],
                            pose_result_t *out);

#ifdef __cplusplus
}
#endif
