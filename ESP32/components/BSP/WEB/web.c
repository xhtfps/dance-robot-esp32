#include "web.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camera.h"
#include "competition_controller.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "img_converters.h"
#include "serial_bridge.h"

#define WEB_CONTROL_PORT 80
#define WEB_STREAM_PORT 81
#define WEB_STREAM_BOUNDARY "123456789000000000000987654321"
#define POSE_BODY_MAX 256U
#define POSE_MIN_CONFIDENCE 0.50

static const char *TAG = "WEB";
static httpd_handle_t s_control_server;
static httpd_handle_t s_stream_server;
static web_pose_action_callback_t s_pose_action_callback;

static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" WEB_STREAM_BOUNDARY;
static const char *STREAM_BOUNDARY =
    "\r\n--" WEB_STREAM_BOUNDARY "\r\n";
static const char *STREAM_PART =
    "Content-Type: image/jpeg\r\n"
    "Content-Length: %u\r\n"
    "X-Timestamp: %ld.%06ld\r\n\r\n";

static const char *competition_state_name(competition_state_t state)
{
    switch (state) {
    case COMPETITION_IDLE: return "IDLE";
    case COMPETITION_READY_FOR_STAGE: return "READY_FOR_STAGE";
    case COMPETITION_RUNNING_RECOGNITION: return "RUNNING_RECOGNITION";
    case COMPETITION_DISPATCH_ACTION: return "DISPATCH_ACTION";
    case COMPETITION_WAIT_ACTION_DONE: return "WAIT_ACTION_DONE";
    case COMPETITION_MUSIC_WAIT_COMMAND: return "MUSIC_WAIT_COMMAND";
    case COMPETITION_MUSIC_RECOGNITION: return "MUSIC_RECOGNITION";
    case COMPETITION_MUSIC_ANNOUNCE_DELAY: return "MUSIC_ANNOUNCE_DELAY";
    case COMPETITION_MUSIC_PERFORMANCE: return "MUSIC_PERFORMANCE";
    case COMPETITION_COMPLETE: return "COMPLETE";
    case COMPETITION_ABORTED: return "ABORTED";
    case COMPETITION_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}

static esp_err_t index_handler(httpd_req_t *request)
{
    static const char page[] =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<title>ESP32 Pose Camera</title></head><body>"
        "<h2>ESP32 camera stream</h2>"
        "<p>Pose relay: POST /api/vision/pose; stream: port 81</p>"
        "<img src='http://192.168.4.1:81/stream' style='max-width:100%'>"
        "<script>"
        "async function submitPoseResult(poseId, confidence, timestamp) {"
        "  const response = await fetch('/api/vision/pose', {method:'POST',"
        "    headers:{'Content-Type':'application/json'},"
        "    body:JSON.stringify({pose_id:poseId,confidence:confidence,timestamp:timestamp})});"
        "  return response.json();"
        "}"
        "window.submitPoseResult = submitPoseResult;"
        "</script>"
        "</body></html>";

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t pose_handler(httpd_req_t *request)
{
    if (request->content_len == 0U || request->content_len > POSE_BODY_MAX) {
        httpd_resp_send_err(request, HTTPD_413_CONTENT_TOO_LARGE,
                            "pose payload too large or empty");
        return ESP_ERR_INVALID_SIZE;
    }

    char body[POSE_BODY_MAX + 1U];
    size_t received = 0U;
    while (received < request->content_len) {
        const int count = httpd_req_recv(request, body + received,
                                         request->content_len - received);
        if (count <= 0) {
            httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                "pose payload receive failed");
            return ESP_FAIL;
        }
        received += (size_t)count;
    }
    body[received] = '\0';

    cJSON *root = cJSON_ParseWithLength(body, received);
    cJSON *pose_id_item = root == NULL ? NULL : cJSON_GetObjectItem(root, "pose_id");
    cJSON *confidence_item = root == NULL ? NULL : cJSON_GetObjectItem(root, "confidence");
    if (!cJSON_IsNumber(pose_id_item) || !cJSON_IsNumber(confidence_item) ||
        pose_id_item->valuedouble < 0.0 || pose_id_item->valuedouble > 255.0 ||
        confidence_item->valuedouble < 0.0 || confidence_item->valuedouble > 1.0) {
        cJSON_Delete(root);
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                            "invalid pose payload");
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t pose_id = (uint8_t)pose_id_item->valuedouble;
    const double confidence = confidence_item->valuedouble;
    bool accepted = false;
    bool action_sent = false;
    bool speech_sent = false;
    if (confidence >= POSE_MIN_CONFIDENCE) {
        accepted = competition_on_visual_recognized(pose_id) == ESP_OK;
    }

    if (accepted) {
        // Speech feedback is independent from robot action dispatch. A
        // failed Arduino send must not hide the recognition result.
        if (pose_id >= 1U && pose_id <= 6U) {
            speech_sent = serial_bridge_send_to_su03_message(pose_id) == ESP_OK;
            if (!speech_sent) {
                ESP_LOGW(TAG, "Cannot send visual speech message: id=%u",
                         (unsigned)pose_id);
            }
        }

        if (s_pose_action_callback != NULL) {
            action_sent = s_pose_action_callback(pose_id) == ESP_OK;
        }
        if (!action_sent) {
            ESP_LOGW(TAG, "Visual action was not dispatched: id=%u",
                     (unsigned)pose_id);
            competition_on_action_done(false);
        }
    }

    competition_status_t status = {0};
    competition_get_status(&status);
    cJSON_Delete(root);

    char response[192];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"accepted\":%s,\"action_sent\":%s,\"speech_sent\":%s,\"state\":\"%s\"}",
             accepted ? "true" : "false", action_sent ? "true" : "false",
             speech_sent ? "true" : "false",
             competition_state_name(status.state));
    ESP_LOGI(TAG, "Visual POST: id=%u conf=%.3f accepted=%s action=%s speech=%s state=%s",
             (unsigned)pose_id, confidence,
             accepted ? "true" : "false",
             action_sent ? "true" : "false",
             speech_sent ? "true" : "false",
             competition_state_name(status.state));
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t web_set_pose_action_callback(web_pose_action_callback_t callback)
{
    s_pose_action_callback = callback;
    return ESP_OK;
}

static esp_err_t snapshot_handler(httpd_req_t *request)
{
    camera_fb_t *frame = camera_get_frame();
    if (frame == NULL) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "camera frame unavailable");
        return ESP_FAIL;
    }

    httpd_resp_set_type(request, "image/jpeg");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    esp_err_t err = httpd_resp_send(request, (const char *)frame->buf, frame->len);
    camera_return_frame(frame);
    return err;
}

