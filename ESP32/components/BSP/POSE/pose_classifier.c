#include "pose_classifier.h"

#include <math.h>
#include <stddef.h>

/* Match the PC classifier's default visible threshold. */
#define SCORE_THRESHOLD 0.20f

static float distance2(const pose_keypoint_t *a, const pose_keypoint_t *b)
{
    const float dx = a->x - b->x;
    const float dy = a->y - b->y;
    return sqrtf(dx * dx + dy * dy);
}

static float angle3(const pose_keypoint_t *a, const pose_keypoint_t *b,
                    const pose_keypoint_t *c)
{
    const float bax = a->x - b->x;
    const float bay = a->y - b->y;
    const float bcx = c->x - b->x;
    const float bcy = c->y - b->y;
    const float first = sqrtf(bax * bax + bay * bay);
    const float second = sqrtf(bcx * bcx + bcy * bcy);
    if (first < 1e-6f || second < 1e-6f) {
        return 180.0f;
    }

    float cosine = (bax * bcx + bay * bcy) / (first * second);
    if (cosine < -1.0f) {
        cosine = -1.0f;
    } else if (cosine > 1.0f) {
        cosine = 1.0f;
    }
    return acosf(cosine) * 57.295779513f;
}

static bool visible(const pose_keypoint_t points[POSE_KEYPOINT_COUNT],
                    const uint8_t *indices, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const pose_keypoint_t *point = &points[indices[i]];
        if (!isfinite(point->x) || !isfinite(point->y) ||
            !isfinite(point->score) || point->score < SCORE_THRESHOLD) {
            return false;
        }
    }
    return true;
}

static float max3(float a, float b, float c)
{
    return fmaxf(fmaxf(a, b), c);
}

