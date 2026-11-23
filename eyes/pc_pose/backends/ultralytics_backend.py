"""Ultralytics YOLO pose backend with COCO-17 output."""


class UltralyticsBackend:
    def __init__(self, model="yolo11n-pose.pt", device="cuda:0"):
        from ultralytics import YOLO

        self.device = device
        try:
            import torch
            self.half = str(device).startswith("cuda") and torch.cuda.is_available()
        except Exception:
            self.half = False
        self.model = YOLO(model)

    @staticmethod
    def _number(value):
        if hasattr(value, "item"):
            return float(value.item())
        return float(value)

    def infer(self, frame):
        import warnings

        with warnings.catch_warnings():
            warnings.filterwarnings("ignore", message=".*half.*deprecated.*")
            results = self.model.predict(
                source=frame,
                device=self.device,
                verbose=False,
                conf=0.20,
                imgsz=320,
                max_det=1,
                classes=[0],
                quantize=16 if self.half else None,
            )
        detections = []
        for result in results:
            if result.keypoints is None or result.keypoints.xy is None:
                continue
            keypoints_xy = result.keypoints.xy.cpu().numpy()
            keypoints_conf = result.keypoints.conf
            if keypoints_conf is None:
                keypoints_conf = None
            else:
                keypoints_conf = keypoints_conf.cpu().numpy()

            boxes = result.boxes
            for index, points in enumerate(keypoints_xy):
                scores = (
                    keypoints_conf[index]
                    if keypoints_conf is not None and index < len(keypoints_conf)
                    else [1.0] * len(points)
                )
                packed_points = [
                    [self._number(point[0]), self._number(point[1]), self._number(score)]
                    for point, score in zip(points, scores)
                ]
                if len(packed_points) != 17:
                    continue

                bbox = []
                person_score = 0.0
                if boxes is not None and index < len(boxes):
                    bbox = [self._number(value) for value in boxes.xyxy[index].cpu().tolist()]
                    if boxes.conf is not None:
                        person_score = self._number(boxes.conf[index])
                detections.append({
                    "keypoints": packed_points,
                    "bbox": bbox,
                    "score": person_score,
                })
        return detections
