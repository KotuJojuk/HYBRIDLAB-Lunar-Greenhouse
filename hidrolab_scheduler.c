#include "hidrolab_scheduler.h"

#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hidrolab_control.h"
#include "hidrolab_buzzer.h"
#include "hidrolab_settings.h"
#include "hidrolab_time.h"

static const char *TAG = "hidrolab_sched";
static SemaphoreHandle_t s_mutex;
static bool s_manual_override[2];
static bool s_manual_state[2];
static int64_t s_manual_until_us[2];
#define MANUAL_OVERRIDE_US (120LL * 1000000LL)
static bool s_clock_unavailable;
static int64_t s_pump_started_us = -1;
static int64_t s_pump_blocked_hour = -1;
#define PUMP_ON_MINUTES 5
#define PUMP_MAX_AUTO_ON_US (PUMP_ON_MINUTES * 60LL * 1000000LL)
static uint32_t s_alarm_runtime_day;
static uint32_t s_last_alarm_config;

static void check_daily_alarm(const struct tm *local)
{
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    uint32_t config = cfg->alarm_config;
    if (config != s_last_alarm_config) {
        s_last_alarm_config = config;
        s_alarm_runtime_day = 0;
    }
    if (!local || (config & (1UL << 16)) == 0 || !cfg->setup_done ||
        local->tm_hour != (int)((config >> 8) & 0xFFU) ||
        local->tm_min != (int)(config & 0xFFU)) return;
    uint32_t date_key = hidrolab_time_date_key();
    if (date_key == 0 || date_key == cfg->alarm_last_date ||
        date_key == s_alarm_runtime_day) return;
    s_alarm_runtime_day = date_key;
    esp_err_t err = hidrolab_settings_mark_alarm_fired(date_key);
    if (err != ESP_OK) ESP_LOGW(TAG, "alarm day could not be saved: %s", esp_err_to_name(err));
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_ALARM);
    ESP_LOGI(TAG, "daily alarm sounded at %02d:%02d", local->tm_hour, local->tm_min);
}

static esp_err_t apply_output(hidrolab_channel_t channel, bool on)
{
    bool was_on = hidrolab_control_air_pump_is_on();
    esp_err_t err;
    if (channel == HIDROLAB_CHANNEL_PUMP) {
        err = hidrolab_control_set_air_pump(on);
        if (err == ESP_OK) {
            if (!on) s_pump_started_us = -1;
            else if (!was_on || s_pump_started_us < 0)
                s_pump_started_us = esp_timer_get_time();
        }
    } else {
        err = hidrolab_control_set_fan(on);
    }
    if (err != ESP_OK)
        ESP_LOGE(TAG, "channel %d command %d failed: %s", channel, on, esp_err_to_name(err));
    return err;
}

static bool output_is_on(hidrolab_channel_t channel)
{
    return channel == HIDROLAB_CHANNEL_PUMP ?
           hidrolab_control_air_pump_is_on() : hidrolab_control_fan_is_on();
}

static bool get_local_time(struct tm *out)
{
    if (!out || !hidrolab_time_is_synced()) return false;
    time_t now = 0;
    time(&now);
    if (!localtime_r(&now, out)) return false;
    return out->tm_year >= 124 && out->tm_year <= 199 &&
           out->tm_mon >= 0 && out->tm_mon < 12 &&
           out->tm_mday >= 1 && out->tm_mday <= 31 &&
           out->tm_hour >= 0 && out->tm_hour < 24 &&
           out->tm_min >= 0 && out->tm_min < 60 &&
           out->tm_sec >= 0 && out->tm_sec < 60;
}

static bool is_daytime(const struct tm *local)
{
    return local && !hidrolab_settings_is_sleep_hour((uint8_t)local->tm_hour);
}

static bool desired_phase(hidrolab_channel_t channel, const struct tm *local)
{
    if (!is_daytime(local)) return false;
    if (channel == HIDROLAB_CHANNEL_PUMP) {
        /* Automatic daytime program: HH:00:00 through HH:04:59. */
        return local->tm_min < PUMP_ON_MINUTES;
    }
    return true;
}

