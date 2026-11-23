#include "pose_contract.h"

#include <stddef.h>

static const pose_id_t s_local_actions[6] = {
    POSE_BIG_STAND, POSE_BOW_STEP, POSE_DOUBLE_HANDS,
    POSE_SQUAT, POSE_HANDS_ON_HIPS, POSE_SALUTE,
};

const char *pose_label(pose_id_t pose_id)
{
    switch (pose_id) {
    case POSE_BIG_STAND: return "big_stand";
    case POSE_BOW_STEP: return "bow_step";
    case POSE_DOUBLE_HANDS: return "double_hands";
    case POSE_SQUAT: return "squat";
    case POSE_HANDS_ON_HIPS: return "hands_on_hips";
    case POSE_SALUTE: return "salute";
    default: return "unknown";
    }
}

const char *pose_label_cn(pose_id_t pose_id)
{
    switch (pose_id) {
    case POSE_BIG_STAND: return "大字站";
    case POSE_BOW_STEP: return "弓箭步";
    case POSE_DOUBLE_HANDS: return "举双手";
    case POSE_SQUAT: return "蹲下";
    case POSE_HANDS_ON_HIPS: return "叉腰";
    case POSE_SALUTE: return "敬礼";
    default: return "未识别";
    }
}

bool pose_team_is_valid(uint8_t team_id)
{
    return team_id == 1U;
}

bool pose_team_allows(uint8_t team_id, pose_id_t pose_id)
{
    if (pose_id == POSE_UNKNOWN || !pose_team_is_valid(team_id)) {
        return false;
    }
    const pose_id_t *actions = s_local_actions;
    for (size_t i = 0; i < 6U; ++i) {
        if (actions[i] == pose_id) {
            return true;
        }
    }
    return false;
}
