"""Lazy MMPose/RTMPose backend for PC inference."""


def _first(value, default=None):
    if value is None:
        return default
    try:
        return value[0] if len(value) == 1 and hasattr(value[0], "__len__") else value
    except (TypeError, IndexError):
        return value


class MMPoseBackend:
    def __init__(self, model="human", device="cuda:0"):
        self.model = model
        self.device = device
        self._inferencer = None

    def _load(self):
        if self._inferencer is None:
            try:
                from mmpose.apis import MMPoseInferencer
            except ImportError as exc:
                raise RuntimeError(
                    "MMPose is not installed; install pc_pose/requirements.txt"
                ) from exc
            self._inferencer = MMPoseInferencer(
                pose2d=self.model,
                device=self.device,
            )
        return self._inferencer

    @staticmethod
    def _prediction_list(predictions):
        if predictions is None:
            return []
        if isinstance(predictions, list) and predictions and isinstance(predictions[0], list):
            return predictions[0]
        return list(predictions)

    @staticmethod
    def _as_detection(prediction):
        if not isinstance(prediction, dict):
            return None
        keypoints = prediction.get("keypoints")
        if keypoints is None:
            keypoints = prediction.get("keypoints_xy")
        if keypoints is None:
            return None

        keypoint_scores = prediction.get("keypoint_scores")
        points = []
        for index, point in enumerate(keypoints):
            score = None
            if keypoint_scores is not None and index < len(keypoint_scores):
                score = keypoint_scores[index]
            if isinstance(point, dict):
                x, y = point.get("x"), point.get("y")
                score = point.get("score", score if score is not None else 0.0)
            else:
                x, y = point[0], point[1]
                if len(point) > 2:
                    score = point[2]
            points.append([float(x), float(y), float(score if score is not None else 1.0)])
        if len(points) != 17:
            return None

        bbox = _first(prediction.get("bbox"), [])
        bbox_score = prediction.get("bbox_score", prediction.get("score", 0.0))
        if hasattr(bbox_score, "item"):
            bbox_score = bbox_score.item()
        return {
            "keypoints": points,
            "bbox": [float(value) for value in bbox[:4]],
            "score": float(bbox_score),
        }

    def infer(self, frame):
        result = next(self._load()(frame, return_vis=False))
        predictions = result.get("predictions", []) if isinstance(result, dict) else []
        detections = []
        for prediction in self._prediction_list(predictions):
            detection = self._as_detection(prediction)
            if detection is not None:
                detections.append(detection)
        return detections
