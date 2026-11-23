#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "pose_classifier.h"
#include "pose_contract.h"

static void set_point(pose_keypoint_t points[POSE_KEYPOINT_COUNT],
                      size_t index, float x, float y)
{
    points[index].x = x;
    points[index].y = y;
    points[index].score = 1.0f;
}

static void base_skeleton(pose_keypoint_t points[POSE_KEYPOINT_COUNT])
{
    const float values[POSE_KEYPOINT_COUNT][2] = {
        {100, 100}, {90, 120}, {110, 120}, {80, 130}, {120, 130},
        {80, 180}, {120, 180}, {50, 180}, {150, 180}, {20, 180}, {180, 180},
        {90, 300}, {110, 300}, {70, 420}, {130, 420}, {0, 520}, {200, 520},
    };
    for (size_t i = 0; i < POSE_KEYPOINT_COUNT; ++i) {
        set_point(points, i, values[i][0], values[i][1]);
    }
}

static void test_contract(void)
{
    assert(pose_team_allows(1, POSE_HANDS_ON_HIPS));
    assert(pose_team_allows(1, POSE_SALUTE));
    assert(!pose_team_allows(1, (pose_id_t)5));
    assert(!pose_team_allows(1, (pose_id_t)10));
    assert(!pose_team_allows(2, POSE_BIG_STAND));
    assert(!pose_team_allows(3, POSE_SALUTE));
}

static void test_team_specific_actions(void)
{
    pose_keypoint_t points[POSE_KEYPOINT_COUNT];
    pose_result_t result;
    pose_classifier_t classifier;

    base_skeleton(points);
    set_point(points, 7, 45, 220);
    set_point(points, 8, 155, 220);
    set_point(points, 9, 88, 260);
    set_point(points, 10, 112, 260);
    assert(pose_classifier_init(&classifier, 1, 4));
    assert(pose_classifier_update(&classifier, points, &result));
    assert(result.pose_id == POSE_HANDS_ON_HIPS);

    assert(!pose_classifier_init(&classifier, 2, 4));
    assert(!pose_classifier_init(&classifier, 3, 4));
}

static void test_stability_and_missing_points(void)
{
    pose_keypoint_t points[POSE_KEYPOINT_COUNT];
    pose_result_t result;
    pose_classifier_t classifier;

    base_skeleton(points);
    assert(pose_classifier_init(&classifier, 1, 3));
    pose_classifier_update(&classifier, points, &result);
    assert(!result.stable);
    pose_classifier_update(&classifier, points, &result);
    assert(!result.stable);
    pose_classifier_update(&classifier, points, &result);
    assert(result.stable);
    assert(result.stable_pose_id == POSE_BIG_STAND);

    points[POSE_LEFT_ANKLE].score = 0.1f;
    assert(pose_classifier_update(&classifier, points, &result));
    assert(result.pose_id == POSE_UNKNOWN);
    assert(result.stable_pose_id == POSE_UNKNOWN);
}

static void test_hands_on_hips(void)
{
    pose_keypoint_t points[POSE_KEYPOINT_COUNT];
    pose_result_t result;
    pose_classifier_t classifier;

    base_skeleton(points);
    set_point(points, POSE_LEFT_ELBOW, 45, 220);
    set_point(points, POSE_RIGHT_ELBOW, 155, 220);
    set_point(points, POSE_LEFT_WRIST, 88, 260);
    set_point(points, POSE_RIGHT_WRIST, 112, 260);

    assert(pose_classifier_init(&classifier, 1, 1));
    assert(pose_classifier_update(&classifier, points, &result));
    assert(result.pose_id == POSE_HANDS_ON_HIPS);
    assert(result.stable);
}

static void test_salute(void)
{
    pose_keypoint_t points[POSE_KEYPOINT_COUNT];
    pose_result_t result;
    pose_classifier_t classifier;

    base_skeleton(points);
    set_point(points, POSE_LEFT_ELBOW, 70, 140);
    set_point(points, POSE_LEFT_WRIST, 100, 100);
    set_point(points, POSE_RIGHT_ELBOW, 130, 220);
    set_point(points, POSE_RIGHT_WRIST, 180, 260);

    assert(pose_classifier_init(&classifier, 1, 1));
    assert(pose_classifier_update(&classifier, points, &result));
    assert(result.pose_id == POSE_SALUTE);
    assert(result.stable);
}

int main(void)
{
    test_contract();
    test_team_specific_actions();
    test_stability_and_missing_points();
    test_hands_on_hips();
    test_salute();
    return 0;
}
