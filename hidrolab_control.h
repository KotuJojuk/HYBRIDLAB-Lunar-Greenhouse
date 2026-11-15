#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// CrowPanel Advance UART1 pins. Change these two values only if the wiring changes.
#define HIDROLAB_AIR_PUMP_GPIO GPIO_NUM_19
#define HIDROLAB_FAN_GPIO      GPIO_NUM_20

/* Both external drivers are active HIGH, with a shared signal ground. */
/* L9110 single-direction wiring: each motor's other input must be held LOW
 * externally (not floating). GPIO HIGH is a control signal, not motor power. */
#define HIDROLAB_CONTROL_ON_LEVEL  1
#define HIDROLAB_CONTROL_OFF_LEVEL 0

esp_err_t hidrolab_control_init(void);
esp_err_t hidrolab_control_set_air_pump(bool on);
esp_err_t hidrolab_control_set_fan(bool on);
esp_err_t hidrolab_control_all_off(void);

bool hidrolab_control_air_pump_is_on(void);
bool hidrolab_control_fan_is_on(void);

typedef struct {
    bool initialized;
    bool commanded_on;  /* Last accepted command, not physical motor feedback. */
    int pin;
    int level;          /* Digital pad readback; -1 before initialization. */
    esp_err_t last_error;
    uint32_t commands;
    uint32_t errors;
} hidrolab_control_snapshot_t;

/* Read-only diagnostic snapshot. false = pump, true = fan. */
hidrolab_control_snapshot_t hidrolab_control_snapshot(bool fan);

#ifdef __cplusplus
}
#endif
