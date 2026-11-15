#include "hidrolab_settings.h"

#include <limits.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "hidrolab_settings";
static const char *NS = "hidrolab";
static hidrolab_settings_t s_cfg;

/* Original 9-slot ring indices: A1=0, A2=1, A3=2, B3=3,
 * C3=4, C2=5, C1=6, B1=7, B2=8. Retain these indices in NVS. */
static const uint8_t GARDEN_SLOT_INDICES[HIDROLAB_GARDEN_SLOT_COUNT] = {0, 2, 8, 6, 4};

uint8_t hidrolab_garden_slot_at(uint8_t display_index)
{
    return display_index < HIDROLAB_GARDEN_SLOT_COUNT ?
        GARDEN_SLOT_INDICES[display_index] : HIDROLAB_SLOT_COUNT;
}

bool hidrolab_slot_is_available(uint8_t slot_index)
{
    return slot_index == 0 || slot_index == 2 || slot_index == 8 ||
           slot_index == 6 || slot_index == 4;
}

const char *hidrolab_slot_position_name(uint8_t slot_index)
{
    static const char *names[HIDROLAB_SLOT_COUNT] = {
        "A1", "A2", "A3", "B3", "C3", "C2", "C1", "B1", "B2"
    };
    return slot_index < HIDROLAB_SLOT_COUNT ? names[slot_index] : "-";
}

typedef struct {
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    hidrolab_chemistry_sample_t samples[HIDROLAB_CHEM_HISTORY_MAX];
} chemistry_history_blob_t;

static chemistry_history_blob_t s_chem_history;
/* Dedicated, versioned NVS blob; existing settings and chemistry blobs remain intact. */
typedef struct {
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    hidrolab_observation_history_entry_t entries[HIDROLAB_OBS_HISTORY_MAX];
} observation_history_blob_t;
static observation_history_blob_t s_obs_history;

/* Stored independently so old nine-slot NVS layouts do not change. */
typedef struct {
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    char names[HIDROLAB_CUSTOM_SPECIES_MAX][17];
} custom_species_blob_t;
static custom_species_blob_t s_custom_species;

static bool custom_species_has(const custom_species_blob_t *db, const char *name)
{
    if (!name || !name[0]) return false;
    for (size_t i = 0; i < db->count; ++i) {
        if (strcmp(db->names[i], name) == 0) return true;
    }
    return false;
}


static void load_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    memset(&s_chem_history, 0, sizeof(s_chem_history));
    s_chem_history.version = 1;
    memset(&s_obs_history, 0, sizeof(s_obs_history));
    s_obs_history.version = 1;
    memset(&s_custom_species, 0, sizeof(s_custom_species));
    s_custom_species.version = 1;
    s_cfg.plant = HIDROLAB_PLANT_LETTUCE;
    s_cfg.season_day = 1;
    s_cfg.quiz_unlocked = 1;
    s_cfg.astronaut_profile = HIDROLAB_ASTRONAUT_NEUTRAL;
    s_cfg.sleep_hours = (20U << 8) | 8U;
    s_cfg.alarm_config = 8U << 8; /* 08:00, off until the user enables it. */
    strlcpy(s_cfg.astronaut_name, "Astronot", sizeof(s_cfg.astronaut_name));
}

