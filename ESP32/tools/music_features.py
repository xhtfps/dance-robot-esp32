import argparse
import math
import re
import struct
import wave
from dataclasses import dataclass
from pathlib import Path


TARGET_RATE = 16_000
FEATURE_SECONDS = 15.0
SPECTRUM_BLOCK_SAMPLES = 256
ANALYSIS_WINDOW_BLOCKS = 16
ANALYSIS_HOP_BLOCKS = 4
FINGERPRINT_BIN_START = 2
FINGERPRINT_BIN_END = 48
FINGERPRINT_PEAK_COUNT = 4
FINGERPRINT_MIN_OVERLAP = 96
FINGERPRINT_BIN_TOLERANCE = 2

_TWIDDLE_REAL = [
    [
        math.cos(2.0 * math.pi * band * sample / SPECTRUM_BLOCK_SAMPLES)
        for sample in range(SPECTRUM_BLOCK_SAMPLES)
    ]
    for band in range(FINGERPRINT_BIN_START, FINGERPRINT_BIN_END + 1)
]
_TWIDDLE_IMAG = [
    [
        -math.sin(2.0 * math.pi * band * sample / SPECTRUM_BLOCK_SAMPLES)
        for sample in range(SPECTRUM_BLOCK_SAMPLES)
    ]
    for band in range(FINGERPRINT_BIN_START, FINGERPRINT_BIN_END + 1)
]


@dataclass(frozen=True)
class FeatureResult:
    reference_count: int
    feature_size: int


def _unpack_fingerprint(fingerprint):
    return tuple(
        (fingerprint >> (peak * 6)) & 0x3F
        for peak in range(FINGERPRINT_PEAK_COUNT)
    )


def _fingerprint_frame_matches(left, right):
    """Return one-to-one peak matches for two sorted Top-4 fingerprints."""
    left_peaks = _unpack_fingerprint(left)
    right_peaks = _unpack_fingerprint(right)
    left_index = 0
    right_index = 0
    matches = 0
    while (left_index < FINGERPRINT_PEAK_COUNT and
           right_index < FINGERPRINT_PEAK_COUNT):
        difference = left_peaks[left_index] - right_peaks[right_index]
        if abs(difference) <= FINGERPRINT_BIN_TOLERANCE:
            matches += 1
            left_index += 1
            right_index += 1
        elif difference < 0:
            left_index += 1
        else:
            right_index += 1
    return matches


def _fingerprint_frame_weighted_votes(left, right):
    """Give discriminative mid/high-frequency peak matches more influence."""
    left_peaks = _unpack_fingerprint(left)
    right_peaks = _unpack_fingerprint(right)
    left_index = 0
    right_index = 0
    votes = 0
    while (left_index < FINGERPRINT_PEAK_COUNT and
           right_index < FINGERPRINT_PEAK_COUNT):
        difference = left_peaks[left_index] - right_peaks[right_index]
        if abs(difference) <= FINGERPRINT_BIN_TOLERANCE:
            average_bin = (left_peaks[left_index] +
                           right_peaks[right_index]) // 2
            weight_base = 12 + average_bin
            votes += weight_base * weight_base * weight_base * weight_base
            left_index += 1
            right_index += 1
        elif difference < 0:
            left_index += 1
        else:
            right_index += 1
    return votes


def match_weighted_fingerprint_window(query, reference,
                                      min_overlap=FINGERPRINT_MIN_OVERLAP):
    """Return frequency-weighted peak votes at the best time offset."""
    if min_overlap <= 0:
        raise ValueError("min_overlap must be positive")
    if min_overlap > min(len(query), len(reference)):
        raise ValueError("min_overlap exceeds the available fingerprints")

    best_votes = 0
    best_overlap = 0
    best_offset = 0
    for offset in range(-len(query) + min_overlap,
                        len(reference) - min_overlap + 1):
        query_start = max(0, -offset)
        query_end = min(len(query), len(reference) - offset)
        overlap = query_end - query_start
        votes = sum(
            _fingerprint_frame_weighted_votes(
                query[index], reference[index + offset]
            )
            for index in range(query_start, query_end)
        )
        if (votes > best_votes or
                (votes == best_votes and overlap > best_overlap)):
            best_votes = votes
            best_overlap = overlap
            best_offset = offset
    return best_votes, best_overlap, best_offset


