import pathlib
import re


SOURCE = pathlib.Path(__file__).resolve().parents[1] / "docs" / "arduino_action_bridge.ino"


EXPECTED_COMMANDS = (
    0x31, 0x33, 0x32, 0x34, 0x39, 0x61, 0x62, 0x63,
    0x42, 0x43, 0x66, 0x64, 0x65, 0x41, 0x35, 0x36,
    0x38, 0x44, 0x45,
)


def parsed_actions():
    source = SOURCE.read_text(encoding="utf-8")
    action_table = source.split("const ActionCommand actions[] = {", 1)[1]
    action_table = action_table.split("};", 1)[0]
    return re.findall(
        r'\{(0x[0-9A-F]{2}),\s*"([^"]+)",\s*"([^"]+)",\s*(\d+)\}',
        action_table,
    )


def test_bridge_has_exactly_19_ordered_actions():
    rows = parsed_actions()
    assert len(rows) == 19
    assert [int(row[0], 16) for row in rows] == list(EXPECTED_COMMANDS)


def test_rb_button_and_robot_command_are_absent():
    source = SOURCE.read_text(encoding="utf-8")
    assert '"RB"' not in source
    assert "1" not in [row[2] for row in parsed_actions()]


def test_reset_is_restored_as_an_independent_maintenance_command():
    source = SOURCE.read_text(encoding="utf-8")
    assert 'resetCommand = {0x46, "RESET", "J", 1500}' in source
    assert "command == resetCommand.command" in source


def test_bridge_button_order_matches_the_approved_design():
    assert [row[1] for row in parsed_actions()] == [
        "LA", "LB", "LC", "LD", "UP", "DW", "LF", "RG", "L1", "L2",
        "X", "Y", "A", "B", "RA", "RC", "RD", "R1", "R2",
    ]
