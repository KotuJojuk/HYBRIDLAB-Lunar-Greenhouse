#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    HIDROLAB_BEEP_TOUCH = 0,
    HIDROLAB_BEEP_CORRECT,
    HIDROLAB_BEEP_WRONG,
    HIDROLAB_BEEP_SUCCESS,
    HIDROLAB_BEEP_ALARM,
    HIDROLAB_BEEP_BADGE,
} hidrolab_beep_t;

esp_err_t hidrolab_buzzer_init(void);
esp_err_t hidrolab_buzzer_beep(hidrolab_beep_t pattern);
bool hidrolab_buzzer_alarm_active(void);
void hidrolab_buzzer_alarm_stop(void);