def match_fingerprint_window(query, reference, min_overlap=FINGERPRINT_MIN_OVERLAP):
    """Return peak-match points while allowing any capture start offset."""
    if min_overlap <= 0:
        raise ValueError("min_overlap must be positive")
    if min_overlap > min(len(query), len(reference)):
        raise ValueError("min_overlap exceeds the available fingerprints")

    best_votes = 0
    best_strong_matches = 0
    best_overlap = 0
    for offset in range(-len(query) + min_overlap, len(reference) - min_overlap + 1):
        query_start = max(0, -offset)
        query_end = min(len(query), len(reference) - offset)
        overlap = query_end - query_start
        if overlap < min_overlap:
            continue
        frame_matches = [
            _fingerprint_frame_matches(query[index], reference[index + offset])
            for index in range(query_start, query_end)
        ]
        perfect_matches = sum(
            matches == FINGERPRINT_PEAK_COUNT for matches in frame_matches
        )
        triple_matches = sum(
            matches == FINGERPRINT_PEAK_COUNT - 1 for matches in frame_matches
        )
        strong_matches = perfect_matches + triple_matches
        votes = perfect_matches * 2 + triple_matches
        if votes > best_votes or (
            votes == best_votes and strong_matches > best_strong_matches
        ) or (
            votes == best_votes and
            strong_matches == best_strong_matches and
            overlap > best_overlap
        ):
            best_votes = votes
            best_strong_matches = strong_matches
            best_overlap = overlap

    return best_votes, best_overlap


