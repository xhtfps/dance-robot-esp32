#include "music_recognizer.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "audio_i2s.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "generated_music_features.h"

#define MUSIC_CAPTURE_SECONDS         16U
#define MUSIC_CAPTURE_SAMPLES         (AUDIO_I2S_SAMPLE_RATE_HZ * MUSIC_CAPTURE_SECONDS)
#define MUSIC_PROMPT_DISCARD_MS       2500U
#define MUSIC_PROMPT_DISCARD_SAMPLES  ((AUDIO_I2S_SAMPLE_RATE_HZ * MUSIC_PROMPT_DISCARD_MS) / 1000U)
#define MUSIC_SPECTRUM_BLOCK_SAMPLES  256U
#define MUSIC_ANALYSIS_WINDOW_BLOCKS  16U
#define MUSIC_ANALYSIS_HOP_BLOCKS     4U
#define MUSIC_CAPTURE_BLOCK_COUNT     (MUSIC_CAPTURE_SAMPLES / MUSIC_SPECTRUM_BLOCK_SAMPLES)
#define MUSIC_CAPTURE_FEATURE_COUNT   (((MUSIC_CAPTURE_BLOCK_COUNT - MUSIC_ANALYSIS_WINDOW_BLOCKS) / MUSIC_ANALYSIS_HOP_BLOCKS) + 1U)
#define MUSIC_BIN_START               2U
#define MUSIC_BIN_END                 48U
#define MUSIC_BAND_COUNT              (MUSIC_BIN_END - MUSIC_BIN_START + 1U)
#define MUSIC_FINGERPRINT_PEAK_COUNT  4U
#define MUSIC_MIN_REFERENCE_OVERLAP   96U
#define MUSIC_BIN_TOLERANCE           2
#define MUSIC_CONFIDENCE_MIN          0.40f
#define MUSIC_WEIGHTED_MARGIN_MIN     0.08f
#define MUSIC_FEATURE_HOP_MS          ((MUSIC_ANALYSIS_HOP_BLOCKS * MUSIC_SPECTRUM_BLOCK_SAMPLES * 1000U) / AUDIO_I2S_SAMPLE_RATE_HZ)

static const char *TAG = "MUSIC_RECOGNIZER";
static float s_twiddle_real[MUSIC_BAND_COUNT][MUSIC_SPECTRUM_BLOCK_SAMPLES];
static float s_twiddle_imag[MUSIC_BAND_COUNT][MUSIC_SPECTRUM_BLOCK_SAMPLES];
static float s_energy_ring[MUSIC_ANALYSIS_WINDOW_BLOCKS][MUSIC_BAND_COUNT];
static float s_energy_sum[MUSIC_BAND_COUNT];
static int16_t *s_samples;
static TaskHandle_t s_task;
static volatile bool s_stop_requested;
static uint8_t s_last_music_id = 0xFFU;
static float s_last_confidence;
static music_recognizer_done_cb_t s_done_callback;
static void *s_done_context;
static bool s_initialized;

static void init_twiddle(void)
{
    for (size_t band = 0; band < MUSIC_BAND_COUNT; ++band) {
        for (size_t sample = 0; sample < MUSIC_SPECTRUM_BLOCK_SAMPLES; ++sample) {
            const float angle = 2.0f * (float)M_PI *
                                (float)(MUSIC_BIN_START + band) *
                                (float)sample /
                                (float)MUSIC_SPECTRUM_BLOCK_SAMPLES;
            s_twiddle_real[band][sample] = cosf(angle);
            s_twiddle_imag[band][sample] = -sinf(angle);
        }
    }
}

