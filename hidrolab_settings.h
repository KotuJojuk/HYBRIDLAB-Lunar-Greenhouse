#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HIDROLAB_QUIZ_LEVELS 15
/* NVS compatibility: keep nine stored positions to preserve old garden and
 * observation blobs. Only these five physical openings are usable:
 * A1 (old index 0), A3 (2), B2 (8), C1 (6), C3 (4). */
#define HIDROLAB_SLOT_COUNT 9
#define HIDROLAB_GARDEN_SLOT_COUNT 5
/* A separate persistent list, not part of the legacy slots blob. */
#define HIDROLAB_CUSTOM_SPECIES_MAX 16

/* Display order: top left, top right, center, bottom left, bottom right. */
uint8_t hidrolab_garden_slot_at(uint8_t display_index);
bool hidrolab_slot_is_available(uint8_t slot_index);
const char *hidrolab_slot_position_name(uint8_t slot_index);

typedef enum {
    HIDROLAB_PLANT_LETTUCE = 0,
    HIDROLAB_PLANT_ARUGULA = 1,
} hidrolab_plant_t;

typedef enum {
    HIDROLAB_ASTRONAUT_BOY = 0,
    HIDROLAB_ASTRONAUT_GIRL = 1,
    HIDROLAB_ASTRONAUT_NEUTRAL = 2,
} hidrolab_astronaut_profile_t;

typedef enum {
    HIDROLAB_SLOT_EMPTY = 0,
    HIDROLAB_SLOT_LETTUCE = 1,
    HIDROLAB_SLOT_ARUGULA = 2,
    HIDROLAB_SLOT_BASIL = 3,
    HIDROLAB_SLOT_MINT = 4,
    HIDROLAB_SLOT_PARSLEY = 5,
    HIDROLAB_SLOT_SPINACH = 6,
    HIDROLAB_SLOT_OTHER = 7,
} hidrolab_slot_type_t;

typedef enum {
    HIDROLAB_STAGE_SEED = 0,
    HIDROLAB_STAGE_SPROUT = 1,
    HIDROLAB_STAGE_PLANT = 2,
} hidrolab_growth_stage_t;

typedef struct {
    bool active;
    char name[17];
    hidrolab_slot_type_t type;
    uint8_t planted_day;
    uint8_t growth_stage;
} hidrolab_plant_slot_t;

/* Kept in a separate NVS blob: the original 28-byte plant slot layout must
 * remain unchanged so existing gardens survive firmware updates. */
typedef struct {
    uint32_t date_key;
    uint8_t leaf;
    uint8_t root;
    uint16_t reserved;
} hidrolab_slot_observation_t;

typedef struct {
    bool setup_done;
    hidrolab_plant_t plant;
    uint8_t season_day;
    uint8_t task_mask;
    uint16_t points;
    char astronaut_name[25];
    hidrolab_astronaut_profile_t astronaut_profile;
    uint8_t quiz_unlocked;
    uint32_t achievements;
    uint16_t quiz_completed_mask;
    uint16_t quiz_perfect_mask;
    uint8_t memory_wins;
    uint8_t bubble_best;
    uint8_t activity_game_mask;
    uint8_t arcade_best;
    uint32_t chemistry_date;
    uint16_t ph_x100;
    uint16_t ec_x100;
    uint32_t task_date;
    uint8_t leaf_observation;
    uint8_t root_observation;
    char wifi_ssid[33];
    char wifi_password[65];
    hidrolab_plant_slot_t slots[HIDROLAB_SLOT_COUNT];
    hidrolab_slot_observation_t slot_observations[HIDROLAB_SLOT_COUNT];
    char slot_custom_types[HIDROLAB_SLOT_COUNT][17];
    /* High byte: sleep begins (hour); low byte: wake hour. */
    uint16_t sleep_hours;
    /* Bit 16: enabled; bits 8-15: hour; bits 0-7: minute. */
    uint32_t alarm_config;
    uint32_t alarm_last_date;
} hidrolab_settings_t;

#define HIDROLAB_CHEM_HISTORY_MAX 7
#define HIDROLAB_OBS_HISTORY_MAX (HIDROLAB_SLOT_COUNT * HIDROLAB_CHEM_HISTORY_MAX)