static uint32_t seconds_to_boundary(hidrolab_channel_t channel, const struct tm *local)
{
    if (!local) return 0;
    const int sec_now = local->tm_hour * 3600 + local->tm_min * 60 + local->tm_sec;
    uint16_t window = hidrolab_settings_get()->sleep_hours;
    int next_hour = is_daytime(local) ? (window >> 8) : (window & 0xFFU);
    int until_window = (next_hour * 3600 - sec_now + 24 * 3600) % (24 * 3600);
    if (until_window == 0) until_window = 24 * 3600;
    if (!is_daytime(local) || channel == HIDROLAB_CHANNEL_FAN) {
        return (uint32_t)until_window;
    }

    if (local->tm_min < PUMP_ON_MINUTES) {
        int phase = PUMP_ON_MINUTES * 60 - (local->tm_min * 60 + local->tm_sec);
        return (uint32_t)(phase < until_window ? phase : until_window);
    }
    int phase = 3600 - (local->tm_min * 60 + local->tm_sec);
    return (uint32_t)(phase < until_window ? phase : until_window);
}

static int64_t schedule_hour_key(const struct tm *local)
{
    /* Includes the date: even a 24-hour RTC/NTP jump changes the cycle. */
    return (((int64_t)local->tm_year * 12 + local->tm_mon) * 31 +
            local->tm_mday - 1) * 24 + local->tm_hour;
}

static void stop_all_locked(void)
{
    memset(s_manual_override, 0, sizeof(s_manual_override));
    memset(s_manual_state, 0, sizeof(s_manual_state));
    memset(s_manual_until_us, 0, sizeof(s_manual_until_us));
    /* Always attempt both outputs, even if the first GPIO command fails. */
    (void)apply_output(HIDROLAB_CHANNEL_PUMP, false);
    (void)apply_output(HIDROLAB_CHANNEL_FAN, false);
}

static esp_err_t apply_schedule_locked(const struct tm *local, bool force)
{
    esp_err_t result = ESP_OK;
    if (!local) {
        if (!s_clock_unavailable)
            ESP_LOGW(TAG, "clock unavailable: automatic outputs OFF; explicit manual control available");
        s_clock_unavailable = true;
    } else s_clock_unavailable = false;
    int64_t hour = local ? schedule_hour_key(local) : -1;
    /* Each manual ON or OFF lasts two monotonic minutes, including at night.
     * Expiry resumes the CURRENT schedule, never a stale saved output. */
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < 2; ++i) {
        if (s_manual_override[i] && now >= s_manual_until_us[i]) {
            s_manual_override[i] = false;
            s_manual_until_us[i] = 0;
            force = true; /* Re-enter AUTO even if its first output stays ON. */
            if (i == HIDROLAB_CHANNEL_PUMP) {
                s_pump_started_us = -1;
                s_pump_blocked_hour = -1;
            }
        }
    }

    /* A monotonic deadline cannot be extended by RTC/NTP adjustments, a
     * frozen wall clock, or repeatedly pressing ON while already running. */
    if (!s_manual_override[HIDROLAB_CHANNEL_PUMP] &&
        output_is_on(HIDROLAB_CHANNEL_PUMP) && s_pump_started_us >= 0 &&
        esp_timer_get_time() - s_pump_started_us >= PUMP_MAX_AUTO_ON_US) {
        s_pump_blocked_hour = hour;
        (void)apply_output(HIDROLAB_CHANNEL_PUMP, false);
        ESP_LOGW(TAG, "automatic pump 5-minute limit reached");
    }

    for (int i = 0; i < 2; ++i) {
        hidrolab_channel_t channel = (hidrolab_channel_t)i;
        bool desired = s_manual_override[i] ? s_manual_state[i] :
                                             desired_phase(channel, local);
        if (channel == HIDROLAB_CHANNEL_PUMP && !s_manual_override[i] &&
            local && s_pump_blocked_hour == hour)
            desired = false;
        /* Reconcile actual commanded state every tick, not just phase edges.
         * This also retries failed writes and handles skipped clock boundaries. */
        if (force || output_is_on(channel) != desired) {
            esp_err_t err = apply_output(channel, desired);
            if (err != ESP_OK && result == ESP_OK) result = err;
            if (err != ESP_OK && desired) (void)apply_output(channel, false);
        }
    }
    return result;
}