static size_t extract_fingerprints(const int16_t *samples, size_t sample_count,
                                   uint32_t *fingerprints,
                                   size_t fingerprint_capacity)
{
    memset(s_energy_ring, 0, sizeof(s_energy_ring));
    memset(s_energy_sum, 0, sizeof(s_energy_sum));

    size_t blocks_seen = 0U;
    size_t ring_index = 0U;
    size_t fingerprint_count = 0U;
    for (size_t offset = 0U;
         offset + MUSIC_SPECTRUM_BLOCK_SAMPLES <= sample_count;
         offset += MUSIC_SPECTRUM_BLOCK_SAMPLES) {
        for (size_t band = 0; band < MUSIC_BAND_COUNT; ++band) {
            float real = 0.0f;
            float imag = 0.0f;
            for (size_t sample = 0;
                 sample < MUSIC_SPECTRUM_BLOCK_SAMPLES;
                 ++sample) {
                const float value = (float)samples[offset + sample];
                real += value * s_twiddle_real[band][sample];
                imag += value * s_twiddle_imag[band][sample];
            }
            const float energy = sqrtf(real * real + imag * imag);
            s_energy_sum[band] -= s_energy_ring[ring_index][band];
            s_energy_ring[ring_index][band] = energy;
            s_energy_sum[band] += energy;
        }

        ++blocks_seen;
        ring_index = (ring_index + 1U) % MUSIC_ANALYSIS_WINDOW_BLOCKS;
        if (blocks_seen < MUSIC_ANALYSIS_WINDOW_BLOCKS ||
            (blocks_seen - MUSIC_ANALYSIS_WINDOW_BLOCKS) %
                    MUSIC_ANALYSIS_HOP_BLOCKS !=
                0U) {
            continue;
        }

        uint8_t selected[MUSIC_FINGERPRINT_PEAK_COUNT] = {0U};
        for (size_t peak = 0; peak < MUSIC_FINGERPRINT_PEAK_COUNT; ++peak) {
            size_t best_band = 0U;
            float best_energy = -1.0f;
            for (size_t band = 0; band < MUSIC_BAND_COUNT; ++band) {
                bool suppressed = false;
                for (size_t previous = 0; previous < peak; ++previous) {
                    const int difference = (int)band -
                                           (int)selected[previous];
                    if (difference >= -2 && difference <= 2) {
                        suppressed = true;
                        break;
                    }
                }
                if (!suppressed && s_energy_sum[band] > best_energy) {
                    best_band = band;
                    best_energy = s_energy_sum[band];
                }
            }
            selected[peak] = (uint8_t)best_band;
        }

        for (size_t left = 0U; left < MUSIC_FINGERPRINT_PEAK_COUNT; ++left) {
            for (size_t right = left + 1U;
                 right < MUSIC_FINGERPRINT_PEAK_COUNT;
                 ++right) {
                if (selected[left] > selected[right]) {
                    const uint8_t swap = selected[left];
                    selected[left] = selected[right];
                    selected[right] = swap;
                }
            }
        }
        if (fingerprint_count >= fingerprint_capacity) {
            break;
        }
        uint32_t packed = 0U;
        for (size_t peak = 0U; peak < MUSIC_FINGERPRINT_PEAK_COUNT; ++peak) {
            packed |= (uint32_t)selected[peak] << (peak * 6U);
        }
        fingerprints[fingerprint_count++] = packed;
    }
    return fingerprint_count;
}

typedef struct {
    uint8_t peak_matches;
    uint64_t weighted_votes;
} music_frame_match_t;