static esp_err_t save_u8(const char *key, uint8_t value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t save_u16(const char *key, uint16_t value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}


static esp_err_t save_u32(const char *key, uint32_t value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t hidrolab_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    load_defaults();

    nvs_handle_t h;
    err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    uint8_t u8 = 0;
    uint16_t u16 = 0;
    if (nvs_get_u8(h, "setup", &u8) == ESP_OK) s_cfg.setup_done = u8 != 0;
    if (nvs_get_u8(h, "plant", &u8) == ESP_OK && u8 <= HIDROLAB_PLANT_ARUGULA) s_cfg.plant = (hidrolab_plant_t)u8;
    if (nvs_get_u8(h, "day", &u8) == ESP_OK && u8 >= 1 && u8 <= 30) s_cfg.season_day = u8;
    if (nvs_get_u8(h, "tasks", &u8) == ESP_OK) s_cfg.task_mask = u8;
    if (nvs_get_u16(h, "points", &u16) == ESP_OK) s_cfg.points = u16;
    if (nvs_get_u8(h, "quizlvl", &u8) == ESP_OK &&
        u8 >= 1 && u8 <= HIDROLAB_QUIZ_LEVELS) {
        s_cfg.quiz_unlocked = u8;
    }
    if (nvs_get_u8(h, "aprofile", &u8) == ESP_OK && u8 <= HIDROLAB_ASTRONAUT_NEUTRAL) s_cfg.astronaut_profile = (hidrolab_astronaut_profile_t)u8;
    uint32_t u32 = 0;
    if (nvs_get_u32(h, "badges", &u32) == ESP_OK) s_cfg.achievements = u32;
    if (nvs_get_u16(h, "quizdone", &u16) == ESP_OK) s_cfg.quiz_completed_mask = u16;
    if (nvs_get_u16(h, "quizperf", &u16) == ESP_OK) s_cfg.quiz_perfect_mask = u16;
    if (nvs_get_u8(h, "memwins", &u8) == ESP_OK) s_cfg.memory_wins = u8;
    if (nvs_get_u8(h, "bubbest", &u8) == ESP_OK) s_cfg.bubble_best = u8;
    if (nvs_get_u8(h, "newgames", &u8) == ESP_OK) s_cfg.activity_game_mask = u8;
    if (nvs_get_u8(h, "arcbest", &u8) == ESP_OK) s_cfg.arcade_best = u8;
    if (nvs_get_u32(h, "chemdate", &u32) == ESP_OK) s_cfg.chemistry_date = u32;
    if (nvs_get_u16(h, "ph100", &u16) == ESP_OK) s_cfg.ph_x100 = u16;
    if (nvs_get_u16(h, "ec100", &u16) == ESP_OK) s_cfg.ec_x100 = u16;
    if (nvs_get_u32(h, "taskdate", &u32) == ESP_OK) s_cfg.task_date = u32;
    if (nvs_get_u8(h, "leafobs", &u8) == ESP_OK && u8 <= 4) s_cfg.leaf_observation = u8;
    if (nvs_get_u8(h, "rootobs", &u8) == ESP_OK && u8 <= 4) s_cfg.root_observation = u8;
    if (nvs_get_u16(h, "sleepwin", &u16) == ESP_OK && (u16 >> 8) < 24 &&
        (u16 & 0xFFU) < 24 && (u16 >> 8) != (u16 & 0xFFU)) {
        s_cfg.sleep_hours = u16;
    }
    if (nvs_get_u32(h, "alarmcfg", &u32) == ESP_OK &&
        (u32 & ~0x1FFFFUL) == 0 && ((u32 >> 8) & 0xFFU) < 24 &&
        (u32 & 0xFFU) < 60) {
        s_cfg.alarm_config = u32;
    }
    if (nvs_get_u32(h, "alarmday", &u32) == ESP_OK) {
        s_cfg.alarm_last_date = u32;
    }
    /* Older builds stored typical 1.20-style EC readings as 120. The current
     * UI and history consistently use the meter's integer uS/cm value. */
    if (s_cfg.ec_x100 > 0 && s_cfg.ec_x100 < 500) {
        s_cfg.ec_x100 = (uint16_t)(s_cfg.ec_x100 * 10U);
    }
    size_t len = sizeof(s_cfg.astronaut_name);
    if (nvs_get_str(h, "aname", s_cfg.astronaut_name, &len) != ESP_OK) strlcpy(s_cfg.astronaut_name, "Astronot", sizeof(s_cfg.astronaut_name));

    len = sizeof(s_cfg.wifi_ssid);
    if (nvs_get_str(h, "ssid", s_cfg.wifi_ssid, &len) != ESP_OK) s_cfg.wifi_ssid[0] = '\0';
    len = sizeof(s_cfg.wifi_password);
    if (nvs_get_str(h, "pass", s_cfg.wifi_password, &len) != ESP_OK) s_cfg.wifi_password[0] = '\0';

    size_t slots_len = sizeof(s_cfg.slots);
    /* Accept legacy eight-slot blobs; load_defaults leaves new center empty. */
    if (nvs_get_blob(h, "slots", s_cfg.slots, &slots_len) != ESP_OK ||
        (slots_len != sizeof(s_cfg.slots) && slots_len != 8 * sizeof(s_cfg.slots[0]))) {
        memset(s_cfg.slots, 0, sizeof(s_cfg.slots));
    }
    for (size_t i = 0; i < HIDROLAB_SLOT_COUNT; ++i) {
        if (s_cfg.slots[i].growth_stage > HIDROLAB_STAGE_PLANT) {
            s_cfg.slots[i].growth_stage = HIDROLAB_STAGE_SEED;
        }
    }
    size_t observation_len = sizeof(s_cfg.slot_observations);
    if (nvs_get_blob(h, "slotobs", s_cfg.slot_observations, &observation_len) != ESP_OK ||
        (observation_len != sizeof(s_cfg.slot_observations) &&
         observation_len != 8 * sizeof(s_cfg.slot_observations[0]))) {
        memset(s_cfg.slot_observations, 0, sizeof(s_cfg.slot_observations));
    }
    for (size_t i = 0; i < HIDROLAB_SLOT_COUNT; ++i) {
        hidrolab_slot_observation_t *entry = &s_cfg.slot_observations[i];
        if (!s_cfg.slots[i].active || entry->date_key == 0 ||
            entry->leaf > 4 || entry->root > 4) {
            memset(entry, 0, sizeof(*entry));
        }
    }
    size_t type_len = sizeof(s_cfg.slot_custom_types);
    if (nvs_get_blob(h, "slottype", s_cfg.slot_custom_types, &type_len) != ESP_OK ||
        (type_len != sizeof(s_cfg.slot_custom_types) &&
         type_len != 8 * sizeof(s_cfg.slot_custom_types[0]))) {
        memset(s_cfg.slot_custom_types, 0, sizeof(s_cfg.slot_custom_types));
    }
    for (size_t i = 0; i < HIDROLAB_SLOT_COUNT; ++i) {
        char *custom = s_cfg.slot_custom_types[i];
        if (!s_cfg.slots[i].active || s_cfg.slots[i].type != HIDROLAB_SLOT_OTHER ||
            !memchr(custom, '\0', sizeof(s_cfg.slot_custom_types[i]))) {
            memset(custom, 0, sizeof(s_cfg.slot_custom_types[i]));
        }
    }

    size_t history_len = sizeof(s_chem_history);
    bool history_valid = nvs_get_blob(h, "chemhist", &s_chem_history, &history_len) == ESP_OK &&
                         history_len == sizeof(s_chem_history) &&
                         s_chem_history.version == 1 &&
                         s_chem_history.count <= HIDROLAB_CHEM_HISTORY_MAX;
    if (history_valid) {
        for (size_t i = 0; i < s_chem_history.count; ++i) {
            const hidrolab_chemistry_sample_t *sample = &s_chem_history.samples[i];
            if (sample->date_key == 0 || sample->ph_x100 > 1400 || sample->ec_us > 5000) {
                history_valid = false;
                break;
            }
        }
    }
    if (!history_valid) {
        memset(&s_chem_history, 0, sizeof(s_chem_history));
        s_chem_history.version = 1;
    }
    if (s_chem_history.count == 0 && s_cfg.chemistry_date != 0 &&
        s_cfg.ph_x100 <= 1400 && s_cfg.ec_x100 <= 5000) {
        s_chem_history.samples[0] = (hidrolab_chemistry_sample_t) {
            .date_key = s_cfg.chemistry_date,
            .ph_x100 = s_cfg.ph_x100,
            .ec_us = s_cfg.ec_x100,
        };
        s_chem_history.count = 1;
    }
    size_t obs_history_len = sizeof(s_obs_history);
    bool obs_history_valid = nvs_get_blob(h, "obshist", &s_obs_history, &obs_history_len) == ESP_OK &&
                             obs_history_len == sizeof(s_obs_history) &&
                             s_obs_history.version == 1 &&
                             s_obs_history.count <= HIDROLAB_OBS_HISTORY_MAX;
    if (obs_history_valid) {
        for (size_t i = 0; i < s_obs_history.count; ++i) {
            const hidrolab_observation_history_entry_t *entry = &s_obs_history.entries[i];
            if (!entry->date_key || entry->slot_index >= HIDROLAB_SLOT_COUNT ||
                entry->leaf > 4 || entry->root > 4) {
                obs_history_valid = false;
                break;
            }
        }
    }
    if (!obs_history_valid) {
        memset(&s_obs_history, 0, sizeof(s_obs_history));
        s_obs_history.version = 1;
    }
    /* One-time in-memory migration of current observations from earlier firmware.
     * They are committed with the first subsequent observation, without guessing
     * colors for earlier dates. */
    if (s_obs_history.count == 0) {
        for (size_t i = 0; i < HIDROLAB_SLOT_COUNT; ++i) {
            const hidrolab_slot_observation_t *old = &s_cfg.slot_observations[i];
            if (old->date_key && (old->leaf || old->root)) {
                s_obs_history.entries[s_obs_history.count++] =
                    (hidrolab_observation_history_entry_t) {
                        .date_key = old->date_key,
                        .slot_index = (uint8_t)i,
                        .leaf = old->leaf,
                        .root = old->root,
                    };
            }
        }
    }
    /* Open old custom-slot names in the new dropdown, even if the new library
     * did not exist before this firmware. The migration does not touch the
     * legacy slots blob and is written only if additions were necessary. */
    size_t species_len = sizeof(s_custom_species);
    bool valid_species = nvs_get_blob(h, "species", &s_custom_species, &species_len) == ESP_OK &&
                         species_len == sizeof(s_custom_species) &&
                         s_custom_species.version == 1 &&
                         s_custom_species.count <= HIDROLAB_CUSTOM_SPECIES_MAX;
    if (valid_species) {
        for (size_t i = 0; i < s_custom_species.count; ++i) {
            const char *name = s_custom_species.names[i];
            if (!name[0] || !memchr(name, '\0', sizeof(s_custom_species.names[i]))) {
                valid_species = false;
                break;
            }
            for (size_t j = 0; j < i; ++j) {
                if (strcmp(name, s_custom_species.names[j]) == 0) {
                    valid_species = false;
                    break;
                }
            }
            if (!valid_species) break;
        }
    }
    if (!valid_species) {
        memset(&s_custom_species, 0, sizeof(s_custom_species));
        s_custom_species.version = 1;
    }
    bool needs_species_save = false;
    for (size_t i = 0; i < HIDROLAB_SLOT_COUNT; ++i) {
        if (!s_cfg.slots[i].active || s_cfg.slots[i].type != HIDROLAB_SLOT_OTHER) continue;
        const char *name = s_cfg.slot_custom_types[i];
        if (!name[0] || custom_species_has(&s_custom_species, name) ||
            s_custom_species.count >= HIDROLAB_CUSTOM_SPECIES_MAX) continue;
        strlcpy(s_custom_species.names[s_custom_species.count++], name,
                sizeof(s_custom_species.names[0]));
        needs_species_save = true;
    }
    nvs_close(h);
    if (needs_species_save) {
        nvs_handle_t migration;
        if (nvs_open(NS, NVS_READWRITE, &migration) == ESP_OK) {
            if (nvs_set_blob(migration, "species", &s_custom_species,
                             sizeof(s_custom_species)) == ESP_OK) {
                (void)nvs_commit(migration);
            }
            nvs_close(migration);
        }
    }

    ESP_LOGI(TAG, "settings loaded: setup=%d plant=%d day=%u", s_cfg.setup_done, s_cfg.plant, s_cfg.season_day);
    return ESP_OK;
}

const hidrolab_settings_t *hidrolab_settings_get(void) { return &s_cfg; }

bool hidrolab_settings_is_sleep_hour(uint8_t hour)
{
    if (hour >= 24) return false;
    uint16_t window = s_cfg.sleep_hours;
    uint8_t start = (uint8_t)(window >> 8);
    uint8_t end = (uint8_t)(window & 0xFFU);
    if (start >= 24 || end >= 24 || start == end) {
        start = 20;
        end = 8;
    }
    return start < end ? (hour >= start && hour < end) :
                         (hour >= start || hour < end);
}

esp_err_t hidrolab_settings_set_sleep_hours(uint8_t start_hour, uint8_t end_hour)
{
    if (start_hour >= 24 || end_hour >= 24 || start_hour == end_hour) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t window = ((uint16_t)start_hour << 8) | end_hour;
    esp_err_t err = save_u16("sleepwin", window);
    if (err == ESP_OK) s_cfg.sleep_hours = window;
    return err;
}

esp_err_t hidrolab_settings_set_alarm(bool enabled, uint8_t hour, uint8_t minute)
{
    if (hour >= 24 || minute >= 60) return ESP_ERR_INVALID_ARG;
    uint32_t config = (enabled ? 1UL << 16 : 0) |
                      ((uint32_t)hour << 8) | minute;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, "alarmcfg", config);
    if (err == ESP_OK) err = nvs_set_u32(h, "alarmday", 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.alarm_config = config;
        s_cfg.alarm_last_date = 0;
    }
    return err;
}

