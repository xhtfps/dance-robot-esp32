import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = (ROOT / "main" / "main.c").read_text(encoding="utf-8")
POSE_CONTRACT = (ROOT / "components" / "BSP" / "POSE" / "pose_contract.h").read_text(
    encoding="utf-8"
)
POSE_CONTRACT_IMPL = (ROOT / "components" / "BSP" / "POSE" / "pose_contract.c").read_text(
    encoding="utf-8"
)
POSE_CLASSIFIER = (ROOT / "components" / "BSP" / "POSE" / "pose_classifier.c").read_text(
    encoding="utf-8"
)
KCONFIG = (ROOT / "main" / "Kconfig.projbuild").read_text(encoding="utf-8")


class LocalPoseRuntimeContractTest(unittest.TestCase):
    def test_stable_local_pose_is_connected_to_competition_action_loop(self):
        start = MAIN.index("static void local_pose_task")
        end = MAIN.index("static esp_err_t start_local_pose")
        local_task = MAIN[start:end]

        self.assertIn("competition_on_visual_recognized", local_task)
        self.assertIn("serial_bridge_send_to_su03_message", local_task)
        self.assertIn("dispatch_visual_action", local_task)

    def test_local_contract_uses_pc_team_one_custom_pose_ids(self):
        self.assertIn("POSE_HANDS_ON_HIPS = 11", POSE_CONTRACT)
        self.assertIn("POSE_SALUTE = 12", POSE_CONTRACT)

    def test_local_pose_team_is_hard_locked_to_six_actions(self):
        self.assertIn("range 1 1", KCONFIG)
        self.assertIn("#define POSE_TEAM_COUNT 1U", POSE_CONTRACT)

        forbidden = (
            "POSE_ATTENTION",
            "POSE_KNEE_RAISE",
            "POSE_ONE_ARM_SIDE",
            "POSE_ONE_HAND_UP",
            "POSE_CROSSED_ARMS",
            "POSE_BEND_FORWARD",
        )
        for name in forbidden:
            self.assertNotIn(name, POSE_CONTRACT)
            self.assertNotIn(name, POSE_CONTRACT_IMPL)
            self.assertNotIn(name, POSE_CLASSIFIER)

    def test_local_classifier_uses_pc_geometry_thresholds(self):
        self.assertIn("#define SCORE_THRESHOLD 0.20f", POSE_CLASSIFIER)
        self.assertIn("0.40f * scale", POSE_CLASSIFIER)
        self.assertIn("> 0.75f", POSE_CLASSIFIER)
        self.assertIn("> 0.28f", POSE_CLASSIFIER)

    def test_boot_button_keeps_local_and_wifi_modes_switchable(self):
        self.assertIn("toggle_vision_mode();", MAIN)
        self.assertIn("stop_pc_pose();", MAIN)
        self.assertIn("stop_local_pose();", MAIN)
        self.assertIn("start_pc_pose();", MAIN)


if __name__ == "__main__":
    unittest.main()
