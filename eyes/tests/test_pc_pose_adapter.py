import unittest

from pc_pose.keypoints import normalize_keypoints
from pc_pose.classifier import TeamPoseClassifier
from pc_pose.pose_geometry import PoseClassifier
from shared.pose_contract import (
    POSE_BIG_STAND,
    POSE_BOW_STEP,
    POSE_DOUBLE_HANDS,
    POSE_HANDS_ON_HIPS,
    POSE_SQUAT,
    POSE_SALUTE,
    POSE_UNKNOWN,
)


def _skeleton():
    return [
        (100, 100, 1.0), (90, 120, 1.0), (110, 120, 1.0),
        (80, 130, 1.0), (120, 130, 1.0),
        (80, 180, 1.0), (120, 180, 1.0),
        (50, 180, 1.0), (150, 180, 1.0),
        (20, 180, 1.0), (180, 180, 1.0),
        (90, 300, 1.0), (110, 300, 1.0),
        (70, 420, 1.0), (130, 420, 1.0),
        (0, 520, 1.0), (200, 520, 1.0),
    ]


def _attention_skeleton():
    return [
        (100, 100, 1.0), (90, 120, 1.0), (110, 120, 1.0),
        (80, 130, 1.0), (120, 130, 1.0),
        (80, 180, 1.0), (120, 180, 1.0),
        (80, 240, 1.0), (120, 240, 1.0),
        (80, 300, 1.0), (120, 300, 1.0),
        (90, 300, 1.0), (110, 300, 1.0),
        (90, 420, 1.0), (110, 420, 1.0),
        (90, 520, 1.0), (110, 520, 1.0),
    ]


def _tolerant_hands_on_hips():
    points = _skeleton()
    points[7], points[8] = (90, 235, 1.0), (110, 235, 1.0)
    points[9], points[10] = (86, 300, 1.0), (114, 300, 1.0)
    return points


def _tolerant_salute():
    points = _skeleton()
    points[7], points[9] = (80, 160, 1.0), (95, 150, 1.0)
    points[8], points[10] = (120, 240, 1.0), (120, 245, 1.0)
    return points


def _tolerant_bow_step():
    points = _skeleton()
    points[13], points[15] = (75, 400, 1.0), (100, 500, 1.0)
    points[14], points[16] = (120, 405, 1.0), (145, 500, 1.0)
    return points


def _tolerant_big_stand():
    points = _skeleton()
    points[7], points[8] = (70, 200, 1.0), (130, 200, 1.0)
    points[9], points[10] = (20, 220, 1.0), (180, 220, 1.0)
    return points


def _tolerant_double_hands():
    points = _skeleton()
    points[7], points[8] = (70, 180, 1.0), (130, 180, 1.0)
    points[9], points[10] = (80, 108, 1.0), (120, 108, 1.0)
    return points


def _tolerant_squat():
    points = _skeleton()
    points[11], points[12] = (90, 350, 1.0), (110, 350, 1.0)
    points[13], points[15] = (150, 430, 1.0), (140, 540, 1.0)
    points[14], points[16] = (170, 430, 1.0), (160, 540, 1.0)
    return points


def _squat_with_horizontal_arm():
    points = _tolerant_squat()
    points[7], points[9] = (80, 180, 1.0), (20, 180, 1.0)
    points[8], points[10] = (120, 240, 1.0), (150, 220, 1.0)
    return points


class PcPoseAdapterTest(unittest.TestCase):
    def test_normalize_keypoints_converts_normalized_points_to_pixels(self):
        points = [(0.5, 0.25, 0.8)] * 17
        normalized = normalize_keypoints(points, 640, 480)
        self.assertEqual(normalized[0], [320.0, 120.0, 0.8])
        self.assertEqual(len(normalized), 17)

    def test_normalize_keypoints_preserves_pixel_points(self):
        points = [(320.0, 120.0, 0.8)] * 17
        self.assertEqual(normalize_keypoints(points, 640, 480)[0], [320.0, 120.0, 0.8])

    def test_malformed_keypoints_become_unknown_safe_points(self):
        points = normalize_keypoints([(0.5, 0.5, 1.0)], 640, 480)
        self.assertEqual(len(points), 17)
        self.assertEqual(points[0], [0.0, 0.0, 0.0])

    def test_team_allow_list_blocks_and_allows_actions(self):
        hands_on_hips = _skeleton()
        hands_on_hips[7], hands_on_hips[8] = (50, 220, 1.0), (150, 220, 1.0)
        hands_on_hips[9], hands_on_hips[10] = (75, 285, 1.0), (125, 285, 1.0)

        self.assertEqual(
            TeamPoseClassifier(1).update(hands_on_hips)["pose_id"],
            POSE_HANDS_ON_HIPS,
        )

    def test_team_one_recognizes_salute(self):
        salute = _skeleton()
        salute[7], salute[9] = (75, 145, 1.0), (92, 75, 1.0)
        salute[8], salute[10] = (125, 220, 1.0), (115, 270, 1.0)

        self.assertEqual(
            TeamPoseClassifier(1).update(salute)["pose_id"],
            POSE_SALUTE,
        )

    def test_team_one_recognizes_side_view_bow_step(self):
        bow_step = _skeleton()
        bow_step[13], bow_step[15] = (65, 400, 1.0), (100, 450, 1.0)
        bow_step[14], bow_step[16] = (120, 410, 1.0), (130, 520, 1.0)

        self.assertEqual(
            TeamPoseClassifier(1).update(bow_step)["pose_id"],
            2,
        )

    def test_classifier_does_not_recognize_removed_attention_action(self):
        result = PoseClassifier().update(_attention_skeleton())
        self.assertEqual(result["pose_id"], POSE_UNKNOWN)
        self.assertEqual(result["pose_cn"], "未识别")

    def test_tolerant_hands_on_hips_variant_is_recognized(self):
        self.assertEqual(
            TeamPoseClassifier(1).update(_tolerant_hands_on_hips())["pose_id"],
            POSE_HANDS_ON_HIPS,
        )

    def test_tolerant_salute_variant_is_recognized(self):
        self.assertEqual(
            TeamPoseClassifier(1).update(_tolerant_salute())["pose_id"],
            POSE_SALUTE,
        )

    def test_tolerant_bow_step_variant_is_recognized(self):
        self.assertEqual(
            TeamPoseClassifier(1).update(_tolerant_bow_step())["pose_id"],
            POSE_BOW_STEP,
        )

    def test_tolerant_big_stand_variant_is_recognized(self):
        self.assertEqual(
            TeamPoseClassifier(1).update(_tolerant_big_stand())["pose_id"],
            POSE_BIG_STAND,
        )

    def test_tolerant_double_hands_variant_is_recognized(self):
        self.assertEqual(
            TeamPoseClassifier(1).update(_tolerant_double_hands())["pose_id"],
            POSE_DOUBLE_HANDS,
        )

    def test_tolerant_squat_variant_is_recognized(self):
        self.assertEqual(
            TeamPoseClassifier(1).update(_tolerant_squat())["pose_id"],
            POSE_SQUAT,
        )

    def test_squat_with_horizontal_arm_is_not_misclassified_as_salute(self):
        self.assertEqual(
            TeamPoseClassifier(1).update(_squat_with_horizontal_arm())["pose_id"],
            POSE_SQUAT,
        )
