import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from tools.pose_message import parse_pose


def test_valid_pose_message():
    assert parse_pose(
        b'{"pose_id":4,"confidence":0.93,"timestamp":1234}'
    ) == (4, 0.93, 1234)


def test_invalid_pose_message_is_rejected():
    assert parse_pose(b'{"confidence":0.93}') is None
    assert parse_pose(b'{"pose_id":4,"confidence":1.5}') is None
    assert parse_pose(b'not-json') is None


if __name__ == "__main__":
    test_valid_pose_message()
    test_invalid_pose_message_is_rejected()
    print("pose message tests: PASS")