static void scheduler_task(void *arg)
{
    (void)arg;
    while (1) {
        struct tm local = {0};
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool time_valid = get_local_time(&local);
            apply_schedule_locked(time_valid ? &local : NULL, false);
            if (time_valid) check_daily_alarm(&local);
            xSemaphoreGive(s_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t hidrolab_scheduler_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    stop_all_locked();
    s_clock_unavailable = false;
    s_pump_blocked_hour = -1;
    if (xTaskCreate(scheduler_task, "hidrolab_sched", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "M4: GPIO19 pump / GPIO20 fan ACTIVE-HIGH. AUTO DAY: pump first 5 min/hour, fan ON. AUTO NIGHT: both OFF. Manual ON/OFF for 120 seconds.");
    return ESP_OK;
}

esp_err_t hidrolab_scheduler_manual_set(hidrolab_channel_t channel, bool on)
{
    if ((unsigned)channel > HIDROLAB_CHANNEL_FAN || !s_mutex) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return ESP_ERR_TIMEOUT;

    /* Do not run the automatic program here: pressing OFF must never first
     * turn a motor ON. The other channel keeps its own independent mode. */
    esp_err_t err = apply_output(channel, on);
    s_manual_until_us[channel] = esp_timer_get_time() + MANUAL_OVERRIDE_US;
    if (err == ESP_OK) {
        s_manual_override[channel] = true;
        s_manual_state[channel] = on;
        if (channel == HIDROLAB_CHANNEL_PUMP && on) s_pump_blocked_hour = -1;
    } else {
        /* Keep the safe OFF intent after an error; the scheduler retries it
         * instead of silently handing the channel back to automatic ON. */
        s_manual_override[channel] = true;
        s_manual_state[channel] = false;
        (void)apply_output(channel, false);
    }
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "manual command channel=%d state=%d result=%s", channel, on, esp_err_to_name(err));
    return err;
}

bool hidrolab_scheduler_manual_override_active(hidrolab_channel_t channel)
{
    if ((unsigned)channel > HIDROLAB_CHANNEL_FAN || !s_mutex) return false;
    bool active = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        active = s_manual_override[channel];
        xSemaphoreGive(s_mutex);
    }
    return active;
}

bool hidrolab_scheduler_night_locked(void)
{
    struct tm local = {0};
    return !get_local_time(&local) || !is_daytime(&local);
}

esp_err_t hidrolab_scheduler_set_auto(hidrolab_channel_t channel, bool automatic,
                                      uint16_t on_minutes, uint16_t off_minutes)
{
    (void)on_minutes;
    (void)off_minutes;
    if ((unsigned)channel > HIDROLAB_CHANNEL_FAN || !s_mutex) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = ESP_OK;
    if (automatic) {
        if (channel == HIDROLAB_CHANNEL_PUMP && s_manual_override[channel]) {
            s_pump_started_us = -1;
            s_pump_blocked_hour = -1;
        }
        s_manual_override[channel] = false;
        s_manual_until_us[channel] = 0;
        struct tm local = {0};
        bool time_valid = get_local_time(&local);
        err = apply_schedule_locked(time_valid ? &local : NULL, true);
        bool expected = time_valid && desired_phase(channel, &local);
        if (channel == HIDROLAB_CHANNEL_PUMP && time_valid &&
            s_pump_blocked_hour == schedule_hour_key(&local)) expected = false;
        if (output_is_on(channel) != expected) err = ESP_FAIL;
    } else {
        s_manual_override[channel] = true;
        s_manual_state[channel] = output_is_on(channel);
        s_manual_until_us[channel] = esp_timer_get_time() + MANUAL_OVERRIDE_US;
    }
    xSemaphoreGive(s_mutex);
    return err;
}

hidrolab_schedule_status_t hidrolab_scheduler_get_status(hidrolab_channel_t channel)
{
    hidrolab_schedule_status_t out = {
        .automatic = true,
        .on_minutes = channel == HIDROLAB_CHANNEL_PUMP ? PUMP_ON_MINUTES : 720,
        .off_minutes = channel == HIDROLAB_CHANNEL_PUMP ? 60 - PUMP_ON_MINUTES : 720,
        .phase_on = false,
        .remaining_seconds = 0,
    };
    if ((unsigned)channel > HIDROLAB_CHANNEL_FAN || !s_mutex) return out;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        out.automatic = !s_manual_override[channel];
        out.phase_on = output_is_on(channel);
        if (!out.automatic) {
            int64_t left = s_manual_until_us[channel] - esp_timer_get_time();
            out.remaining_seconds = left > 0 ? (uint32_t)((left + 999999) / 1000000) : 0;
        }
        struct tm local = {0};
        if (out.automatic && get_local_time(&local))
            out.remaining_seconds = seconds_to_boundary(channel, &local);
        if (out.automatic && channel == HIDROLAB_CHANNEL_PUMP && out.phase_on && s_pump_started_us >= 0) {
            int64_t left = PUMP_MAX_AUTO_ON_US - (esp_timer_get_time() - s_pump_started_us);
            uint32_t seconds = left > 0 ? (uint32_t)((left + 999999) / 1000000) : 0;
            if (seconds < out.remaining_seconds) out.remaining_seconds = seconds;
        }
        xSemaphoreGive(s_mutex);
    }
    return out;
}
