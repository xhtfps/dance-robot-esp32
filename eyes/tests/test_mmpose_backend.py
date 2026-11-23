import unittest

from pc_pose.backends.mmpose_backend import MMPoseBackend


class MmposeBackendTest(unittest.TestCase):
    def test_prediction_list_flattens_inferencer_batch(self):
        prediction = {"keypoints": [[0.0, 0.0, 1.0]] * 17}
        self.assertEqual(MMPoseBackend._prediction_list([[prediction]]), [prediction])

    def test_as_detection_combines_keypoint_scores(self):
        prediction = {
            "keypoints": [[float(i), float(i + 1)] for i in range(17)],
            "keypoint_scores": [0.8] * 17,
            "bbox": [[1.0, 2.0, 30.0, 40.0]],
            "bbox_score": 0.9,
        }
        detection = MMPoseBackend._as_detection(prediction)
        self.assertEqual(len(detection["keypoints"]), 17)
        self.assertEqual(detection["keypoints"][3], [3.0, 4.0, 0.8])
        self.assertEqual(detection["bbox"], [1.0, 2.0, 30.0, 40.0])
        self.assertEqual(detection["score"], 0.9)

    def test_as_detection_rejects_non_coco_keypoint_count(self):
        self.assertIsNone(MMPoseBackend._as_detection({"keypoints": [[0, 0, 1]] * 16}))