esp_err_t hidrolab_settings_mark_alarm_fired(uint32_t date_key)
{
    if (date_key == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = save_u32("alarmday", date_key);
    if (err == ESP_OK) s_cfg.alarm_last_date = date_key;
    return err;
}

esp_err_t hidrolab_settings_set_setup_done(bool done)
{
    esp_err_t err = save_u8("setup", done ? 1 : 0);
    if (err == ESP_OK) s_cfg.setup_done = done;
    return err;
}

esp_err_t hidrolab_settings_set_plant(hidrolab_plant_t plant)
{
    if (plant > HIDROLAB_PLANT_ARUGULA) return ESP_ERR_INVALID_ARG;
    esp_err_t err = save_u8("plant", (uint8_t)plant);
    if (err == ESP_OK) s_cfg.plant = plant;
    return err;
}


esp_err_t hidrolab_settings_set_astronaut_name(const char *name)
{
    if (!name) return ESP_ERR_INVALID_ARG;
    char next_name[sizeof(s_cfg.astronaut_name)];
    strlcpy(next_name, name[0] ? name : "Astronot", sizeof(next_name));
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "aname", next_name);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) strlcpy(s_cfg.astronaut_name, next_name,
                               sizeof(s_cfg.astronaut_name));
    return err;
}