static void classify_frame(const pose_keypoint_t points[POSE_KEYPOINT_COUNT],
                           pose_id_t *pose_id, float *confidence)
{
    static const uint8_t body_required[] = {
        POSE_LEFT_SHOULDER, POSE_RIGHT_SHOULDER, POSE_LEFT_HIP,
        POSE_RIGHT_HIP, POSE_LEFT_KNEE, POSE_RIGHT_KNEE,
        POSE_LEFT_ANKLE, POSE_RIGHT_ANKLE,
    };
    static const uint8_t arms_required[] = {
        POSE_LEFT_SHOULDER, POSE_RIGHT_SHOULDER, POSE_LEFT_ELBOW,
        POSE_RIGHT_ELBOW, POSE_LEFT_WRIST, POSE_RIGHT_WRIST,
    };

    *pose_id = POSE_UNKNOWN;
    *confidence = 0.0f;
    if (points == NULL || !visible(points, body_required,
                                   sizeof(body_required) / sizeof(body_required[0]))) {
        return;
    }

    const bool arms_reliable = visible(
        points, arms_required, sizeof(arms_required) / sizeof(arms_required[0]));
    const float shoulder_width =
        distance2(&points[POSE_LEFT_SHOULDER], &points[POSE_RIGHT_SHOULDER]);
    const float hip_width =
        distance2(&points[POSE_LEFT_HIP], &points[POSE_RIGHT_HIP]);
    const float shoulder_center_x =
        (points[POSE_LEFT_SHOULDER].x + points[POSE_RIGHT_SHOULDER].x) * 0.5f;
    const float shoulder_center_y =
        (points[POSE_LEFT_SHOULDER].y + points[POSE_RIGHT_SHOULDER].y) * 0.5f;
    const float hip_center_x =
        (points[POSE_LEFT_HIP].x + points[POSE_RIGHT_HIP].x) * 0.5f;
    const float hip_center_y =
        (points[POSE_LEFT_HIP].y + points[POSE_RIGHT_HIP].y) * 0.5f;
    const float torso = hypotf(shoulder_center_x - hip_center_x,
                               shoulder_center_y - hip_center_y);
    const float scale = fmaxf(max3(shoulder_width, hip_width, torso), 1.0f);

    const float left_arm = angle3(&points[POSE_LEFT_SHOULDER],
                                  &points[POSE_LEFT_ELBOW],
                                  &points[POSE_LEFT_WRIST]);
    const float right_arm = angle3(&points[POSE_RIGHT_SHOULDER],
                                   &points[POSE_RIGHT_ELBOW],
                                   &points[POSE_RIGHT_WRIST]);
    const float left_knee = angle3(&points[POSE_LEFT_HIP],
                                   &points[POSE_LEFT_KNEE],
                                   &points[POSE_LEFT_ANKLE]);
    const float right_knee = angle3(&points[POSE_RIGHT_HIP],
                                    &points[POSE_RIGHT_KNEE],
                                    &points[POSE_RIGHT_ANKLE]);
    const float nose_x = points[POSE_NOSE].x;
    const float nose_y = points[POSE_NOSE].y;
    const float leg_spread =
        fabsf(points[POSE_LEFT_ANKLE].x - points[POSE_RIGHT_ANKLE].x) / scale;
    const float ankle_height_gap =
        fabsf(points[POSE_LEFT_ANKLE].y - points[POSE_RIGHT_ANKLE].y) / scale;
    const bool feet_side_by_side = ankle_height_gap < 0.35f;
    const bool knees_straight = left_knee > 150.0f && right_knee > 150.0f;

    const bool left_salute = arms_reliable &&
        left_arm < 165.0f &&
        points[POSE_LEFT_WRIST].y < nose_y + 0.55f * scale &&
        fabsf(points[POSE_LEFT_WRIST].x - nose_x) < 0.45f * scale;
    const bool right_salute = arms_reliable &&
        right_arm < 165.0f &&
        points[POSE_RIGHT_WRIST].y < nose_y + 0.55f * scale &&
        fabsf(points[POSE_RIGHT_WRIST].x - nose_x) < 0.45f * scale;
    const bool salute =
        (left_salute && points[POSE_RIGHT_WRIST].y >
             shoulder_center_y + 0.35f * scale) ||
        (right_salute && points[POSE_LEFT_WRIST].y >
             shoulder_center_y + 0.35f * scale);

    const bool one_leg_bent =
        fminf(left_knee, right_knee) < 165.0f &&
        fmaxf(left_knee, right_knee) > 135.0f &&
        fabsf(left_knee - right_knee) > 8.0f;
    const float ankle_distance =
        distance2(&points[POSE_LEFT_ANKLE], &points[POSE_RIGHT_ANKLE]) / scale;
    const bool front_back_stance =
        fmaxf(fmaxf(leg_spread, ankle_height_gap), ankle_distance) > 0.28f;
    const bool bow_step = one_leg_bent && front_back_stance;

    const bool left_wrist_near_hip =
        fminf(distance2(&points[POSE_LEFT_WRIST], &points[POSE_LEFT_HIP]),
              distance2(&points[POSE_LEFT_WRIST], &points[POSE_RIGHT_HIP])) <
        0.85f * scale;
    const bool right_wrist_near_hip =
        fminf(distance2(&points[POSE_RIGHT_WRIST], &points[POSE_LEFT_HIP]),
              distance2(&points[POSE_RIGHT_WRIST], &points[POSE_RIGHT_HIP])) <
        0.85f * scale;
    const bool elbows_out =
        fabsf(points[POSE_LEFT_ELBOW].x - points[POSE_LEFT_SHOULDER].x) >
            0.06f * scale &&
        fabsf(points[POSE_RIGHT_ELBOW].x - points[POSE_RIGHT_SHOULDER].x) >
            0.06f * scale;
    const float hip_y = hip_center_y;
    const bool hands_on_hips = arms_reliable &&
        points[POSE_LEFT_WRIST].y > shoulder_center_y + 0.10f * scale &&
        points[POSE_RIGHT_WRIST].y > shoulder_center_y + 0.10f * scale &&
        points[POSE_LEFT_WRIST].y < hip_y + 0.95f * scale &&
        points[POSE_RIGHT_WRIST].y < hip_y + 0.95f * scale &&
        left_wrist_near_hip && right_wrist_near_hip && elbows_out;
    const bool arm_horizontal = arms_reliable &&
        fabsf(points[POSE_LEFT_WRIST].y - points[POSE_LEFT_SHOULDER].y) <
            0.40f * scale &&
        fabsf(points[POSE_RIGHT_WRIST].y - points[POSE_RIGHT_SHOULDER].y) <
            0.40f * scale;

    /* Match the PC precedence: bow step, salute, hips, then four basics. */
    if (bow_step) {
        *pose_id = POSE_BOW_STEP;
        *confidence = 0.90f;
        return;
    }
    if (salute) {
        *pose_id = POSE_SALUTE;
        *confidence = 0.92f;
        return;
    }
    if (hands_on_hips) {
        *pose_id = POSE_HANDS_ON_HIPS;
        *confidence = 0.92f;
        return;
    }

    pose_id_t best_id = POSE_UNKNOWN;
    float best_score = 0.0f;
    const bool wrists_high =
        points[POSE_LEFT_WRIST].y < nose_y + 0.15f * scale &&
        points[POSE_RIGHT_WRIST].y < nose_y + 0.15f * scale;
    const bool elbows_high =
        points[POSE_LEFT_ELBOW].y < shoulder_center_y + 0.25f * scale &&
        points[POSE_RIGHT_ELBOW].y < shoulder_center_y + 0.25f * scale;
    if (arms_reliable && wrists_high && elbows_high) {
        best_id = POSE_DOUBLE_HANDS;
        best_score = 0.95f;
    }

    if (leg_spread > 0.75f && arm_horizontal && knees_straight &&
        feet_side_by_side && best_score < 0.95f) {
        best_id = POSE_BIG_STAND;
        best_score = 0.95f;
    }

    const bool both_knees_bent = left_knee < 150.0f && right_knee < 150.0f;
    const bool hips_low = hip_y - shoulder_center_y > 0.75f * scale;
    if (both_knees_bent && hips_low && leg_spread < 1.35f &&
        best_score < 0.90f) {
        best_id = POSE_SQUAT;
        best_score = 0.90f;
    }

    if (one_leg_bent && front_back_stance && best_score < 0.90f) {
        best_id = POSE_BOW_STEP;
        best_score = 0.90f;
    }

    if (best_score < 0.50f) {
        best_id = POSE_UNKNOWN;
        best_score = 0.0f;
    }
    *pose_id = best_id;
    *confidence = best_score;
}

