import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
AUDIO_SOURCE = ROOT / "components" / "BSP" / "AUDIO_I2S" / "audio_i2s.c"
AUDIO_HEADER = ROOT / "components" / "BSP" / "AUDIO_I2S" / "audio_i2s.h"
MAIN_SOURCE = ROOT / "main" / "main.c"


def test_audio_i2s_is_receive_only_and_releases_gpio16():
    source = AUDIO_SOURCE.read_text(encoding="utf-8")
    header = AUDIO_HEADER.read_text(encoding="utf-8")
    main = MAIN_SOURCE.read_text(encoding="utf-8")

    assert "i2s_new_channel(&channel_config, NULL, &s_rx_channel)" in source
    assert ".dout = I2S_GPIO_UNUSED" in source
    assert "GPIO_NUM_16" not in source
    assert "audio_i2s_write_pcm16" not in source
    assert "audio_i2s_play_test_tone" not in source
    assert "audio_i2s_write_pcm16" not in header
    assert "audio_i2s_play_test_tone" not in header
    assert "MAX98357" not in main
