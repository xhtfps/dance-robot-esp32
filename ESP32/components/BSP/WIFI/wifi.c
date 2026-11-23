#include "wifi.h"

#include <stdbool.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "WIFI";
static bool s_initialized;
static bool s_started;
static bool s_sta_enabled;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA 正在连接到路由器: %s", CONFIG_POSE_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_started && s_sta_enabled) {
            ESP_LOGW(TAG, "STA 连接断开，正在重连");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA 已连接，电脑可使用: http://" IPSTR ":81/stream",
                 IP2STR(&event->ip_info.ip));
    }
}

esp_err_t wifi_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        return err;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_create_default_wifi_ap();
    s_sta_enabled = CONFIG_POSE_WIFI_SSID[0] != '\0' &&
                    CONFIG_POSE_WIFI_PASSWORD[0] != '\0';
    if (s_sta_enabled) {
        esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     &wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     &wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_started) {
        return ESP_OK;
    }

    wifi_config_t ap_config = {0};
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 2;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.pmf_cfg.required = false;
    strlcpy((char *)ap_config.ap.ssid, CONFIG_POSE_WIFI_AP_SSID,
            sizeof(ap_config.ap.ssid));
    strlcpy((char *)ap_config.ap.password, CONFIG_POSE_WIFI_AP_PASSWORD,
            sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen(CONFIG_POSE_WIFI_AP_SSID);

    esp_err_t err = esp_wifi_set_mode(s_sta_enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        return err;
    }

    if (s_sta_enabled) {
        wifi_config_t sta_config = {0};
        strlcpy((char *)sta_config.sta.ssid, CONFIG_POSE_WIFI_SSID,
                sizeof(sta_config.sta.ssid));
        strlcpy((char *)sta_config.sta.password, CONFIG_POSE_WIFI_PASSWORD,
                sizeof(sta_config.sta.password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    /* Keep the AP awake while carrying a latency-sensitive camera stream. */
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not disable WiFi power save: %s", esp_err_to_name(err));
    }

    s_started = true;
    ESP_LOGI(TAG, "AP 已启动: SSID=%s, password=%s", CONFIG_POSE_WIFI_AP_SSID,
             CONFIG_POSE_WIFI_AP_PASSWORD);
    ESP_LOGI(TAG, "AP 地址: http://192.168.4.1:81/stream");
    if (!s_sta_enabled) {
        ESP_LOGI(TAG, "未配置路由器 Wi-Fi，电脑请连接 ESP32 热点");
    }
    return ESP_OK;
}

esp_err_t wifi_stop(void)
{
    if (!s_initialized || !s_started) {
        return ESP_OK;
    }

    s_started = false;
    return esp_wifi_stop();
}
