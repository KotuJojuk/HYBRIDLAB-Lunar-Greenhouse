#include "hidrolab_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hidrolab_settings.h"

#define MAX_NETWORKS 8
#define MAX_RETRIES 4

static const char *TAG = "hidrolab_wifi";
static volatile hidrolab_wifi_state_t s_state = HIDROLAB_WIFI_IDLE;
static hidrolab_wifi_network_t s_networks[MAX_NETWORKS];
static volatile size_t s_network_count;
static char s_ip[16] = "-";
static char s_current_ssid[33] = "";
static char s_current_password[65] = "";
static int s_retry_count;
static bool s_initialized;
static volatile bool s_ignore_disconnect;
static volatile uint32_t s_scan_generation;
static volatile uint32_t s_connect_generation;

typedef struct {
    char ssid[33];
    char password[65];
    uint32_t generation;
} wifi_connect_job_t;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state == HIDROLAB_WIFI_IDLE) {
            s_ignore_disconnect = false;
            strlcpy(s_ip, "-", sizeof(s_ip));
            return;
        }
        if (s_ignore_disconnect) {
            s_ignore_disconnect = false;
            ESP_LOGD(TAG, "expected disconnect while applying new Wi-Fi config");
            return;
        }

        if (s_state == HIDROLAB_WIFI_CONNECTING && s_retry_count < MAX_RETRIES) {
            s_retry_count++;
            (void)esp_wifi_connect();
            return;
        }

        if (s_state != HIDROLAB_WIFI_SCANNING) {
            s_state = HIDROLAB_WIFI_FAILED;
            strlcpy(s_ip, "-", sizeof(s_ip));
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_ignore_disconnect = false;
        s_retry_count = 0;
        esp_err_t err = hidrolab_settings_set_wifi(s_current_ssid, s_current_password);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "connected, but credentials could not be saved: %s",
                     esp_err_to_name(err));
        }
        s_state = HIDROLAB_WIFI_CONNECTED;
        ESP_LOGI(TAG, "connected to %s, ip=%s", s_current_ssid, s_ip);
    }
}

static void connect_task(void *arg)
{
    wifi_connect_job_t job = *(wifi_connect_job_t *)arg;
    vPortFree(arg);

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, job.ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, job.password, sizeof(cfg.sta.password));
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    s_ignore_disconnect = true;
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        s_ignore_disconnect = false;
        ESP_LOGD(TAG, "no active association to disconnect: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    if (job.generation != s_connect_generation) {
        vTaskDelete(NULL);
        return;
    }
    s_ignore_disconnect = false;

    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK && job.generation == s_connect_generation) {
        err = esp_wifi_connect();
    }
    if (err != ESP_OK && job.generation == s_connect_generation) {
        s_state = HIDROLAB_WIFI_FAILED;
        ESP_LOGE(TAG, "connection start failed: %s", esp_err_to_name(err));
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "connecting to %s", job.ssid);
    }
    vTaskDelete(NULL);
}

static void scan_task(void *arg)
{
    uint32_t generation = (uint32_t)(uintptr_t)arg;
    wifi_scan_config_t scan = {0};
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (generation != s_scan_generation) {
        vTaskDelete(NULL);
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
        s_state = HIDROLAB_WIFI_FAILED;
        vTaskDelete(NULL);
        return;
    }

    uint16_t count = MAX_NETWORKS;
    wifi_ap_record_t records[MAX_NETWORKS];
    hidrolab_wifi_network_t next_networks[MAX_NETWORKS];
    memset(records, 0, sizeof(records));
    memset(next_networks, 0, sizeof(next_networks));
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (generation != s_scan_generation) {
        vTaskDelete(NULL);
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not read scan records: %s", esp_err_to_name(err));
        s_state = HIDROLAB_WIFI_FAILED;
        vTaskDelete(NULL);
        return;
    }

    for (uint16_t i = 0; i < count; ++i) {
        strlcpy(next_networks[i].ssid, (const char *)records[i].ssid,
                sizeof(next_networks[i].ssid));
        next_networks[i].rssi = records[i].rssi;
        next_networks[i].open = records[i].authmode == WIFI_AUTH_OPEN;
    }

    /* Publish the count only after every row is complete. UI readers use the
     * count as the validity boundary, so they can never observe a half-filled
     * scan result while this worker is still copying records. */
    memcpy(s_networks, next_networks, sizeof(s_networks));
    __sync_synchronize();
    s_network_count = count;
    s_state = HIDROLAB_WIFI_IDLE;
    ESP_LOGI(TAG, "scan complete: %u networks", (unsigned)count);
    vTaskDelete(NULL);
}