esp_err_t hidrolab_settings_set_astronaut_profile(hidrolab_astronaut_profile_t profile)
{
    if (profile > HIDROLAB_ASTRONAUT_NEUTRAL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = save_u8("aprofile", (uint8_t)profile);
    if (err == ESP_OK) s_cfg.astronaut_profile = profile;
    return err;
}

esp_err_t hidrolab_settings_set_quiz_unlocked(uint8_t level)
{
    if (level < 1) {
        level = 1;
    }
    if (level > HIDROLAB_QUIZ_LEVELS) {
        level = HIDROLAB_QUIZ_LEVELS;
    }
    esp_err_t err = save_u8("quizlvl", level);
    if (err == ESP_OK) s_cfg.quiz_unlocked = level;
    return err;
}

esp_err_t hidrolab_settings_unlock_achievement(uint8_t index)
{
    if (index >= 20) return ESP_ERR_INVALID_ARG;
    return hidrolab_settings_unlock_achievements(1UL << index);
}

esp_err_t hidrolab_settings_unlock_achievements(uint32_t bits)
{
    bits &= 0x000FFFFFUL;
    uint32_t merged = s_cfg.achievements | bits;
    if (merged == s_cfg.achievements) return ESP_OK;
    esp_err_t err = save_u32("badges", merged);
    if (err == ESP_OK) s_cfg.achievements = merged;
    return err;
}

esp_err_t hidrolab_settings_mark_quiz_result(uint8_t level, uint8_t score)
{
    if (level < 1 || level > HIDROLAB_QUIZ_LEVELS) return ESP_ERR_INVALID_ARG;
    uint16_t bit = (uint16_t)(1U << (level - 1));
    uint16_t completed = s_cfg.quiz_completed_mask;
    uint16_t perfect = s_cfg.quiz_perfect_mask;
    if (score >= 2) completed |= bit;
    if (score == 3) perfect |= bit;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(h, "quizdone", completed);
    if (err == ESP_OK) err = nvs_set_u16(h, "quizperf", perfect);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.quiz_completed_mask = completed;
        s_cfg.quiz_perfect_mask = perfect;
    }
    return err;
}

