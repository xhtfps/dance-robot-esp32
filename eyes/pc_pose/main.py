"""Run PC-side GPU pose recognition for group 1."""

import argparse
import json
import time
import urllib.request

from pc_pose.classifier import TeamPoseClassifier
from pc_pose.keypoints import normalize_keypoints
from pc_pose.mjpeg import LatestMjpegReader
from pc_pose.web_overlay import PoseOverlayServer
from shared.pose_contract import make_result


COCO_SKELETON = (
    (0, 1), (0, 2), (1, 3), (2, 4),
    (5, 6), (5, 7), (7, 9), (6, 8), (8, 10),
    (5, 11), (6, 12), (11, 12),
    (11, 13), (13, 15), (12, 14), (14, 16),
)

VISUAL_MESSAGE_BY_POSE = {
    1: 1,   # big_stand -> 大字站
    3: 2,   # double_hands -> 举双手
    4: 3,   # squat -> 蹲下
    2: 4,   # bow_step -> 弓箭步
    11: 5,  # hands_on_hips -> 插腰
    12: 6,  # salute -> 敬礼
}


def format_result_line(result):
    return "RESULT " + json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n"


def visual_message_id(result):
    """Map a stable PC pose ID to the SU-03T visual message number."""
    try:
        return VISUAL_MESSAGE_BY_POSE.get(int(result.get("stable_pose_id", 0)))
    except (AttributeError, TypeError, ValueError):
        return None


def esp32_pose_payload(result, timestamp=None):
    """Build the compact visual-recognition payload expected by ESP32."""
    message_id = visual_message_id(result)
    if message_id is None:
        return None
    if timestamp is None:
        timestamp = int(time.time() * 1000)
    return {
        "pose_id": message_id,
        "confidence": round(float(result.get("confidence", 0.0)), 3),
        "timestamp": int(timestamp),
    }


def post_esp32_pose(url, result, timestamp=None, timeout=1.0):
    """POST one stable visual result to the ESP32 pose endpoint."""
    payload = esp32_pose_payload(result, timestamp=timestamp)
    if payload is None:
        return False
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        if not 200 <= int(response.status) < 300:
            return None
        return json.loads(response.read().decode("utf-8"))


class PoseRunner:
    def __init__(self, backend, team_id, source="pc", stable_frames=4):
        self.backend = backend
        self.team_id = int(team_id)
        self.source = source
        self.classifier = TeamPoseClassifier(self.team_id, stable_frames=stable_frames)

    def _empty_result(self, fps=0.0):
        self.classifier.reset()
        result = make_result(self.team_id, 0, 0.0, False, self.source, fps=fps)
        result.update({"keypoints": [], "bbox": [], "person_score": 0.0})
        return result

    def process(self, frame, fps=0.0):
        height, width = frame.shape[:2]
        detections = self.backend.infer(frame) or []
        if not detections:
            return self._empty_result(fps=fps)

        detection = max(detections, key=lambda item: float(item.get("score", 0.0)))
        points = normalize_keypoints(detection.get("keypoints"), width, height)
        classified = self.classifier.update(points)
        result = make_result(
            self.team_id,
            classified["pose_id"],
            classified["confidence"],
            classified["stable"],
            self.source,
            fps=fps,
        )
        result.update({
            "stable_pose_id": classified["stable_pose_id"],
            "stable_pose": classified["stable_pose"],
            "stable_pose_cn": classified["stable_pose_cn"],
            "keypoints": points,
            "bbox": detection.get("bbox", []),
            "person_score": round(float(detection.get("score", 0.0)), 3),
        })
        return result


def _source_value(source):
    try:
        return int(source)
    except (TypeError, ValueError):
        return source


def draw_pose_overlay(frame, result, cv2):
    """Draw COCO-17 joints, skeleton, bbox, and classifier result on a frame."""
    keypoints = result.get("keypoints", [])
    visible = {}
    for index, point in enumerate(keypoints):
        if len(point) < 3 or float(point[2]) < 0.20:
            continue
        x, y = int(round(float(point[0]))), int(round(float(point[1])))
        visible[index] = (x, y)

    for start, end in COCO_SKELETON:
        if start in visible and end in visible:
            cv2.line(frame, visible[start], visible[end], (0, 220, 255), 2, cv2.LINE_AA)
    for x, y in visible.values():
        cv2.circle(frame, (x, y), 4, (0, 80, 255), -1, cv2.LINE_AA)
        cv2.circle(frame, (x, y), 6, (255, 255, 255), 1, cv2.LINE_AA)

    bbox = result.get("bbox", [])
    if len(bbox) >= 4:
        left, top, right, bottom = [int(round(float(value))) for value in bbox[:4]]
        cv2.rectangle(frame, (left, top), (right, bottom), (80, 255, 80), 2)

    pose = result.get("pose", "unknown")
    stable = result.get("stable_pose", "unknown") if result.get("stable") else "-"
    confidence = float(result.get("confidence", 0.0))
    fps = float(result.get("fps", 0.0))
    text_lines = (
        f"TEAM {result.get('team', '?')} | FPS {fps:.1f}",
        f"POSE: {pose}  conf={confidence:.2f}",
        f"STABLE: {stable}",
    )
    for line_index, text in enumerate(text_lines):
        y = 30 + line_index * 30
        cv2.putText(frame, text, (12, y), cv2.FONT_HERSHEY_SIMPLEX,
                    0.65, (0, 0, 0), 4, cv2.LINE_AA)
        cv2.putText(frame, text, (12, y), cv2.FONT_HERSHEY_SIMPLEX,
                    0.65, (255, 255, 255), 1, cv2.LINE_AA)
    return frame


