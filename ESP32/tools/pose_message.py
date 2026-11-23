import json


def parse_pose(payload):
    try:
        value = json.loads(payload.decode("utf-8"))
        pose_id = value["pose_id"]
        confidence = value["confidence"]
        timestamp = value.get("timestamp", 0)
        if not isinstance(pose_id, int) or not 0 <= pose_id <= 255:
            return None
        if not isinstance(confidence, (int, float)) or not 0.0 <= confidence <= 1.0:
            return None
        if not isinstance(timestamp, int) or timestamp < 0:
            return None
        return pose_id, float(confidence), timestamp
    except (UnicodeDecodeError, json.JSONDecodeError, KeyError, TypeError, ValueError):
        return None
