#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    POSE_UNKNOWN = 0,
    POSE_BIG_STAND = 1,
    POSE_BOW_STEP = 2,
    POSE_DOUBLE_HANDS = 3,
    POSE_SQUAT = 4,
    POSE_HANDS_ON_HIPS = 11,
    POSE_SALUTE = 12,
} pose_id_t;

#ifdef __cplusplus
extern "C" {
#endif

#define POSE_KEYPOINT_COUNT 17U
#define POSE_TEAM_COUNT 1U

enum {
    POSE_NOSE = 0,
    POSE_LEFT_SHOULDER = 5,
    POSE_RIGHT_SHOULDER = 6,
    POSE_LEFT_ELBOW = 7,
    POSE_RIGHT_ELBOW = 8,
    POSE_LEFT_WRIST = 9,
    POSE_RIGHT_WRIST = 10,
    POSE_LEFT_HIP = 11,
    POSE_RIGHT_HIP = 12,
    POSE_LEFT_KNEE = 13,
    POSE_RIGHT_KNEE = 14,
    POSE_LEFT_ANKLE = 15,
    POSE_RIGHT_ANKLE = 16,
};

const char *pose_label(pose_id_t pose_id);
const char *pose_label_cn(pose_id_t pose_id);
bool pose_team_is_valid(uint8_t team_id);
bool pose_team_allows(uint8_t team_id, pose_id_t pose_id);

#ifdef __cplusplus
}
#endif