esp_err_t hidrolab_wifi_init(void)
{
    if (s_initialized) return ESP_OK;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    if (!esp_netif_create_default_wifi_sta()) return ESP_FAIL;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    /* Credentials are persisted only by HidroLab settings. Keeping the ESP-IDF
     * station config in RAM makes a factory reset truly forget the old network. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set Wi-Fi storage failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    &wifi_event, NULL),
                        TAG, "Wi-Fi event registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                    &wifi_event, NULL),
                        TAG, "IP event registration failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");

    s_initialized = true;
    return ESP_OK;
}

esp_err_t hidrolab_wifi_scan_async(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_state == HIDROLAB_WIFI_SCANNING || s_state == HIDROLAB_WIFI_CONNECTING) {
        return ESP_ERR_INVALID_STATE;
    }

    s_network_count = 0;
    s_state = HIDROLAB_WIFI_SCANNING;
    uint32_t generation = ++s_scan_generation;
    if (xTaskCreate(scan_task, "wifi_scan", 4096,
                    (void *)(uintptr_t)generation, 4, NULL) != pdPASS) {
        s_state = HIDROLAB_WIFI_FAILED;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t hidrolab_wifi_connect(const char *ssid, const char *password)
{
    if (!s_initialized || !ssid || !ssid[0] || !password) return ESP_ERR_INVALID_ARG;
    if (strlen(ssid) > 32 || strlen(password) > 63) return ESP_ERR_INVALID_ARG;

    strlcpy(s_current_ssid, ssid, sizeof(s_current_ssid));
    strlcpy(s_current_password, password, sizeof(s_current_password));
    strlcpy(s_ip, "-", sizeof(s_ip));
    s_retry_count = 0;

    /* Invalidate a pending scan before starting the non-blocking connection
     * worker. The 100 ms driver settle time must never run on the LVGL task. */
    ++s_scan_generation;
    (void)esp_wifi_scan_stop();
    uint32_t generation = ++s_connect_generation;
    s_state = HIDROLAB_WIFI_CONNECTING;

    wifi_connect_job_t *job = pvPortMalloc(sizeof(*job));
    if (!job) {
        s_state = HIDROLAB_WIFI_FAILED;
        return ESP_ERR_NO_MEM;
    }
    strlcpy(job->ssid, ssid, sizeof(job->ssid));
    strlcpy(job->password, password, sizeof(job->password));
    job->generation = generation;

    if (xTaskCreate(connect_task, "wifi_connect", 4096, job, 4, NULL) != pdPASS) {
        vPortFree(job);
        s_state = HIDROLAB_WIFI_FAILED;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t hidrolab_wifi_connect_saved(void)
{
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    if (!cfg->wifi_ssid[0]) return ESP_ERR_NOT_FOUND;
    return hidrolab_wifi_connect(cfg->wifi_ssid, cfg->wifi_password);
}

hidrolab_wifi_state_t hidrolab_wifi_get_state(void)
{
    return s_state;
}

size_t hidrolab_wifi_network_count(void)
{
    return s_network_count;
}

hidrolab_wifi_network_t hidrolab_wifi_network_get(size_t index)
{
    hidrolab_wifi_network_t empty = {0};
    if (index >= s_network_count || index >= MAX_NETWORKS) return empty;
    return s_networks[index];
}

const char *hidrolab_wifi_ip(void)
{
    return s_ip;
}

const char *hidrolab_wifi_connected_ssid(void)
{
    return s_current_ssid;
}

esp_err_t hidrolab_wifi_disconnect(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    ++s_connect_generation;
    ++s_scan_generation;
    s_state = HIDROLAB_WIFI_IDLE;
    s_ignore_disconnect = false;
    esp_err_t err = esp_wifi_disconnect();
    s_retry_count = 0;
    s_network_count = 0;
    strlcpy(s_ip, "-", sizeof(s_ip));
    s_current_ssid[0] = '\0';
    s_current_password[0] = '\0';
    return err;
}
