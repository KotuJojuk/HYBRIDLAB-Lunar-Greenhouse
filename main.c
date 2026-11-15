#include <stdint.h>

#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hidrolab_control.h"
#include "hidrolab_buzzer.h"
#include "hidrolab_scheduler.h"
#include "hidrolab_settings.h"
#include "hidrolab_time.h"
#include "hidrolab_ui.h"
#include "hidrolab_wifi.h"
#include "lvgl_port.h"
#include "waveshare_rgb_lcd_port.h"

#define BOARD_CONTROL_ADDRESS 0x30

static const char *MAIN_TAG = "hidrolab_main";

static esp_err_t board_i2c_init(void)
{
    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        .clk_flags = 0,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_MASTER_NUM, &config), MAIN_TAG,
                        "I2C configuration failed");
    return i2c_driver_install(I2C_MASTER_NUM, config.mode, 0, 0, 0);
}

static esp_err_t board_send_command(uint8_t command)
{
    return i2c_master_write_to_device(I2C_MASTER_NUM, BOARD_CONTROL_ADDRESS,
                                      &command, sizeof(command),
                                      pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

static void board_send_command_best_effort(uint8_t command)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3; ++attempt) {
        err = board_send_command(command);
        if (err == ESP_OK) return;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    /* The 0x30 helper controller can briefly NACK while the board powers up.
     * It must never keep the LCD and UI from booting. */
    ESP_LOGW(MAIN_TAG, "board command %u unavailable; continuing: %s",
             command, esp_err_to_name(err));
}

void app_main(void)
{
    ESP_LOGI(MAIN_TAG, "HybridLab Mini v0.4.2 starting");

    ESP_ERROR_CHECK(hidrolab_settings_init());

    // Safety first: both outputs are OFF before UI/Wi-Fi startup.
    ESP_ERROR_CHECK(hidrolab_control_init());

    /* SNTP uses the lwIP tcpip mailbox, so the network stack must exist before
     * hidrolab_time_init starts the time service. No connection is required. */
    ESP_ERROR_CHECK(hidrolab_wifi_init());

    ESP_ERROR_CHECK(board_i2c_init());
    vTaskDelay(pdMS_TO_TICKS(50));
    board_send_command_best_effort(250);
    board_send_command_best_effort(0);

    /* Restore the battery-backed RTC before schedules and UI time decisions. */
    ESP_ERROR_CHECK(hidrolab_time_init());
    ESP_ERROR_CHECK(hidrolab_scheduler_init());

    if (hidrolab_settings_get()->wifi_ssid[0] != '\0') {
        hidrolab_wifi_connect_saved();
    }

    esp_err_t buzzer_err = hidrolab_buzzer_init();
    if (buzzer_err != ESP_OK) {
        ESP_LOGW(MAIN_TAG, "Buzzer unavailable; continuing without sound: %s",
                 esp_err_to_name(buzzer_err));
    }
    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init());

    if (lvgl_port_lock(-1)) {
        hidrolab_ui_init();
        lvgl_port_unlock();
    }

    ESP_LOGI(MAIN_TAG, "HybridLab Mini ready");
}
