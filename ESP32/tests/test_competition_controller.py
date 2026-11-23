import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from tools.competition_contract import CompetitionModel


ROOT = pathlib.Path(__file__).resolve().parents[1]
CONTROLLER_HEADER = (
    ROOT / "components" / "BSP" / "COMPETITION" / "competition_controller.h"
)
CONTROLLER_SOURCE = (
    ROOT / "components" / "BSP" / "COMPETITION" / "competition_controller.c"
)
WEB_SOURCE = ROOT / "components" / "BSP" / "WEB" / "web.c"


def test_stage_command_requires_prepare():
    model = CompetitionModel()
    assert not model.stage(0x01)
    assert model.state == "IDLE"


def test_voice_action_requires_task_one_stage():
    model = CompetitionModel()
    assert model.prepare()
    assert not model.voice(1)
    assert model.stage(0)
    assert model.voice(1)


def test_stage_order_is_selected_by_voice_events():
    model = CompetitionModel()
    assert model.prepare()
    assert model.stage(2)
    model.complete_stage()
    assert model.stage(0)
    model.complete_stage()
    assert model.stage(1)
    model.complete_stage()
    assert model.state == "COMPLETE"


def test_duplicate_stage_is_rejected():
    model = CompetitionModel()
    model.prepare()
    assert model.stage(0)
    model.complete_stage()
    assert not model.stage(0)


def test_visual_result_is_accepted_only_in_visual_stage():
    model = CompetitionModel()
    model.prepare()
    assert not model.visual(3)
    assert model.stage(1)
    assert model.arm_visual()
    assert model.visual(3)


def test_stage_switch_does_not_depend_on_action_count():
    model = CompetitionModel()
    assert model.prepare()
    assert model.stage(0)
    for _ in range(20):
        assert model.action_done(True)
    assert model.stage(1)


def test_visual_requires_a_new_start_command_for_each_pose():
    model = CompetitionModel()
    assert model.prepare()
    assert model.stage(1)
    assert not model.visual(1, now_ms=0)
    assert model.arm_visual()
    assert model.visual(1, now_ms=0)
    assert model.action_done(True, now_ms=0)
    assert not model.visual(2, now_ms=10_000)
    assert not model.stage(1)
    assert model.arm_visual()
    assert model.visual(2, now_ms=10_000)


def test_visual_arm_requires_an_active_visual_stage():
    model = CompetitionModel()
    assert model.prepare()
    assert not model.arm_visual()


def test_official_music_auto_flow_returns_to_recognition_wait():
    model = CompetitionModel()
    assert model.prepare()
    assert model.stage(2)
    assert model.music_start_recognition()
    assert model.music_recognized(3)
    assert model.state == "MUSIC_ANNOUNCE_DELAY"
    assert model.music_start_performance(3)
    assert model.music_performance_done()
    assert model.state == "MUSIC_WAIT"


def test_custom_music_auto_flow_completes_music_task():
    model = CompetitionModel()
    assert model.prepare()
    assert model.stage(2)
    assert model.music_start_recognition()
    assert model.music_recognized(6)
    assert model.music_start_performance(6)
    assert model.music_performance_done()
    assert model.state == "COMPLETE"


def test_announcement_failure_restores_retry_state():
    model = CompetitionModel()
    assert model.prepare()
    assert model.stage(2)
    assert model.music_start_recognition()
    assert model.music_recognized(1)
    assert model.music_announcement_failed()
    assert model.state == "MUSIC_WAIT"


def test_delay_rejects_new_recognition_and_wrong_music_id():
    model = CompetitionModel()
    assert model.prepare()
    assert model.stage(2)
    assert model.music_start_recognition()
    assert model.music_recognized(2)
    assert not model.music_start_recognition()
    assert not model.music_start_performance(3)
    assert model.music_start_performance(2)


def test_c_controller_exposes_automatic_music_delay_contract():
    header = CONTROLLER_HEADER.read_text(encoding="utf-8")
    source = CONTROLLER_SOURCE.read_text(encoding="utf-8")
    assert "COMPETITION_MUSIC_ANNOUNCE_DELAY" in header
    assert "competition_on_music_announcement_failed(void)" in header
    assert "competition_on_music_performance_start(uint8_t music_id)" in header
    assert "COMPETITION_MUSIC_READY_OFFICIAL" not in header
    assert "COMPETITION_MUSIC_READY_CUSTOM" not in header
    assert "s_status.last_music_id == 6U" in source


def test_web_status_name_tracks_the_automatic_music_delay_state():
    source = WEB_SOURCE.read_text(encoding="utf-8")
    assert "COMPETITION_MUSIC_ANNOUNCE_DELAY" in source
    assert "COMPETITION_MUSIC_READY_OFFICIAL" not in source
    assert "COMPETITION_MUSIC_READY_CUSTOM" not in source


if __name__ == "__main__":
    test_stage_command_requires_prepare()
    test_voice_action_requires_task_one_stage()
    test_stage_order_is_selected_by_voice_events()
    test_duplicate_stage_is_rejected()
    test_visual_result_is_accepted_only_in_visual_stage()
    test_stage_switch_does_not_depend_on_action_count()
    test_visual_requires_a_new_start_command_for_each_pose()
    test_visual_arm_requires_an_active_visual_stage()
    test_official_music_auto_flow_returns_to_recognition_wait()
    test_custom_music_auto_flow_completes_music_task()
    test_announcement_failure_restores_retry_state()
    test_delay_rejects_new_recognition_and_wrong_music_id()
    print("competition controller contract tests: PASS")
