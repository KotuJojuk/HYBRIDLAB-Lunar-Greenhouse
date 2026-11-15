#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HIDROLAB_TIME_NONE = 0,
    HIDROLAB_TIME_RTC,
    HIDROLAB_TIME_NTP,
} hidrolab_time_source_t;

/* I2C must be initialized before this function is called. */
esp_err_t hidrolab_time_init(void);
bool hidrolab_time_is_synced(void);
bool hidrolab_time_rtc_available(void);
hidrolab_time_source_t hidrolab_time_source(void);
bool hidrolab_time_is_night(void);
uint32_t hidrolab_time_date_key(void);
void hidrolab_time_format(char *time_buf, size_t time_len, char *date_buf, size_t date_len);

#ifdef __cplusplus
}
#endif
