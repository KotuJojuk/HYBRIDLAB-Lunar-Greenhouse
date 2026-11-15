#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HIDROLAB_CHANNEL_PUMP = 0,
    HIDROLAB_CHANNEL_FAN = 1,
} hidrolab_channel_t;

typedef struct {
    bool automatic;              /* false while a manual override is active */
    uint16_t on_minutes;         /* pump: 5 minutes per daytime hour */
    uint16_t off_minutes;        /* pump: remaining 55 minutes */
    bool phase_on;               /* last successfully commanded GPIO state, not motor feedback */
    uint32_t remaining_seconds;  /* manual: until auto return; auto: next schedule boundary */
} hidrolab_schedule_status_t;

esp_err_t hidrolab_scheduler_init(void);

/* Explicit manual ON/OFF is available regardless of sleep hours or RTC state.
 * Each accepted ON/OFF lasts 120 monotonic seconds, then resumes automatic.
 * Repeated commands restart only that channel's independent timer. */
esp_err_t hidrolab_scheduler_manual_set(hidrolab_channel_t channel, bool on);
bool hidrolab_scheduler_manual_override_active(hidrolab_channel_t channel);
/* Legacy name: reports an inactive AUTOMATIC window, not a manual lock. */
bool hidrolab_scheduler_night_locked(void);

/* Return a channel to its automatic program; legacy minute arguments unused. */
esp_err_t hidrolab_scheduler_set_auto(hidrolab_channel_t channel, bool automatic,
                                      uint16_t on_minutes, uint16_t off_minutes);
hidrolab_schedule_status_t hidrolab_scheduler_get_status(hidrolab_channel_t channel);

#ifdef __cplusplus
}
#endif