bool pose_classifier_init(pose_classifier_t *self,
                          uint8_t team_id, uint8_t stable_frames)
{
    if (!self || !pose_team_is_valid(team_id)) {
        return false;
    }
    self->team_id = team_id;
    self->stable_frames = stable_frames == 0U ? 1U : stable_frames;
    if (self->stable_frames > POSE_CLASSIFIER_MAX_STABLE_FRAMES) {
        self->stable_frames = POSE_CLASSIFIER_MAX_STABLE_FRAMES;
    }
    self->initialized = true;
    pose_classifier_reset(self);
    return true;
}

void pose_classifier_reset(pose_classifier_t *self)
{
    if (!self) {
        return;
    }
    self->history_count = 0U;
    self->stable_pose_id = POSE_UNKNOWN;
    for (size_t i = 0; i < POSE_CLASSIFIER_MAX_STABLE_FRAMES; ++i) {
        self->history[i] = POSE_UNKNOWN;
    }
}

bool pose_classifier_update(pose_classifier_t *self,
                            const pose_keypoint_t points[POSE_KEYPOINT_COUNT],
                            pose_result_t *out)
{
    if (!self || !out || !self->initialized) {
        return false;
    }

    pose_id_t pose_id;
    float confidence;
    classify_frame(points, &pose_id, &confidence);
    if (!pose_team_allows(self->team_id, pose_id)) {
        pose_id = POSE_UNKNOWN;
        confidence = 0.0f;
    }

    if (self->history_count < self->stable_frames) {
        self->history[self->history_count++] = pose_id;
    } else {
        for (size_t i = 1; i < self->stable_frames; ++i) {
            self->history[i - 1U] = self->history[i];
        }
        self->history[self->stable_frames - 1U] = pose_id;
    }

    bool stable = pose_id != POSE_UNKNOWN &&
                  self->history_count == self->stable_frames;
    if (stable) {
        for (size_t i = 0; i < self->stable_frames; ++i) {
            if (self->history[i] != pose_id) {
                stable = false;
                break;
            }
        }
    }
    if (stable) {
        self->stable_pose_id = pose_id;
    } else if (pose_id == POSE_UNKNOWN) {
        self->stable_pose_id = POSE_UNKNOWN;
    }

    out->pose_id = pose_id;
    out->confidence = confidence;
    out->stable = stable;
    out->stable_pose_id = self->stable_pose_id;
    return true;
}
