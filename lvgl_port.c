#include "lvgl_port.h"

#include <assert.h>

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "lvgl_port";
static SemaphoreHandle_t lvgl_mutex;
static TaskHandle_t s_lvgl_task;

static void display_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixel_map)
{
    (void)area;
    /* Direct mode can present several dirty rectangles for one frame. The
     * hardware framebuffer must only switch after the last rectangle. */
    if (!lv_display_flush_is_last(display)) {
        lv_display_flush_ready(display);
        return;
    }

    esp_lcd_panel_handle_t panel = lv_display_get_user_data(display);
    /* Drop a stale full-frame notification before asking the RGB driver to
     * present the newly drawn offline buffer. */
    ulTaskNotifyTake(pdTRUE, 0);
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0,
                                              LVGL_PORT_H_RES, LVGL_PORT_V_RES, pixel_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB draw failed: %s", esp_err_to_name(err));
        lv_display_flush_ready(display);
    }
}

static void display_flush_wait(lv_display_t *display)
{
    if (lv_display_flush_is_last(display)) {
        /* The old buffer may be reused only after DMA has completed a frame.
         * A bounded wait keeps the UI recoverable if a panel callback is lost. */
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200)) == 0) {
            ESP_LOGW(TAG, "RGB frame completion timeout");
        }
    }
    lv_display_flush_ready(display);
}

static lv_display_t *display_init(esp_lcd_panel_handle_t panel)
{
    /* LCD uses two PSRAM framebuffers. Repaint the COMPLETE frame before
     * swapping; otherwise an incompletely synchronized DIRECT-mode buffer can
     * show stale horizontal regions, especially in the status bar.
     * This deliberately trades some drawing throughput for stable pixels. */
    void *buffer1 = NULL;
    void *buffer2 = NULL;
    esp_err_t err = esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &buffer1, &buffer2);
    if (err != ESP_OK || buffer1 == NULL || buffer2 == NULL) {
        ESP_LOGE(TAG, "Could not acquire RGB framebuffers: %s", esp_err_to_name(err));
        return NULL;
    }

    const size_t buffer_bytes = LVGL_PORT_H_RES * LVGL_PORT_V_RES * sizeof(lv_color16_t);

    lv_display_t *display = lv_display_create(LVGL_PORT_H_RES, LVGL_PORT_V_RES);
    lv_display_set_user_data(display, panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer1, buffer2, buffer_bytes, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_flush_wait_cb(display, display_flush_wait);
    return display;
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch = lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(touch);
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t points = 0;
    if (esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &points, 1) && points > 0) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static lv_indev_t *indev_init(esp_lcd_touch_handle_t touch)
{
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);
    lv_indev_set_user_data(indev, touch);
    return indev;
}

static void tick_increment(void *arg)
{
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static esp_err_t tick_init(void)
{
    const esp_timer_create_args_t args = {
        .callback = tick_increment,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t timer;
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &timer), TAG, "create tick timer");
    return esp_timer_start_periodic(timer, LVGL_PORT_TICK_PERIOD_MS * 1000);
}

static void lvgl_task(void *arg)
{
    uint32_t delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
    while (true) {
        if (lvgl_port_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        delay_ms = delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS ? LVGL_PORT_TASK_MIN_DELAY_MS : delay_ms;
        delay_ms = delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS ? LVGL_PORT_TASK_MAX_DELAY_MS : delay_ms;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t touch)
{
    lv_init();
    lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    ESP_RETURN_ON_FALSE(lvgl_mutex != NULL, ESP_ERR_NO_MEM, TAG, "create LVGL mutex");
    ESP_RETURN_ON_ERROR(tick_init(), TAG, "initialize LVGL tick");
    ESP_RETURN_ON_FALSE(display_init(panel) != NULL, ESP_ERR_NO_MEM, TAG, "initialize display");
    if (touch != NULL) {
        ESP_RETURN_ON_FALSE(indev_init(touch) != NULL, ESP_ERR_NO_MEM, TAG, "initialize touch");
    }

    BaseType_t core = LVGL_PORT_TASK_CORE < 0 ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, NULL,
                                               LVGL_PORT_TASK_PRIORITY, &s_lvgl_task, core) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create LVGL task");
    return ESP_OK;
}

bool lvgl_port_lock(int timeout_ms)
{
    assert(lvgl_mutex != NULL);
    TickType_t timeout = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mutex, timeout) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    assert(lvgl_mutex != NULL);
    xSemaphoreGiveRecursive(lvgl_mutex);
}

bool lvgl_port_notify_rgb_vsync(void)
{
    /* Called at whole-frame completion, not at each color-copy completion.
     * Wake the LVGL task; LVGL APIs themselves stay out of interrupt context. */
    BaseType_t need_yield = pdFALSE;
    if (s_lvgl_task) vTaskNotifyGiveFromISR(s_lvgl_task, &need_yield);
    return need_yield == pdTRUE;
}
