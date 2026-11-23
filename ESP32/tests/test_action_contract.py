import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from tools.action_contract import (  # noqa: E402
    ACTION_ACK_ERROR,
    ActionBridgeModel,
)


def test_command_is_not_complete_until_ack():
    bridge = ActionBridgeModel(timeout_ms=20_000)

    assert bridge.start(0x31, now_ms=100)
    assert not bridge.complete
    assert bridge.accept_ack(0x31, now_ms=500)
    assert bridge.complete
    assert bridge.success


def test_second_command_is_rejected_while_first_is_pending():
    bridge = ActionBridgeModel(timeout_ms=20_000)

    assert bridge.start(0x31, now_ms=100)
    assert not bridge.start(0x32, now_ms=200)
    assert bridge.pending_command == 0x31


def test_error_ack_completes_action_as_failure():
    bridge = ActionBridgeModel(timeout_ms=20_000)

    assert bridge.start(0x31, now_ms=100)
    assert bridge.accept_ack(ACTION_ACK_ERROR, now_ms=500)
    assert bridge.complete
    assert not bridge.success


def test_pending_action_times_out_as_failure():
    bridge = ActionBridgeModel(timeout_ms=1_000)

    assert bridge.start(0x31, now_ms=100)
    assert not bridge.check_timeout(now_ms=1_099)
    assert bridge.check_timeout(now_ms=1_100)
    assert bridge.complete
    assert not bridge.success


if __name__ == "__main__":
    test_command_is_not_complete_until_ack()
    test_second_command_is_rejected_while_first_is_pending()
    test_error_ack_completes_action_as_failure()
    test_pending_action_times_out_as_failure()
    print("action contract tests: PASS")
