import unittest
from pathlib import Path

from pc_pose.main import PoseRunner, esp32_pose_payload, format_result_line, visual_message_id


class FakeBackend:
    def __init__(self, detections):
        self.detections = detections

    def infer(self, frame):
        return self.detections


def _big_stand_detection():
    points = [
        (100, 100, 1.0), (90, 120, 1.0), (110, 120, 1.0),
        (80, 130, 1.0), (120, 130, 1.0),
        (80, 180, 1.0), (120, 180, 1.0),
        (50, 180, 1.0), (150, 180, 1.0),
        (20, 180, 1.0), (180, 180, 1.0),
        (90, 300, 1.0), (110, 300, 1.0),
        (70, 420, 1.0), (130, 420, 1.0),
        (45, 520, 1.0), (155, 520, 1.0),
    ]
    return {"keypoints": points, "bbox": [20, 80, 180, 540], "score": 0.9}


class PcRunnerTest(unittest.TestCase):
    def test_browser_overlay_is_the_only_default_display(self):
        source = Path(__file__).resolve().parents[1].joinpath("pc_pose", "main.py")
        text = source.read_text(encoding="utf-8")
        self.assertNotIn('cv2.imshow("PC Pose Recognition"', text)
        self.assertNotIn("cv2.waitKey", text)

    def test_visual_message_mapping_includes_hands_on_hips(self):
        self.assertEqual(visual_message_id({"stable_pose_id": 11}), 5)
        self.assertEqual(visual_message_id({"stable_pose_id": 12}), 6)

    def test_esp32_payload_uses_visual_message_id(self):
        payload = esp32_pose_payload({
            "stable_pose_id": 11,
            "confidence": 0.94,
        }, timestamp=1234)
        self.assertEqual(payload, {
            "pose_id": 5,
            "confidence": 0.94,
            "timestamp": 1234,
        })

    def test_double_hands_uses_visual_message_two(self):
        payload = esp32_pose_payload({
            "stable_pose_id": 3,
            "confidence": 0.95,
        }, timestamp=1234)
        self.assertEqual(payload["pose_id"], 2)

    def test_runner_selects_highest_score_detection_and_emits_contract(self):
        low = _big_stand_detection()
        low["score"] = 0.2
        high = _big_stand_detection()
        high["score"] = 0.9
        runner = PoseRunner(FakeBackend([low, high]), team_id=1, source="test")

        result = runner.process(type("Frame", (), {"shape": (600, 800, 3)})())

        self.assertEqual(result["task"], "pose")
        self.assertEqual(result["team"], 1)
        self.assertEqual(result["pose"], "big_stand")
        self.assertEqual(result["source"], "test")
        self.assertEqual(result["bbox"], [20, 80, 180, 540])

    def test_runner_without_detection_returns_unknown_and_clears_stable_state(self):
        runner = PoseRunner(FakeBackend([_big_stand_detection()]), team_id=1)
        frame = type("Frame", (), {"shape": (600, 800, 3)})()
        runner.process(frame)

        runner.backend.detections = []
        result = runner.process(frame)

        self.assertEqual(result["pose_id"], 0)
        self.assertEqual(result["stable_pose_id"], 0)
        self.assertFalse(result["stable"])

    def test_format_result_line_is_single_json_command_line(self):
        line = format_result_line({"pose": "big_stand", "stable": True})
        self.assertTrue(line.startswith("RESULT "))
        self.assertTrue(line.endswith("\n"))
        self.assertNotIn("\n", line[:-1])