static music_frame_match_t feature_frame_match(uint32_t left, uint32_t right)
{
    uint8_t left_peaks[MUSIC_FINGERPRINT_PEAK_COUNT];
    uint8_t right_peaks[MUSIC_FINGERPRINT_PEAK_COUNT];
    for (size_t peak = 0U; peak < MUSIC_FINGERPRINT_PEAK_COUNT; ++peak) {
        left_peaks[peak] = (uint8_t)((left >> (peak * 6U)) & 0x3FU);
        right_peaks[peak] = (uint8_t)((right >> (peak * 6U)) & 0x3FU);
    }

    size_t left_index = 0U;
    size_t right_index = 0U;
    music_frame_match_t result = {0};
    while (left_index < MUSIC_FINGERPRINT_PEAK_COUNT &&
           right_index < MUSIC_FINGERPRINT_PEAK_COUNT) {
        const int difference = (int)left_peaks[left_index] -
                               (int)right_peaks[right_index];
        if (abs(difference) <= MUSIC_BIN_TOLERANCE) {
            const uint64_t average_band =
                ((uint64_t)left_peaks[left_index] +
                 (uint64_t)right_peaks[right_index]) /
                2U;
            const uint64_t weight_base = 12U + average_band;
            ++result.peak_matches;
            result.weighted_votes +=
                weight_base * weight_base * weight_base * weight_base;
            ++left_index;
            ++right_index;
        } else if (difference < 0) {
            ++left_index;
        } else {
            ++right_index;
        }
    }
    return result;
}

typedef struct {
    uint64_t weighted_votes;
    uint16_t votes;
    uint16_t overlap;
    uint16_t strong_matches;
    uint16_t perfect_matches;
    uint16_t triple_matches;
    uint16_t weak_matches;
    int16_t offset;
} music_vote_result_t;

static music_vote_result_t score_reference(const uint32_t *fingerprints,
                                           size_t fingerprint_count,
                                           const uint32_t *reference)
{
    music_vote_result_t best = {0};
    const int min_offset = -(int)fingerprint_count +
                           (int)MUSIC_MIN_REFERENCE_OVERLAP;
    const int max_offset = (int)MUSIC_FINGERPRINT_LENGTH -
                           (int)MUSIC_MIN_REFERENCE_OVERLAP;
    for (int reference_offset = min_offset;
         reference_offset <= max_offset;
         ++reference_offset) {
        const int query_start = reference_offset < 0 ? -reference_offset : 0;
        const int reference_limited_end =
            (int)MUSIC_FINGERPRINT_LENGTH - reference_offset;
        const int query_end = reference_limited_end < (int)fingerprint_count
                                  ? reference_limited_end
                                  : (int)fingerprint_count;
        const int overlap = query_end - query_start;
        if (overlap < (int)MUSIC_MIN_REFERENCE_OVERLAP) {
            continue;
        }

        music_vote_result_t candidate = {
            .overlap = (uint16_t)overlap,
            .offset = (int16_t)reference_offset,
        };
        for (int query_index = query_start;
             query_index < query_end;
             ++query_index) {
            const int reference_index = query_index + reference_offset;
            const music_frame_match_t frame_match = feature_frame_match(
                fingerprints[(size_t)query_index],
                reference[(size_t)reference_index]);
            candidate.weighted_votes += frame_match.weighted_votes;
            if (frame_match.peak_matches == MUSIC_FINGERPRINT_PEAK_COUNT) {
                ++candidate.perfect_matches;
                ++candidate.strong_matches;
                candidate.votes += 2U;
            } else if (frame_match.peak_matches ==
                       MUSIC_FINGERPRINT_PEAK_COUNT - 1U) {
                ++candidate.triple_matches;
                ++candidate.strong_matches;
                ++candidate.votes;
            } else {
                ++candidate.weak_matches;
            }
        }
        if (candidate.weighted_votes > best.weighted_votes ||
            (candidate.weighted_votes == best.weighted_votes &&
             candidate.votes > best.votes) ||
            (candidate.weighted_votes == best.weighted_votes &&
             candidate.votes == best.votes &&
             candidate.strong_matches > best.strong_matches) ||
            (candidate.weighted_votes == best.weighted_votes &&
             candidate.votes == best.votes &&
             candidate.strong_matches == best.strong_matches &&
             candidate.overlap > best.overlap)) {
            best = candidate;
        }
    }
    return best;
}