esp_err_t hidrolab_settings_add_memory_win(void)
{
    uint8_t wins = s_cfg.memory_wins < UINT8_MAX ?
                   (uint8_t)(s_cfg.memory_wins + 1U) : UINT8_MAX;
    esp_err_t err = save_u8("memwins", wins);
    if (err == ESP_OK) s_cfg.memory_wins = wins;
    return err;
}

esp_err_t hidrolab_settings_set_bubble_best(uint8_t score)
{
    if (score <= s_cfg.bubble_best) return ESP_OK;
    esp_err_t err = save_u8("bubbest", score);
    if (err == ESP_OK) s_cfg.bubble_best = score;
    return err;
}

esp_err_t hidrolab_settings_set_wifi(const char *ssid, const char *password)
{
    if (!ssid || !password) return ESP_ERR_INVALID_ARG;
    if (!ssid[0] || strlen(ssid) >= sizeof(s_cfg.wifi_ssid) ||
        strlen(password) >= sizeof(s_cfg.wifi_password)) {
        return ESP_ERR_INVALID_ARG;
    }
    char next_ssid[sizeof(s_cfg.wifi_ssid)];
    char next_password[sizeof(s_cfg.wifi_password)];
    strlcpy(next_ssid, ssid, sizeof(next_ssid));
    strlcpy(next_password, password, sizeof(next_password));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "ssid", next_ssid);
    if (err == ESP_OK) err = nvs_set_str(h, "pass", next_password);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        strlcpy(s_cfg.wifi_ssid, next_ssid, sizeof(s_cfg.wifi_ssid));
        strlcpy(s_cfg.wifi_password, next_password,
                sizeof(s_cfg.wifi_password));
    }
    return err;
}

esp_err_t hidrolab_settings_set_task_mask(uint8_t mask)
{
    esp_err_t err = save_u8("tasks", mask);
    if (err == ESP_OK) s_cfg.task_mask = mask;
    return err;
}

esp_err_t hidrolab_settings_set_points(uint16_t points)
{
    esp_err_t err = save_u16("points", points);
    if (err == ESP_OK) s_cfg.points = points;
    return err;
}

esp_err_t hidrolab_settings_set_task_progress(uint8_t mask, uint16_t points)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "tasks", mask);
    if (err == ESP_OK) err = nvs_set_u16(h, "points", points);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.task_mask = mask;
        s_cfg.points = points;
    }
    return err;
}

static uint16_t add_points_saturated(uint16_t current, uint16_t amount)
{
    return amount > (uint16_t)(UINT16_MAX - current) ? UINT16_MAX :
                                                            (uint16_t)(current + amount);
}

esp_err_t hidrolab_settings_record_memory_completion(void)
{
    uint8_t wins = s_cfg.memory_wins < UINT8_MAX ?
                   (uint8_t)(s_cfg.memory_wins + 1U) : UINT8_MAX;
    uint16_t points = add_points_saturated(s_cfg.points, 30);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "memwins", wins);
    if (err == ESP_OK) err = nvs_set_u16(h, "points", points);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.memory_wins = wins;
        s_cfg.points = points;
    }
    return err;
}

esp_err_t hidrolab_settings_record_quiz_completion(uint8_t level, uint8_t score)
{
    if (level < 1 || level > HIDROLAB_QUIZ_LEVELS || score > 3) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t completed = s_cfg.quiz_completed_mask;
    uint16_t perfect = s_cfg.quiz_perfect_mask;
    uint8_t unlocked = s_cfg.quiz_unlocked;
    uint16_t points = s_cfg.points;
    uint16_t bit = (uint16_t)(1U << (level - 1));
    if (score >= 2) {
        completed |= bit;
        if (level < HIDROLAB_QUIZ_LEVELS && unlocked <= level) {
            unlocked = level + 1;
        }
        points = add_points_saturated(points, (uint16_t)(score * 10));
    }
    if (score == 3) perfect |= bit;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(h, "quizdone", completed);
    if (err == ESP_OK) err = nvs_set_u16(h, "quizperf", perfect);
    if (err == ESP_OK) err = nvs_set_u8(h, "quizlvl", unlocked);
    if (err == ESP_OK) err = nvs_set_u16(h, "points", points);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.quiz_completed_mask = completed;
        s_cfg.quiz_perfect_mask = perfect;
        s_cfg.quiz_unlocked = unlocked;
        s_cfg.points = points;
    }
    return err;
}

