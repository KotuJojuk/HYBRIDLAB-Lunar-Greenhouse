#include "hidrolab_time.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "hidrolab_settings.h"
#include "hidrolab_control.h"
#include "hidrolab_scheduler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RTC_I2C_PORT I2C_NUM_0
#define RTC_ADDRESS  0x51

/* The CrowPanel Advance 5.0 uses a PCF8563-compatible RTC. Its clock block
 * begins at 0x02. PCF85063 uses 0x04; using that map appears to ACK but writes
 * into the wrong PCF8563 fields, which was the cause of the clock rollback. */
#define PCF8563_CONTROL1_REGISTER 0x00
#define PCF8563_SECONDS_REGISTER  0x02
#define PCF8563_STOP_BIT          0x20
#define PCF8563_VL_BIT            0x80
#define COMPILE_TO_FIRST_BOOT_SECONDS 75

static const char *TAG = "hidrolab_time";
static bool s_started;
static volatile bool s_ntp_write_pending;
static volatile bool s_rtc_available;
static volatile hidrolab_time_source_t s_source = HIDROLAB_TIME_NONE;
static char s_serial_time_line[48];
static size_t s_serial_time_length;

static uint8_t from_bcd(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

static uint8_t to_bcd(int value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static bool valid_bcd(uint8_t value)
{
    return (value & 0x0FU) <= 9U && ((value >> 4) & 0x0FU) <= 9U;
}

static int compile_month(const char *abbr)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *match = strstr(months, abbr);
    return match ? (int)((match - months) / 3) : -1;
}

static esp_err_t compile_time_epoch(time_t *epoch_out)
{
    if (!epoch_out) return ESP_ERR_INVALID_ARG;
    char month_name[4] = {0};
    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    if (sscanf(__DATE__, "%3s %d %d", month_name, &day, &year) != 3 ||
        sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return ESP_FAIL;
    }
    int month = compile_month(month_name);
    if (month < 0) return ESP_FAIL;
    struct tm local = {
        .tm_sec = second,
        .tm_min = minute,
        .tm_hour = hour,
        .tm_mday = day,
        .tm_mon = month,
        .tm_year = year - 1900,
        .tm_isdst = -1,
    };
    time_t epoch = mktime(&local);
    if (epoch <= 0) return ESP_FAIL;
    *epoch_out = epoch;
    return ESP_OK;
}

static esp_err_t set_system_epoch(time_t epoch)
{
    struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
    return settimeofday(&tv, NULL) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t set_compile_time_fallback(time_t *epoch_out)
{
    time_t epoch = 0;
    esp_err_t err = compile_time_epoch(&epoch);
    if (err != ESP_OK) return err;
    /* This seed is used only when the RTC is uninitialized or older than this
     * firmware. It accounts for the incremental build + app flashing time and
     * is immediately persisted after a verified RTC write. */
    epoch += COMPILE_TO_FIRST_BOOT_SECONDS;
    err = set_system_epoch(epoch);
    if (err == ESP_OK && epoch_out) *epoch_out = epoch;
    return err;
}

static esp_err_t rtc_read_bytes(uint8_t start_register, uint8_t *data, size_t size)
{
    esp_err_t err = i2c_master_write_read_device(RTC_I2C_PORT, RTC_ADDRESS,
                                                  &start_register, 1, data, size,
                                                  pdMS_TO_TICKS(120));
    if (err == ESP_OK) s_rtc_available = true;
    return err;
}

static esp_err_t rtc_read_epoch(time_t *epoch_out, bool *low_voltage_out)
{
    if (!epoch_out) return ESP_ERR_INVALID_ARG;
    uint8_t data[7] = {0};
    esp_err_t err = rtc_read_bytes(PCF8563_SECONDS_REGISTER, data, sizeof(data));
    if (err != ESP_OK) return err;

    bool low_voltage = (data[0] & PCF8563_VL_BIT) != 0U;
    if (low_voltage_out) *low_voltage_out = low_voltage;
    if (low_voltage) return ESP_ERR_INVALID_STATE;

    uint8_t second_bcd = data[0] & 0x7FU;
    uint8_t minute_bcd = data[1] & 0x7FU;
    uint8_t hour_bcd = data[2] & 0x3FU;
    uint8_t day_bcd = data[3] & 0x3FU;
    uint8_t weekday_bcd = data[4] & 0x07U;
    uint8_t month_bcd = data[5] & 0x1FU;
    uint8_t year_bcd = data[6];
    if (!valid_bcd(second_bcd) || !valid_bcd(minute_bcd) ||
        !valid_bcd(hour_bcd) || !valid_bcd(day_bcd) ||
        !valid_bcd(month_bcd) || !valid_bcd(year_bcd)) {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm local = {
        .tm_sec = from_bcd(second_bcd),
        .tm_min = from_bcd(minute_bcd),
        .tm_hour = from_bcd(hour_bcd),
        .tm_mday = from_bcd(day_bcd),
        .tm_wday = from_bcd(weekday_bcd),
        .tm_mon = from_bcd(month_bcd) - 1,
        /* HidroLab production dates are in the 2000-2099 range. */
        .tm_year = from_bcd(year_bcd) + 100,
        .tm_isdst = -1,
    };
    int year = local.tm_year + 1900;
    if (year < 2024 || year > 2099 || local.tm_mon < 0 || local.tm_mon > 11 ||
        local.tm_mday < 1 || local.tm_mday > 31 || local.tm_wday > 6 ||
        local.tm_hour > 23 || local.tm_min > 59 || local.tm_sec > 59) {
        return ESP_ERR_INVALID_STATE;
    }

    time_t epoch = mktime(&local);
    if (epoch <= 0) return ESP_ERR_INVALID_STATE;
    *epoch_out = epoch;
    return ESP_OK;
}

static esp_err_t rtc_write_epoch_verified(time_t epoch)
{
    struct tm local = {0};
    localtime_r(&epoch, &local);

    uint8_t control = 0;
    esp_err_t err = rtc_read_bytes(PCF8563_CONTROL1_REGISTER, &control, 1);
    if (err != ESP_OK) return err;

    uint8_t stop_data[2] = {
        PCF8563_CONTROL1_REGISTER,
        (uint8_t)(control | PCF8563_STOP_BIT),
    };
    err = i2c_master_write_to_device(RTC_I2C_PORT, RTC_ADDRESS, stop_data,
                                     sizeof(stop_data), pdMS_TO_TICKS(120));
    if (err != ESP_OK) return err;

    uint8_t clock_data[8] = {
        PCF8563_SECONDS_REGISTER,
        to_bcd(local.tm_sec),
        to_bcd(local.tm_min),
        to_bcd(local.tm_hour),
        to_bcd(local.tm_mday),
        to_bcd(local.tm_wday),
        to_bcd(local.tm_mon + 1),
        to_bcd((local.tm_year + 1900) % 100),
    };
    err = i2c_master_write_to_device(RTC_I2C_PORT, RTC_ADDRESS, clock_data,
                                     sizeof(clock_data), pdMS_TO_TICKS(120));

    uint8_t run_data[2] = {
        PCF8563_CONTROL1_REGISTER,
        (uint8_t)(control & (uint8_t)~PCF8563_STOP_BIT),
    };
    esp_err_t run_err = i2c_master_write_to_device(RTC_I2C_PORT, RTC_ADDRESS,
                                                    run_data, sizeof(run_data),
                                                    pdMS_TO_TICKS(120));
    if (err != ESP_OK) return err;
    if (run_err != ESP_OK) return run_err;

    vTaskDelay(pdMS_TO_TICKS(25));
    time_t readback = 0;
    bool low_voltage = false;
    err = rtc_read_epoch(&readback, &low_voltage);
    if (err != ESP_OK || low_voltage ||
        llabs((long long)readback - (long long)epoch) > 2) {
        ESP_LOGE(TAG, "PCF8563 write verification failed");
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_rtc_available = true;
    return ESP_OK;
}

static esp_err_t rtc_write_system_time(void)
{
    if (!hidrolab_time_is_synced()) return ESP_ERR_INVALID_STATE;
    time_t now = 0;
    time(&now);
    return rtc_write_epoch_verified(now);
}

static void apply_serial_time_command(void)
{
    s_serial_time_line[s_serial_time_length] = '\0';
    /* Read-only service command: never changes a motor, RTC, or saved data. */
    if (strcmp(s_serial_time_line, "HLIO?") == 0) {
        for (int i = 0; i < 2; ++i) {
            hidrolab_control_snapshot_t io = hidrolab_control_snapshot(i == 1);
            hidrolab_schedule_status_t schedule = hidrolab_scheduler_get_status((hidrolab_channel_t)i);
            ESP_LOGI(TAG, "HLIO M4 %s GPIO%d command=%d readback=%d mode=%s writes=%lu errors=%lu last=%s",
                     i ? "FAN" : "PUMP", io.pin, io.commanded_on, io.level,
                     schedule.automatic ? "AUTO" : "MANUAL",
                     (unsigned long)io.commands, (unsigned long)io.errors,
                     esp_err_to_name(io.last_error));
        }
        gpio_dump_io_configuration(stdout, (1ULL << HIDROLAB_AIR_PUMP_GPIO) |
                                          (1ULL << HIDROLAB_FAN_GPIO));
        s_serial_time_length = 0;
        return;
    }
    const char prefix[] = "HLTIME=";
    if (strncmp(s_serial_time_line, prefix, sizeof(prefix) - 1) != 0) {
        s_serial_time_length = 0;
        return;
    }

    char *end = NULL;
    long long raw_epoch = strtoll(s_serial_time_line + sizeof(prefix) - 1, &end, 10);
    time_t epoch = (time_t)raw_epoch;
    struct tm local = {0};
    localtime_r(&epoch, &local);
    if (!end || *end != '\0' || raw_epoch <= 0 ||
        local.tm_year + 1900 < 2024 || local.tm_year + 1900 > 2099) {
        ESP_LOGW(TAG, "serial time command rejected");
        s_serial_time_length = 0;
        return;
    }

    esp_err_t err = set_system_epoch(epoch);
    if (err == ESP_OK) err = rtc_write_epoch_verified(epoch);
    if (err == ESP_OK) {
        s_source = HIDROLAB_TIME_RTC;
        ESP_LOGI(TAG, "absolute serial time saved and verified: %04d-%02d-%02d %02d:%02d:%02d",
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                 local.tm_hour, local.tm_min, local.tm_sec);
    } else {
        ESP_LOGW(TAG, "absolute serial time failed: %s", esp_err_to_name(err));
    }
    s_serial_time_length = 0;
}

static void poll_serial_time_command(void)
{
    if (!uart_is_driver_installed(UART_NUM_0)) return;
    uint8_t bytes[24];
    int count = uart_read_bytes(UART_NUM_0, bytes, sizeof(bytes), 0);
    for (int i = 0; i < count; ++i) {
        uint8_t ch = bytes[i];
        if (ch == '\r' || ch == '\n') {
            if (s_serial_time_length > 0) apply_serial_time_command();
            continue;
        }
        if (s_serial_time_length + 1 < sizeof(s_serial_time_line)) {
            s_serial_time_line[s_serial_time_length++] = (char)ch;
        } else {
            s_serial_time_length = 0;
        }
    }
}

static void time_sync_notification(struct timeval *tv)
{
    (void)tv;
    s_source = HIDROLAB_TIME_NTP;
    s_ntp_write_pending = true;
}

static void time_service_task(void *arg)
{
    (void)arg;
    while (1) {
        poll_serial_time_command();
        if (s_ntp_write_pending) {
            s_ntp_write_pending = false;
            esp_err_t err = rtc_write_system_time();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "NTP correction saved and verified on PCF8563");
            } else {
                ESP_LOGW(TAG, "RTC write after NTP failed: %s", esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t hidrolab_time_init(void)
{
    if (s_started) return ESP_OK;

    /* Türkiye yıl boyunca UTC+3 kullanıyor. POSIX TZ işareti ters yazılır. */
    setenv("TZ", "TRT-3", 1);
    tzset();

    time_t rtc_epoch = 0;
    time_t build_epoch = 0;
    bool low_voltage = false;
    esp_err_t rtc_err = rtc_read_epoch(&rtc_epoch, &low_voltage);
    bool build_time_ok = compile_time_epoch(&build_epoch) == ESP_OK;
    bool stale = rtc_err == ESP_OK && build_time_ok && rtc_epoch + 120 < build_epoch;

    if (rtc_err == ESP_OK && !stale) {
        if (set_system_epoch(rtc_epoch) == ESP_OK) {
            struct tm local = {0};
            localtime_r(&rtc_epoch, &local);
            s_source = HIDROLAB_TIME_RTC;
            ESP_LOGI(TAG, "clock restored from PCF8563: %04d-%02d-%02d %02d:%02d:%02d",
                     local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                     local.tm_hour, local.tm_min, local.tm_sec);
        }
    } else {
        time_t seed_epoch = 0;
        if (set_compile_time_fallback(&seed_epoch) == ESP_OK) {
            esp_err_t write_err = rtc_write_epoch_verified(seed_epoch);
            if (write_err == ESP_OK) {
                s_source = HIDROLAB_TIME_RTC;
                if (stale) {
                    ESP_LOGI(TAG, "stale PCF8563 advanced; write-back verified");
                } else {
                    ESP_LOGI(TAG, "PCF8563 initialized; write-back verified");
                }
                rtc_err = ESP_OK;
            } else {
                ESP_LOGW(TAG, "PCF8563 initialization write failed: %s",
                         esp_err_to_name(write_err));
            }
        }
        if (low_voltage) {
            ESP_LOGW(TAG, "PCF8563 low-voltage flag was set; clock was reinitialized");
        }
    }
    if (rtc_err != ESP_OK) {
        ESP_LOGW(TAG, "PCF8563 time unavailable; NTP will correct it when Wi-Fi is online: %s",
                 esp_err_to_name(rtc_err));
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification);
    esp_sntp_init();

    if (!uart_is_driver_installed(UART_NUM_0)) {
        esp_err_t uart_err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
        if (uart_err != ESP_OK) {
            ESP_LOGW(TAG, "serial time service unavailable: %s", esp_err_to_name(uart_err));
        }
    }

    if (xTaskCreate(time_service_task, "hidrolab_time", 3072, NULL, 3, NULL) != pdPASS) {
        esp_sntp_stop();
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "time service ready: PCF8563 offline clock + NTP correction");
    return ESP_OK;
}

bool hidrolab_time_is_synced(void)
{
    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);
    return (ti.tm_year + 1900) >= 2024;
}

bool hidrolab_time_rtc_available(void)
{
    return s_rtc_available;
}

hidrolab_time_source_t hidrolab_time_source(void)
{
    return s_source;
}

bool hidrolab_time_is_night(void)
{
    if (!hidrolab_time_is_synced()) return false;
    time_t now = 0;
    struct tm local = {0};
    time(&now);
    localtime_r(&now, &local);
    return hidrolab_settings_is_sleep_hour((uint8_t)local.tm_hour);
}

uint32_t hidrolab_time_date_key(void)
{
    if (!hidrolab_time_is_synced()) return 0;
    time_t now = 0;
    struct tm local = {0};
    time(&now);
    localtime_r(&now, &local);
    return (uint32_t)(local.tm_year + 1900) * 10000U +
           (uint32_t)(local.tm_mon + 1) * 100U + (uint32_t)local.tm_mday;
}

void hidrolab_time_format(char *time_buf, size_t time_len, char *date_buf, size_t date_len)
{
    if (time_buf && time_len) time_buf[0] = '\0';
    if (date_buf && date_len) date_buf[0] = '\0';

    if (!hidrolab_time_is_synced()) {
        if (time_buf && time_len) snprintf(time_buf, time_len, "--:--");
        if (date_buf && date_len) snprintf(date_buf, date_len, "Saat bekleniyor");
        return;
    }

    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);

    if (time_buf && time_len) strftime(time_buf, time_len, "%H:%M", &ti);
    if (date_buf && date_len) strftime(date_buf, date_len, "%d.%m.%Y", &ti);
}
