import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT
    / "components"
    / "BSP"
    / "MUSIC"
    / "music_recognizer.c"
)
HEADER = (
    ROOT
    / "components"
    / "BSP"
    / "MUSIC"
    / "music_recognizer.h"
)


def test_music_runtime_captures_16_seconds_with_overlapping_analysis_windows():
    source = SOURCE.read_text(encoding="utf-8")
    assert "#define MUSIC_CAPTURE_SECONDS         16U" in source
    assert "#define MUSIC_ANALYSIS_WINDOW_BLOCKS  16U" in source
    assert "#define MUSIC_ANALYSIS_HOP_BLOCKS     4U" in source
    assert "#define MUSIC_FINGERPRINT_PEAK_COUNT  4U" in source
    assert "MUSIC_CAPTURE_FEATURE_COUNT" in source


def test_music_runtime_allows_late_capture_alignment():
    source = SOURCE.read_text(encoding="utf-8")
    assert "MUSIC_MIN_REFERENCE_OVERLAP" in source
    assert "reference_offset" in source
    assert "feature_frame_match" in source
    assert "weighted_votes" in source
    assert "candidate.votes += 2U" in source
    assert "++candidate.votes" in source


def test_music_recognizer_reports_a_typed_result_callback():
    header = HEADER.read_text(encoding="utf-8")
    assert "music_recognition_result_t" in header
    assert "music_recognizer_done_cb_t" in header
    assert "music_recognizer_init(music_recognizer_done_cb_t callback" in header


def test_music_recognizer_no_longer_controls_competition_or_speech():
    source = SOURCE.read_text(encoding="utf-8")
    assert "competition_on_music" not in source
    assert "serial_bridge_send_to_su03_message" not in source
    assert "s_done_callback(&result, s_done_context);" in source


def test_music_recognizer_reports_failure_through_the_same_callback():
    source = SOURCE.read_text(encoding="utf-8")
    assert "ESP_ERR_NOT_FOUND" in source
    assert ".music_id = 0xFFU" in source


def test_music_runtime_emits_actionable_diagnostics():
    source = SOURCE.read_text(encoding="utf-8")
    assert "DIAG capture second=" in source
    assert "DIAG fingerprints:" in source
    assert "DIAG candidate=" in source
    assert "Weighted votes:" in source
    assert "DIAG decision:" in source
    assert "DIAG rejected:" in source


def test_music_runtime_discards_voice_prompt_before_capture():
    source = SOURCE.read_text(encoding="utf-8")
    assert "#define MUSIC_PROMPT_DISCARD_MS       2500U" in source
    assert "const bool prompt_guard_ok = discard_prompt_audio();" in source
    assert "if (prompt_guard_ok && read_samples())" in source
    assert "Prompt guard complete:" in source