esp_err_t hidrolab_settings_record_bubble_completion(uint8_t score)
{
    uint8_t best = score > s_cfg.bubble_best ? score : s_cfg.bubble_best;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "bubbest", best);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) s_cfg.bubble_best = best;
    return err;
}

esp_err_t hidrolab_settings_record_activity_game(uint8_t game_bit, uint16_t points,
                                                  uint8_t arcade_score)
{
    uint8_t games = s_cfg.activity_game_mask | game_bit;
    uint8_t best = arcade_score > s_cfg.arcade_best ? arcade_score :
                                                        s_cfg.arcade_best;
    uint16_t total_points = add_points_saturated(s_cfg.points, points);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "newgames", games);
    if (err == ESP_OK) err = nvs_set_u8(h, "arcbest", best);
    if (err == ESP_OK) err = nvs_set_u16(h, "points", total_points);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.activity_game_mask = games;
        s_cfg.arcade_best = best;
        s_cfg.points = total_points;
    }
    return err;
}

esp_err_t hidrolab_settings_record_daily_chemistry(uint16_t ph_x100, uint16_t ec_x100,
                                                    uint32_t date_key)
{
    if (ph_x100 > 1400 || ec_x100 > 5000 || date_key == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    bool already_rewarded = s_cfg.chemistry_date == date_key;
    uint8_t task_mask = s_cfg.task_mask | (1U << 4);
    uint16_t points = already_rewarded ? s_cfg.points :
                      add_points_saturated(s_cfg.points, 10);
    chemistry_history_blob_t next_history = s_chem_history;

    int history_index = -1;
    for (size_t i = 0; i < next_history.count; ++i) {
        if (next_history.samples[i].date_key == date_key) {
            history_index = (int)i;
            break;
        }
    }
    if (history_index < 0) {
        if (next_history.count < HIDROLAB_CHEM_HISTORY_MAX) {
            history_index = next_history.count++;
        } else {
            memmove(&next_history.samples[0], &next_history.samples[1],
                    sizeof(next_history.samples[0]) * (HIDROLAB_CHEM_HISTORY_MAX - 1));
            history_index = HIDROLAB_CHEM_HISTORY_MAX - 1;
        }
    }
    next_history.samples[history_index] = (hidrolab_chemistry_sample_t) {
        .date_key = date_key,
        .ph_x100 = ph_x100,
        .ec_us = ec_x100,
    };

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, "chemdate", date_key);
    if (err == ESP_OK) err = nvs_set_u16(h, "ph100", ph_x100);
    if (err == ESP_OK) err = nvs_set_u16(h, "ec100", ec_x100);
    if (err == ESP_OK) err = nvs_set_u8(h, "tasks", task_mask);
    if (err == ESP_OK) err = nvs_set_u16(h, "points", points);
    if (err == ESP_OK) err = nvs_set_blob(h, "chemhist", &next_history,
                                          sizeof(next_history));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.chemistry_date = date_key;
        s_cfg.ph_x100 = ph_x100;
        s_cfg.ec_x100 = ec_x100;
        s_cfg.task_mask = task_mask;
        s_cfg.points = points;
        s_chem_history = next_history;
    }
    return err;
}

esp_err_t hidrolab_settings_sync_daily_date(uint32_t date_key)
{
    if (date_key == 0) return ESP_ERR_INVALID_ARG;
    if (s_cfg.task_date == date_key && (s_cfg.task_mask & 1U) == 0) return ESP_OK;

    bool first_calendar_sync = s_cfg.task_date == 0;
    bool new_day = !first_calendar_sync && s_cfg.task_date != date_key;
    uint8_t season_day = s_cfg.season_day;
    uint8_t task_mask = s_cfg.task_mask;
    uint8_t leaf_observation = s_cfg.leaf_observation;
    uint8_t root_observation = s_cfg.root_observation;
    if (new_day) {
        if (season_day < 30) season_day++;
        task_mask = 0;
        leaf_observation = 0;
        root_observation = 0;
    } else if (first_calendar_sync) {
        /* Old firmware had a bubble checkbox plus manual leaf/root checks.
         * Preserve automatic game/chemistry progress, but require real color
         * observations after this migration. */
        task_mask &= (uint8_t)~((1U << 0) | (1U << 1) | (1U << 2));
        leaf_observation = 0;
        root_observation = 0;
    } else {
        task_mask &= (uint8_t)~(1U << 0);
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, "taskdate", date_key);
    if (err == ESP_OK) err = nvs_set_u8(h, "day", season_day);
    if (err == ESP_OK) err = nvs_set_u8(h, "tasks", task_mask);
    if (err == ESP_OK) err = nvs_set_u8(h, "leafobs", leaf_observation);
    if (err == ESP_OK) err = nvs_set_u8(h, "rootobs", root_observation);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.task_date = date_key;
        s_cfg.season_day = season_day;
        s_cfg.task_mask = task_mask;
        s_cfg.leaf_observation = leaf_observation;
        s_cfg.root_observation = root_observation;
    }
    return err;
}

