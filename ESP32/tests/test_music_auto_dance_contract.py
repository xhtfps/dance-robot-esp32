import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from tools.music_flow_contract import MusicFlowModel


ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT / "main" / "main.c"


def test_official_prompt_precedes_common_dance_by_five_seconds():
    flow = MusicFlowModel()
    assert flow.recognized(3, 1_000) == 9
    assert flow.dance_due(5_999) is None
    assert flow.dance_due(6_000) == 0x44


def test_custom_prompt_precedes_custom_dance_by_five_seconds():
    flow = MusicFlowModel()
    assert flow.recognized(6, 10_000) == 12
    assert flow.dance_due(14_999) is None
    assert flow.dance_due(15_000) == 0x45


def test_repeated_result_is_rejected_during_announcement_delay():
    flow = MusicFlowModel()
    assert flow.recognized(1, 0) == 7
    assert flow.recognized(2, 100) is None


def test_main_uses_new_protocol_and_no_explicit_dance_commands():
    source = MAIN.read_text(encoding="utf-8")
    assert "#define SU03_TASK1_COMMAND 0x44U" in source
    assert "#define SU03_TASK2_COMMAND 0x55U" in source
    assert "#define SU03_TASK3_COMMAND 0x66U" in source
    assert "#define SU03_LEAR_EYES_COMMAND 0x22U" in source
    assert "#define SU03_LEAR_MUSIC_COMMAND 0x33U" in source
    assert "MUSIC_DANCE_DELAY_MS 5000U" in source
    assert "SU03_OFFICIAL_MUSIC" not in source
    assert "SU03_CUSTOM_MUSIC" not in source


def test_main_uses_command_specific_ack_timeout():
    source = MAIN.read_text(encoding="utf-8")
    assert "arduino_command_ack_timeout_ms(arduino_byte)" in source
    assert "s_pending_ack_timeout_ms" in source


def test_main_routes_recognition_to_speech_then_delay_task():
    source = MAIN.read_text(encoding="utf-8")
    assert "on_music_recognizer_done" in source
    assert "const uint8_t message_id = (uint8_t)(6U + result->music_id);" in source
    assert "music_dance_delay_task" in source
    assert "music_recognizer_init(on_music_recognizer_done, NULL)" in source
