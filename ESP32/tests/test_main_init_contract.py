import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_SOURCE = ROOT / "main" / "main.c"


def test_app_main_does_not_initialize_face_display():
    source = MAIN_SOURCE.read_text(encoding="utf-8")

    assert "ESP_ERROR_CHECK(face_display_init())" not in source