esp_err_t hidrolab_settings_record_observation(bool leaf, uint8_t observation,
                                                uint32_t date_key)
{
    if (observation < 1 || observation > 4) return ESP_ERR_INVALID_ARG;
    if (s_cfg.task_date != date_key) {
        esp_err_t sync_err = hidrolab_settings_sync_daily_date(date_key);
        if (sync_err != ESP_OK) return sync_err;
    }

    uint8_t bit = leaf ? (1U << 1) : (1U << 2);
    bool already_rewarded = (s_cfg.task_mask & bit) != 0;
    uint8_t task_mask = s_cfg.task_mask | bit;
    uint16_t points = already_rewarded ? s_cfg.points :
                      add_points_saturated(s_cfg.points, 10);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, leaf ? "leafobs" : "rootobs", observation);
    if (err == ESP_OK) err = nvs_set_u8(h, "tasks", task_mask);
    if (err == ESP_OK) err = nvs_set_u16(h, "points", points);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        if (leaf) s_cfg.leaf_observation = observation;
        else s_cfg.root_observation = observation;
        s_cfg.task_mask = task_mask;
        s_cfg.points = points;
    }
    return err;
}

size_t hidrolab_settings_observation_history_count(void)
{
    return s_obs_history.count;
}

bool hidrolab_settings_observation_history_get(size_t index,
                                                hidrolab_observation_history_entry_t *entry)
{
    if (!entry || index >= s_obs_history.count) return false;
    *entry = s_obs_history.entries[index];
    return true;
}

size_t hidrolab_settings_chemistry_history_count(void)
{
    return s_chem_history.count;
}

bool hidrolab_settings_chemistry_history_get(size_t index,
                                              hidrolab_chemistry_sample_t *sample)
{
    if (!sample || index >= s_chem_history.count) return false;
    *sample = s_chem_history.samples[index];
    return true;
}

esp_err_t hidrolab_settings_advance_day(void)
{
    uint8_t season_day = s_cfg.season_day < 30 ?
                         (uint8_t)(s_cfg.season_day + 1U) : s_cfg.season_day;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, "day", season_day);
    if (err == ESP_OK) err = nvs_set_u8(h, "tasks", 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.season_day = season_day;
        s_cfg.task_mask = 0;
    }
    return err;
}

esp_err_t hidrolab_settings_reset_setup(void)
{
    esp_err_t err = save_u8("setup", 0);
    if (err == ESP_OK) s_cfg.setup_done = false;
    return err;
}

esp_err_t hidrolab_settings_factory_reset(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        load_defaults();
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) load_defaults();
    return err;
}

const char *hidrolab_settings_plant_name(void)
{
    return s_cfg.plant == HIDROLAB_PLANT_ARUGULA ? "Roka" : "Marul";
}


size_t hidrolab_settings_custom_species_count(void)
{
    return s_custom_species.count;
}

const char *hidrolab_settings_custom_species_name(size_t index)
{
    return index < s_custom_species.count ? s_custom_species.names[index] : NULL;
}

const char *hidrolab_slot_type_name(hidrolab_slot_type_t type)
{
    switch (type) {
        case HIDROLAB_SLOT_LETTUCE: return "Marul";
        case HIDROLAB_SLOT_ARUGULA: return "Roka";
        case HIDROLAB_SLOT_BASIL: return "Fesleğen";
        case HIDROLAB_SLOT_MINT: return "Nane";
        case HIDROLAB_SLOT_PARSLEY: return "Maydanoz";
        case HIDROLAB_SLOT_SPINACH: return "Ispanak";
        case HIDROLAB_SLOT_OTHER: return "Diğer";
        default: return "Bos";
    }
}