def run_source(source, team_id, backend, serial_port=None, esp32_url=None,
               display=True, web_host="127.0.0.1", web_port=8080):
    # The browser overlay is the only user-facing display. Keep the display
    # argument for API compatibility with older launch scripts.
    del display
    try:
        import cv2
    except ImportError as exc:
        raise RuntimeError("OpenCV is not installed; install pc_pose/requirements.txt") from exc

    source_text = str(source)
    mjpeg_reader = None
    capture = None
    if source_text.lower().startswith(("http://", "https://")):
        mjpeg_reader = LatestMjpegReader(source_text).start()
        last_sequence = 0
    else:
        capture = cv2.VideoCapture(_source_value(source))
        if not capture.isOpened():
            raise RuntimeError("unable to open pose video source: " + str(source))
        # Keep the newest frame when the inference/display loop is slower than the stream.
        capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    overlay_server = PoseOverlayServer(host=web_host, port=web_port)
    overlay_server.start()
    print(f"Browser pose view: http://{web_host}:{web_port}/", flush=True)

    serial = None
    if serial_port:
        try:
            import serial as serial_module
        except ImportError as exc:
            raise RuntimeError("PySerial is required for --serial-port") from exc
        serial = serial_module.Serial(serial_port, 115200, timeout=0.2)

    runner = PoseRunner(backend, team_id, source="pc")
    last_time = time.perf_counter()
    last_report_time = last_time
    last_serial_pose = None
    last_accepted_pose = None
    last_attempted_pose = None
    last_pose_attempt_time = 0.0
    pose_retry_interval = 0.5
    try:
        while True:
            if mjpeg_reader is not None:
                ok, frame, last_sequence = mjpeg_reader.read(last_sequence, timeout=10.0)
                if not ok:
                    continue
            else:
                ok, frame = capture.read()
            if not ok:
                break
            now = time.perf_counter()
            fps = 1.0 / max(now - last_time, 1e-6)
            last_time = now
            result = runner.process(frame, fps=fps)
            annotated = draw_pose_overlay(frame, result, cv2)
            encoded_ok, encoded = cv2.imencode(
                ".jpg", annotated, [cv2.IMWRITE_JPEG_QUALITY, 85]
            )
            if encoded_ok:
                overlay_server.state.update(encoded.tobytes(), result)
            if now - last_report_time >= 0.5:
                print(json.dumps(result, ensure_ascii=False), flush=True)
                last_report_time = now
            if result["stable"]:
                pose_id = result["stable_pose_id"]
                if serial and pose_id != last_serial_pose:
                    serial.write(format_result_line(result).encode("utf-8"))
                    last_serial_pose = pose_id

                should_attempt = (
                    esp32_url is not None
                    and (
                        pose_id != last_attempted_pose
                        or now - last_pose_attempt_time >= pose_retry_interval
                    )
                )
                if should_attempt:
                    last_attempted_pose = pose_id
                    last_pose_attempt_time = now
                    try:
                        response = post_esp32_pose(esp32_url, result)
                        accepted = bool(response and response.get("accepted"))
                        print(
                            "ESP32 pose POST: id=%s accepted=%s response=%s"
                            % (pose_id, accepted, response),
                            flush=True,
                        )
                        if accepted:
                            last_accepted_pose = pose_id
                    except (OSError, ValueError) as error:
                        print("ESP32 pose POST failed: %s" % error, flush=True)
            elif not result["stable"]:
                # Allow the same pose to be submitted again after the user
                # briefly leaves the pose or becomes unrecognizable.
                last_serial_pose = None
                last_accepted_pose = None
                last_attempted_pose = None
                last_pose_attempt_time = 0.0

    finally:
        if mjpeg_reader is not None:
            mjpeg_reader.close()
        if capture is not None:
            capture.release()
        if serial:
            serial.close()
        overlay_server.stop()


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--team", type=int, choices=(1,), required=True)
    parser.add_argument("--source", default="http://192.168.4.1:81/stream")
    parser.add_argument("--device", default="cuda:0")
    parser.add_argument("--backend", choices=("ultralytics", "mmpose"), default="ultralytics")
    parser.add_argument("--model", default="yolo11n-pose.pt")
    parser.add_argument("--serial-port")
    parser.add_argument("--esp32-url", default=None,
                        help="POST stable visual messages to the ESP32 endpoint")
    parser.add_argument("--no-display", action="store_true")
    parser.add_argument("--web-host", default="127.0.0.1")
    parser.add_argument("--web-port", type=int, default=8080)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.backend == "mmpose":
        from pc_pose.backends.mmpose_backend import MMPoseBackend
        backend = MMPoseBackend(model=args.model, device=args.device)
    else:
        from pc_pose.backends.ultralytics_backend import UltralyticsBackend
        backend = UltralyticsBackend(model=args.model, device=args.device)
    run_source(
        args.source,
        args.team,
        backend,
        serial_port=args.serial_port,
        esp32_url=args.esp32_url,
        display=not args.no_display,
        web_host=args.web_host,
        web_port=args.web_port,
    )


if __name__ == "__main__":
    main()