typedef struct {
    uint32_t date_key;
    uint8_t slot_index;
    uint8_t leaf;
    uint8_t root;
    uint8_t reserved;
} hidrolab_observation_history_entry_t;

typedef struct {
    uint32_t date_key;
    uint16_t ph_x100;
    uint16_t ec_us;
} hidrolab_chemistry_sample_t;

esp_err_t hidrolab_settings_init(void);
const hidrolab_settings_t *hidrolab_settings_get(void);
esp_err_t hidrolab_settings_set_setup_done(bool done);
esp_err_t hidrolab_settings_set_plant(hidrolab_plant_t plant);
esp_err_t hidrolab_settings_set_astronaut_name(const char *name);
esp_err_t hidrolab_settings_set_astronaut_profile(hidrolab_astronaut_profile_t profile);
esp_err_t hidrolab_settings_set_quiz_unlocked(uint8_t level);
esp_err_t hidrolab_settings_unlock_achievement(uint8_t index);
esp_err_t hidrolab_settings_mark_quiz_result(uint8_t level, uint8_t score);
esp_err_t hidrolab_settings_add_memory_win(void);
esp_err_t hidrolab_settings_set_bubble_best(uint8_t score);
esp_err_t hidrolab_settings_set_wifi(const char *ssid, const char *password);
esp_err_t hidrolab_settings_set_task_mask(uint8_t mask);
esp_err_t hidrolab_settings_set_points(uint16_t points);
/* Progress mutations below commit all related fields in one NVS transaction.
 * They keep touch callbacks responsive and avoid unnecessary flash wear. */
esp_err_t hidrolab_settings_set_task_progress(uint8_t mask, uint16_t points);
esp_err_t hidrolab_settings_record_memory_completion(void);
esp_err_t hidrolab_settings_record_quiz_completion(uint8_t level, uint8_t score);
esp_err_t hidrolab_settings_record_bubble_completion(uint8_t score);
esp_err_t hidrolab_settings_record_activity_game(uint8_t game_bit, uint16_t points,
                                                  uint8_t arcade_score);
esp_err_t hidrolab_settings_record_daily_chemistry(uint16_t ph_x100, uint16_t ec_x100,
                                                    uint32_t date_key);
esp_err_t hidrolab_settings_sync_daily_date(uint32_t date_key);
esp_err_t hidrolab_settings_record_observation(bool leaf, uint8_t observation,
                                                uint32_t date_key);
size_t hidrolab_settings_observation_history_count(void);
bool hidrolab_settings_observation_history_get(size_t index,
                                                hidrolab_observation_history_entry_t *entry);
size_t hidrolab_settings_chemistry_history_count(void);
bool hidrolab_settings_chemistry_history_get(size_t index,
                                              hidrolab_chemistry_sample_t *sample);
esp_err_t hidrolab_settings_unlock_achievements(uint32_t bits);
esp_err_t hidrolab_settings_advance_day(void);
esp_err_t hidrolab_settings_reset_setup(void);
esp_err_t hidrolab_settings_factory_reset(void);
const char *hidrolab_settings_plant_name(void);
const char *hidrolab_slot_type_name(hidrolab_slot_type_t type);
/* Custom species stay in the list after their planting slots are cleared. */
size_t hidrolab_settings_custom_species_count(void);
const char *hidrolab_settings_custom_species_name(size_t index);
esp_err_t hidrolab_settings_set_slot(uint8_t index, const hidrolab_plant_slot_t *slot,
                                    const char *custom_type);
esp_err_t hidrolab_settings_set_sleep_hours(uint8_t start_hour, uint8_t end_hour);
bool hidrolab_settings_is_sleep_hour(uint8_t hour);
esp_err_t hidrolab_settings_set_alarm(bool enabled, uint8_t hour, uint8_t minute);
esp_err_t hidrolab_settings_mark_alarm_fired(uint32_t date_key);
esp_err_t hidrolab_settings_clear_slot(uint8_t index);
esp_err_t hidrolab_settings_record_slot_observation(uint8_t index, bool leaf,
                                                     uint8_t observation,
                                                     uint32_t date_key);

#ifdef __cplusplus
}
#endif
