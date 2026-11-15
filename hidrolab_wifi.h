#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HIDROLAB_WIFI_IDLE = 0,
    HIDROLAB_WIFI_SCANNING,
    HIDROLAB_WIFI_CONNECTING,
    HIDROLAB_WIFI_CONNECTED,
    HIDROLAB_WIFI_FAILED,
} hidrolab_wifi_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open;
} hidrolab_wifi_network_t;

esp_err_t hidrolab_wifi_init(void);
esp_err_t hidrolab_wifi_scan_async(void);
esp_err_t hidrolab_wifi_connect(const char *ssid, const char *password);
esp_err_t hidrolab_wifi_connect_saved(void);
esp_err_t hidrolab_wifi_disconnect(void);
hidrolab_wifi_state_t hidrolab_wifi_get_state(void);
size_t hidrolab_wifi_network_count(void);
hidrolab_wifi_network_t hidrolab_wifi_network_get(size_t index);
const char *hidrolab_wifi_ip(void);
const char *hidrolab_wifi_connected_ssid(void);

#ifdef __cplusplus
}
#endif