esp_err_t hidrolab_settings_set_slot(uint8_t index, const hidrolab_plant_slot_t *slot,
                                    const char *custom_type)
{
    if (!hidrolab_slot_is_available(index) || !slot) return ESP_ERR_INVALID_ARG;
    if (slot->active && slot->type == HIDROLAB_SLOT_OTHER &&
        (!custom_type || !custom_type[0] || strlen(custom_type) >= 17)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Add a new name to the reusable type list in the same flash transaction
     * that saves the plant. An existing name never takes another entry. */
    custom_species_blob_t next_species = s_custom_species;
    bool species_changed = false;
    if (slot->active && slot->type == HIDROLAB_SLOT_OTHER &&
        !custom_species_has(&next_species, custom_type)) {
        if (next_species.count >= HIDROLAB_CUSTOM_SPECIES_MAX) {
            return ESP_ERR_NO_MEM;
        }
        strlcpy(next_species.names[next_species.count++], custom_type,
                sizeof(next_species.names[0]));
        species_changed = true;
    }
    /* Keep the live garden unchanged until the complete blob is committed.
     * The persisted layout stays identical to earlier firmware versions. */
    hidrolab_plant_slot_t next_slots[HIDROLAB_SLOT_COUNT];
    hidrolab_slot_observation_t next_observations[HIDROLAB_SLOT_COUNT];
    char next_types[HIDROLAB_SLOT_COUNT][17];
    memcpy(next_slots, s_cfg.slots, sizeof(next_slots));
    memcpy(next_observations, s_cfg.slot_observations, sizeof(next_observations));
    memcpy(next_types, s_cfg.slot_custom_types, sizeof(next_types));
    bool reset_observation = !slot->active || !s_cfg.slots[index].active ||
                             s_cfg.slots[index].type != slot->type ||
                             slot->growth_stage == HIDROLAB_STAGE_SEED;
    if (reset_observation) {
        memset(&next_observations[index], 0, sizeof(next_observations[index]));
    }
    memset(next_types[index], 0, sizeof(next_types[index]));
    if (slot->active && slot->type == HIDROLAB_SLOT_OTHER) {
        strlcpy(next_types[index], custom_type, sizeof(next_types[index]));
    }
    bool types_changed = memcmp(next_types[index], s_cfg.slot_custom_types[index],
                                sizeof(next_types[index])) != 0;
    memcpy(&next_slots[index], slot, sizeof(next_slots[index]));
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "slots", next_slots, sizeof(next_slots));
    if (err == ESP_OK && reset_observation) {
        err = nvs_set_blob(h, "slotobs", next_observations,
                           sizeof(next_observations));
    }
    if (err == ESP_OK && types_changed) {
        err = nvs_set_blob(h, "slottype", next_types, sizeof(next_types));
    }
    if (err == ESP_OK && species_changed) {
        err = nvs_set_blob(h, "species", &next_species, sizeof(next_species));
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        if (species_changed) s_custom_species = next_species;
        memcpy(s_cfg.slots, next_slots, sizeof(s_cfg.slots));
        if (reset_observation) {
            memcpy(s_cfg.slot_observations, next_observations,
                   sizeof(s_cfg.slot_observations));
        }
        if (types_changed) {
            memcpy(s_cfg.slot_custom_types, next_types,
                   sizeof(s_cfg.slot_custom_types));
        }
    }
    return err;
}

esp_err_t hidrolab_settings_record_slot_observation(uint8_t index, bool leaf,
                                                     uint8_t observation,
                                                     uint32_t date_key)
{
    if (!hidrolab_slot_is_available(index) || !s_cfg.slots[index].active || observation < 1 ||
        observation > 4 || date_key == 0) return ESP_ERR_INVALID_ARG;
    if (s_cfg.slots[index].growth_stage == HIDROLAB_STAGE_SEED)
        return ESP_ERR_INVALID_STATE;
    hidrolab_slot_observation_t next[HIDROLAB_SLOT_COUNT];
    memcpy(next, s_cfg.slot_observations, sizeof(next));
    hidrolab_slot_observation_t *entry = &next[index];
    if (entry->date_key != date_key) {
        memset(entry, 0, sizeof(*entry));
        entry->date_key = date_key;
    }
    if (leaf) entry->leaf = observation;
    else entry->root = observation;
    observation_history_blob_t next_history = s_obs_history;
    int history_index = -1;
    for (size_t i = 0; i < next_history.count; ++i) {
        if (next_history.entries[i].date_key == date_key &&
            next_history.entries[i].slot_index == index) {
            history_index = (int)i;
            break;
        }
    }
    if (history_index < 0) {
        if (next_history.count < HIDROLAB_OBS_HISTORY_MAX) {
            history_index = next_history.count++;
        } else {
            memmove(&next_history.entries[0], &next_history.entries[1],
                    sizeof(next_history.entries[0]) * (HIDROLAB_OBS_HISTORY_MAX - 1));
            history_index = HIDROLAB_OBS_HISTORY_MAX - 1;
        }
        next_history.entries[history_index] = (hidrolab_observation_history_entry_t) {
            .date_key = date_key, .slot_index = index,
        };
    }
    if (leaf) next_history.entries[history_index].leaf = observation;
    else next_history.entries[history_index].root = observation;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "slotobs", next, sizeof(next));
    if (err == ESP_OK) err = nvs_set_blob(h, "obshist", &next_history,
                                          sizeof(next_history));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        memcpy(s_cfg.slot_observations, next, sizeof(s_cfg.slot_observations));
        s_obs_history = next_history;
    }
    return err;
}

esp_err_t hidrolab_settings_clear_slot(uint8_t index)
{
    if (!hidrolab_slot_is_available(index)) return ESP_ERR_INVALID_ARG;
    hidrolab_plant_slot_t empty_slot;
    memset(&empty_slot, 0, sizeof(empty_slot));
    return hidrolab_settings_set_slot(index, &empty_slot, NULL);
}