static esp_err_t stream_handler(httpd_req_t *request)
{
    esp_err_t err = httpd_resp_set_type(request, STREAM_CONTENT_TYPE);
    if (err != ESP_OK) {
        return err;
    }
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request, "X-Framerate", "15");

    while (true) {
        camera_fb_t *frame = camera_get_frame();
        if (frame == NULL) {
            ESP_LOGW(TAG, "Camera capture failed while streaming");
            return ESP_FAIL;
        }

        uint8_t *jpeg = frame->buf;
        size_t jpeg_len = frame->len;
        bool converted = false;
        if (frame->format != PIXFORMAT_JPEG) {
            converted = frame2jpg(frame, 80, &jpeg, &jpeg_len);
            if (!converted) {
                camera_return_frame(frame);
                ESP_LOGW(TAG, "JPEG conversion failed while streaming");
                return ESP_FAIL;
            }
        }

        char header[128];
        int header_len = snprintf(header, sizeof(header), STREAM_PART,
                                  (unsigned)jpeg_len,
                                  (long)frame->timestamp.tv_sec,
                                  (long)frame->timestamp.tv_usec);
        if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
            err = ESP_ERR_INVALID_SIZE;
        }
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(request, STREAM_BOUNDARY,
                                        strlen(STREAM_BOUNDARY));
        }
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(request, header, (size_t)header_len);
        }
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(request, (const char *)jpeg, jpeg_len);
        }

        if (converted) {
            free(jpeg);
        }
        camera_return_frame(frame);

        if (err != ESP_OK) {
            httpd_resp_send_chunk(request, NULL, 0);
            return err;
        }
    }
}

static esp_err_t register_control_uris(httpd_handle_t server)
{
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
    };
    httpd_uri_t snapshot_uri = {
        .uri = "/snapshot",
        .method = HTTP_GET,
        .handler = snapshot_handler,
    };
    httpd_uri_t pose_uri = {
        .uri = "/api/vision/pose",
        .method = HTTP_POST,
        .handler = pose_handler,
    };

    esp_err_t err = httpd_register_uri_handler(server, &index_uri);
    if (err != ESP_OK) {
        return err;
    }
    err = httpd_register_uri_handler(server, &snapshot_uri);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_register_uri_handler(server, &pose_uri);
}

esp_err_t web_start(void)
{
    if (s_control_server != NULL || s_stream_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t control_config = HTTPD_DEFAULT_CONFIG();
    control_config.server_port = WEB_CONTROL_PORT;
    control_config.max_uri_handlers = 4;
    control_config.stack_size = 8192;

    esp_err_t err = httpd_start(&s_control_server, &control_config);
    if (err != ESP_OK) {
        return err;
    }
    err = register_control_uris(s_control_server);
    if (err != ESP_OK) {
        httpd_stop(s_control_server);
        s_control_server = NULL;
        return err;
    }

    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = WEB_STREAM_PORT;
    stream_config.ctrl_port = control_config.ctrl_port + 1;
    stream_config.max_uri_handlers = 2;
    stream_config.stack_size = 8192;

    err = httpd_start(&s_stream_server, &stream_config);
    if (err != ESP_OK) {
        httpd_stop(s_control_server);
        s_control_server = NULL;
        return err;
    }

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
    };
    err = httpd_register_uri_handler(s_stream_server, &stream_uri);
    if (err != ESP_OK) {
        httpd_stop(s_stream_server);
        httpd_stop(s_control_server);
        s_stream_server = NULL;
        s_control_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Control HTTP server: port %d (/ and /snapshot)",
             WEB_CONTROL_PORT);
    ESP_LOGI(TAG, "Dedicated stream HTTP server: port %d (/stream)",
             WEB_STREAM_PORT);
    return ESP_OK;
}

esp_err_t web_stop(void)
{
    esp_err_t err = ESP_OK;
    if (s_stream_server != NULL) {
        err = httpd_stop(s_stream_server);
        s_stream_server = NULL;
    }
    if (s_control_server != NULL) {
        esp_err_t control_err = httpd_stop(s_control_server);
        if (err == ESP_OK) {
            err = control_err;
        }
        s_control_server = NULL;
    }
    return err;
}
