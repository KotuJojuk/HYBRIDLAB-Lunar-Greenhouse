#include "hidrolab_control.h"

#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "hidrolab_control";

static bool s_initialized;
static bool s_air_pump_on;
static bool s_fan_on;
static uint32_t s_commands[2];
static uint32_t s_errors[2];
static esp_err_t s_last_error[2];
static portMUX_TYPE s_output_lock = portMUX_INITIALIZER_UNLOCKED;

_Static_assert(HIDROLAB_AIR_PUMP_GPIO != HIDROLAB_FAN_GPIO,
               "Pump and fan must use independent GPIOs");

static esp_err_t set_output(gpio_num_t pin, bool on)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    /* Preload the requested HIGH/LOW, then explicitly reclaim GPIO output
     * routing and output-enable on EVERY command. This is not a UI-only flag:
     * gpio_set_direction also detaches peripheral routing (including native
     * USB on GPIO19/20). Input stays enabled only for digital pad readback. */
    esp_err_t err = gpio_set_level(pin, on ? HIDROLAB_CONTROL_ON_LEVEL : HIDROLAB_CONTROL_OFF_LEVEL);
    if (err != ESP_OK) return err;
    return gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
}

esp_err_t hidrolab_control_init(void)
{
    /* Preload both output latches LOW before enabling either output driver.
     * This does not replace external pull-downs during reset/boot ROM. */
    ESP_RETURN_ON_ERROR(gpio_set_level(HIDROLAB_AIR_PUMP_GPIO, HIDROLAB_CONTROL_OFF_LEVEL),
                        TAG, "Could not preload pump OFF");
    ESP_RETURN_ON_ERROR(gpio_set_level(HIDROLAB_FAN_GPIO, HIDROLAB_CONTROL_OFF_LEVEL),
                        TAG, "Could not preload fan OFF");
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << HIDROLAB_AIR_PUMP_GPIO) |
                        (1ULL << HIDROLAB_FAN_GPIO),
        /* Input enabled only to read back the actual digital pad level. */
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "GPIO configuration failed");

    s_initialized = true;
    ESP_RETURN_ON_ERROR(hidrolab_control_all_off(), TAG,
                        "Failed to set safe startup state");

    ESP_LOGI(TAG, "ACTIVE-HIGH: pump GPIO%d=%d, fan GPIO%d=%d; both commanded OFF (pad readback, not motor feedback)",
             HIDROLAB_AIR_PUMP_GPIO, gpio_get_level(HIDROLAB_AIR_PUMP_GPIO),
             HIDROLAB_FAN_GPIO, gpio_get_level(HIDROLAB_FAN_GPIO));
    return ESP_OK;
}

esp_err_t hidrolab_control_set_air_pump(bool on)
{
    portENTER_CRITICAL(&s_output_lock);
    bool changed = s_air_pump_on != on;
    esp_err_t err = set_output(HIDROLAB_AIR_PUMP_GPIO, on);
    s_commands[0]++;
    s_last_error[0] = err;
    if (err != ESP_OK) s_errors[0]++;
    if (err == ESP_OK) s_air_pump_on = on;
    portEXIT_CRITICAL(&s_output_lock);
    if (err != ESP_OK) ESP_LOGE(TAG, "Air pump GPIO write failed: %s", esp_err_to_name(err));
    else if (changed) ESP_LOGI(TAG, "Air pump command=%s GPIO%d readback=%d", on ? "ON" : "OFF",
                              HIDROLAB_AIR_PUMP_GPIO, gpio_get_level(HIDROLAB_AIR_PUMP_GPIO));
    return err;
}

esp_err_t hidrolab_control_set_fan(bool on)
{
    portENTER_CRITICAL(&s_output_lock);
    bool changed = s_fan_on != on;
    esp_err_t err = set_output(HIDROLAB_FAN_GPIO, on);
    s_commands[1]++;
    s_last_error[1] = err;
    if (err != ESP_OK) s_errors[1]++;
    if (err == ESP_OK) s_fan_on = on;
    portEXIT_CRITICAL(&s_output_lock);
    if (err != ESP_OK) ESP_LOGE(TAG, "Fan GPIO write failed: %s", esp_err_to_name(err));
    else if (changed) ESP_LOGI(TAG, "Fan command=%s GPIO%d readback=%d", on ? "ON" : "OFF",
                              HIDROLAB_FAN_GPIO, gpio_get_level(HIDROLAB_FAN_GPIO));
    return err;
}

esp_err_t hidrolab_control_all_off(void)
{
    esp_err_t pump_err = hidrolab_control_set_air_pump(false);
    esp_err_t fan_err = hidrolab_control_set_fan(false);
    return pump_err != ESP_OK ? pump_err : fan_err;
}

bool hidrolab_control_air_pump_is_on(void)
{
    portENTER_CRITICAL(&s_output_lock);
    bool on = s_air_pump_on;
    portEXIT_CRITICAL(&s_output_lock);
    return on;
}

bool hidrolab_control_fan_is_on(void)
{
    portENTER_CRITICAL(&s_output_lock);
    bool on = s_fan_on;
    portEXIT_CRITICAL(&s_output_lock);
    return on;
}

hidrolab_control_snapshot_t hidrolab_control_snapshot(bool fan)
{
    unsigned i = fan ? 1 : 0;
    portENTER_CRITICAL(&s_output_lock);
    hidrolab_control_snapshot_t out = {
        .initialized = s_initialized,
        .commanded_on = fan ? s_fan_on : s_air_pump_on,
        .pin = fan ? HIDROLAB_FAN_GPIO : HIDROLAB_AIR_PUMP_GPIO,
        .level = s_initialized ? gpio_get_level(fan ? HIDROLAB_FAN_GPIO : HIDROLAB_AIR_PUMP_GPIO) : -1,
        .last_error = s_last_error[i],
        .commands = s_commands[i],
        .errors = s_errors[i],
    };
    portEXIT_CRITICAL(&s_output_lock);
    return out;
}
