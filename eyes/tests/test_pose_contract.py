import unittest

from shared.pose_contract import (
    POSE_LABELS,
    POSE_LABELS_CN,
    TEAM_ALLOWED_POSE_IDS,
    make_result,
    team_pose_ids,
)


class PoseContractTest(unittest.TestCase):
    def test_group_one_has_only_six_allowed_actions(self):
        self.assertEqual(TEAM_ALLOWED_POSE_IDS, {
            1: (1, 11, 12, 3, 2, 4),
        })

    def test_removed_action_ids_are_normalized_to_unknown(self):
        result = make_result(1, 5, 0.9, True, "pc")
        self.assertEqual(result["pose_id"], 0)
        self.assertEqual(result["pose"], "unknown")
        self.assertEqual(result["stable_pose_id"], 0)


    def test_all_team_labels_are_stable_and_unique(self):
        self.assertEqual(POSE_LABELS[0], "unknown")
        self.assertEqual(POSE_LABELS_CN[1], "大字站")
        self.assertEqual(len(set(POSE_LABELS.values())), len(POSE_LABELS))


    def test_invalid_team_is_rejected(self):
        with self.assertRaises(ValueError):
            team_pose_ids(2)
        with self.assertRaises(ValueError):
            team_pose_ids(4)

    def test_make_result_has_stable_schema_and_unknown_fallback(self):
        result = make_result(1, 999, 1.4, False, "pc", fps=12.3456)
        self.assertEqual(result["team"], 1)
        self.assertEqual(result["pose_id"], 0)
        self.assertEqual(result["pose"], "unknown")
        self.assertEqual(result["stable_pose_id"], 0)
        self.assertEqual(result["confidence"], 1.0)
        self.assertEqual(result["fps"], 12.346)
