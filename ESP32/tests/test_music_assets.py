import os
import pathlib
import re
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import tools.music_features as music_features
from tools.music_features import (
    FINGERPRINT_MIN_OVERLAP,
    generate_features,
    match_fingerprint_window,
    match_weighted_fingerprint_window,
    validate_assets,
)


SOURCE = pathlib.Path(
    os.environ.get(
        "MUSIC_ASSET_DIR",
        r"D:\robot\舞蹈-表演赛指定音乐",
    )
)
EXPECTED_HEADER = (
    pathlib.Path(__file__).resolve().parents[1]
    / "components"
    / "BSP"
    / "MUSIC"
    / "generated_music_features.h"
)


def test_assets_discover_five_official_and_fixed_test_audio():
    assets = music_features.discover_assets(SOURCE)
    assert list(assets) == [1, 2, 3, 4, 5, 6]
    assert assets[6].name == "试音.mp3"
    assert assets[6].suffix.lower() == ".mp3"


def test_fixed_test_audio_keeps_native_rate_before_shared_resampling():
    assets = music_features.discover_assets(SOURCE)
    rate, seconds, mono = music_features._read_asset(assets[6])
    assert rate == 48_000
    assert seconds == 15.0
    assert len(mono) == rate * 15


def test_six_reference_files_are_valid():
    metadata = validate_assets(SOURCE)
    assert [item["id"] for item in metadata] == [1, 2, 3, 4, 5, 6]
    assert all(item["seconds"] == 15.0 for item in metadata)


def test_feature_header_contains_six_equal_fingerprints():
    with tempfile.TemporaryDirectory(
        dir=EXPECTED_HEADER.parents[3], prefix=".music_features_test_"
    ) as temp_dir:
        output = pathlib.Path(temp_dir) / "generated_music_features.h"
        result = generate_features(SOURCE, output)
        assert result.reference_count == 6
        assert result.feature_size == 231
        content = output.read_text(encoding="utf-8")
        assert "MUSIC_REFERENCE_COUNT 6" in content
        assert "MUSIC_FINGERPRINT_LENGTH 231" in content
        assert "static const uint32_t" in content
        assert "music_reference_fingerprints[MUSIC_REFERENCE_COUNT]" in content
        assert content == EXPECTED_HEADER.read_text(encoding="utf-8")


def test_matcher_finds_reference_after_capture_started_late():
    def frame(seed):
        peaks = (
            3 + seed % 5,
            14 + (seed // 2) % 5,
            27 + (seed // 3) % 5,
            40 + (seed // 4) % 5,
        )
        return sum(value << (peak * 6) for peak, value in enumerate(peaks))

    reference = [frame(index) for index in range(140)]
    query = reference[23:] + [0] * 23

    matches, overlap = match_fingerprint_window(query, reference)

    assert overlap >= 117
    assert matches >= 2 * 117


def test_matcher_rejects_impossible_overlap():
    reference = [0] * (FINGERPRINT_MIN_OVERLAP - 1)
    try:
        match_fingerprint_window(reference, reference)
    except ValueError as error:
        assert "min_overlap" in str(error)
    else:
        raise AssertionError("short fingerprints must be rejected")


def test_matcher_votes_for_three_or_four_peaks_only():
    def pack(peaks):
        return sum(value << (peak * 6) for peak, value in enumerate(peaks))

    reference = [pack((4, 14, 28, 40))] * FINGERPRINT_MIN_OVERLAP
    three_peak_query = [pack((4, 14, 28, 47))] * FINGERPRINT_MIN_OVERLAP
    two_peak_query = [pack((4, 14, 50, 56))] * FINGERPRINT_MIN_OVERLAP

    three_peak_votes, _ = match_fingerprint_window(
        three_peak_query, reference
    )
    two_peak_votes, _ = match_fingerprint_window(two_peak_query, reference)

    assert three_peak_votes == FINGERPRINT_MIN_OVERLAP
    assert two_peak_votes == 0


def test_generated_references_vote_most_strongly_for_themselves():
    content = EXPECTED_HEADER.read_text(encoding="utf-8")
    body = content.split(
        "music_reference_fingerprints[MUSIC_REFERENCE_COUNT]"
        "[MUSIC_FINGERPRINT_LENGTH] = {", 1
    )[1]
    references = []
    for row in re.findall(r"\{([^{}]+)\}", body):
        values = [int(value.strip()) for value in row.split(",") if value.strip()]
        if len(values) == 231:
            references.append(values)

    assert len(references) == 6
    for expected_id, query in enumerate(references):
        votes = [
            match_fingerprint_window(query, reference)[0]
            for reference in references
        ]
        assert max(range(len(votes)), key=votes.__getitem__) == expected_id


def test_weighted_references_vote_most_strongly_for_themselves():
    content = EXPECTED_HEADER.read_text(encoding="utf-8")
    body = content.split(
        "music_reference_fingerprints[MUSIC_REFERENCE_COUNT]"
        "[MUSIC_FINGERPRINT_LENGTH] = {", 1
    )[1]
    references = []
    for row in re.findall(r"\{([^{}]+)\}", body):
        values = [int(value.strip()) for value in row.split(",") if value.strip()]
        if len(values) == 231:
            references.append(values)

    assert len(references) == 6
    for expected_id, query in enumerate(references):
        votes = [
            match_weighted_fingerprint_window(query, reference)[0]
            for reference in references
        ]
        assert max(range(len(votes)), key=votes.__getitem__) == expected_id


if __name__ == "__main__":
    test_asset_prefixes_discover_five_official_and_one_custom()
    test_six_reference_files_are_valid()
    test_feature_header_contains_six_equal_fingerprints()
    test_matcher_finds_reference_after_capture_started_late()
    test_matcher_rejects_impossible_overlap()
    print("music asset tests: PASS")