static void log_fingerprint_summary(const uint32_t *fingerprints,
                                    size_t fingerprint_count)
{
    uint32_t hash = 2166136261U;
    size_t unique_count = 0U;
    size_t dominant_repeats = 0U;
    for (size_t index = 0U; index < fingerprint_count; ++index) {
        hash ^= fingerprints[index];
        hash *= 16777619U;
        bool seen_before = false;
        size_t repeats = 0U;
        for (size_t compare = 0U; compare < fingerprint_count; ++compare) {
            if (fingerprints[compare] == fingerprints[index]) {
                ++repeats;
                if (compare < index) {
                    seen_before = true;
                }
            }
        }
        if (!seen_before) {
            ++unique_count;
        }
        if (repeats > dominant_repeats) {
            dominant_repeats = repeats;
        }
    }
    ESP_LOGI(TAG,
             "DIAG fingerprints: count=%u unique=%u dominant_repeats=%u hash=0x%08lX",
             (unsigned)fingerprint_count, (unsigned)unique_count,
             (unsigned)dominant_repeats, (unsigned long)hash);
}

static void log_fingerprint_dump(const uint32_t *fingerprints,
                                 size_t fingerprint_count)
{
    enum {
        FINGERPRINTS_PER_LINE = 12,
        DUMP_LINE_CAPACITY = 160,
    };
    char line[DUMP_LINE_CAPACITY];
    for (size_t start = 0U; start < fingerprint_count;
         start += FINGERPRINTS_PER_LINE) {
        size_t used = 0U;
        const size_t end =
            start + FINGERPRINTS_PER_LINE < fingerprint_count
                ? start + FINGERPRINTS_PER_LINE
                : fingerprint_count;
        for (size_t index = start; index < end; ++index) {
            const int written = snprintf(
                line + used, sizeof(line) - used,
                index == start ? "%06lX" : ",%06lX",
                (unsigned long)fingerprints[index]);
            if (written < 0 || (size_t)written >= sizeof(line) - used) {
                break;
            }
            used += (size_t)written;
        }
        ESP_LOGI(TAG, "DIAG fp[%03u..%03u]: %s", (unsigned)start,
                 (unsigned)(end - 1U), line);
    }
}

