import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import tools.serial_contract as serial_contract
from tools.serial_contract import (
    ARDUINO_COMMAND_BYTES,
    build_frame,
    parse,
    stage_for_command,
    translate_su03_action,
    translate_visual_message,
    translate_music_dance,
)


EXPECTED_COMMANDS = (
    0x31, 0x33, 0x32, 0x34, 0x39, 0x61, 0x62, 0x63,
    0x42, 0x43, 0x66, 0x64, 0x65, 0x41, 0x35, 0x36,
    0x38, 0x44, 0x45,
)
ARDUINO_MAP_SOURCE = (
    pathlib.Path(__file__).resolve().parents[1]
    / "components"
    / "BSP"
    / "SERIAL_BRIDGE"
    / "arduino_command_map.c"
)


def test_message_4_frame():
    assert build_frame(4) == bytes.fromhex("AA 55 04 55 AA")


def test_music_3_message_frame():
    assert build_frame(12) == bytes.fromhex("AA 55 0C 55 AA")


def test_split_frame_and_resynchronization():
    assert parse([0xAA, 0x55, 0x04, 0x55, 0xAA]) == [4]
    assert parse([0xAA, 0x55, 0x0C, 0x55, 0xAA]) == [12]
    assert parse([
        0xAA, 0x55, 0x04, 0x00, 0xAA,
        0xAA, 0x55, 0x07, 0x55, 0xAA,
    ]) == [7]


def test_arduino_command_contract_has_exactly_19_sequential_mappings():
    assert tuple(ARDUINO_COMMAND_BYTES) == EXPECTED_COMMANDS
    assert 0x37 not in ARDUINO_COMMAND_BYTES
    assert 0x46 not in ARDUINO_COMMAND_BYTES


def test_reset_is_known_but_is_not_a_competition_action():
    assert serial_contract.ARDUINO_RESET_COMMAND == 0x46
    assert serial_contract.is_known_arduino_command(0x46)
    assert 0x46 not in ARDUINO_COMMAND_BYTES
    source = ARDUINO_MAP_SOURCE.read_text(encoding="utf-8")
    assert "if (byte == 0x46U)" in source


def test_su03_actions_translate_all_thirteen_actions_in_order():
    assert [translate_su03_action(value) for value in range(1, 14)] == list(
        EXPECTED_COMMANDS[:13]
    )


def test_su03_hex_style_bytes_10_to_13_select_last_four_voice_actions():
    assert [translate_su03_action(value) for value in range(0x10, 0x14)] == [
        0x43, 0x66, 0x64, 0x65,
    ]
    source = ARDUINO_MAP_SOURCE.read_text(encoding="utf-8")
    assert "su03_byte >= 0x10U && su03_byte <= 0x13U" in source


def test_visual_custom_actions_reuse_voice_keys():
    assert [translate_visual_message(value) for value in range(1, 7)] == [
        0x41, 0x35, 0x36, 0x38, 0x64, 0x65,
    ]
    assert translate_visual_message(5) == translate_su03_action(12)
    assert translate_visual_message(6) == translate_su03_action(13)


def test_all_official_music_uses_r1_and_custom_uses_r2():
    assert [translate_music_dance(value) for value in range(1, 6)] == [0x44] * 5
    assert translate_music_dance(6) == 0x45


def test_new_su03_stage_commands_only():
    assert stage_for_command(0x44) == 0
    assert stage_for_command(0x55) == 1
    assert stage_for_command(0x66) == 2
    assert stage_for_command(0x22) == 1
    assert stage_for_command(0x33) == 2
    assert stage_for_command(0xAA) is None
    assert stage_for_command(0xBB) is None
    assert stage_for_command(0xCC) is None


if __name__ == "__main__":
    test_message_4_frame()
    test_music_3_message_frame()
    test_split_frame_and_resynchronization()
    test_arduino_command_contract_has_21_known_bytes()
    test_su03_actions_translate_to_arduino_ascii_commands()
    test_su03_stage_and_music_commands()
    print("serial contract tests: PASS")
