#include "pose_inference.h"

#include <list>
#include <new>
#include <vector>

#include "coco_pose.hpp"
#include "dl_image_define.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "jpeg_decoder.h"

static const char *TAG = "POSE_LOCAL";

static COCOPose *s_pose;
static pose_classifier_t s_classifier;
static uint8_t *s_rgb_buffer;
static size_t s_rgb_capacity;
static bool s_initialized;

static void reset_output(pose_result_t *out)
{
    if (out == nullptr) {
        return;
    }
    out->pose_id = POSE_UNKNOWN;
    out->confidence = 0.0f;
    out->stable = false;
    out->stable_pose_id = POSE_UNKNOWN;
}

static void reset_classifier(pose_result_t *out)
{
    pose_classifier_reset(&s_classifier);
    reset_output(out);
}

extern "C" esp_err_t pose_inference_init(uint8_t team_id, uint8_t stable_frames)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pose_classifier_init(&s_classifier, team_id, stable_frames)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_pose = new (std::nothrow) COCOPose(COCOPose::YOLO11N_POSE_S8_V2);
    if (s_pose == nullptr) {
        pose_classifier_reset(&s_classifier);
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ESP-DL COCO Pose ready, team=%u, stable_frames=%u",
             (unsigned)team_id, (unsigned)s_classifier.stable_frames);
    return ESP_OK;
}

extern "C" esp_err_t pose_inference_run_jpeg(const uint8_t *jpeg_data,
                                               size_t jpeg_size,
                                               pose_result_t *out)
{
    if (!s_initialized || jpeg_data == nullptr || jpeg_size == 0U || out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    reset_output(out);

    esp_jpeg_image_cfg_t jpeg_cfg = {};
    jpeg_cfg.indata = const_cast<uint8_t *>(jpeg_data);
    jpeg_cfg.indata_size = static_cast<uint32_t>(jpeg_size);
    jpeg_cfg.out_format = JPEG_IMAGE_FORMAT_RGB888;
    jpeg_cfg.out_scale = JPEG_IMAGE_SCALE_1_2;
    esp_jpeg_image_output_t image_info = {};
    esp_err_t err = esp_jpeg_get_image_info(&jpeg_cfg, &image_info);
    if (err != ESP_OK || image_info.width == 0U || image_info.height == 0U) {
        reset_classifier(out);
        return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    }

    const size_t required = static_cast<size_t>(image_info.width) *
                            static_cast<size_t>(image_info.height) * 3U;
    if (required > s_rgb_capacity) {
        uint8_t *new_buffer = static_cast<uint8_t *>(
            heap_caps_realloc(s_rgb_buffer, required, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (new_buffer == nullptr) {
            reset_classifier(out);
            return ESP_ERR_NO_MEM;
        }
        s_rgb_buffer = new_buffer;
        s_rgb_capacity = required;
    }

    jpeg_cfg.outbuf = s_rgb_buffer;
    jpeg_cfg.outbuf_size = static_cast<uint32_t>(s_rgb_capacity);
    esp_jpeg_image_output_t decoded = {};
    err = esp_jpeg_decode(&jpeg_cfg, &decoded);
    if (err != ESP_OK) {
        reset_classifier(out);
        return err;
    }

    dl::image::img_t image;
    image.data = s_rgb_buffer;
    image.width = decoded.width;
    image.height = decoded.height;
    image.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;
    std::list<dl::detect::result_t> &detections = s_pose->run(image);

    pose_keypoint_t points[POSE_KEYPOINT_COUNT] = {};
    const dl::detect::result_t *best = nullptr;
    for (const dl::detect::result_t &candidate : detections) {
        if (candidate.keypoint.size() < POSE_KEYPOINT_COUNT * 2U ||
            candidate.box.size() < 4U) {
            continue;
        }
        if (best == nullptr || candidate.score > best->score) {
            best = &candidate;
        }
    }
    if (best != nullptr) {
        for (size_t i = 0; i < POSE_KEYPOINT_COUNT; ++i) {
            points[i].x = static_cast<float>(best->keypoint[i * 2U]);
            points[i].y = static_cast<float>(best->keypoint[i * 2U + 1U]);
            points[i].score = best->score;
        }
    }

    if (!pose_classifier_update(&s_classifier, points, out)) {
        reset_output(out);
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void pose_inference_deinit(void)
{
    delete s_pose;
    s_pose = nullptr;
    if (s_rgb_buffer != nullptr) {
        heap_caps_free(s_rgb_buffer);
        s_rgb_buffer = nullptr;
    }
    s_rgb_capacity = 0U;
    s_initialized = false;
    pose_classifier_reset(&s_classifier);
}