static void match_fingerprints(const uint32_t *fingerprints,
                               size_t fingerprint_count, uint8_t *music_id,
                               float *confidence)
{
    music_vote_result_t results[MUSIC_REFERENCE_COUNT] = {0};
    uint64_t best_weighted_votes = 0U;
    uint64_t second_weighted_votes = 0U;
    uint16_t best_votes = 0U;
    uint16_t second_votes = 0U;
    uint8_t best_id = 0xFFU;
    uint8_t second_id = 0xFFU;
    for (uint8_t reference = 0; reference < MUSIC_REFERENCE_COUNT; ++reference) {
        results[reference] = score_reference(
            fingerprints, fingerprint_count,
            music_reference_fingerprints[reference]);
        const float candidate_confidence = results[reference].overlap == 0U
                                               ? 0.0f
                                               : (float)results[reference].strong_matches /
                                                     (float)results[reference].overlap;
        ESP_LOGI(TAG,
                 "DIAG candidate=%u weighted=%llu votes=%u overlap=%u offset=%d offset_ms=%ld confidence=%.3f perfect4=%u strong3=%u weak=%u",
                 (unsigned)(reference + 1U),
                 (unsigned long long)results[reference].weighted_votes,
                 (unsigned)results[reference].votes,
                 (unsigned)results[reference].overlap,
                 (int)results[reference].offset,
                 (long)results[reference].offset * (long)MUSIC_FEATURE_HOP_MS,
                 candidate_confidence,
                 (unsigned)results[reference].perfect_matches,
                 (unsigned)results[reference].triple_matches,
                 (unsigned)results[reference].weak_matches);
        if (results[reference].weighted_votes > best_weighted_votes ||
            (results[reference].weighted_votes == best_weighted_votes &&
             results[reference].votes > best_votes)) {
            second_weighted_votes = best_weighted_votes;
            second_votes = best_votes;
            second_id = best_id;
            best_weighted_votes = results[reference].weighted_votes;
            best_votes = results[reference].votes;
            best_id = (uint8_t)(reference + 1U);
        } else if (results[reference].weighted_votes > second_weighted_votes ||
                   (results[reference].weighted_votes ==
                        second_weighted_votes &&
                    results[reference].votes > second_votes)) {
            second_weighted_votes = results[reference].weighted_votes;
            second_votes = results[reference].votes;
            second_id = (uint8_t)(reference + 1U);
        }
    }

    const uint16_t selected_overlap = best_id == 0xFFU
                                          ? 0U
                                          : results[best_id - 1U].overlap;
    const float score = selected_overlap == 0U
                            ? 0.0f
                            : (float)results[best_id - 1U].strong_matches /
                                  (float)selected_overlap;
    const float margin = best_weighted_votes == 0U
                             ? 0.0f
                             : (float)((double)(best_weighted_votes -
                                               second_weighted_votes) /
                                       (double)best_weighted_votes);
    const bool confidence_ok = score >= MUSIC_CONFIDENCE_MIN;
    const bool margin_ok = margin >= MUSIC_WEIGHTED_MARGIN_MIN;
    *confidence = score;
    *music_id = best_id != 0xFFU && confidence_ok && margin_ok
                    ? best_id
                    : 0xFFU;
    ESP_LOGI(TAG, "Fingerprint votes: %u,%u,%u,%u,%u best=%u overlap=%u confidence=%.3f margin=%.3f",
             (unsigned)results[0].votes, (unsigned)results[1].votes,
             (unsigned)results[2].votes, (unsigned)results[3].votes,
             (unsigned)results[4].votes, (unsigned)best_id,
             (unsigned)selected_overlap, score, margin);
    ESP_LOGI(TAG,
             "Weighted votes: %llu,%llu,%llu,%llu,%llu",
             (unsigned long long)results[0].weighted_votes,
             (unsigned long long)results[1].weighted_votes,
             (unsigned long long)results[2].weighted_votes,
             (unsigned long long)results[3].weighted_votes,
             (unsigned long long)results[4].weighted_votes);
    ESP_LOGI(TAG,
             "DIAG decision: best=%u weighted=%llu votes=%u second=%u weighted=%llu votes=%u confidence=%.3f/%0.3f margin=%.3f/%0.3f",
             (unsigned)best_id, (unsigned long long)best_weighted_votes,
             (unsigned)best_votes, (unsigned)second_id,
             (unsigned long long)second_weighted_votes,
             (unsigned)second_votes,
             score, (double)MUSIC_CONFIDENCE_MIN, margin,
             (double)MUSIC_WEIGHTED_MARGIN_MIN);
    if (*music_id == 0xFFU) {
        ESP_LOGW(TAG,
                 "DIAG rejected: candidate=%u confidence_ok=%s margin_ok=%s",
                 (unsigned)best_id, confidence_ok ? "yes" : "no",
                 margin_ok ? "yes" : "no");
    } else {
        ESP_LOGI(TAG, "DIAG accepted: music_id=%u", (unsigned)*music_id);
    }
}

