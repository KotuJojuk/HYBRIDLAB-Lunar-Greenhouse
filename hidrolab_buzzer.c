#include "hidrolab_buzzer.h"
#include <stdint.h>
#include <stdatomic.h>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "waveshare_rgb_lcd_port.h"

#define BOARD_CONTROL_ADDRESS 0x30
#define BUZZER_ON_COMMAND  246
#define BUZZER_OFF_COMMAND 247

static QueueHandle_t s_beep_queue;
static atomic_bool s_alarm_active;

bool hidrolab_buzzer_alarm_active(void)
{
    return atomic_load(&s_alarm_active);
}

void hidrolab_buzzer_alarm_stop(void)
{
    atomic_store(&s_alarm_active, false);
}

static void send_cmd(uint8_t cmd)
{
    (void)i2c_master_write_to_device(I2C_MASTER_NUM, BOARD_CONTROL_ADDRESS, &cmd, 1,
                                     pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}
static void pulse(uint32_t ms)
{
    send_cmd(BUZZER_ON_COMMAND);
    vTaskDelay(pdMS_TO_TICKS(ms));
    send_cmd(BUZZER_OFF_COMMAND);
}
static void buzzer_task(void *arg)
{
    (void)arg;
    hidrolab_beep_t pattern;
    while (true) {
        if (atomic_load(&s_alarm_active)) {
            /* Only this worker touches the buzzer. Check dismissal every 20 ms. */
            for (int phase = 0; phase < 6 && atomic_load(&s_alarm_active); ++phase) {
                send_cmd(phase % 2 == 0 ? BUZZER_ON_COMMAND : BUZZER_OFF_COMMAND);
                int duration = phase == 5 ? 700 : 160;
                for (int ms = 0; ms < duration && atomic_load(&s_alarm_active); ms += 20)
                    vTaskDelay(pdMS_TO_TICKS(20));
            }
            send_cmd(BUZZER_OFF_COMMAND);
            continue;
        }
        if (xQueueReceive(s_beep_queue, &pattern, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        switch (pattern) {
            case HIDROLAB_BEEP_CORRECT:
                pulse(55); vTaskDelay(pdMS_TO_TICKS(65)); pulse(70); break;
            case HIDROLAB_BEEP_WRONG:
                pulse(220); break;
            case HIDROLAB_BEEP_SUCCESS:
                pulse(55); vTaskDelay(pdMS_TO_TICKS(55)); pulse(55);
                vTaskDelay(pdMS_TO_TICKS(55)); pulse(110); break;
            case HIDROLAB_BEEP_ALARM:
                pulse(180); vTaskDelay(pdMS_TO_TICKS(140)); pulse(180);
                vTaskDelay(pdMS_TO_TICKS(140)); pulse(240); break;
            case HIDROLAB_BEEP_BADGE:
                pulse(80); vTaskDelay(pdMS_TO_TICKS(70)); pulse(80);
                vTaskDelay(pdMS_TO_TICKS(70)); pulse(160);
                vTaskDelay(pdMS_TO_TICKS(130)); pulse(90);
                vTaskDelay(pdMS_TO_TICKS(70)); pulse(260); break;
            case HIDROLAB_BEEP_TOUCH:
            default: pulse(35); break;
        }
        send_cmd(BUZZER_OFF_COMMAND);
    }
}

esp_err_t hidrolab_buzzer_init(void)
{
    if (s_beep_queue) return ESP_OK;
    s_beep_queue = xQueueCreate(6, sizeof(hidrolab_beep_t));
    if (!s_beep_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(buzzer_task, "hidro_buzz", 2048, NULL, 3, NULL) != pdPASS) {
        vQueueDelete(s_beep_queue);
        s_beep_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t hidrolab_buzzer_beep(hidrolab_beep_t pattern)
{
    esp_err_t err = hidrolab_buzzer_init();
    if (err != ESP_OK) return err;
    if (pattern == HIDROLAB_BEEP_ALARM) {
        xQueueReset(s_beep_queue);
        atomic_store(&s_alarm_active, true);
        return ESP_OK;
    }
    if (atomic_load(&s_alarm_active)) return ESP_OK;
    if (xQueueSend(s_beep_queue, &pattern, 0) != pdTRUE) {
        /* Rapid bubble taps should never allocate more tasks or stall the UI.
         * Ordinary touch sounds may be dropped; success and a daily alarm
         * take priority over queued game feedback. */
        if (pattern != HIDROLAB_BEEP_SUCCESS && pattern != HIDROLAB_BEEP_ALARM &&
            pattern != HIDROLAB_BEEP_BADGE) {
            return ESP_ERR_TIMEOUT;
        }
        xQueueReset(s_beep_queue);
        if (xQueueSend(s_beep_queue, &pattern, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