def _read_wav(path):
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        rate = source.getframerate()
        width = source.getsampwidth()
        frames = source.getnframes()
        if channels != 2 or width != 2 or rate not in (44_100, 48_000):
            raise ValueError(
                f"{path.name}: expected stereo 16-bit PCM at 44.1/48 kHz, "
                f"got channels={channels}, width={width}, rate={rate}"
            )
        source_seconds = frames / rate
        if not 14.0 <= source_seconds <= 17.0:
            raise ValueError(
                f"{path.name}: duration {source_seconds:.3f}s is outside 14-17s"
            )
        # Match the competition and ESP32 runtime recognition window.
        feature_frames = min(frames, int(rate * FEATURE_SECONDS))
        raw = source.readframes(feature_frames)
        seconds = feature_frames / rate
    values = struct.unpack("<" + "h" * (len(raw) // 2), raw)
    mono = [(values[i] + values[i + 1]) * 0.5 for i in range(0, len(values), 2)]
    return rate, seconds, mono


def _read_mp3_prefix(path):
    try:
        import miniaudio
    except ImportError as error:
        raise RuntimeError(
            "MP3 feature generation requires tools/requirements-music.txt"
        ) from error

    info = miniaudio.get_file_info(str(path))
    if info.nchannels != 2 or info.sample_rate not in (44_100, 48_000):
        raise ValueError(
            f"{path.name}: expected stereo MP3 at 44.1/48 kHz, "
            f"got channels={info.nchannels}, rate={info.sample_rate}"
        )

    decoded = miniaudio.decode(
        path.read_bytes(),
        output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=info.nchannels,
        sample_rate=info.sample_rate,
    )
    required_frames = int(info.sample_rate * FEATURE_SECONDS)
    required_samples = required_frames * info.nchannels
    if len(decoded.samples) < required_samples:
        raise ValueError(f"{path.name}: shorter than {FEATURE_SECONDS:.0f}s")
    values = decoded.samples[:required_samples]
    mono = [
        (values[index] + values[index + 1]) * 0.5
        for index in range(0, required_samples, info.nchannels)
    ]
    return info.sample_rate, FEATURE_SECONDS, mono


def discover_assets(source_dir):
    source_dir = Path(source_dir)
    files = sorted(path for path in source_dir.iterdir() if path.is_file())
    assets = {}
    for music_id in range(1, 6):
        matches = [
            path
            for path in files
            if re.match(rf"^{music_id}(?:\D|$)", path.stem)
            and path.suffix.lower() == ".wav"
        ]
        if len(matches) != 1:
            raise ValueError(
                f"music {music_id}: expected one numeric-prefix WAV, "
                f"got {matches}"
            )
        assets[music_id] = matches[0]

    custom = source_dir / "试音.mp3"
    if not custom.is_file():
        raise FileNotFoundError(f"music 6: missing {custom}")
    assets[6] = custom
    return assets


def _read_asset(path):
    if path.suffix.lower() == ".wav":
        return _read_wav(path)
    if path.suffix.lower() == ".mp3":
        return _read_mp3_prefix(path)
    raise ValueError(f"{path.name}: unsupported music asset format")


def validate_assets(source_dir):
    assets = discover_assets(source_dir)
    metadata = []
    for music_id, path in assets.items():
        rate, seconds, _ = _read_asset(path)
        metadata.append({"id": music_id, "rate": rate, "seconds": seconds})
    return metadata


def _resample(samples, source_rate):
    target_count = round(len(samples) * TARGET_RATE / source_rate)
    ratio = source_rate / TARGET_RATE
    result = []
    for index in range(target_count):
        position = index * ratio
        left = min(int(position), len(samples) - 1)
        right = min(left + 1, len(samples) - 1)
        fraction = position - left
        result.append(samples[left] * (1.0 - fraction) + samples[right] * fraction)
    return result


def _spectrum_block_energies(samples):
    energies = []
    for offset in range(0, len(samples) - SPECTRUM_BLOCK_SAMPLES + 1,
                        SPECTRUM_BLOCK_SAMPLES):
        block = samples[offset:offset + SPECTRUM_BLOCK_SAMPLES]
        block_energies = []
        for band in range(len(_TWIDDLE_REAL)):
            real = sum(
                value * _TWIDDLE_REAL[band][index]
                for index, value in enumerate(block)
            )
            imag = sum(
                value * _TWIDDLE_IMAG[band][index]
                for index, value in enumerate(block)
            )
            block_energies.append(math.sqrt(real * real + imag * imag))
        energies.append(block_energies)
    return energies


def fingerprint_samples(samples):
    """Build overlapping spectral-peak frames for one 16 kHz mono clip."""
    block_energies = _spectrum_block_energies(samples)
    if len(block_energies) < ANALYSIS_WINDOW_BLOCKS:
        return []

    fingerprints = []
    band_count = FINGERPRINT_BIN_END - FINGERPRINT_BIN_START + 1
    window_energies = [
        sum(block[band] for block in block_energies[:ANALYSIS_WINDOW_BLOCKS])
        for band in range(band_count)
    ]
    for start_block in range(
        0, len(block_energies) - ANALYSIS_WINDOW_BLOCKS + 1,
        ANALYSIS_HOP_BLOCKS,
    ):
        if start_block != 0:
            previous_start = start_block - ANALYSIS_HOP_BLOCKS
            previous_end = previous_start + ANALYSIS_HOP_BLOCKS
            new_start = start_block + ANALYSIS_WINDOW_BLOCKS - ANALYSIS_HOP_BLOCKS
            new_end = start_block + ANALYSIS_WINDOW_BLOCKS
            for band in range(band_count):
                window_energies[band] -= sum(
                    block_energies[index][band]
                    for index in range(previous_start, previous_end)
                )
                window_energies[band] += sum(
                    block_energies[index][band]
                    for index in range(new_start, new_end)
                )

        energies = []
        energies.extend(window_energies)

        selected = []
        for _ in range(FINGERPRINT_PEAK_COUNT):
            candidates = [
                index for index in range(len(energies))
                if all(abs(index - previous) > 2 for previous in selected)
            ]
            if not candidates:
                selected.append(0)
                continue
            selected.append(max(candidates, key=energies.__getitem__))
        selected.sort()
        fingerprints.append(
            sum(value << (peak * 6) for peak, value in enumerate(selected))
        )
    return fingerprints


def generate_features(source_dir, output_path):
    output_path = Path(output_path)
    metadata = validate_assets(source_dir)
    assets = discover_assets(source_dir)
    fingerprints = []
    for item in metadata:
        _, _, mono = _read_asset(assets[item["id"]])
        fingerprints.append(fingerprint_samples(_resample(mono, item["rate"])))

    feature_size = len(fingerprints[0])
    if any(len(fingerprint) != feature_size for fingerprint in fingerprints):
        raise ValueError("music fingerprints do not have equal length")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8", newline="\n") as output:
        output.write("#pragma once\n\n")
        output.write("#include <stdint.h>\n\n")
        output.write(f"#define MUSIC_REFERENCE_COUNT {len(fingerprints)}\n")
        output.write(f"#define MUSIC_FINGERPRINT_LENGTH {feature_size}\n\n")
        output.write(
            "static const uint32_t "
            "music_reference_fingerprints[MUSIC_REFERENCE_COUNT]"
            "[MUSIC_FINGERPRINT_LENGTH] = {\n"
        )
        for fingerprint in fingerprints:
            output.write(
                "    {" + ", ".join(str(value) for value in fingerprint) + "},\n"
            )
        output.write("};\n")
    return FeatureResult(len(fingerprints), feature_size)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("components/BSP/MUSIC/generated_music_features.h"),
    )
    args = parser.parse_args()
    result = generate_features(args.source, args.output)
    print(f"generated {result.reference_count} references, {result.feature_size} features each")


if __name__ == "__main__":
    main()