static bool read_samples(void)
{
    size_t filled = 0U;
    bool read_failed = false;
    uint64_t magnitude_sum = 0U;
    uint32_t peak = 0U;
    size_t nonzero_samples = 0U;
    size_t clipped_samples = 0U;
    uint64_t second_magnitude_sum = 0U;
    uint32_t second_peak = 0U;
    size_t second_nonzero = 0U;
    size_t second_clipped = 0U;
    size_t second_samples = 0U;
    unsigned second_number = 1U;
    int16_t chunk[AUDIO_I2S_DMA_FRAMES];
    while (filled < MUSIC_CAPTURE_SAMPLES && !s_stop_requested) {
        size_t read = 0U;
        const esp_err_t err = audio_i2s_read_pcm16(chunk, AUDIO_I2S_DMA_FRAMES,
                                                    &read, 1000U);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "INMP441 read failed: %s", esp_err_to_name(err));
            read_failed = true;
            break;
        }
        if (read == 0U) {
            ESP_LOGW(TAG, "INMP441 read returned no samples");
            read_failed = true;
            break;
        }
        const size_t remaining = MUSIC_CAPTURE_SAMPLES - filled;
        if (read > remaining) {
            read = remaining;
        }
        for (size_t index = 0U; index < read; ++index) {
            int32_t magnitude = chunk[index];
            if (magnitude < 0) {
                magnitude = -magnitude;
            }
            magnitude_sum += (uint32_t)magnitude;
            if (magnitude != 0) {
                ++nonzero_samples;
            }
            if (magnitude >= 32760) {
                ++clipped_samples;
            }
            if ((uint32_t)magnitude > peak) {
                peak = (uint32_t)magnitude;
            }
            second_magnitude_sum += (uint32_t)magnitude;
            if (magnitude != 0) {
                ++second_nonzero;
            }
            if (magnitude >= 32760) {
                ++second_clipped;
            }
            if ((uint32_t)magnitude > second_peak) {
                second_peak = (uint32_t)magnitude;
            }
            ++second_samples;
            if (second_samples == AUDIO_I2S_SAMPLE_RATE_HZ) {
                ESP_LOGI(TAG,
                         "DIAG capture second=%u samples=%u peak=%u mean_abs=%u nonzero=%u clipped=%u",
                         second_number, (unsigned)second_samples,
                         (unsigned)second_peak,
                         (unsigned)(second_magnitude_sum / second_samples),
                         (unsigned)second_nonzero,
                         (unsigned)second_clipped);
                ++second_number;
                second_magnitude_sum = 0U;
                second_peak = 0U;
                second_nonzero = 0U;
                second_clipped = 0U;
                second_samples = 0U;
            }
        }
        memcpy(s_samples + filled, chunk, read * sizeof(chunk[0]));
        filled += read;
    }
    const uint32_t mean_abs = filled == 0U
                                  ? 0U
                                  : (uint32_t)(magnitude_sum / filled);
    ESP_LOGI(TAG,
             "INMP441 capture stats: samples=%u peak=%u mean_abs=%u nonzero=%u clipped=%u",
             (unsigned)filled, (unsigned)peak, (unsigned)mean_abs,
             (unsigned)nonzero_samples, (unsigned)clipped_samples);
    return !read_failed && filled == MUSIC_CAPTURE_SAMPLES &&
           !s_stop_requested;
}

static bool discard_prompt_audio(void)
{
    size_t discarded = 0U;
    uint64_t magnitude_sum = 0U;
    uint32_t peak = 0U;
    int16_t chunk[AUDIO_I2S_DMA_FRAMES];

    ESP_LOGI(TAG,
             "Prompt guard started: discarding %u ms before music capture",
             (unsigned)MUSIC_PROMPT_DISCARD_MS);
    while (discarded < MUSIC_PROMPT_DISCARD_SAMPLES && !s_stop_requested) {
        size_t read = 0U;
        const esp_err_t err = audio_i2s_read_pcm16(
            chunk, AUDIO_I2S_DMA_FRAMES, &read, 1000U);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Prompt guard read failed: %s",
                     esp_err_to_name(err));
            return false;
        }
        if (read == 0U) {
            ESP_LOGW(TAG, "Prompt guard read returned no samples");
            return false;
        }
        const size_t remaining = MUSIC_PROMPT_DISCARD_SAMPLES - discarded;
        if (read > remaining) {
            read = remaining;
        }
        for (size_t index = 0U; index < read; ++index) {
            int32_t magnitude = chunk[index];
            if (magnitude < 0) {
                magnitude = -magnitude;
            }
            magnitude_sum += (uint32_t)magnitude;
            if ((uint32_t)magnitude > peak) {
                peak = (uint32_t)magnitude;
            }
        }
        discarded += read;
    }

    const uint32_t mean_abs = discarded == 0U
                                  ? 0U
                                  : (uint32_t)(magnitude_sum / discarded);
    ESP_LOGI(TAG,
             "Prompt guard complete: discarded=%u peak=%u mean_abs=%u",
             (unsigned)discarded, (unsigned)peak, (unsigned)mean_abs);
    return discarded == MUSIC_PROMPT_DISCARD_SAMPLES && !s_stop_requested;
}

static void music_task(void *context)
{
    (void)context;
    uint32_t *fingerprints = heap_caps_malloc(
        MUSIC_CAPTURE_FEATURE_COUNT * sizeof(uint32_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (fingerprints == NULL) {
        ESP_LOGE(TAG, "Cannot allocate music fingerprint buffer");
        const music_recognition_result_t result = {
            .status = ESP_ERR_NO_MEM,
            .music_id = 0xFFU,
            .confidence = 0.0f,
        };
        s_task = NULL;
        s_done_callback(&result, s_done_context);
        vTaskDelete(NULL);
        return;
    }

    const bool prompt_guard_ok = discard_prompt_audio();
    if (prompt_guard_ok) {
        ESP_LOGI(TAG, "INMP441 capture started: duration=%u seconds",
                 (unsigned)MUSIC_CAPTURE_SECONDS);
    }
    if (prompt_guard_ok && read_samples()) {
        ESP_LOGI(TAG, "Audio capture complete; extracting fingerprints");
        const size_t fingerprint_count = extract_fingerprints(
            s_samples, MUSIC_CAPTURE_SAMPLES, fingerprints,
            MUSIC_CAPTURE_FEATURE_COUNT);
        log_fingerprint_summary(fingerprints, fingerprint_count);
        log_fingerprint_dump(fingerprints, fingerprint_count);
        if (fingerprint_count != MUSIC_CAPTURE_FEATURE_COUNT) {
            ESP_LOGE(TAG, "Fingerprint count mismatch: got=%u expected=%u",
                     (unsigned)fingerprint_count,
                     (unsigned)MUSIC_CAPTURE_FEATURE_COUNT);
        } else {
            match_fingerprints(fingerprints, fingerprint_count,
                               &s_last_music_id, &s_last_confidence);
            ESP_LOGI(TAG, "Music result: id=%u confidence=%.3f",
                     (unsigned)s_last_music_id, s_last_confidence);
        }
    }

    free(fingerprints);
    const music_recognition_result_t result = {
        .status = s_last_music_id == 0xFFU ? ESP_ERR_NOT_FOUND : ESP_OK,
        .music_id = s_last_music_id,
        .confidence = s_last_confidence,
    };
    s_task = NULL;
    s_done_callback(&result, s_done_context);
    vTaskDelete(NULL);
}

esp_err_t music_recognizer_init(music_recognizer_done_cb_t callback,
                                void *context)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_samples = heap_caps_malloc(MUSIC_CAPTURE_SAMPLES * sizeof(int16_t),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_samples == NULL) {
        s_samples = heap_caps_malloc(MUSIC_CAPTURE_SAMPLES * sizeof(int16_t),
                                     MALLOC_CAP_8BIT);
    }
    if (s_samples == NULL) {
        return ESP_ERR_NO_MEM;
    }
    init_twiddle();
    s_last_music_id = 0xFFU;
    s_last_confidence = 0.0f;
    s_done_callback = callback;
    s_done_context = context;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t music_recognizer_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_stop_requested = false;
    s_last_music_id = 0xFFU;
    s_last_confidence = 0.0f;
    return xTaskCreate(music_task, "music_recognizer", 8192, NULL, 4, &s_task) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

esp_err_t music_recognizer_stop(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_stop_requested = true;
    while (s_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

esp_err_t music_recognizer_get_last(uint8_t *music_id, float *confidence)
{
    if (!s_initialized || music_id == NULL || confidence == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *music_id = s_last_music_id;
    *confidence = s_last_confidence;
    return ESP_OK;
}
