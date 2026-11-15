#include "hidrolab_ui.h"
#include "hidrolab_simulation_ui.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "hidrolab_assets.h"
#include "hidrolab_game_assets.h"
/* Kept in this translation unit so an offline build can reuse the configured
 * ESP-IDF/Ninja project even when the host Python launcher is unavailable. */
#include "hidrolab_game_assets.c"
#include "hidrolab_buzzer.h"
#include "hidrolab_control.h"
#include "hidrolab_font_tr.h"
#include "hidrolab_scheduler.h"
#include "hidrolab_settings.h"
#include "hidrolab_time.h"
#include "hidrolab_wifi.h"

#define C_NAVY       0x073763
#define C_NAVY2      0x204D5C
#define C_SKY        0xEAF7FF
#define C_PANEL      0xFFFFFF
#define C_TEXT       0x18344F
#define C_MUTED      0x65839A
#define C_LINE       0xD8E8F0
#define C_GREEN      0x2EAD73
#define C_GREEN2     0xDFF7EB
#define C_BLUE       0x2F86DB
#define C_BLUE2      0xDDEEFF
#define C_PURPLE     0x8B65D7
#define C_PURPLE2    0xEEE7FF
#define C_YELLOW     0xF6B93B
#define C_ORANGE     0xF28B43
#define C_PINK       0xE96DA6
#define C_RED        0xE35B63
#define C_LOCK       0xA3B5C1
#define C_SPACE_TOP  0x0B1733
#define C_SPACE_BOT  0x183758
#define C_SPACE_CARD 0x1D3555
#define C_SPACE_TEXT 0xF4FAFF
#define C_SPACE_MUTE 0xBED0E5
#define C_STAR       0xB3CDD3
#define C_SURFACE     0x1B3452
#define C_SURFACE_ALT 0x254664
#define C_BORDER      0x4C7095
#define C_CYAN        0x79DEEC

#define TOP_H 24
#define CONTENT_Y TOP_H
#define CONTENT_H_FULL (480 - TOP_H)
#define MOON_BASE_W 800
#define MOON_BASE_H CONTENT_H_FULL
#define MOON_BASE_BYTES (MOON_BASE_W * MOON_BASE_H * 2U)
#define BADGE_WARP_W 800
#define BADGE_WARP_H 480
#define BADGE_WARP_BYTES (BADGE_WARP_W * BADGE_WARP_H * 2U)
#define TASK_ALL 0x1E
#define QUIZ_LEVELS HIDROLAB_QUIZ_LEVELS
#define QUIZ_POOL 6
#define MEMORY_CARDS 16
#define MEMORY_POOL 16
#define BADGE_COUNT 20

typedef enum {
    PAGE_GARDEN = 0,
    PAGE_TASKS,
    PAGE_GAMES,
    PAGE_SETTINGS,
} page_t;

typedef enum {
    SETUP_WELCOME = 0,
    SETUP_NAME,
    SETUP_ASTRONAUT,
    SETUP_WIFI,
} setup_step_t;

typedef enum {
    GAME_HOME = 0,
    GAME_MEMORY,
    GAME_BUBBLE,
    GAME_WATER_PATH,
    GAME_ROOT_MAZE,
    GAME_ARCADE,
    GAME_SEED_LANDING,
    GAME_BUG_ESCAPE,
} game_view_t;

typedef enum {
    SETTINGS_MOTORS = 0,
    SETTINGS_SLEEP_ALARM,
    SETTINGS_WIFI,
} settings_view_t;

typedef enum {
    MEMORY_TIMER_NONE = 0,
    MEMORY_TIMER_HIDE_MISMATCH,
    MEMORY_TIMER_COMMIT_MATCH,
} memory_timer_action_t;

typedef struct {
    const char *name;
    const char *desc;
    const char *reward;
} badge_t;

typedef struct {
    const char *q;
    const char *a;
    const char *b;
    const char *c;
    uint8_t ans;
} quiz_q_t;

typedef struct {
    uint8_t category;
    uint8_t art_kind;
    const char *grade;
    const char *unit;
    const char *outcome;
    const char *title;
    const char *subtitle;
    const char *step_title[3];
    const char *step_body[3];
    uint32_t accent;
} academy_lesson_t;

typedef struct {
    const char *prompt;
    const char *choice[3];
    uint8_t correct;
} academy_interaction_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_content;
static lv_obj_t *s_top_wifi;
static lv_obj_t *s_top_date;
static lv_obj_t *s_top_clock;
static lv_obj_t *s_badge_travel_overlay;
static lv_obj_t *s_badge_speed_bar;
static lv_obj_t *s_badge_flight_status;
static lv_obj_t *s_badge_destination_orb;
static page_t s_badge_return_page = PAGE_GARDEN;
static bool s_badge_return_base_home = true;
static lv_timer_t *s_tick_timer;
static const char *UI_TAG = "hidrolab_ui";
static uint8_t *s_moon_base_pixels;
static lv_image_dsc_t s_moon_base_image;
static bool s_moon_base_load_attempted;
static uint8_t *s_badge_warp_pixels;
static lv_image_dsc_t s_badge_warp_image;
static bool s_badge_warp_load_attempted;
static page_t s_page = PAGE_GARDEN;
/* PAGE_GARDEN now owns two layers: the living base launcher and the existing
 * eight-pod greenhouse.  Keeping the old page enum intact avoids disturbing
 * the already-stable task/game/settings navigation. */
static bool s_base_home = true;
static lv_obj_t *s_base_astronaut;
static lv_obj_t *s_base_shadow;
static lv_obj_t *s_base_companion_ui;
static bool s_base_departing;
static bool s_base_returning;
static bool s_badge_boarding;
static bool s_badge_travel_returning;
static bool s_badge_disembark_pending;
static uint8_t s_badge_group;
static lv_obj_t *s_alarm_ringing_overlay;
/* Keep the chosen origin across the one render that rebuilds the hub. */
static bool s_base_return_pending;
static page_t s_base_return_origin;
static page_t s_base_destination;
/* Foot positions, not the transformed image's top-left corner. The last
 * segment rounds the foreground station's flank to reach its front door. */
static const lv_point_t s_base_routes[4][4] = {
    [PAGE_GARDEN]   = {{400, 276}, {326, 250}, {262, 225}, {210, 202}},
    [PAGE_TASKS]    = {{400, 276}, {478, 252}, {568, 230}, {671, 207}},
    [PAGE_GAMES]    = {{400, 276}, {492, 290}, {600, 325}, {675, 350}},
    [PAGE_SETTINGS] = {{400, 276}, {307, 295}, {257, 327}, {208, 352}},
};
#define BASE_WALK_SCALE 256
#define BASE_FOOT_X 60
#define BASE_FOOT_Y 116
static game_view_t s_game_view = GAME_HOME;
static bool s_game_guide_visible;
static settings_view_t s_settings_view = SETTINGS_MOTORS;
static bool s_tasks_badges_view;
static bool s_tasks_academy_view;
static int s_academy_category = -1;
static int s_academy_lesson = -1;
static uint8_t s_academy_step;
static uint8_t s_academy_done_mask;
static int8_t s_academy_choice = -1;
static bool s_academy_question_visible;

/* Setup state is intentionally separated from Wi-Fi password state. */
static bool s_setup_mode;
static bool s_setup_finishing;
static setup_step_t s_setup_step = SETUP_WELCOME;
static char s_setup_name_buf[25];
static bool s_setup_wifi_scan_started;
static lv_obj_t *s_setup_name_ta;
static lv_obj_t *s_wifi_password_ta;
static lv_obj_t *s_wifi_password_status;
static char s_selected_ssid[33];
static lv_obj_t *s_wifi_status_label;
static hidrolab_wifi_state_t s_last_wifi_state = (hidrolab_wifi_state_t)-1;
static size_t s_last_network_count = (size_t)-1;

/* Garden editor. */
static uint8_t s_edit_slot;
static lv_obj_t *s_garden_detail_modal;
static bool s_reopen_garden_detail;
static lv_obj_t *s_slot_name_ta;
static lv_obj_t *s_slot_custom_ta;
static lv_obj_t *s_slot_custom_label;
static lv_obj_t *s_slot_delete_button;
static lv_obj_t *s_slot_keyboard;
static lv_obj_t *s_slot_type_dd;
static lv_obj_t *s_slot_stage_dd;
static lv_obj_t *s_slot_stage_label;
static lv_obj_t *s_slot_modal;
static lv_obj_t *s_slot_delete_overlay;
static lv_obj_t *s_slot_error;
static lv_obj_t *s_slot_stage_buttons[3];
static uint8_t s_slot_stage_value;
static bool s_slot_saving;

/* Settings control state. */
static lv_obj_t *s_pump_on_button;
static lv_obj_t *s_pump_off_button;
static lv_obj_t *s_fan_on_button;
static lv_obj_t *s_fan_off_button;
static esp_err_t s_motor_command_error[2];
static lv_obj_t *s_pump_state_label;
static lv_obj_t *s_fan_state_label;
static lv_obj_t *s_settings_wifi_label;
static lv_obj_t *s_sleep_modal;
static lv_obj_t *s_sleep_start_dd;
static lv_obj_t *s_sleep_end_dd;
static lv_obj_t *s_sleep_error;
static lv_obj_t *s_alarm_modal;
static lv_obj_t *s_alarm_hour_dd;
static lv_obj_t *s_alarm_minute_dd;
static lv_obj_t *s_alarm_error;
static lv_obj_t *s_alarm_switch;
static lv_obj_t *s_alarm_state_label;
static lv_obj_t *s_wifi_manager_overlay;
static lv_obj_t *s_wifi_manager_panel;
static hidrolab_wifi_state_t s_wifi_manager_last_state = (hidrolab_wifi_state_t)-1;
static size_t s_wifi_manager_last_count = (size_t)-1;

/* Memory game. */
static int s_memory_values[MEMORY_CARDS];
static lv_obj_t *s_memory_cards[MEMORY_CARDS];
static lv_obj_t *s_memory_images[MEMORY_CARDS];
static lv_obj_t *s_memory_back_labels[MEMORY_CARDS];
static lv_timer_t *s_memory_hide_timer;
static memory_timer_action_t s_memory_timer_action = MEMORY_TIMER_NONE;
static int s_memory_first = -1;
static int s_memory_second = -1;
static bool s_memory_busy;
static int s_memory_pairs;
static lv_obj_t *s_memory_info;

/* Mini Quiz was retired; keep its implementation below disconnected so the
 * linker can reclaim the question bank from the application image. */
#if 0
static int s_quiz_level;
static int s_quiz_index;
static int s_quiz_pick[3];
static int s_quiz_score;
static uint8_t s_quiz_current_wrong_mask;
static uint8_t s_quiz_prev_wrong_mask[QUIZ_LEVELS];
static lv_obj_t *s_quiz_question;
static lv_obj_t *s_quiz_info;
static lv_obj_t *s_quiz_buttons[3];
static lv_timer_t *s_quiz_feedback_timer;
#endif

/* Bubble game. */
static int s_bubble_score;
static int s_bubble_combo;
static int s_bubble_hits;
static int s_bubble_bonus_until;
static int64_t s_bubble_end_us;
static lv_obj_t *s_bubble_bonus;
static lv_obj_t *s_bubble;
static lv_obj_t *s_bubble_info;
static lv_obj_t *s_bubble_area;
static lv_obj_t *s_bubble_progress;
static lv_obj_t *s_bubble_time;
static bool s_bubble_finished;

/* Shared reactive companion state. */
static lv_obj_t *s_game_astronaut;
static lv_obj_t *s_game_reaction;
static lv_obj_t *s_game_reaction_card;

/* Su Yolu: rotate a newly generated 3x3 pipe network on every round. */
static uint8_t s_water_level;
static uint8_t s_water_rot[9];
static uint8_t s_water_solution[9];
static uint8_t s_water_start_index;
static uint8_t s_water_end_index;
static uint32_t s_water_last_signature;
static int s_water_moves;
static int64_t s_water_start_us;
static int s_water_final_seconds;
static bool s_water_finished;
static bool s_water_recorded;
static lv_obj_t *s_water_tiles[9];
static lv_obj_t *s_water_arms[9][4];
static lv_obj_t *s_water_status;

/* Kök Labirenti: grow a reversible root path through three mazes. */
static uint8_t s_maze_level;
static uint8_t s_maze_path[30];
static uint8_t s_maze_path_len;
static bool s_maze_visited[30];
static bool s_maze_finished;
static bool s_maze_recorded;
static lv_obj_t *s_maze_cells[30];
static lv_obj_t *s_maze_nodes[30];
static lv_obj_t *s_maze_status;

/* Tohum İnişi: steer a falling seed capsule around meteors. */
static int s_landing_score;
static int s_landing_lives;
static int s_landing_round;
static int s_landing_col;
static int s_landing_row;
static int s_landing_target_col;
static int s_landing_obstacles[4];
static bool s_landing_finished;
static bool s_landing_recorded;
static lv_obj_t *s_landing_area;
static lv_obj_t *s_landing_pod;
static lv_obj_t *s_landing_target;
static lv_obj_t *s_landing_rocks[4];
static lv_obj_t *s_landing_info;
static lv_timer_t *s_landing_timer;

/* Böceklerden Kaç: a side-view, accelerating one-life runner. */
#define BUG_TARGET_SECONDS 60
static int s_bug_score;
static int s_bug_lives;
static int s_bug_pack = 1;
static int s_bug_hud_lives;
static lv_obj_t *s_bug_pack_extra[2];
static lv_obj_t *s_bug_air;
static int s_bug_air_draw_x;
static int s_bug_air_kind;
static int32_t s_bug_air_x_q8;
static bool s_bug_air_active;
static int64_t s_bug_air_due_us;
static int64_t s_bug_safe_until_us;
static int s_bug_jumps;
static int s_bug_x;
static int32_t s_bug_x_q8;
static int s_bug_speed;
static int s_bug_variant;
static int s_bug_final_seconds;
static int64_t s_bug_start_us;
static int64_t s_bug_jump_start_us;
static int64_t s_bug_jump_until_us;
static int64_t s_bug_jump_cooldown_us;
static int64_t s_bug_jump_duration_us;
static bool s_bug_jump_queued;
static int64_t s_bug_game_now_us;
static int64_t s_bug_last_tick_us;
static int s_bug_hud_score;
static int s_bug_hud_seconds;
static int s_bug_hud_speed;
static int s_bug_draw_plant_y;
static int s_bug_draw_object_x;
static lv_opa_t s_bug_draw_shadow_opa;
static bool s_bug_collision_checked;
static bool s_bug_started;
static bool s_bug_finished;
static bool s_bug_recorded;
static lv_obj_t *s_bug_area;
static lv_obj_t *s_bug_plant;
static lv_obj_t *s_bug_shadow;
static lv_obj_t *s_bug_object;
static lv_obj_t *s_bug_info;
static lv_obj_t *s_bug_time;
static lv_obj_t *s_bug_progress;
/* Draw the entire animated ground stripe in ONE lightweight object.
 * Six changing LVGL child rectangles used to cause six invalidations/frame. */
static lv_obj_t *s_bug_road_strip;
static int32_t s_bug_road_phase_q8;
static int s_bug_road_draw_phase = -1;
/* Real LVGL crater sprites: moving these makes the ground visibly scroll
 * without invalidating the whole 764x82 track on every frame. */
#define BUG_GROUND_CRATERS 3
static lv_obj_t *s_bug_craters[BUG_GROUND_CRATERS];
static int s_bug_crater_draw_x[BUG_GROUND_CRATERS];
static bool s_bug_fast_display;
static lv_timer_t *s_bug_timer;

/* Separate arcade game; Kabarcık Yakala keeps its own state above. */
static int s_arcade_score;
static int s_arcade_combo;
static int s_arcade_lives;
static int s_arcade_wave;
static int s_arcade_target;
static int64_t s_arcade_end_us;
static int64_t s_arcade_move_us;
static int64_t s_arcade_target_since_us;
static bool s_arcade_finished;
static lv_obj_t *s_arcade_cells[6];
static lv_obj_t *s_arcade_orbs[6];
static lv_obj_t *s_arcade_names[6];
static lv_obj_t *s_arcade_meteors[6];
static lv_obj_t *s_arcade_info;
static lv_obj_t *s_arcade_time;
static lv_obj_t *s_arcade_progress;
static lv_obj_t *s_arcade_area;

/* pH is stored as hundredths; EC is stored as the meter's integer uS/cm value. */
static uint16_t s_edit_ph_x100 = 600;
static uint16_t s_edit_ec_x100 = 1200;
static lv_obj_t *s_ph_slider;
static lv_obj_t *s_ec_slider;
static lv_obj_t *s_ph_value_label;
static lv_obj_t *s_ec_value_label;
static lv_obj_t *s_chem_feedback_card;
static lv_obj_t *s_chem_feedback_label;
static lv_obj_t *s_chem_error;
static bool s_chem_ph_touched;
static bool s_chem_ec_touched;
static lv_obj_t *s_observation_modal;
static lv_obj_t *s_observation_error;

/* RTC-driven quiet screen. Waking the interface never unlocks outputs. */
static bool s_sleep_screen;
static int64_t s_night_awake_until_us;
static lv_obj_t *s_sleep_clock;
static lv_obj_t *s_sleep_date;

/* Badge popup queue. */
static uint32_t s_pending_badges;
static bool s_badge_popup_open;
static bool s_badge_close_pending;
static lv_obj_t *s_badge_overlay;
static bool s_page_render_pending;
static bool s_page_render_retry;
static bool s_setup_build_pending;
static bool s_setup_build_retry;

static void render_page(void);
static void render_game(void);
static void build_setup_step(void);
static void build_badges(void);
static void evaluate_badges(void);
static void show_next_badge_popup(void);
static void badge_popup_close_async_cb(void *unused);
static void game_back_event(lv_event_t *e);
static void game_prepare_selected(void);
static void shuffle_memory(void);
static void memory_restart_event(lv_event_t *e);
static void build_sleep_screen(void);
static void water_init_level(void);
static void bug_init_game(void);
static void bug_begin_run(void);
static void chemistry_graphs_open_event(lv_event_t *e);
static void badge_portal_event(lv_event_t *e);
static void badge_travel_start(void);
static void clear_slot_ui(void);
static void save_slot_event(lv_event_t *e);
static void open_garden_detail(void);
static void observation_open_event(lv_event_t *e);

static void render_page_async_cb(void *unused)
{
    (void)unused;
    s_page_render_pending = false;
    s_page_render_retry = false;
    if (!s_sleep_screen && !s_setup_mode) render_page();
}

/* Keep the known-good full-frame mode on the whole product, while the
 * runner uses LVGL's double-buffered DIRECT mode. LVGL 9 automatically
 * synchronizes dirty areas between the two framebuffers (lv_refr.c).
 * Only the runner needs high-frequency small dirty-rectangle updates. */
static void bug_display_fast_mode(bool enable)
{
    if (s_bug_fast_display == enable) return;
    lv_display_t *display = lv_display_get_default();
    if (!display) return;
    lv_display_set_render_mode(display, enable ? LV_DISPLAY_RENDER_MODE_DIRECT
                                               : LV_DISPLAY_RENDER_MODE_FULL);
    s_bug_fast_display = enable;
    /* First frame in either mode must be complete: no stale top bar pixels. */
    if (s_screen) lv_obj_invalidate(s_screen);
}

static void request_page_render(void)
{
    if (s_page_render_pending) return;
    s_page_render_retry = false;
    s_page_render_pending = true;
    if (lv_async_call(render_page_async_cb, NULL) != LV_RESULT_OK) {
        s_page_render_pending = false;
        /* Retry from the regular UI timer: never tear down a clicked object
         * synchronously, and never leave navigation locked after low memory. */
        s_page_render_retry = true;
    }
}

static void build_setup_step_async_cb(void *unused)
{
    (void)unused;
    s_setup_build_pending = false;
    s_setup_build_retry = false;
    if (!s_setup_mode || s_setup_finishing) return;
    build_setup_step();
}

static void request_setup_build(void)
{
    if (s_setup_build_pending) return;
    s_setup_build_retry = false;
    s_setup_build_pending = true;
    if (lv_async_call(build_setup_step_async_cb, NULL) != LV_RESULT_OK) {
        s_setup_build_pending = false;
        /* Low-memory conditions must not strand setup on an unresponsive
         * step. The regular UI timer retries outside the pressed callback. */
        s_setup_build_retry = true;
    }
}

static void cancel_game_timers(void)
{
    if (s_memory_hide_timer) {
        lv_timer_delete(s_memory_hide_timer);
        s_memory_hide_timer = NULL;
    }
    if (s_landing_timer) {
        lv_timer_delete(s_landing_timer);
        s_landing_timer = NULL;
    }
    if (s_bug_timer) {
        lv_timer_delete(s_bug_timer);
        s_bug_timer = NULL;
    }
}

static const badge_t BADGES[BADGE_COUNT] = {
    {"Ay Filizi", "Bahçene ilk bitkiyi ekle", "İlk bahçe başarımını aç"},
    {"Sera Ekibi", "Bahçede aynı anda 4 bitki yetiştir", "Sera koleksiyonunu büyüt"},
    {"Yörünge Bahçesi", "5 yuvanın tamamını bitkilerle doldur", "Tam bahçe tacını kazan"},
    {"İlk Damla", "İlk pH ve EC ölçümünü kaydet", "Su günlüğünü başlat"},
    {"Su Günlüğü", "3 farklı gün su değerlerini takip et", "Ölçüm serisini büyüt"},
    {"Reaktör Ustası", "7 farklı gün pH ve EC kaydet", "Reaktör ustalığını tamamla"},
    {"Su Kahramanı", "Su Yolu oyununu tamamla", "Bahçe yuvalarına su mavisi çerçeve"},
    {"Ay Koşucusu", "Böcek Kaçışı'nda bir koşuyu tamamla", "Koşu rozetini koleksiyona ekle"},
    {"Meteor Pilotu", "Meteor Arcade görevini tamamla", "Ay Üssüne meteor yıldızları"},
    {"Hafıza Çırağı", "Hasat Hafızası'nı bir kez tamamla", "Hafıza rozetini koleksiyona ekle"},
    {"Hafıza Ustası", "Hasat Hafızası'nı 3 kez tamamla", "Usta rozetini koleksiyona ekle"},
    {"Kabarcık Avcısı", "Kabarcık Yakala'da 10 puana ulaş", "Rehber kartına mavi parıltı"},
    {"Kombo Kaptanı", "Kabarcık Yakala'da 20 puana ulaş", "Rehber kartına altın parıltı"},
    {"Yıldız Tozu", "Toplam 100 puana ulaş", "Puan koleksiyonunda ilk yıldız"},
    {"Yörünge Kaşifi", "Toplam 250 puana ulaş", "Puan koleksiyonunda gümüş yıldız"},
    {"Galaksi Kaptanı", "Toplam 500 puana ulaş", "Puan koleksiyonunda altın yıldız"},
    {"Üs Komutanı", "İlk kurulumu tamamla", "Komutanlık armasını kazan"},
    {"Dünya Sinyali", "Bahçeyi Wi-Fi ağına bağla", "Dünya bağlantısı rozetini kazan"},
    {"7. Üs Günü", "7. bahçe gününe ulaş", "İlk hafta hatırasını kazan"},
    {"14. Üs Günü", "14. bahçe gününe ulaş", "İki hafta hatırasını kazan"},
};

/* The starter academy lives entirely in internal flash.  Its small text-only
 * model keeps lessons available without Wi-Fi or removable storage, while the
 * illustrations are drawn from lightweight LVGL primitives below. */
static const academy_lesson_t ACADEMY_LESSONS[8] = {
    {
        1, 1,
        "5. SINIF", "3. Ünite • Canlıların Yapısına Yolculuk", "FB.5.3.1",
        "Bitki Hücresi Üssü", "Bitki ve hayvan hücresini karşılaştır.",
        {"Temel Kısımlar", "Ortak Takım", "Farkı Bul"},
        {
            "Hücre zarı, sitoplazma ve çekirdek hem bitki hem hayvan hücresinin temel kısımlarıdır.",
            "İki hücrede de yaşamı sürdüren görevli yapılar bulunur. Ortak özellikleri birlikte listele.",
            "Hücre duvarı ve kloroplast bitki hücresini ayırt etmeye yardım eder. İki hücreyi özelliklerine göre karşılaştır."
        },
        0x69D7F3
    },
    {
        1, 2,
        "6. SINIF", "6. Ünite • Elektriğin İletimi ve Direnç", "FB.6.6.1",
        "EC İletkenlik Deneyi", "Sıvıların iletkenliğini verilerle karşılaştır.",
        {"Deneyi Tasarla", "Ölç ve Kaydet", "Veriyi Yorumla"},
        {
            "EC sensörü çözeltinin elektrik iletkenliğini güvenli biçimde ölçer. Önce hangi sıvıları karşılaştıracağını belirle.",
            "Saf su ve besin çözeltisi için ölçüm yap. Değerleri aynı birimle kaydet ve koşulları değiştirme.",
            "Değerleri karşılaştır: çözünmüş maddeler arttığında iletkenliğin nasıl değiştiğine ilişkin çıkarım yap."
        },
        0xB99AF4
    },
    {
        2, 4,
        "7. SINIF", "7. Ünite • Sürdürülebilir Yaşam ve Enerji", "FB.7.7.2",
        "Su Tasarrufu Görevi", "Kaynakları koruyan bir model geliştir.",
        {"Problemi Tanı", "Modelini Kur", "Kanıtla ve Paylaş"},
        {
            "Tatlı su sınırlı bir kaynaktır. Bahçede suyun nerede kullanıldığını ve nerede kaybolabileceğini belirle.",
            "Suyu döngüde tutan hidroponik sistem ile açık sulamayı karşılaştıran basit bir model geliştir.",
            "Gözlem ve ölçümlerini yorumla; suyu daha tasarruflu kullanmak için kanıta dayalı bir öneri paylaş."
        },
        0x63E1AF
    },
    {
        2, 3,
        "8. SINIF", "7. Ünite • Sürdürülebilir Yaşam ve Madde Döngüleri", "FB.8.7.1-2",
        "Fotosentez Laboratuvarı", "Fotosentezi ve etkileyen faktörleri keşfet.",
        {"Besin Üretimi", "Değişkeni Seç", "Hipotez Kur"},
        {
            "Fotosentez, bitkinin ışık enerjisini kullanarak besin üretmesini sağlar ve yaşam için önemlidir.",
            "Işık şiddeti, karbondioksit, su ve sıcaklıktan yalnızca birini değişken olarak seç.",
            "Seçtiğin değişken ile fotosentez hızı arasında neden-sonuç ilişkisi kur ve test edilebilir bir hipotez yaz."
        },
        0xFFD16F
    },
    {
        3, 2,
        "HİDROPONİK LAB", "Su Kimyası • Uygulamalı Ölçüm", "HL.1",
        "pH ve EC Dedektifi", "İki ölçümün bahçede ne anlattığını keşfet.",
        {"İki Ayrı İpucu", "Ölç ve Karşılaştır", "Bahçe Kararı"},
        {
            "pH çözeltinin asitlik-bazlık dengesini, EC ise çözünmüş maddelerin oluşturduğu iletkenliği gösterir.",
            "Aynı saatte pH ve EC ölç, değerleri kaydet ve önceki günlerle karşılaştır. Tek ölçüm yerine eğilimi izle.",
            "Değer sıra dışıysa hemen madde ekleme. Ölçümü doğrula, yetişkin rehberliğinde küçük ve kontrollü bir karar ver."
        },
        0x8FADF8
    },
    {
        3, 0,
        "HİDROPONİK LAB", "Bitki Sağlığı • Uygulamalı Gözlem", "HL.2",
        "Kök ve Yaprak Günlüğü", "Bitkinin verdiği ipuçlarını birlikte oku.",
        {"Köke Bak", "Yaprağı İncele", "Kanıtları Birleştir"},
        {
            "Köklerde renk, koku ve yapı gözlenir. Açık renkli ve diri kökler genellikle sağlıklı bir işarettir.",
            "Yaprağın rengi, duruşu ve lekeleri kaydedilir. Aynı bitkiyi düzenli aralıklarla gözlemek değişimi görünür yapar.",
            "Kök ve yaprak gözlemini pH/EC grafiğiyle birlikte yorumla; yalnızca tek belirtiye bakarak karar verme."
        },
        0x78D89C
    },
    {
        0, 0,
        "3. SINIF", "7. Ünite • Toprağı Tanıyorum, Tarımı Keşfediyorum", "FB.3.7.2",
        "Bitkinin Yaşam Destek Sistemi", "İpuçlarını oku, adil deney kur, bahçede uygula.",
        {"İpuçlarını Oku", "Adil Bir Deney Kur", "Bahçede Kanıt Topla"},
        {
            "Bu bitkinin yaprakları sarkmış ve yetiştirme süngeri kuru. Bitkiler suya, ışığa ve uygun sıcaklığa ihtiyaç duyar; ama önce gördüğümüz kanıtı izlemeliyiz.",
            "Bir etkenin sonucunu anlamak için iki benzer bitki kullanırız. Yalnızca araştırdığımız koşulu değiştirir, diğerlerini aynı tutarız.",
            "Bilim insanı 'iyi' ya da 'kötü' demekle yetinmez. Tarihi, yaprak durumunu ve kökün suya ulaşıp ulaşmadığını birlikte kaydeder."
        },
        0x77D99B
    },
    {
        0, 4,
        "4. SINIF", "8. Ünite • Sürdürülebilir Yaşam Alanları", "FB.4.8.1",
        "Sürdürülebilir Ay Üssü", "Su, enerji ve gıdayı koruyan bir model tasarla.",
        {"Problemi Seç", "Modeli Geliştir", "Kanıtla ve İyileştir"},
        {
            "Ay üssünde su, enerji veya gıda ile ilgili çözmek istediğin tek bir problem belirle.",
            "Kaynakları verimli kullanan bir yaşam alanı modeli geliştir. Hidroponik bahçeyi modeline bağla.",
            "Modelinden veri topla, sürdürülebilir olup olmadığını açıkla ve daha iyi bir çözüm öner."
        },
        0xF0B86A
    },
};

/* Each page is a touch activity rather than a passive slide. The required
 * action is kept compact so the full academy remains usable from internal
 * flash and does not add a second animation/render loop. */
static const academy_interaction_t ACADEMY_INTERACTIONS[8][3] = {
    {
        {"İki hücrede de olan yapıya dokun.",
         {"Kloroplast", "Çekirdek", "Hücre duvarı"}, 1},
        {"Ortak parçaları karşılaştırma alanına taşı.",
         {"Yalnız renk", "Ortak parçalar", "Yalnız boy"}, 1},
        {"Bitkiye özel yapıyı işaretle.",
         {"Çekirdek", "Sitoplazma", "Kloroplast"}, 2},
    },
    {
        {"Karşılaştıracağın iki bardağı seç.",
         {"İki saf su", "Saf + besinli", "Işık + gölge"}, 1},
        {"Adil ölçüm koşulunu etkinleştir.",
         {"Aynı koşul", "Farklı sıcaklık", "Rastgele saat"}, 0},
        {"Yüksek EC göstergesini yorumla.",
         {"Az çözünmüş", "Çok çözünmüş", "Parlak ışık"}, 1},
    },
    {
        {"Önce suyun kaybolduğu yeri bul.",
         {"Kaçak noktası", "Ekran saati", "Kart rengi"}, 0},
        {"Suyu koruyan sistemi kur.",
         {"Açık musluk", "Kapalı döngü", "Tek kullanımlık"}, 1},
        {"Modelini kanıtlayacak aracı seç.",
         {"Ölç ve karşılaştır", "Sadece tahmin", "Rengini değiştir"}, 0},
    },
    {
        {"Bitkinin enerji kaynağını aç.",
         {"Ses", "Işık", "Rüzgar"}, 1},
        {"Deneyde yalnız bir değişken seç.",
         {"Hepsi birden", "Yalnız ışık", "Hiçbiri"}, 1},
        {"Test edilebilir ilişkiyi kur.",
         {"Işık / hız", "Renk / Wi-Fi", "Saat / isim"}, 0},
    },
    {
        {"Su hakkında iki ipucunu birlikte aç.",
         {"pH + EC", "Yalnız pH", "Yalnız EC"}, 0},
        {"Karşılaştırılabilir kayıt düzenini seç.",
         {"Aynı saatte", "Rastgele gün", "Yalnız bir kez"}, 0},
        {"Sıra dışı değerde ilk güvenli adımı seç.",
         {"Madde ekle", "Ölçümü doğrula", "Suyu boşalt"}, 1},
    },
    {
        {"Sağlıklı kök ipucuna dokun.",
         {"Açık ve diri", "Koyu-yumuşak", "Kötü kokulu"}, 0},
        {"Yaprak günlüğüne anlamlı veriyi ekle.",
         {"Wi-Fi adı", "Renk ve duruş", "Oyun puanı"}, 1},
        {"Karar için kanıtları birleştir.",
         {"Tek yaprak", "Tek ölçüm", "Kök+yaprak+pH/EC"}, 2},
    },
    {
        {"Sünger kuru, yapraklar sarkmış. Önce neyi kontrol edersin?",
         {"Kökün suya ulaşmasını", "Hemen daha çok besin eklemeyi", "Işığı daha uzun açmayı"}, 0},
        {"Işığın büyümeye etkisini hangi düzenek daha iyi gösterir?",
         {"Benzer iki bitki; yalnız ışık farklı", "Farklı bitkiler; su ve ışık farklı", "Aynı bitkiye her gün farklı bakım"}, 0},
        {"Bahçe günlüğüne hangi kayıt en güçlü kanıtı ekler?",
         {"Bugün iyi görünüyor", "Yaprak + su erişimi + tarih", "Bir kez bakıp karar vermek"}, 1},
    },
    {
        {"Ay üssündeki gerçek kaynak sorununu seç.",
         {"Logo rengi", "Su kaybı", "Rozet adı"}, 1},
        {"Kaynakları koruyan modeli kur.",
         {"Açık musluk", "Tek kullanımlık", "Kapalı su döngüsü"}, 2},
        {"Modeli iyileştirecek adımı seç.",
         {"Veriyle karşılaştır", "Tahminle bırak", "Ölçmeden değiştir"}, 0},
    },
};

/* Choice-specific explanations are currently part of the fully designed
 * pilot only.  Keeping them in a separate table avoids reserving 72 empty
 * pointers for the compact starter lessons. */
static const char *ACADEMY_PILOT_FEEDBACK[3][3] = {
    {
        "Evet. Kuru sünger, kökün suya ulaşamadığını gösteren doğrudan bir kanıttır.",
        "Besin önemli olabilir; fakat kuru sünger önce su yolunu kontrol etmemiz gerektiğini söyler.",
        "Işık önemlidir; fakat gördüğümüz kuru sünger suya ulaşımı önce kontrol etmemizi gerektirir."
    },
    {
        "Doğru. Yalnız ışık değiştiği için oluşan farkı ışıkla ilişkilendirebiliriz.",
        "Bitki türü, su ve ışık birlikte değişirse sonucun nedenini ayıramayız.",
        "Her gün birden fazla koşulu değiştirmek karşılaştırmayı güvenilmez yapar."
    },
    {
        "'İyi' tek başına ölçülebilir değildir; ne gördüğümüzü ve ne zaman gördüğümüzü yazmalıyız.",
        "Harika. Tarihli yaprak ve su erişimi kaydı, değişimi daha sonra karşılaştırmamızı sağlar.",
        "Tek gözlem değişimi göstermez. Aynı bitkiyi düzenli aralıklarla yeniden incelemeliyiz."
    }
};

#define ACADEMY_CATEGORY_COUNT 4
#define ACADEMY_LESSON_COUNT 8

static const char *ACADEMY_CATEGORY_TITLES[ACADEMY_CATEGORY_COUNT] = {
    "TEMEL", "ORTA", "İLERİ", "HİDROPONİK"
};

static const char *ACADEMY_CATEGORY_SUBTITLES[ACADEMY_CATEGORY_COUNT] = {
    "3-4. sınıflar  •  Tanı, gözle, büyüt",
    "5-6. sınıflar  •  Karşılaştır, deney kur, ölç",
    "7-8. sınıflar  •  Değişkenleri yönet, veriyi yorumla",
    "3-8. sınıflar  •  Gerçek bahçeyi keşfet"
};

static const uint32_t ACADEMY_CATEGORY_ACCENTS[ACADEMY_CATEGORY_COUNT] = {
    0xF0B86A, 0x69D7F3, 0xFFD16F, 0x78D89C
};

static const char *MEMORY_PLANTS[MEMORY_POOL] = {
    "Marul", "Roka", "Maydanoz", "Nane",
    "Ispanak", "Domates", "Çilek", "Kıvırcık",
    "Elma", "Kiraz", "Muz", "Havuç",
    "Portakal", "Üzüm", "Karpuz", "Brokoli",
};

static const lv_image_dsc_t * const MEMORY_IMAGES[MEMORY_POOL] = {
    &hidrolab_img_memory_marul,
    &hidrolab_img_memory_roka,
    &hidrolab_img_memory_maydanoz,
    &hidrolab_img_memory_nane,
    &hidrolab_img_memory_ispanak,
    &hidrolab_img_memory_domates,
    &hidrolab_img_memory_cilek,
    &hidrolab_img_memory_kivircik,
    &hidrolab_img_memory_elma,
    &hidrolab_img_memory_kiraz,
    &hidrolab_img_memory_muz,
    &hidrolab_img_memory_havuc,
    &hidrolab_img_memory_portakal,
    &hidrolab_img_memory_uzum,
    &hidrolab_img_memory_karpuz,
    &hidrolab_img_memory_brokoli,
};


/* Retired Mini Quiz data. It is intentionally excluded from compilation so
 * neither the questions nor the old screens consume flash. */
#if 0
/* Six questions per level: a retry can avoid the exact items answered wrong. */
static const quiz_q_t QUIZ_BANK[QUIZ_LEVELS][QUIZ_POOL] = {
    {
        {"Yolculuğun ilk adımı hangisi?", "Tohum", "Hasat", "Yetişkin bitki", 0},
        {"Tohumdan sonra gelen aşama hangisi?", "Filiz", "Hasat", "Meyve", 0},
        {"Bitki gelişip olgunlaştığında sıradaki adım nedir?", "Hasat", "Yeniden tohum olmak", "Işığı tamamen kapatmak", 0},
        {"Bir bitkiyi sağlıklı gözlemlemek için en iyi yöntem hangisi?", "Düzenli bakmak", "Hiç kontrol etmemek", "Her gün yerini değiştirmek", 0},
        {"Yapraklar bitki için en çok hangi işi yapar?", "Işıktan yararlanır", "Suyu kaynatır", "Kökü keser", 0},
        {"Hidroponik kelimesi en çok neyi anlatır?", "Topraksız yetiştirmeyi", "Sadece çiçek sulamayı", "Tohum boyamayı", 0},
    },
    {
        {"Kökler açık renkli ve diri görünüyor. Teşhisin nedir?", "Sağlıklı görünüyor", "Hemen kesilmeli", "Daha çok gübre gerek", 0},
        {"Kökler koyu, yumuşak ve kötü kokulu. İlk kontrol ne olmalı?", "Sadece ışık", "Su ve oksijen", "Wi-Fi sinyali", 1},
        {"Kök uçları kuruyor, su seviyesi de düşük. Ne yaparsın?", "Su seviyesini düzeltirim", "Fanı sökerim", "pH ölçmeyi bırakırım", 0},
        {"Hava pompası hangi bölgeyi destekler?", "Kök bölgesini", "Ekranı", "Tohum paketini", 0},
        {"Kökleri gözlemlerken en anlamlı değişim hangisidir?", "Renk ve yapı", "Kutu rengi", "Ekran parlaklığı", 0},
        {"Köklerde kötü koku fark edilirse ilk yaklaşım ne olmalı?", "Sistemi kontrol etmek", "Daha çok ışık vermek", "Etiketi değiştirmek", 0},
    },
    {
        {"EC ölçümü bize en çok ne hakkında fikir verir?", "Çözeltideki iyon yoğunluğu", "Odanın sesi", "Işığın rengi", 0},
        {"EC çok yükselirse bitki için ne olabilir?", "Çözelti fazla yoğun olabilir", "Su saflaşır", "Kökler daha çok oksijen alır", 0},
        {"EC çok düşükse ilk şüphe ne olabilir?", "Besin yoğunluğu azdır", "Fan çok hızlıdır", "Saat yanlıştır", 0},
        {"EC değeri tek başına neyi söylemez?", "Hangi iyonun ne kadar olduğunu", "Çözeltinin iletkenliğini", "Değişim eğilimini", 0},
        {"Besin eklerken en güvenli yöntem hangisi?", "Azar azar ekleyip ölçmek", "Bir anda çok eklemek", "Ölçmeden doldurmak", 0},
        {"EC değişimini anlamak için ne yapmak daha iyidir?", "Düzenli kayıt tutmak", "Tek ölçüme bakmak", "Ekranı kapatmak", 0},
    },
    {
        {"pH, çözeltinin hangi özelliğini anlatır?", "Asitlik-bazlık durumunu", "Sıcaklığını", "Hacmini", 0},
        {"pH uygun aralığın dışına çıkarsa ne etkilenebilir?", "Besin alımı", "Wi-Fi hızı", "Kutu fanının çapı", 0},
        {"pH ayarlarken neden küçük adımlar tercih edilir?", "Aşırı düzeltmeyi önlemek için", "Suyu ısıtmak için", "EC'yi sıfırlamak için", 0},
        {"pH ölçümünde tutarlılık için ne önemlidir?", "Doğru ve temiz ölçüm", "Kabı sallamamak", "Ekran rengini değiştirmek", 0},
        {"pH bir anda sıra dışı değişirse ilk yapılacak nedir?", "Ölçümü doğrulamak", "Tüm besini boşaltmak", "Bitkiyi sökmek", 0},
        {"pH ve EC neden birlikte takip edilir?", "Farklı bilgiler verdikleri için", "Aynı şeyi ölçtükleri için", "Sadece oyun puanı için", 0},
    },
    {
        {"Yeni yapraklar canlı, eski yaprak hafif sarı. En doğru adım?", "Kaydedip eğilimi izlemek", "Bitkiyi hemen atmak", "Tüm ayarları değiştirmek", 0},
        {"Yaprak uçları kıvrılıyor. Önce hangilerini ölçersin?", "pH ve EC", "Saat ve Wi-Fi", "Rozet sayısı", 0},
        {"Bir yaprakta lekeler var, diğerleri temiz. Ne yaparsın?", "Yakından gözlemleyip kaydederim", "Tüm kökleri keserim", "Ekranı kapatırım", 0},
        {"Çok az ışıkta hangi belirti görülebilir?", "Zayıf ve uzamış gelişim", "Köklerin metal olması", "Suyun donması", 0},
        {"Işıkla ilgili bir sorunda önce ne karşılaştırılmalı?", "Süre ve konum", "Wi-Fi şifresi", "Rozet sayısı", 0},
        {"Bitkinin gece dönemine sahip olması ne sağlar?", "Düzenli gelişim döngüsü", "Daha yüksek internet hızı", "Daha fazla ekran parlaklığı", 0},
    },
    {
        {"Su çok ısınırsa kök bölgesindeki hangi durum zorlaşabilir?", "Oksijen dengesi", "Ekran çözünürlüğü", "Tohum adı", 0},
        {"Haznede istenmeyen koku oluşursa ne yapılmalı?", "Su ve kökler kontrol edilmeli", "Daha çok gübre eklenmeli", "Fan kapatılmalı", 0},
        {"Sistemde temizlik neden önemlidir?", "Biyolojik sorun riskini azaltır", "Saat daha doğru olur", "Rozet açar", 0},
        {"Bitkide ani değişim gördüğünde en iyi ilk adım nedir?", "Gözlem ve ölçüm yapmak", "Her ayarı değiştirmek", "Bitkiyi hemen atmak", 0},
        {"Bir problemi çözerken tek değişkeni değiştirmek neden faydalıdır?", "Neyin etkili olduğunu anlamak için", "Daha hızlı puan almak için", "Ekranı boşaltmak için", 0},
        {"Su seviyesini gözle takip etmek neden önemlidir?", "Köklerin erişimini anlamak için", "Wi-Fi simgesini görmek için", "Rozet rengini değiştirmek için", 0},
    },
    {
        {"Bir ölçüm cihazı beklenmedik değer gösterirse önce ne yapılmalı?", "Ölçümü tekrar doğrulamak", "Doğrudan besin eklemek", "Sistemi sökmek", 0},
        {"Kalibrasyonun amacı nedir?", "Ölçüm doğruluğunu iyileştirmek", "Bitkiyi hızlandırmak", "Pompayı sessizleştirmek", 0},
        {"Aynı suyu iki kez ölçüp çok farklı sonuç aldıysan ne düşünmelisin?", "Ölçüm koşullarını kontrol etmelisin", "Bitki türü değişmiştir", "Saat bozulmuştur", 0},
        {"Veri kaydı tutmak hangi avantajı sağlar?", "Eğilimleri görmeyi", "Suyu otomatik renklendirmeyi", "Kökleri kısaltmayı", 0},
        {"Tek bir yüksek EC ölçümü gördüğünde en doğru yaklaşım nedir?", "Tekrar ölçüp doğrulamak", "Hemen tüm suyu boşaltmak", "Fanı sökmek", 0},
        {"Ölçüm probunu temiz tutmak neden önemlidir?", "Daha güvenilir sonuç için", "Daha parlak ekran için", "Daha hızlı Wi-Fi için", 0},
    },
    {
        {"Yapraklar solgun, kökler de koyulaşmışsa hangi yaklaşım daha sistematiktir?", "Kök, su ve hava koşullarını birlikte incelemek", "Sadece ışığı artırmak", "Sadece adı değiştirmek", 0},
        {"Bitki yavaş büyüyor ama kökler sağlıklıysa sıradaki kontrol ne olabilir?", "Işık ve besin dengesi", "Rozet ekranı", "Wi-Fi adı", 0},
        {"EC yükseliyor, su seviyesi düşüyorsa olası açıklamalardan biri nedir?", "Su besinden daha hızlı azalıyor olabilir", "Saat geri kalmıştır", "Fan resim üretmiştir", 0},
        {"Bir ayarı değiştirdikten sonra sonucu hemen değil bir süre gözlemek neden gerekir?", "Bitkinin tepki vermesi zaman alır", "Ekran yavaş olduğu için", "NVS dolduğu için", 0},
        {"Aynı anda pH, EC ve ışığı değiştirmenin sakıncası nedir?", "Hangi değişikliğin etkili olduğu anlaşılmaz", "Pompa çalışmaz", "Wi-Fi kapanır", 0},
        {"Sorun çözmede en iyi sıra hangisidir?", "Gözlemle, ölç, tek değişken değiştir, tekrar gözle", "Her şeyi değiştir, bekleme", "Sadece tahmin et", 0},
    },
    {
        {"Bitkinin yeni yaprakları iyi, eski yaprakları zayıfsa neden kayıt tutmak faydalıdır?", "Değişimin yönünü görmeyi sağlar", "Kökleri büyütür", "Suyu azaltır", 0},
        {"EC aynı kalırken bitki kötüleşiyorsa ne çıkarımı yapılabilir?", "Sorun yalnızca EC olmayabilir", "EC kesinlikle hatalıdır", "Bitki su istemez", 0},
        {"İki bitki aynı sistemde farklı gelişiyorsa ne yapılmalı?", "Bitki bazlı gözlem yapılmalı", "İkisi de aynı kabul edilmeli", "Tüm sistemi kapatmalı", 0},
        {"Bir trendi tek bir ölçümden ayıran şey nedir?", "Zaman içinde tekrar eden değişim", "Daha büyük yazı", "Daha çok rozet", 0},
        {"Sağlıklı sistem yönetiminde hangi yaklaşım daha iyidir?", "Önleyici ve düzenli kontrol", "Sadece sorun çıkınca bakmak", "Hiç kayıt tutmamak", 0},
        {"Bitki gelişiminde karar verirken en güçlü kanıt hangisidir?", "Gözlem ve ölçümlerin birlikte değerlendirilmesi", "Tek bir tahmin", "Kart rengi", 0},
    },
    {
        {"Senaryo: Köklerde kötü koku, yapraklarda düşüş var. İlk öncelik nedir?", "Kök bölgesi ve su koşullarını kontrol etmek", "Daha çok besin eklemek", "Quiz oynamak", 0},
        {"Senaryo: EC beklenenden yüksek çıktı. Ölçümü doğruladın. Sonraki adım ne olmalı?", "Çözelti yoğunluğunu kontrollü düzeltmek", "Rastgele pH değiştirmek", "Fanı sürekli açmak", 0},
        {"Senaryo: pH hızla değişiyor. En doğru yaklaşım hangisi?", "Ölçüm, su ve eklenen maddeleri sistematik kontrol etmek", "Her şeyi bir anda değiştirmek", "Bitki adını silmek", 0},
        {"Senaryo: Bir bitki zayıf, diğerleri sağlıklı. Ne düşünmek mantıklıdır?", "O yuvaya özgü kök/bitki sorunu olabilir", "Tüm sistem kesin bozuktur", "Saat yanlıştır", 0},
        {"Senaryo: Sistem seçilen uyku saatinde. Otomatik davranış hangisidir?", "Pompa ve fan uyku modunda kapalıdır", "İkisi zorunlu açık kalır", "Wi-Fi silinir", 0},
        {"Bir hidroponik sorunu güvenli biçimde çözerken temel ilke nedir?", "Önce doğrula, sonra kontrollü değiştir", "Tahminle büyük değişiklik yap", "Ölçümleri görmezden gel", 0},
    },
    {
        {"pH hedefteyken EC sürekli yükseliyor. En olası ilk açıklama nedir?", "Su besinden hızlı eksiliyor", "RTC pili doluyor", "Işık rengi değişiyor", 0},
        {"EC düşerken su seviyesi de düşüyorsa ne düşünülebilir?", "Bitki besini sudan hızlı alıyor olabilir", "Ölçüm kesin bozuk", "Pompa saati hızlanmıştır", 0},
        {"Günlük pH grafiğinde yavaş bir kayma görmek ne sağlar?", "Sorunu erken fark etmeyi", "Bitkiyi otomatik hasat etmeyi", "Wi-Fi'yi güçlendirmeyi", 0},
        {"Bir ölçüm trendini değerlendirirken hangisi önemlidir?", "Aynı koşullarda düzenli ölçmek", "Her seferinde farklı cihaz kullanmak", "Sadece en yüksek değeri saklamak", 0},
        {"pH uygun, EC uygun fakat yapraklar sarkık. Sıradaki kontrol nedir?", "Kök, su sıcaklığı ve oksijen", "Quiz puanı", "Ekran parlaklığı", 0},
        {"Çözeltide büyük düzeltme yerine küçük adımların yararı nedir?", "Bitkinin tepkisini güvenle izlemek", "RTC pilini şarj etmek", "Hasadı geciktirmek", 0},
    },
    {
        {"Su sıcaklığı yükseldikçe çözünmüş oksijen genelde nasıl etkilenir?", "Azalma eğilimi gösterir", "Sınırsız artar", "Hiç değişmez", 0},
        {"Kök bölgesine hava verilmesinin temel amacı nedir?", "Oksijen desteği sağlamak", "pH'ı doğrudan 7 yapmak", "Yaprakları kurutmak", 0},
        {"Gece modunda motorların durması hangi bilgiye dayanır?", "RTC saatine", "Quiz skoruna", "Bitki adına", 0},
        {"Sıcak su ve koyulaşan kökler birlikte görülürse ilk öncelik nedir?", "Sıcaklık ve havalandırmayı kontrol etmek", "EC'yi rastgele artırmak", "Wi-Fi'yi kapatmak", 0},
        {"Hava taşı kabarcık üretmiyorsa önce ne kontrol edilir?", "Pompa, hortum ve bağlantılar", "Ekran teması", "Hasat hafızası", 0},
        {"Köklerin tamamını ışığa maruz bırakmamak neden önemlidir?", "Alg ve kök stresi riskini azaltır", "Saati düzeltir", "EC sensörünü şarj eder", 0},
    },
    {
        {"Alt yapraklarda sararma var, yeni yapraklar iyi. En iyi yaklaşım nedir?", "Kaydet, besin dengesini ve trendi incele", "Tüm yaprakları kes", "Sistemi sıfırla", 0},
        {"Yeni yapraklarda bozulma görülmesi neden dikkat çekicidir?", "Yeni büyümeyi etkileyen koşulları gösterebilir", "Sadece yaşlılığı gösterir", "RTC pilini gösterir", 0},
        {"Yaprak lekesinin yayılıp yayılmadığını anlamanın yolu nedir?", "Tarihli gözlem ve karşılaştırma", "Bir kez bakıp unutmak", "Bitki adını değiştirmek", 0},
        {"Kökler sağlıklı ama bir yaprak zarar görmüşse ne yapılmalı?", "Bitkiyi bütün olarak izlemek", "Tüm sistemi boşaltmak", "Pompayı sökmek", 0},
        {"Aynı belirtinin birden çok nedeni olabileceğinde ne yapılır?", "Ölçümlerle olasılıkları elemek", "İlk tahmine göre her şeyi değiştirmek", "Veriyi silmek", 0},
        {"Yaprak gözleminde en güçlü kanıt hangisidir?", "Görsel değişim ile ölçüm trendinin uyumu", "Kartın rengi", "Astronotun konumu", 0},
    },
    {
        {"Tohum aşamasında en önemli başlangıç koşullarından biri nedir?", "Uygun nem ve düzenli gözlem", "Yüksek EC ile başlamak", "Sürekli karanlık", 0},
        {"Filiz aşamasında ani ve yoğun besin neden risklidir?", "Hassas kökleri strese sokabilir", "Ekranı yavaşlatır", "RTC'yi durdurur", 0},
        {"Bitki aşamasına geçişi değerlendirirken neye bakılır?", "Kök ve yaprak gelişimine", "Sadece gün sayısına", "Wi-Fi adına", 0},
        {"Hasat zamanını belirlemek için en iyi yaklaşım hangisidir?", "Tür, gelişim ve sağlık durumunu birlikte değerlendirmek", "İlk yaprakta hasat etmek", "Sadece puana bakmak", 0},
        {"Hasattan sonra sistemi temizlemek neden önemlidir?", "Yeni döngüde sorun riskini azaltır", "Astronotu büyütür", "Saati hızlandırır", 0},
        {"Tohumdan hasada kayıt tutmak ne kazandırır?", "Sonraki yetiştirmeyi iyileştiren deneyim", "Daha fazla ekran çözünürlüğü", "Otomatik internet", 0},
    },
    {
        {"Usta senaryo: pH düşüyor, EC yükseliyor. İlk adım nedir?", "İki ölçümü doğrulayıp su seviyesini kontrol etmek", "Rastgele iki katkı eklemek", "Kayıtları silmek", 0},
        {"Usta senaryo: Bir yuva geri kalıyor, diğerleri iyi. Odak nerede olmalı?", "Yuvanın kök ve akış koşullarında", "Tüm sistemin saatinde", "Quiz bölümünde", 0},
        {"Usta senaryo: Düzeltmeden sonra değerler hedefe geldi. Ne yaparsın?", "Kaydedip bitki tepkisini izlerim", "Hemen tekrar değiştiririm", "Ölçümü bırakırım", 0},
        {"Usta senaryo: Sensör değeri fiziksel gözlemle çelişiyor. Ne yapılır?", "Sensör temizlenip kalibrasyon doğrulanır", "Gözlem yok sayılır", "Bitki hemen atılır", 0},
        {"İyi bir hidroponik rutin hangi döngüyü izler?", "Gözlem, ölçüm, kayıt, kontrollü karar", "Tahmin, büyük değişim, unutma", "Sadece otomasyon", 0},
        {"15 bölümün ortak mesajı hangisidir?", "Önce doğrula, sonra küçük ve izlenebilir adım at", "Her sorunda daha çok besin ekle", "Tek ölçüm her şeyi açıklar", 0},
    },
};
#endif

/* Bits 0-1 belonged to the retired Root Doctor / Leaf Detective. Keep them
 * reserved so old NVS data cannot pre-complete the new adventures. */
#define ACTIVITY_SEED       (1U << 2)
#define ACTIVITY_ARCADE     (1U << 3)
#define ACTIVITY_LANDING    (1U << 4)
#define ACTIVITY_WATER_PATH (1U << 5)
#define ACTIVITY_ROOT_MAZE  (1U << 6)
#define ACTIVITY_BUG_ESCAPE (1U << 7)

static const lv_font_t *ui_font(const lv_font_t *font)
{
    if (font == &lv_font_montserrat_32 || font == &hidrolab_font_tr_32) {
        return &hidrolab_font_tr_32;
    }
    return &hidrolab_font_tr_14;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       uint32_t color, const lv_font_t *font)
{
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, text);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(o, ui_font(font), 0);
    return o;
}

static lv_obj_t *centered_label(lv_obj_t *parent, const char *text, int x, int y,
                                int w, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *o = label(parent, text, x, y, color, font);
    lv_obj_set_width(o, w);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    return o;
}

/* LVGL invalidates a label even when it is assigned the same text.  The clock,
 * Wi-Fi and motor status are polled frequently, so avoiding no-op writes keeps
 * the RGB panel idle and prevents the visible half-second shimmer. */
static bool label_text_changed(lv_obj_t *object, const char *text)
{
    if (!object || !text) return false;
    const char *current = lv_label_get_text(object);
    if (current && strcmp(current, text) == 0) return false;
    lv_label_set_text(object, text);
    return true;
}

static void label_status_changed(lv_obj_t *object, const char *text, uint32_t color)
{
    if (label_text_changed(object, text)) {
        lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    }
}

static void panel_style(lv_obj_t *o, uint32_t bg, int radius)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    /* Light forms and dark cards share spacing, but need different outlines. */
    unsigned luminance = (((bg >> 16) & 255U) * 3U +
                          ((bg >> 8) & 255U) * 6U + (bg & 255U)) / 10U;
    lv_obj_set_style_border_color(o,
        lv_color_hex(luminance > 160U ? C_LINE : C_BORDER), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    panel_style(o, bg, 16);
    return o;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y,
                        int w, int h, uint32_t bg)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    /* Tactile feedback without a full-screen animation or extra draw buffers. */
    lv_obj_set_style_translate_y(b, 2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, (lv_opa_t)220, LV_STATE_PRESSED);
    lv_obj_t *t = lv_label_create(b);
    lv_label_set_text(t, text);
    lv_obj_set_style_text_font(t, &hidrolab_font_tr_14, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(t);
    return b;
}

static lv_obj_t *image(lv_obj_t *parent, const lv_image_dsc_t *src, int x, int y)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_set_pos(img, x, y);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return img;
}

/* Each profile now has its own finished transparent artwork. Keeping character
 * details in the asset avoids brittle LVGL overlays and preserves silhouettes. */
static lv_obj_t *profile_art(lv_obj_t *parent, hidrolab_astronaut_profile_t profile,
                             int x, int y, int scale)
{
    lv_obj_t *img = image(parent, hidrolab_astronaut_image((uint8_t)profile), x, y);
    /* Native 120 px drawing is much cheaper than transforming RGB565A8. The
     * 240/256 card scale difference is visually negligible, so avoid it. */
    if (scale < 224 || scale > 272) lv_image_set_scale(img, scale);
    return img;
}

static void anim_translate_y(void *object, int32_t value)
{
    lv_obj_set_style_translate_y((lv_obj_t *)object, value, 0);
}

/* First-person flight: launch, acceleration, cruise and soft arrival.
 * Fixed object count, no per-frame allocation, no flashing screen overlay. */
#define BADGE_WARP_STAR_COUNT 32
static lv_point_precise_t s_warp_segments[BADGE_WARP_STAR_COUNT][2];
static const int16_t BADGE_WARP_END_X[24] = {
    8, 92, 184, 310, 490, 620, 718, 792,
    18, 116, 226, 336, 464, 574, 684, 782,
    10, 104, 198, 322, 478, 602, 706, 790,
};
static const int16_t BADGE_WARP_END_Y[24] = {
    22, 10, 48, 6, 18, 52, 14, 38,
    188, 150, 116, 84, 92, 124, 164, 202,
    448, 468, 426, 474, 456, 422, 470, 440,
};

static void badge_warp_anim_exec(void *object, int32_t value)
{
    lv_obj_t *field = (lv_obj_t *)object;
    /* Bound invalidation to 25 updates/s; the UI task also serves touch. */
    static int32_t last_frame = -1;
    if (value == 0) last_frame = -1;
    int32_t frame = value / 40;
    if (frame == last_frame && value != 1400) return;
    last_frame = frame;
    int32_t speed;
    int32_t travel;
    if (value < 400) {
        speed = 200 + value * 2;
        travel = value / 4 + value * value / 800;
    } else if (value < 1100) {
        speed = 1000;
        travel = 300 + (value - 400) * 2;
    } else {
        int32_t arrival = value - 1100;
        speed = 1000 - arrival * 800 / 300;
        travel = 1700 + arrival * 2 - arrival * arrival / 375;
    }
    if (s_badge_speed_bar) lv_bar_set_value(s_badge_speed_bar, speed, LV_ANIM_OFF);
    const char *phase = value < 400 ? "Kalkış" : value < 1100 ? "Yolculuk" : "Varış";
    if (s_badge_flight_status &&
        strcmp(lv_label_get_text(s_badge_flight_status), phase) != 0)
        lv_label_set_text(s_badge_flight_status, phase);
    for (int i = 0; i < BADGE_WARP_STAR_COUNT; ++i) {
        lv_obj_t *star = lv_obj_get_child(field, i);
        if (!star) break;
        int32_t dx = BADGE_WARP_END_X[i % 24] - 400 + (i >= 24 ? 31 : 0);
        int32_t dy = BADGE_WARP_END_Y[i % 24] - 218 - (i >= 24 ? 23 : 0);
        int32_t phase = (travel + i * 137) % 1000;
        int32_t distance = 150000 / (1050 - phase);
        int32_t tail = distance - (12 + speed * distance / 2200);
        if (tail < 15) tail = 15;
        s_warp_segments[i][0] = (lv_point_precise_t){400 + dx * tail / 1000,
                                                       218 + dy * tail / 1000};
        s_warp_segments[i][1] = (lv_point_precise_t){400 + dx * distance / 1000,
                                                       218 + dy * distance / 1000};
        lv_line_set_points(star, s_warp_segments[i], 2);
    }
}

static void start_float_animation(lv_obj_t *object, int from, int to,
                                  uint32_t duration, uint32_t delay)
{
    if (!object) return;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, anim_translate_y);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_playback_duration(&animation, duration);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_delay(&animation, delay);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

static void anim_opacity(void *object, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void start_breathe_animation(lv_obj_t *object, lv_opa_t from,
                                    lv_opa_t to, uint32_t duration)
{
    if (!object) return;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, anim_opacity);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_playback_duration(&animation, duration);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

/* Foreground scene decoration. Unlike background_shape(), this deliberately
 * stays above the moon-base bitmap while remaining transparent to touches. */
static lv_obj_t *scene_decoration(lv_obj_t *parent, int x, int y, int w, int h,
                                  uint32_t color, int radius, lv_opa_t opa)
{
    lv_obj_t *shape = lv_obj_create(parent);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, w, h);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(shape, 0, 0);
    lv_obj_set_style_border_width(shape, 0, 0);
    lv_obj_set_style_radius(shape, radius, 0);
    lv_obj_set_style_bg_color(shape, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(shape, opa, 0);
    return shape;
}

static void set_bg_gradient(lv_obj_t *o, uint32_t from, uint32_t to)
{
    lv_obj_set_style_bg_color(o, lv_color_hex(from), 0);
    lv_obj_set_style_bg_grad_color(o, lv_color_hex(to), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

static void background_orb(lv_obj_t *parent, int x, int y, int size, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *orb = lv_obj_create(parent);
    lv_obj_set_pos(orb, x, y);
    lv_obj_set_size(orb, size, size);
    lv_obj_remove_flag(orb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(orb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(orb, 0, 0);
    lv_obj_set_style_pad_all(orb, 0, 0);
    lv_obj_set_style_bg_color(orb, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(orb, opa, 0);
    lv_obj_move_background(orb);
}

static lv_obj_t *background_shape(lv_obj_t *parent, int x, int y, int w, int h,
                                  uint32_t color, int radius, lv_opa_t opa)
{
    lv_obj_t *shape = lv_obj_create(parent);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, w, h);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(shape, 0, 0);
    lv_obj_set_style_border_width(shape, 0, 0);
    lv_obj_set_style_radius(shape, radius, 0);
    lv_obj_set_style_bg_color(shape, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(shape, opa, 0);
    lv_obj_move_background(shape);
    return shape;
}

static void space_star(lv_obj_t *parent, int x, int y, int size, uint32_t color)
{
    lv_obj_t *star = lv_obj_create(parent);
    lv_obj_set_pos(star, x, y);
    lv_obj_set_size(star, size, size);
    lv_obj_remove_flag(star, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(star, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(star, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(star, 0, 0);
    lv_obj_set_style_pad_all(star, 0, 0);
    lv_obj_set_style_bg_color(star, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(star, size >= 4 ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_move_background(star);
}

static void decorate_space_background(lv_obj_t *parent, int height)
{
    static const int16_t stars[][3] = {
        {54, 36, 3}, {226, 78, 2}, {414, 34, 3},
        {626, 104, 2}, {742, 52, 4}, {314, 348, 3},
    };

    /* Sparse stars leave room for the content on each page. */
    for (size_t i = 0; i < sizeof(stars) / sizeof(stars[0]); ++i) {
        if (stars[i][1] < height) {
            space_star(parent, stars[i][0], stars[i][1], stars[i][2],
                       stars[i][2] >= 4 ? 0xFFF2B6 : C_STAR);
        }
    }
}

static void decorate_page_background(lv_obj_t *parent)
{
    /* Static backgrounds only: no clipped children, opacity animations or
     * transformed layers. Content stays on opaque, readable surfaces. */
    static const uint32_t tops[4] = {0x0A2936, 0x142344, 0x171F44, 0x0D2842};
    static const uint32_t bottoms[4] = {0x174A52, 0x303B62, 0x303664, 0x1A4560};
    static const uint32_t accents[4] = {0x73E3B3, 0xF3C77B, 0xA6B6FF, 0x78DCEB};
    int page = (int)s_page;
    if (page < 0 || page > 3) page = 0;
    set_bg_gradient(parent, tops[page], bottoms[page]);
    background_orb(parent, 690, 335, 170, accents[page], LV_OPA_10);
    background_orb(parent, -98, 211, 160, 0x6BCDE4, LV_OPA_10);
    for (int i = 0; i < 12; ++i)
        space_star(parent, 18 + (i * 157) % 760, 94 + (i * 79) % 350,
                   i % 4 == 0 ? 3 : 2, 0x9EB9DE);
    background_shape(parent, 20, 79, 760, 1, C_BORDER, 0, LV_OPA_60);
    background_shape(parent, 20, 78, 74, 3, accents[page], 1, LV_OPA_COVER);
}

static lv_obj_t *create_stock_keyboard(lv_obj_t *parent, lv_obj_t *ta,
                                       int x, int y, int w, int h)
{
    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_keyboard_set_textarea(kb, ta);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_popovers(kb, false);
    lv_obj_set_style_text_font(kb, &hidrolab_font_tr_14, LV_PART_ITEMS);
    lv_obj_set_align(kb, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(kb, x, y);
    lv_obj_set_size(kb, w, h);
    lv_obj_set_style_pad_all(kb, 4, 0);
    lv_obj_set_style_pad_row(kb, 4, 0);
    lv_obj_set_style_pad_column(kb, 3, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x081724), 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x20394B), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(0xF1F7FC), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x34758D), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x304C60), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, lv_color_hex(0xF1F7FC), LV_PART_ITEMS | LV_STATE_CHECKED);
    if (ta) {
        lv_obj_set_style_bg_color(ta, lv_color_hex(0x102B3B), 0);
        lv_obj_set_style_text_color(ta, lv_color_hex(0xF1F7FC), 0);
        lv_obj_set_style_border_color(ta, lv_color_hex(0x5B879C), 0);
        lv_obj_set_style_text_color(ta, lv_color_hex(0xA8BFCD), LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_bg_color(ta, lv_color_hex(0xF1F7FC), LV_PART_CURSOR);
    }
    lv_obj_move_foreground(kb);
    return kb;
}

static int active_plant_count(void)
{
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    int count = 0;
    for (uint8_t j = 0; j < HIDROLAB_GARDEN_SLOT_COUNT; ++j) {
        uint8_t i = hidrolab_garden_slot_at(j);
        if (cfg->slots[i].active) count++;
    }
    return count;
}

static int bit_count32(uint32_t value)
{
    int count = 0;
    while (value) {
        count += value & 1U;
        value >>= 1;
    }
    return count;
}


static uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    if (!*s) return 0;
    uint32_t cp;
    if (s[0] < 0x80) {
        cp = s[0];
        *p += 1;
    } else if ((s[0] & 0xE0) == 0xC0 && s[1]) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *p += 2;
    } else if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12) |
             ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *p += 3;
    } else {
        cp = s[0];
        *p += 1;
    }
    return cp;
}

static uint32_t lower_tr_cp(uint32_t cp)
{
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp == 0x0130) return 'i';
    if (cp == 0x00D6) return 0x00F6;
    if (cp == 0x00DC) return 0x00FC;
    return cp;
}

static bool is_tr_vowel(uint32_t cp)
{
    cp = lower_tr_cp(cp);
    return cp == 'a' || cp == 'e' || cp == 'i' || cp == 'o' || cp == 'u' ||
           cp == 0x0131 || cp == 0x00F6 || cp == 0x00FC;
}

static const char *possessive_suffix(const char *name)
{
    static char suffix[8];
    uint32_t last_vowel = 'e';
    uint32_t last_cp = 0;
    const char *p = name;
    while (p && *p) {
        uint32_t cp = utf8_next(&p);
        last_cp = cp;
        if (is_tr_vowel(cp)) last_vowel = lower_tr_cp(cp);
    }

    const char *core = "in";
    if (last_vowel == 'a' || last_vowel == 0x0131) core = "ın";
    else if (last_vowel == 'o' || last_vowel == 'u') core = "un";
    else if (last_vowel == 0x00F6 || last_vowel == 0x00FC) core = "ün";

    if (is_tr_vowel(last_cp)) snprintf(suffix, sizeof(suffix), "n%s", core);
    else strlcpy(suffix, core, sizeof(suffix));
    return suffix;
}

static void garden_title(char *out, size_t size)
{
    const char *name = hidrolab_settings_get()->astronaut_name;
    if (!name[0] || strcmp(name, "Astronot") == 0) {
        strlcpy(out, "Astronot Bahçesi", size);
        return;
    }
    snprintf(out, size, "Astronot %s'%s Bahçesi", name, possessive_suffix(name));
}

static uint32_t current_daily_key(void)
{
    uint32_t key = hidrolab_time_date_key();
    return key ? key : (20000000U + hidrolab_settings_get()->season_day);
}

static bool load_moon_base_art(void)
{
    if (s_moon_base_pixels) return true;
    if (s_moon_base_load_attempted) return false;
    s_moon_base_load_attempted = true;

    const esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 3,
        .format_if_mount_failed = false,
    };
    esp_err_t mount_err = esp_vfs_spiffs_register(&conf);
    if (mount_err != ESP_OK && mount_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(UI_TAG, "moon base art unavailable: SPIFFS mount failed (%s)",
                 esp_err_to_name(mount_err));
        return false;
    }

    FILE *file = fopen("/spiffs/moonbase.bin", "rb");
    if (!file) {
        ESP_LOGW(UI_TAG, "moon base art unavailable: /spiffs/moonbase.bin missing");
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 || ftell(file) != (long)MOON_BASE_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        ESP_LOGW(UI_TAG, "moon base art has an unexpected size");
        fclose(file);
        return false;
    }

    uint8_t *pixels = heap_caps_malloc(MOON_BASE_BYTES,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        ESP_LOGW(UI_TAG, "moon base art skipped: PSRAM allocation failed");
        fclose(file);
        return false;
    }
    size_t read_size = fread(pixels, 1, MOON_BASE_BYTES, file);
    fclose(file);
    if (read_size != MOON_BASE_BYTES) {
        ESP_LOGW(UI_TAG, "moon base art read failed: %u/%u bytes",
                 (unsigned)read_size, (unsigned)MOON_BASE_BYTES);
        heap_caps_free(pixels);
        return false;
    }

    s_moon_base_pixels = pixels;
    s_moon_base_image = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = MOON_BASE_W,
            .h = MOON_BASE_H,
            .stride = MOON_BASE_W * 2,
            .reserved_2 = 0,
        },
        .data_size = MOON_BASE_BYTES,
        .data = s_moon_base_pixels,
        .reserved = NULL,
    };
    ESP_LOGI(UI_TAG, "living moon base art loaded into PSRAM (%u bytes)",
             (unsigned)MOON_BASE_BYTES);
    return true;
}

static bool load_badge_warp_art(void)
{
    if (s_badge_warp_pixels) return true;
    if (s_badge_warp_load_attempted) return false;
    s_badge_warp_load_attempted = true;

    const esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 3,
        .format_if_mount_failed = false,
    };
    esp_err_t mount_err = esp_vfs_spiffs_register(&conf);
    if (mount_err != ESP_OK && mount_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(UI_TAG, "badge warp unavailable: SPIFFS mount failed (%s)",
                 esp_err_to_name(mount_err));
        return false;
    }

    FILE *file = fopen("/spiffs/badgewp.bin", "rb");
    if (!file) {
        ESP_LOGW(UI_TAG, "badge warp unavailable: /spiffs/badgewp.bin missing");
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 || ftell(file) != (long)BADGE_WARP_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        ESP_LOGW(UI_TAG, "badge warp art has an unexpected size");
        fclose(file);
        return false;
    }

    uint8_t *pixels = heap_caps_malloc(BADGE_WARP_BYTES,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        ESP_LOGW(UI_TAG, "badge warp skipped: PSRAM allocation failed");
        fclose(file);
        return false;
    }
    size_t read_size = fread(pixels, 1, BADGE_WARP_BYTES, file);
    fclose(file);
    if (read_size != BADGE_WARP_BYTES) {
        ESP_LOGW(UI_TAG, "badge warp read failed: %u/%u bytes",
                 (unsigned)read_size, (unsigned)BADGE_WARP_BYTES);
        heap_caps_free(pixels);
        return false;
    }

    s_badge_warp_pixels = pixels;
    s_badge_warp_image = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = BADGE_WARP_W,
            .h = BADGE_WARP_H,
            .stride = BADGE_WARP_W * 2,
            .reserved_2 = 0,
        },
        .data_size = BADGE_WARP_BYTES,
        .data = s_badge_warp_pixels,
        .reserved = NULL,
    };
    ESP_LOGI(UI_TAG, "badge warp art loaded into PSRAM (%u bytes)",
             (unsigned)BADGE_WARP_BYTES);
    return true;
}

static void garden_base_event(lv_event_t *e)
{
    (void)e;
    if (s_sleep_screen || s_base_departing || s_page_render_pending ||
        s_page_render_retry || (s_page == PAGE_GARDEN && s_base_home)) return;
    s_base_return_origin = s_page;
    s_base_return_pending = true;
    s_base_home = true;
    s_page = PAGE_GARDEN;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    request_page_render();
}

static lv_obj_t *base_return_button(lv_obj_t *parent, int x, int y, int width)
{
    const char *caption = width < 120 ? LV_SYMBOL_HOME "  ÜS" :
                                       LV_SYMBOL_HOME "  Ay üssü";
    lv_obj_t *home = button(parent, caption, x, y, width, 38, C_SURFACE_ALT);
    lv_obj_set_style_border_width(home, 1, 0);
    lv_obj_set_style_border_color(home, lv_color_hex(C_BORDER), 0);
    lv_obj_add_event_cb(home, garden_base_event, LV_EVENT_CLICKED, NULL);
    return home;
}

static lv_obj_t *scene_hotspot(lv_obj_t *parent, int x, int y, int width,
                               int height, uint32_t accent)
{
    lv_obj_t *hotspot = lv_button_create(parent);
    lv_obj_set_pos(hotspot, x, y);
    lv_obj_set_size(hotspot, width, height);
    lv_obj_set_style_pad_all(hotspot, 0, 0);
    lv_obj_set_style_radius(hotspot, 24, 0);
    lv_obj_set_style_bg_opa(hotspot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hotspot, 0, 0);
    lv_obj_set_style_shadow_width(hotspot, 0, 0);
    lv_obj_set_style_bg_color(hotspot, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(hotspot, LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(hotspot, 1, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(hotspot, lv_color_hex(accent), LV_STATE_PRESSED);
    return hotspot;
}

/* One position animation controls x, y, walking bob and perspective scale.
 * Keeping these in a single LVGL animation prevents competing completion
 * callbacks from deleting the astronaut while another property still runs. */
static void base_walk_anim_exec(void *object, int32_t progress)
{
    lv_obj_t *astronaut = (lv_obj_t *)object;
    if (!s_base_departing || astronaut != s_base_astronaut) return;
    if (progress < 0) progress = 0;
    if (progress > 1000) progress = 1000;
    int route_progress = s_base_returning ? 1000 - progress : progress;
    const lv_point_t *route = s_base_routes[s_base_destination];
    int segment = route_progress == 1000 ? 2 : route_progress * 3 / 1000;
    int fraction = route_progress * 3 - segment * 1000;
    int x = route[segment].x +
            (route[segment + 1].x - route[segment].x) * fraction / 1000;
    int y = route[segment].y +
            (route[segment + 1].y - route[segment].y) * fraction / 1000;
    int step = (progress * 4) % 1000;
    int bob = (step < 500 ? step : 1000 - step) / 100;
    int end_scale = (s_base_destination == PAGE_GARDEN ||
                     s_base_destination == PAGE_TASKS) ? 256 : 320;
    /* All three profile assets are 120x120. A bottom pivot keeps the feet on
     * the route even while the astronaut recedes into the scene. */
    lv_image_set_scale(astronaut, BASE_WALK_SCALE +
                       (end_scale - BASE_WALK_SCALE) * route_progress / 1000);
    lv_obj_set_pos(astronaut, x - BASE_FOOT_X, y - BASE_FOOT_Y - bob);
    if (s_base_shadow) lv_obj_set_pos(s_base_shadow, x - 44, y - 4);
}

static void badge_board_anim_exec(void *object, int32_t progress)
{
    lv_obj_t *astronaut = (lv_obj_t *)object;
    if (!s_badge_boarding || astronaut != s_base_astronaut) return;
    if (progress < 0) progress = 0;
    if (progress > 1000) progress = 1000;
    int32_t eased = lv_map(progress, 0, 1000, 0, 1000);
    if (s_badge_travel_returning) eased = 1000 - eased;
    /* Walk to the ramp base, then ascend toward the relocated shuttle door. */
    int x = eased < 500 ? 400 : 400 - (90 * (eased - 500) / 500);
    int y = eased < 500 ? 305 - (107 * eased / 500) :
                          198 - (90 * (eased - 500) / 500);
    int step = (progress * 5) % 1000;
    int bob = (step < 500 ? step : 1000 - step) / 110;
    int scale = BASE_WALK_SCALE - (170 * eased / 1000);
    lv_image_set_scale(astronaut, scale);
    lv_obj_set_style_opa(astronaut, eased > 850 ?
        LV_OPA_COVER * (1000 - eased) / 150 : LV_OPA_COVER, 0);
    lv_obj_set_pos(astronaut, x - BASE_FOOT_X, y - BASE_FOOT_Y - bob);
    if (s_base_shadow) {
        lv_obj_set_pos(s_base_shadow, x - 44, y - 4);
        lv_obj_set_style_opa(s_base_shadow,
                             LV_OPA_40 - (LV_OPA_40 * eased / 1000), 0);
    }
}

static void badge_board_anim_completed(lv_anim_t *animation)
{
    if (!s_badge_boarding || !s_base_astronaut ||
        (animation && animation->var != s_base_astronaut)) return;
    badge_board_anim_exec(s_base_astronaut, 1000);
    if (s_badge_travel_returning) {
        s_badge_travel_returning = false;
        s_badge_boarding = false;
        s_base_departing = false;
        if (s_base_companion_ui)
            lv_obj_remove_flag(s_base_companion_ui, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_base_astronaut, LV_OBJ_FLAG_HIDDEN);
    if (s_base_shadow) lv_obj_add_flag(s_base_shadow, LV_OBJ_FLAG_HIDDEN);
    s_badge_boarding = false;
    s_base_departing = false;
    badge_travel_start();
}

static void cancel_base_walk(void)
{
    if (s_base_astronaut) {
        lv_anim_delete(s_base_astronaut, base_walk_anim_exec);
        lv_anim_delete(s_base_astronaut, badge_board_anim_exec);
    }
    s_base_astronaut = NULL;
    s_base_shadow = NULL;
    s_base_companion_ui = NULL;
    s_base_departing = false;
    s_base_returning = false;
    s_badge_boarding = false;
}

static void base_walk_anim_completed(lv_anim_t *animation)
{
    if (!s_base_departing || !s_base_astronaut || s_sleep_screen ||
        (animation && animation->var != s_base_astronaut)) return;
    if (s_base_returning) {
        /* The hub is already rendered. Reveal its guide in place on arrival,
         * avoiding another full-screen rebuild after the reverse walk. */
        base_walk_anim_exec(s_base_astronaut, 1000);
        s_base_returning = false;
        s_base_departing = false;
        if (s_base_companion_ui)
            lv_obj_remove_flag(s_base_companion_ui, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    page_t destination = s_base_destination;
    /* Keep the tap lock until the deferred render has replaced the scene. */
    s_page = destination;
    if (destination == PAGE_GARDEN) {
        s_base_home = false;
    } else if (destination == PAGE_TASKS) {
        s_tasks_badges_view = false;
        s_tasks_academy_view = false;
        s_academy_category = -1;
        s_academy_lesson = -1;
        s_academy_step = 0;
        s_academy_done_mask = 0;
        s_academy_choice = -1;
        s_academy_question_visible = false;
    } else if (destination == PAGE_GAMES) {
        s_game_view = GAME_HOME;
        s_game_guide_visible = false;
    }
    request_page_render();
}

static void start_base_walk(page_t destination, bool returning)
{
    if (!s_base_astronaut || destination < PAGE_GARDEN ||
        destination > PAGE_SETTINGS) return;
    s_base_destination = destination;
    s_base_departing = true;
    s_base_returning = returning;
    /* A short tap may occur entirely between the 500ms input samples. */
    s_night_awake_until_us = esp_timer_get_time() + 60000000LL;

    if (s_base_companion_ui) lv_obj_add_flag(s_base_companion_ui, LV_OBJ_FLAG_HIDDEN);
    /* Place the first frame at the doorway before the hub can be painted. */
    base_walk_anim_exec(s_base_astronaut, 0);

    lv_anim_t walk;
    lv_anim_init(&walk);
    lv_anim_set_var(&walk, s_base_astronaut);
    lv_anim_set_exec_cb(&walk, base_walk_anim_exec);
    lv_anim_set_values(&walk, 0, 1000);
    lv_anim_set_duration(&walk, 760);
    lv_anim_set_path_cb(&walk, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&walk, base_walk_anim_completed);
    if (!lv_anim_start(&walk)) {
        base_walk_anim_exec(s_base_astronaut, 1000);
        base_walk_anim_completed(NULL);
    }
}

static void base_depart_event(lv_event_t *e)
{
    if (s_base_departing || !s_base_astronaut || s_sleep_screen ||
        s_page_render_pending || s_page_render_retry) return;
    page_t destination = (page_t)(uintptr_t)lv_event_get_user_data(e);
    if (destination < PAGE_GARDEN || destination > PAGE_SETTINGS) return;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    start_base_walk(destination, false);
}

static void start_pending_base_return(void)
{
    if (!s_base_return_pending) return;
    page_t origin = s_base_return_origin;
    s_base_return_pending = false;
    start_base_walk(origin, true);
}

#if 0 /* Retired: a forced tour interrupted the child's natural flow. */
#define BASE_TOUR_STEPS 6

static const page_t s_base_tour_rooms[4] = {
    PAGE_GARDEN, PAGE_TASKS, PAGE_GAMES, PAGE_SETTINGS,
};

static void base_tour_foot(uint8_t step, int *x, int *y, int *scale)
{
    if (step >= 1 && step <= 4) {
        page_t room = s_base_tour_rooms[step - 1];
        *x = s_base_routes[room][3].x;
        *y = s_base_routes[room][3].y;
        *scale = (room == PAGE_GARDEN || room == PAGE_TASKS) ? 256 : 320;
    } else {
        *x = 400;
        *y = 250;
        *scale = BASE_WALK_SCALE;
    }
}

static void base_tour_place_astronaut(uint8_t step)
{
    if (!s_base_astronaut) return;
    int x = 400;
    int y = 250;
    int scale = BASE_WALK_SCALE;
    base_tour_foot(step, &x, &y, &scale);
    lv_image_set_scale(s_base_astronaut, (uint32_t)scale);
    lv_obj_set_pos(s_base_astronaut, x - BASE_FOOT_X, y - BASE_FOOT_Y);
    if (s_base_shadow) lv_obj_set_pos(s_base_shadow, x - 44, y - 4);
}

static void base_tour_walk_anim_exec(void *object, int32_t progress)
{
    lv_obj_t *astronaut = (lv_obj_t *)object;
    if (!s_base_tour_active || !s_base_tour_moving ||
        astronaut != s_base_astronaut) return;
    if (progress < 0) progress = 0;
    if (progress > 1000) progress = 1000;
    int x = s_base_tour_from_x +
            (s_base_tour_to_x - s_base_tour_from_x) * progress / 1000;
    int y = s_base_tour_from_y +
            (s_base_tour_to_y - s_base_tour_from_y) * progress / 1000;
    int scale = s_base_tour_from_scale +
                (s_base_tour_to_scale - s_base_tour_from_scale) * progress / 1000;
    int stride = (progress * 4) % 1000;
    int bob = (stride < 500 ? stride : 1000 - stride) / 110;
    lv_image_set_scale(astronaut, (uint32_t)scale);
    lv_obj_set_pos(astronaut, x - BASE_FOOT_X, y - BASE_FOOT_Y - bob);
    if (s_base_shadow) lv_obj_set_pos(s_base_shadow, x - 44, y - 4);
}

static void base_tour_walk_completed(lv_anim_t *animation)
{
    if (!s_base_tour_active || !s_base_tour_moving ||
        !s_base_astronaut ||
        (animation && animation->var != s_base_astronaut)) return;
    base_tour_walk_anim_exec(s_base_astronaut, 1000);
    s_base_tour_moving = false;
    if (s_base_tour_step + 1 < BASE_TOUR_STEPS) s_base_tour_step++;
    request_page_render();
}

static void base_tour_finish_event(lv_event_t *e)
{
    (void)e;
    if (!s_base_tour_active || s_base_tour_moving) return;
    s_base_tour_active = false;
    if (s_base_tour_layer) lv_obj_add_flag(s_base_tour_layer,
                                           LV_OBJ_FLAG_HIDDEN);
    s_base_tour_layer = NULL;
    (void)hidrolab_settings_set_base_guide_done(true);
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
    /* Finish with a real action instead of leaving the child at the hub. */
    start_base_walk(PAGE_GARDEN, false);
}

static void base_tour_next_event(lv_event_t *e)
{
    (void)e;
    if (!s_base_tour_active || s_base_tour_moving || !s_base_astronaut) return;
    if (s_base_tour_step + 1 >= BASE_TOUR_STEPS) {
        base_tour_finish_event(e);
        return;
    }
    base_tour_foot(s_base_tour_step, &s_base_tour_from_x,
                   &s_base_tour_from_y, &s_base_tour_from_scale);
    base_tour_foot((uint8_t)(s_base_tour_step + 1), &s_base_tour_to_x,
                   &s_base_tour_to_y, &s_base_tour_to_scale);
    s_base_tour_moving = true;
    s_night_awake_until_us = esp_timer_get_time() + 60000000LL;
    if (s_base_tour_layer) lv_obj_add_flag(s_base_tour_layer, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t walk;
    lv_anim_init(&walk);
    lv_anim_set_var(&walk, s_base_astronaut);
    lv_anim_set_exec_cb(&walk, base_tour_walk_anim_exec);
    lv_anim_set_values(&walk, 0, 1000);
    lv_anim_set_duration(&walk, 640);
    lv_anim_set_path_cb(&walk, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&walk, base_tour_walk_completed);
    if (!lv_anim_start(&walk)) base_tour_walk_completed(NULL);
}

static void build_base_tour(void)
{
    static const char *titles[BASE_TOUR_STEPS] = {
        "MERHABA, BEN REHBERİNİM", "BAHÇE", "GÖREVLER",
        "OYUNLAR", "AYARLAR", "HAZIRSIN!",
    };
    static const char *bodies[BASE_TOUR_STEPS] = {
        "Dört bölümü birlikte gezeceğiz. Devam'a dokun!",
        "Bir yuvaya dokun; bitkine isim ver ve gelişimini izle.",
        "pH ve EC ölçümünü kaydet; günlük bakımını burada gör.",
        "Öğrendiklerini oyunlarla dene ve yıldız topla.",
        "Pompa, fan, Wi-Fi ve uyku saatleri burada.",
        "Şimdi Bahçe'ye gidip ilk bitkimizi ekleyelim.",
    };
    static const uint32_t accents[BASE_TOUR_STEPS] = {
        0x55C8FF, 0x58D890, 0xF8C86A, 0xE95C53, 0xA987FF, 0x58D890,
    };
    static const int highlights[4][4] = {
        {14, 12, 236, 174}, {550, 10, 244, 176},
        {556, 222, 244, 180}, {0, 224, 244, 180},
    };
    if (!s_base_tour_active || s_base_tour_step >= BASE_TOUR_STEPS) return;

    base_tour_place_astronaut(s_base_tour_step);
    lv_obj_remove_flag(s_base_astronaut, LV_OBJ_FLAG_CLICKABLE);
    if (s_base_companion_ui) lv_obj_add_flag(s_base_companion_ui,
                                              LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *layer = lv_obj_create(s_content);
    s_base_tour_layer = layer;
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_set_size(layer, 800, CONTENT_H_FULL);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(layer, 0, 0);
    lv_obj_set_style_radius(layer, 0, 0);
    lv_obj_set_style_border_width(layer, 0, 0);
    lv_obj_set_style_bg_color(layer, lv_color_hex(0x020A14), 0);
    lv_obj_set_style_bg_opa(layer, LV_OPA_40, 0);

    uint32_t accent = accents[s_base_tour_step];
    lv_obj_t *focus;
    if (s_base_tour_step >= 1 && s_base_tour_step <= 4) {
        const int *box = highlights[s_base_tour_step - 1];
        focus = panel(layer, box[0], box[1], box[2], box[3], 0x071522);
        lv_obj_set_style_bg_opa(focus, LV_OPA_10, 0);
        lv_obj_add_flag(focus, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(focus, base_tour_next_event, LV_EVENT_CLICKED,
                            NULL);
        lv_obj_set_style_translate_y(focus, 3, LV_STATE_PRESSED);
    } else {
        focus = panel(layer, 334, 116, 132, 146, 0x071522);
        lv_obj_set_style_bg_opa(focus, LV_OPA_10, 0);
        lv_obj_add_flag(focus, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(focus,
                            s_base_tour_step + 1 < BASE_TOUR_STEPS ?
                            base_tour_next_event : base_tour_finish_event,
                            LV_EVENT_CLICKED, NULL);
    }
    lv_obj_set_style_border_width(focus, 4, 0);
    lv_obj_set_style_border_color(focus, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_width(focus, 18, 0);
    lv_obj_set_style_shadow_color(focus, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_opa(focus, LV_OPA_50, 0);
    start_breathe_animation(focus, LV_OPA_70, LV_OPA_COVER, 850);

    int card_y = s_base_tour_step <= 2 ? 306 :
                 (s_base_tour_step <= 4 ? 12 : 306);
    lv_obj_t *card = panel(layer, 194, card_y, 412, 116, 0x071A2D);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, s_base_tour_step + 1 < BASE_TOUR_STEPS ?
                        base_tour_next_event : base_tour_finish_event,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_translate_y(card, 3, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(accent), 0);
    centered_label(card, titles[s_base_tour_step], 12, 10, 388, accent,
                   &hidrolab_font_tr_14);
    lv_obj_t *body = centered_label(card, bodies[s_base_tour_step],
                                     12, 36, 388, C_SPACE_TEXT,
                                     &hidrolab_font_tr_14);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    centered_label(card,
                   s_base_tour_step + 1 < BASE_TOUR_STEPS ?
                   "DEVAM  >" : "BAHÇEYE GİT  >",
                   12, 88, 388, accent, &hidrolab_font_tr_14);

    lv_obj_move_foreground(s_base_shadow);
    lv_obj_move_foreground(s_base_astronaut);
}
#endif

static void build_moon_base(void)
{
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    uint32_t today = current_daily_key();
    bool chemistry_today = cfg->chemistry_date == today;
    int plants = active_plant_count();
    int observable_plants = 0;
    int seed_plants = 0;
    bool garden_observation_pending = false;
    bool garden_concern = false;
    for (uint8_t j = 0; j < HIDROLAB_GARDEN_SLOT_COUNT; ++j) {
        uint8_t i = hidrolab_garden_slot_at(j);
        if (!cfg->slots[i].active) continue;
        if (cfg->slots[i].growth_stage == HIDROLAB_STAGE_SEED) {
            seed_plants++;
            continue;
        }
        observable_plants++;
        const hidrolab_slot_observation_t *entry = &cfg->slot_observations[i];
        if (entry->date_key == today &&
            (entry->leaf >= 3 || entry->root >= 3)) {
            garden_concern = true;
        }
        if (entry->date_key != today || !entry->leaf || !entry->root) {
            garden_observation_pending = true;
        }
    }
    if (load_moon_base_art()) {
        lv_obj_t *background = image(s_content, &s_moon_base_image, 0, 0);
        lv_obj_move_background(background);
    } else {
        decorate_page_background(s_content);
        background_shape(s_content, -30, 352, 860, 104, 0x52627B,
                         44, LV_OPA_50);
    }

    /* Hit areas follow the four buildings and the parked Badge Galaxy shuttle,
     * never the empty roads or decorative terrain. */
    lv_obj_t *greenhouse = scene_hotspot(s_content, 7, 83, 251, 143, 0x58D890);
    lv_obj_add_event_cb(greenhouse, base_depart_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)PAGE_GARDEN);

    lv_obj_t *settings = scene_hotspot(s_content, 0, 242, 252, 175, 0xA987FF);
    lv_obj_add_event_cb(settings, base_depart_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)PAGE_SETTINGS);

    lv_obj_t *mission = scene_hotspot(s_content, 544, 83, 255, 143, 0xF8C86A);
    lv_obj_add_event_cb(mission, base_depart_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)PAGE_TASKS);

    lv_obj_t *arcade = scene_hotspot(s_content, 544, 245, 255, 176, 0xE34F45);
    lv_obj_add_event_cb(arcade, base_depart_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)PAGE_GAMES);

    lv_obj_t *badge_shuttle = scene_hotspot(s_content, 276, 45, 245, 160,
                                             0xFFD76A);
    lv_obj_add_event_cb(badge_shuttle, badge_portal_event,
                        LV_EVENT_CLICKED, NULL);

    /* Destination names are integrated into the approved architectural art. */

    /* Guidance is carried by the astronaut's dialogue, without destination dots. */
    /* The astronaut is the contextual guide for the existing four rooms.
     * It never creates a duplicate route: a tap reuses the same room walk. */
    s_base_shadow = background_shape(s_content, 375, 272, 50, 8,
                                     0x04101F, LV_RADIUS_CIRCLE, LV_OPA_40);
    s_base_astronaut = profile_art(s_content, cfg->astronaut_profile,
                                   400 - BASE_FOOT_X, 276 - BASE_FOOT_Y,
                                   BASE_WALK_SCALE);
    lv_image_set_pivot(s_base_astronaut, BASE_FOOT_X, BASE_FOOT_Y);
    lv_obj_t *command = lv_obj_create(s_content);
    lv_obj_set_pos(command, 216, 62);
    lv_obj_set_size(command, 368, 380);
    lv_obj_remove_flag(command, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(command, 0, 0);
    lv_obj_set_style_radius(command, 0, 0);
    lv_obj_set_style_border_width(command, 0, 0);
    lv_obj_set_style_bg_opa(command, LV_OPA_TRANSP, 0);
    s_base_companion_ui = command;

    const char *speech;
    page_t guide_destination;
    if (plants == 0) {
        speech = "İlk bitkini Ay Bahçesi'ne ekle.";
        guide_destination = PAGE_GARDEN;
    } else if (!chemistry_today) {
        speech = "Bugünün pH ve EC kaydını ekle.";
        guide_destination = PAGE_TASKS;
    } else if (garden_concern) {
        speech = "Bitkinin günlüğüne bakalım.";
        guide_destination = PAGE_GARDEN;
    } else if (observable_plants == 0 && seed_plants > 0) {
        speech = "Filizlenince gözlem yapacağız.";
        guide_destination = PAGE_GAMES;
    } else if (garden_observation_pending) {
        speech = "Bitkilerinin gözlemlerini kaydet.";
        guide_destination = PAGE_GARDEN;
    } else {
        speech = "Bugünlük tamam. Oyun zamanı!";
        guide_destination = PAGE_GAMES;
    }

    lv_obj_add_flag(s_base_astronaut, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_base_astronaut, base_depart_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)guide_destination);
    lv_obj_set_style_translate_y(s_base_astronaut, 3, LV_STATE_PRESSED);

    /* Quiet one-line action strip. No speech tail, glow, translucent layer or
     * animated decoration; keep a full 44px touch target under the astronaut. */
    lv_obj_t *bubble = panel(command, 34, 290, 300, 44, 0x102538);
    lv_obj_add_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bubble, base_depart_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)guide_destination);
    lv_obj_set_style_translate_y(bubble, 2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(0x203D50), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(bubble, 1, 0);
    lv_obj_set_style_border_color(bubble, lv_color_hex(0x42647A), 0);
    lv_obj_set_style_radius(bubble, 10, 0);
    lv_obj_t *speech_label = centered_label(bubble, speech, 10, 12, 260,
                                             0xD5E5EE,
                                             &hidrolab_font_tr_14);
    lv_label_set_long_mode(speech_label, LV_LABEL_LONG_WRAP);
    label(bubble, LV_SYMBOL_RIGHT, 278, 13, 0x80C9E0, &lv_font_montserrat_14);

    start_pending_base_return();
    if (s_badge_disembark_pending) {
        s_badge_disembark_pending = false;
        s_badge_travel_returning = true;
        s_badge_boarding = true;
        s_base_departing = true;
        lv_obj_add_flag(s_base_companion_ui, LV_OBJ_FLAG_HIDDEN);
        lv_anim_t arrive;
        lv_anim_init(&arrive);
        lv_anim_set_var(&arrive, s_base_astronaut);
        lv_anim_set_exec_cb(&arrive, badge_board_anim_exec);
        lv_anim_set_values(&arrive, 0, 1000);
        lv_anim_set_duration(&arrive, 600);
        lv_anim_set_completed_cb(&arrive, badge_board_anim_completed);
        if (!lv_anim_start(&arrive)) badge_board_anim_completed(NULL);
    }
}

#if 0
static void build_moon_base_legacy(void)
{
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    uint32_t today = current_daily_key();
    bool chemistry_today = cfg->chemistry_date == today;
    bool leaf_done = (cfg->task_mask & (1U << 1)) != 0;
    bool root_done = (cfg->task_mask & (1U << 2)) != 0;
    int missions = (chemistry_today ? 1 : 0) + (leaf_done ? 1 : 0) +
                   (root_done ? 1 : 0);
    int plants = active_plant_count();
    uint16_t display_ec = cfg->ec_x100 && cfg->ec_x100 < 500 ?
                          (uint16_t)(cfg->ec_x100 * 10U) : cfg->ec_x100;
    bool chemistry_good = chemistry_today && cfg->ph_x100 >= 550 &&
                          cfg->ph_x100 <= 650 && display_ec >= 800 &&
                          display_ec <= 2000;

    /* Moon floor and two restrained craters make the screen read as a place,
     * not another dashboard. */
    background_shape(s_content, -30, 341, 860, 100, 0x52627B, 44, LV_OPA_50);
    background_shape(s_content, 72, 367, 94, 20, 0x33445E,
                     LV_RADIUS_CIRCLE, LV_OPA_40);
    background_shape(s_content, 652, 354, 118, 25, 0x34465E,
                     LV_RADIUS_CIRCLE, LV_OPA_30);

    label(s_content, "YAŞAYAN AY ÜSSÜ", 18, 5, 0x8DEBFF,
          &lv_font_montserrat_14);
    lv_obj_t *day = label(s_content, "", 18, 28, C_SPACE_TEXT,
                          &hidrolab_font_tr_32);
    lv_label_set_text_fmt(day, "Üs Günü %u", (unsigned)cfg->season_day);
    label(s_content, "Bahçe, görevler ve oyunlar bugün de seninle yaşıyor.",
          178, 38, C_SPACE_MUTE, &lv_font_montserrat_14);

    /* Left upper portal: greenhouse dome. */
    lv_obj_t *greenhouse = panel(s_content, 18, 72, 210, 120, 0x102F3D);
    set_bg_gradient(greenhouse, 0x164B45, 0x0C263A);
    lv_obj_add_flag(greenhouse, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_translate_y(greenhouse, 3, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(greenhouse, 2, 0);
    lv_obj_set_style_border_color(greenhouse, lv_color_hex(0x58D890), 0);
    lv_obj_add_event_cb(greenhouse, base_open_garden_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dome = panel(greenhouse, 14, 25, 74, 57, 0x143D51);
    lv_obj_remove_flag(dome, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(dome, 38, 0);
    lv_obj_set_style_border_width(dome, 3, 0);
    lv_obj_set_style_border_color(dome, lv_color_hex(0x72E5AE), 0);
    background_shape(dome, 32, 16, 8, 31, 0x6BC887, 4, LV_OPA_COVER);
    background_shape(dome, 17, 20, 24, 17, 0x55D98C,
                     LV_RADIUS_CIRCLE, LV_OPA_COVER);
    background_shape(dome, 37, 12, 23, 18, 0x78E7A4,
                     LV_RADIUS_CIRCLE, LV_OPA_COVER);
    background_shape(greenhouse, 10, 82, 82, 7, 0x9BE9C3, 3, LV_OPA_COVER);
    label(greenhouse, "AY SERASI", 103, 18, 0x8DFFD0,
          &lv_font_montserrat_14);
    lv_obj_t *plant_count = label(greenhouse, "", 103, 43, C_SPACE_TEXT,
                                  &hidrolab_font_tr_14);
    lv_label_set_text_fmt(plant_count, "%d / %d bitki", plants, HIDROLAB_GARDEN_SLOT_COUNT);
    label(greenhouse, plants ? "Yuvaları incele  " LV_SYMBOL_RIGHT :
          "İlk bitkini ekle  " LV_SYMBOL_RIGHT,
          103, 76, C_SPACE_MUTE, &lv_font_montserrat_14);

    /* Left lower portal: the daily chemistry has become a reactor, not a form. */
    lv_obj_t *reactor = panel(s_content, 18, 204, 210, 174, 0x172B50);
    set_bg_gradient(reactor, chemistry_today ? 0x173D48 : 0x282752, 0x101F3C);
    lv_obj_add_flag(reactor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_translate_y(reactor, 3, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(reactor, 2, 0);
    lv_obj_set_style_border_color(reactor,
        lv_color_hex(chemistry_today ? 0x62E6B0 : 0x8E83FF), 0);
    lv_obj_add_event_cb(reactor, base_reactor_event, LV_EVENT_CLICKED, NULL);
    label(reactor, "UZAY REAKTÖRÜ", 14, 12, 0xD7D0FF,
          &lv_font_montserrat_14);
    reactor_core(reactor, 14, 43, 76, chemistry_good);
    if (chemistry_today) {
        lv_obj_t *ph = label(reactor, "", 104, 44, C_SPACE_TEXT,
                             &hidrolab_font_tr_14);
        lv_label_set_text_fmt(ph, "pH %u.%02u", (unsigned)(cfg->ph_x100 / 100U),
                              (unsigned)(cfg->ph_x100 % 100U));
        lv_obj_t *ec = label(reactor, "", 104, 75, 0x8DEBFF,
                             &hidrolab_font_tr_14);
        lv_label_set_text_fmt(ec, "EC %u", (unsigned)display_ec);
    } else {
        label(reactor, "Yakıt karışımı", 104, 47, C_SPACE_TEXT,
              &hidrolab_font_tr_14);
        label(reactor, "ölçüm bekliyor", 104, 78, 0xFFD18B,
              &lv_font_montserrat_14);
    }
    lv_obj_t *reactor_state = panel(reactor, 14, 132, 182, 28,
        chemistry_good ? 0x1B5B4A : (chemistry_today ? 0x60472D : 0x40375E));
    lv_obj_remove_flag(reactor_state, LV_OBJ_FLAG_CLICKABLE);
    centered_label(reactor_state,
        chemistry_good ? "DENGE HARİKA  " LV_SYMBOL_RIGHT :
        (chemistry_today ? "DEĞERLERİ İNCELE  " LV_SYMBOL_RIGHT :
         "REAKTÖRÜ DOLDUR  " LV_SYMBOL_RIGHT),
        4, 8, 174, chemistry_good ? 0xA5FFD2 : 0xFFFFFF,
        &lv_font_montserrat_14);

    /* Center: the astronaut is the interface and the contextual call-to-action. */
    lv_obj_t *command = panel(s_content, 244, 61, 312, 317, 0x0B2038);
    set_bg_gradient(command, 0x102F52, 0x10243C);
    lv_obj_add_flag(command, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_translate_y(command, 3, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(command, 2, 0);
    lv_obj_set_style_border_color(command, lv_color_hex(0x45D7FF), 0);
    lv_obj_add_event_cb(command, base_astronaut_event, LV_EVENT_CLICKED, NULL);
    background_orb(command, 68, 26, 176, 0x2F86DB, LV_OPA_20);
    space_star(command, 30, 42, 3, 0xFFF2B6);
    space_star(command, 269, 73, 3, 0xBCEBFF);
    centered_label(command, "ÜS REHBERİN", 0, 10, 312, 0x8DEBFF,
                   &lv_font_montserrat_14);
    const char *name = (cfg->astronaut_name[0] &&
                        strcmp(cfg->astronaut_name, "Astronot") != 0) ?
                       cfg->astronaut_name : "Astronot";
    centered_label(command, name, 0, 34, 312, C_SPACE_TEXT,
                   &hidrolab_font_tr_14);
    (void)profile_art(command, cfg->astronaut_profile, 78, 60, 330);

    const char *speech;
    const char *action;
    uint32_t mood;
    if (plants == 0) {
        speech = "Ay seramız sessiz. İlk bitkimizi uyandıralım mı?";
        action = "BANA DOKUN • SERAYA GİDELİM";
        mood = 0x58D890;
    } else if (!chemistry_today) {
        speech = "Bitkilerin yakıt karışımını bugün ölçmedik!";
        action = "BANA DOKUN • REAKTÖRÜ AÇ";
        mood = 0xA987FF;
    } else if (!leaf_done || !root_done) {
        speech = "Reaktör hazır! Şimdi bitkileri gözlemleyelim.";
        action = "BANA DOKUN • GÖREVE GİT";
        mood = 0x55C8FF;
    } else {
        speech = "Üs ışıl ışıl! Bugünün maceraları tamamlandı.";
        action = "BANA DOKUN • OYUN SEÇ";
        mood = 0xF8C86A;
    }
    lv_obj_t *bubble = panel(command, 18, 221, 276, 58, 0x091B31);
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_color(bubble, lv_color_hex(mood), 0);
    lv_obj_t *speech_label = centered_label(bubble, speech, 12, 12, 252,
                                             C_SPACE_TEXT,
                                             &lv_font_montserrat_14);
    lv_label_set_long_mode(speech_label, LV_LABEL_LONG_WRAP);
    lv_obj_t *action_pill = panel(command, 18, 287, 276, 23, mood);
    lv_obj_remove_flag(action_pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(action_pill, 0, 0);
    centered_label(action_pill, action, 2, 5, 272, 0x07182E,
                   &lv_font_montserrat_14);

    /* Right-hand activity modules complete the base scene. */
    lv_obj_t *mission = panel(s_content, 572, 72, 210, 120, 0x24254D);
    set_bg_gradient(mission, 0x32275D, 0x112645);
    lv_obj_add_flag(mission, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_translate_y(mission, 3, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(mission, 2, 0);
    lv_obj_set_style_border_color(mission, lv_color_hex(0xA987FF), 0);
    lv_obj_add_event_cb(mission, base_open_page_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)PAGE_TASKS);
    background_shape(mission, 16, 22, 58, 58, 0x6A55C8,
                     LV_RADIUS_CIRCLE, LV_OPA_80);
    centered_label(mission, LV_SYMBOL_LIST, 16, 43, 58, 0xFFFFFF,
                   &lv_font_montserrat_14);
    label(mission, "GÖREV UYDUSU", 90, 18, 0xD9D1FF,
          &lv_font_montserrat_14);
    lv_obj_t *mission_count = label(mission, "", 90, 43, C_SPACE_TEXT,
                                    &hidrolab_font_tr_14);
    lv_label_set_text_fmt(mission_count, "%d / 3 yıldız", missions);
    label(mission, missions == 3 ? "Hepsi tamam!  " LV_SYMBOL_RIGHT :
          "Sıradaki görevi gör  " LV_SYMBOL_RIGHT,
          90, 76, C_SPACE_MUTE, &lv_font_montserrat_14);

    lv_obj_t *arcade = panel(s_content, 572, 204, 210, 174, 0x102F52);
    set_bg_gradient(arcade, 0x123E64, 0x111E45);
    lv_obj_add_flag(arcade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_translate_y(arcade, 3, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(arcade, 2, 0);
    lv_obj_set_style_border_color(arcade, lv_color_hex(0x55C8FF), 0);
    lv_obj_add_event_cb(arcade, base_open_page_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)PAGE_GAMES);
    label(arcade, "AY OYUN İSTASYONU", 14, 12, 0x8DEBFF,
          &lv_font_montserrat_14);
    background_shape(arcade, 18, 48, 174, 68, 0x0A1E38, 18, LV_OPA_COVER);
    background_shape(arcade, 35, 67, 34, 15, 0x45D7FF,
                     LV_RADIUS_CIRCLE, LV_OPA_COVER);
    background_shape(arcade, 141, 63, 13, 13, 0xF8C86A,
                     LV_RADIUS_CIRCLE, LV_OPA_COVER);
    background_shape(arcade, 162, 79, 11, 11, 0xE96DA6,
                     LV_RADIUS_CIRCLE, LV_OPA_COVER);
    background_shape(arcade, 94, 55, 4, 46, 0x345D82, 2, LV_OPA_COVER);
    centered_label(arcade, "5 ETKİLEŞİMLİ OYUN", 14, 130, 182,
                   C_SPACE_TEXT, &lv_font_montserrat_14);
    centered_label(arcade, "İstasyona gir  " LV_SYMBOL_RIGHT, 14, 151, 182,
                   0x8DEBFF, &lv_font_montserrat_14);
}
#endif

static void game_companion(lv_obj_t *parent, const char *message, int x, int y)
{
    lv_obj_t *card = panel(parent, x, y, 154, 330, C_SURFACE);
    lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
    centered_label(card, "Yol arkadaşın", 8, 12, 138, C_SPACE_MUTE,
                   &hidrolab_font_tr_14);
    s_game_astronaut = profile_art(card, hidrolab_settings_get()->astronaut_profile,
                                   17, 34, 256);
    lv_obj_t *name = centered_label(card,
        hidrolab_settings_get()->astronaut_name, 10, 160, 134,
        C_SPACE_TEXT, &hidrolab_font_tr_14);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    background_shape(card, 18, 191, 118, 1, C_BORDER, 0, LV_OPA_COVER);
    s_game_reaction_card = panel(card, 10, 206, 134, 108, C_SURFACE);
    lv_obj_set_style_border_width(s_game_reaction_card, 0, 0);
    s_game_reaction = centered_label(s_game_reaction_card, message, 7, 10, 120,
                                     C_SPACE_TEXT, &hidrolab_font_tr_14);
    lv_label_set_long_mode(s_game_reaction, LV_LABEL_LONG_WRAP);
}

static void game_react(const char *message, bool positive)
{
    if (s_game_reaction) {
        if (label_text_changed(s_game_reaction, message)) {
            lv_obj_set_style_text_color(s_game_reaction,
                                        lv_color_hex(positive ? 0x8DFFD0 : 0xFFD18B), 0);
            lv_obj_set_style_opa(s_game_reaction, LV_OPA_COVER, 0);
        }
    }
    if (s_game_reaction_card) {
        lv_obj_set_style_border_color(s_game_reaction_card,
                                      lv_color_hex(positive ? 0x55D89B : 0xF1A45E), 0);
        lv_obj_set_style_bg_color(s_game_reaction_card,
                                  lv_color_hex(positive ? 0x103D3D : 0x423040), 0);
        lv_obj_set_style_translate_x(s_game_reaction_card, positive ? 0 : 3, 0);
    }
    if (s_game_astronaut) {
        lv_obj_set_style_translate_y(s_game_astronaut, positive ? -4 : 3, 0);
        /* Fading the scaled astronaut image on every hit repaints a large
         * region during time-critical games. Position and dialog still react. */
    }
}

static void sleep_wake_event(lv_event_t *e)
{
    (void)e;
    s_sleep_screen = false;
    s_night_awake_until_us = esp_timer_get_time() + 60000000LL;
    s_page = PAGE_GARDEN;
    s_base_home = true;
    s_game_view = GAME_HOME;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    request_page_render();
}

static void update_sleep_clock(void)
{
    if (!s_sleep_clock || !s_sleep_date) return;
    char time_buf[12];
    char date_buf[24];
    hidrolab_time_format(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));
    label_text_changed(s_sleep_clock, time_buf);
    label_text_changed(s_sleep_date, date_buf);
}

static void build_sleep_screen(void)
{
    /* Sleep transition may bypass render_page(): restore FULL here, too. */
    bug_display_fast_mode(false);
    s_badge_travel_overlay = NULL;
    s_badge_travel_returning = false;
    s_badge_disembark_pending = false;
    clear_slot_ui();
    s_reopen_garden_detail = false;
    s_sleep_modal = NULL;
    s_sleep_start_dd = NULL;
    s_sleep_end_dd = NULL;
    s_sleep_error = NULL;
    s_alarm_modal = NULL;
    s_alarm_hour_dd = NULL;
    s_alarm_minute_dd = NULL;
    s_alarm_error = NULL;
    s_alarm_switch = NULL;
    s_alarm_state_label = NULL;
    s_observation_modal = NULL;
    s_observation_error = NULL;
    lv_async_call_cancel(render_page_async_cb, NULL);
    s_page_render_pending = false;
    s_page_render_retry = false;
    s_base_return_pending = false;
    cancel_base_walk();
    s_base_home = true;
    cancel_game_timers();
    s_game_guide_visible = false;
    lv_async_call_cancel(badge_popup_close_async_cb, NULL);
    s_badge_overlay = NULL;
    s_badge_popup_open = false;
    s_badge_close_pending = false;
    s_sleep_screen = true;
    s_content = NULL;
    s_top_wifi = NULL;
    s_top_date = NULL;
    s_top_clock = NULL;
    s_wifi_manager_overlay = NULL;
    s_wifi_manager_panel = NULL;
    lv_obj_clean(s_screen);
    set_bg_gradient(s_screen, 0x010612, 0x071426);
    lv_obj_invalidate(s_screen);

    background_orb(s_screen, 642, -104, 230, 0x173A59, LV_OPA_20);
    background_orb(s_screen, -82, 330, 210, 0x102E47, LV_OPA_20);
    space_star(s_screen, 112, 76, 2, 0x86A6BF);
    space_star(s_screen, 684, 116, 2, 0x86A6BF);
    space_star(s_screen, 154, 356, 2, 0x66839A);

    s_sleep_clock = centered_label(s_screen, "--:--", 0, 26, 800, 0xD8ECF8,
                                   &hidrolab_font_tr_32);
    s_sleep_date = centered_label(s_screen, "", 0, 68, 800, 0x6F91A8,
                                   &lv_font_montserrat_14);

    lv_obj_t *capsule = panel(s_screen, 218, 108, 364, 260, 0x061522);
    set_bg_gradient(capsule, 0x061522, 0x0A1C2B);
    lv_obj_set_style_radius(capsule, 130, 0);
    lv_obj_set_style_border_width(capsule, 1, 0);
    lv_obj_set_style_border_color(capsule, lv_color_hex(0x17374B), 0);
    lv_obj_set_style_shadow_width(capsule, 18, 0);
    lv_obj_set_style_shadow_color(capsule, lv_color_hex(0x06101C), 0);
    lv_obj_set_style_shadow_opa(capsule, LV_OPA_50, 0);
    lv_obj_add_flag(capsule, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(capsule, sleep_wake_event, LV_EVENT_CLICKED, NULL);
    space_star(capsule, 56, 58, 2, 0x547389);
    space_star(capsule, 304, 74, 2, 0x547389);
    space_star(capsule, 286, 194, 2, 0x3D5D72);
    lv_obj_t *sleep_shadow = background_shape(capsule, 96, 222, 172, 12,
                                               0x020912, LV_RADIUS_CIRCLE,
                                               LV_OPA_30);
    lv_obj_remove_flag(sleep_shadow, LV_OBJ_FLAG_CLICKABLE);

    hidrolab_astronaut_profile_t sleep_profile = hidrolab_settings_get()->astronaut_profile;
    lv_obj_t *sleeping = image(capsule,
        hidrolab_astronaut_sleep_image((uint8_t)sleep_profile), 0, 0);
    /* The sleep artwork is 160 px, not the 120 px awake sprite. Align its
     * centre to the capsule instead of keeping offsets for the older asset. */
    /* The girl's upright pose needs more vertical breathing room. */
    lv_image_set_scale(sleeping, sleep_profile == HIDROLAB_ASTRONAUT_GIRL ? 384 : 448);
    lv_image_set_antialias(sleeping, true);
    lv_obj_center(sleeping);
    lv_obj_add_flag(sleeping, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sleeping, sleep_wake_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *wake_hint = panel(s_screen, 274, 424, 252, 40, 0x0B2032);
    lv_obj_remove_flag(wake_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_color(wake_hint, lv_color_hex(0x34576D), 0);
    centered_label(wake_hint, "Astronota dokun ve uyandır", 8, 10, 236,
                   0x8EAFBF, &hidrolab_font_tr_14);
    update_sleep_clock();
}

/* ---------------- Setup ---------------- */
static bool any_input_pressed(void)
{
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev;
         indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) return true;
    }
    return false;
}

static void finish_setup_to_garden(bool offline)
{
    if (!s_setup_mode) return;
    s_setup_finishing = true;
    /* A setup refresh queued by a scan/result change must not be allowed to
     * paint over the garden after setup has finished. */
    lv_async_call_cancel(build_setup_step_async_cb, NULL);
    s_setup_build_pending = false;
    s_setup_build_retry = false;
    esp_err_t save_err = hidrolab_settings_set_setup_done(true);
    if (save_err != ESP_OK) {
        ESP_LOGE(UI_TAG, "setup completion could not be saved: %s",
                 esp_err_to_name(save_err));
        if (s_wifi_status_label) {
            lv_label_set_text(s_wifi_status_label,
                              "Kurulumu kaydedemedim. Yeniden deneyelim.");
        }
        s_setup_finishing = false;
        return;
    }
    s_setup_mode = false;
    if (offline) {
        (void)hidrolab_wifi_disconnect();
    }
    s_page = PAGE_GARDEN;
    s_base_home = true;
    s_game_view = GAME_HOME;
    evaluate_badges();
    render_page();
    s_setup_finishing = false;
}

static void finish_setup_async(void *offline_value)
{
    finish_setup_to_garden((uintptr_t)offline_value != 0U);
}

static void request_setup_finish(bool offline)
{
    if (!s_setup_mode || s_setup_finishing) return;
    /* Do not delete the pressed button/screen tree from inside its own input
     * callback. The async callback runs safely after LVGL finishes the event. */
    s_setup_finishing = true;
    if (lv_async_call(finish_setup_async,
                      (void *)(uintptr_t)(offline ? 1U : 0U)) != LV_RESULT_OK) {
        s_setup_finishing = false;
    }
}

static void setup_offline_event(lv_event_t *e)
{
    (void)e;
    request_setup_finish(true);
}

static void setup_garden_event(lv_event_t *e)
{
    (void)e;
    request_setup_finish(false);
}

static void setup_continue_event(lv_event_t *e)
{
    (void)e;
    if (s_setup_step == SETUP_WELCOME) {
        s_setup_step = SETUP_NAME;
    } else if (s_setup_step == SETUP_NAME) {
        const char *name = s_setup_name_ta ? lv_textarea_get_text(s_setup_name_ta) : "";
        strlcpy(s_setup_name_buf, (name && name[0]) ? name : "Astronot",
                sizeof(s_setup_name_buf));
        (void)hidrolab_settings_set_astronaut_name(s_setup_name_buf);
        /* Important: the name textarea is never reused for a Wi-Fi password. */
        s_setup_name_ta = NULL;
        s_setup_step = SETUP_ASTRONAUT;
    }
    request_setup_build();
}

static void setup_astronaut_event(lv_event_t *e)
{
    hidrolab_astronaut_profile_t profile =
        (hidrolab_astronaut_profile_t)(uintptr_t)lv_event_get_user_data(e);
    (void)hidrolab_settings_set_astronaut_profile(profile);
    s_setup_step = SETUP_WIFI;
    s_setup_wifi_scan_started = false;
    request_setup_build();
}

static void submit_wifi_password(lv_obj_t *modal)
{
    if (!s_wifi_password_ta) return;
    const char *pass = s_wifi_password_ta ? lv_textarea_get_text(s_wifi_password_ta) : "";
    esp_err_t err = hidrolab_wifi_connect(s_selected_ssid, pass ? pass : "");
    if (err != ESP_OK) {
        ESP_LOGE(UI_TAG, "Wi-Fi connection could not start: %s", esp_err_to_name(err));
        if (s_wifi_password_status) {
            lv_label_set_text(s_wifi_password_status,
                              "Bağlantı başlatılamadı. Biraz bekleyip yeniden dene.");
        }
        return;
    }
    s_wifi_password_ta = NULL;
    s_wifi_password_status = NULL;
    lv_obj_delete_async(modal);
}

static void wifi_connect_button_event(lv_event_t *e)
{
    submit_wifi_password((lv_obj_t *)lv_event_get_user_data(e));
}

static void wifi_keyboard_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_t *modal = lv_obj_get_parent(kb);

    if (code == LV_EVENT_READY) {
        submit_wifi_password(modal);
    } else if (code == LV_EVENT_CANCEL) {
        s_wifi_password_ta = NULL;
        s_wifi_password_status = NULL;
        lv_obj_delete_async(modal);
    }
}

static void open_password_modal(const char *ssid)
{
    strlcpy(s_selected_ssid, ssid, sizeof(s_selected_ssid));

    lv_obj_t *modal = lv_obj_create(s_screen);
    lv_obj_set_pos(modal, 0, 0);
    lv_obj_set_size(modal, 800, 480);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    set_bg_gradient(modal, 0x071724, 0x102C3F);

    label(modal, "Wi-Fi şifresi", 28, 14, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_t *ssid_label = label(modal, ssid, 30, 55, 0x9DDDEF, &hidrolab_font_tr_14);
    lv_obj_set_width(ssid_label, 730);
    lv_label_set_long_mode(ssid_label, LV_LABEL_LONG_DOT);

    s_wifi_password_ta = lv_textarea_create(modal);
    lv_obj_set_pos(s_wifi_password_ta, 26, 84);
    lv_obj_set_size(s_wifi_password_ta, 575, 54);
    lv_textarea_set_one_line(s_wifi_password_ta, true);
    lv_textarea_set_max_length(s_wifi_password_ta, 63);
    lv_textarea_set_password_mode(s_wifi_password_ta, true);
    lv_textarea_set_placeholder_text(s_wifi_password_ta, "Şifreni yaz");
    lv_obj_set_style_text_font(s_wifi_password_ta, &hidrolab_font_tr_14, 0);

    lv_obj_t *connect = button(modal, LV_SYMBOL_WIFI "  Bağlan", 615, 84, 159, 54, C_GREEN);
    lv_obj_add_event_cb(connect, wifi_connect_button_event, LV_EVENT_CLICKED, modal);

    s_wifi_password_status = label(modal, "", 28, 137, 0xFFB49F, &hidrolab_font_tr_14);
    lv_obj_set_width(s_wifi_password_status, 744);
    lv_label_set_long_mode(s_wifi_password_status, LV_LABEL_LONG_DOT);

    lv_obj_t *kb = create_stock_keyboard(modal, s_wifi_password_ta, 0, 164, 800, 316);
    lv_obj_add_event_cb(kb, wifi_keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, wifi_keyboard_event, LV_EVENT_CANCEL, NULL);
}

static void wifi_network_event(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    hidrolab_wifi_network_t net = hidrolab_wifi_network_get(index);
    if (!net.ssid[0]) return;

    if (net.open) {
        (void)hidrolab_wifi_connect(net.ssid, "");
    } else {
        open_password_modal(net.ssid);
    }
}

static void wifi_scan_event(lv_event_t *e)
{
    (void)e;
    (void)hidrolab_wifi_scan_async();
}

static void build_setup_progress(lv_obj_t *parent, int step)
{
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_set_pos(dot, 326 + i * 42, 28);
        lv_obj_set_size(dot, i == step ? 28 : 12, 12);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(dot, 7, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(i <= step ? C_BLUE : 0xBED4E2), 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
    }
}

static void build_setup_wifi_list(lv_obj_t *parent)
{
    hidrolab_wifi_state_t state = hidrolab_wifi_get_state();
    size_t count = hidrolab_wifi_network_count();

    const char *status = "Bir ağ seç";
    uint32_t color = C_SPACE_MUTE;
    if (state == HIDROLAB_WIFI_SCANNING) {
        status = "Ağlar taranıyor...";
        color = C_BLUE;
    } else if (state == HIDROLAB_WIFI_CONNECTING) {
        status = "Bağlanıyor...";
        color = C_BLUE;
    } else if (state == HIDROLAB_WIFI_CONNECTED) {
        status = "Bağlandı! Bahçen hazırlanıyor...";
        color = C_GREEN;
    } else if (state == HIDROLAB_WIFI_FAILED) {
        status = "Bağlantı kurulamadı. Şifreyi kontrol et.";
        color = C_RED;
    } else if (count == 0 && s_setup_wifi_scan_started) {
        status = "Ağ bulunamadı. Tekrar tara.";
        color = C_RED;
    }
    if (state == HIDROLAB_WIFI_SCANNING || state == HIDROLAB_WIFI_CONNECTING) {
        lv_obj_t *spinner = lv_spinner_create(parent);
        lv_obj_set_pos(spinner, 274, 94);
        lv_obj_set_size(spinner, 20, 20);
        lv_spinner_set_anim_params(spinner, 850, 270);
        lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(C_BLUE), LV_PART_INDICATOR);
    }
    s_wifi_status_label = label(parent, status, 304, 96, color, &lv_font_montserrat_14);
    lv_obj_set_width(s_wifi_status_label, 464);
    lv_label_set_long_mode(s_wifi_status_label, LV_LABEL_LONG_DOT);

    if (state == HIDROLAB_WIFI_CONNECTED) {
        lv_obj_t *go = button(parent, "Bahçeye Geç  " LV_SYMBOL_RIGHT, 304, 332, 464, 48, C_GREEN);
        lv_obj_add_event_cb(go, setup_garden_event, LV_EVENT_PRESSED, NULL);
    } else {
        lv_obj_t *scan = button(parent, LV_SYMBOL_REFRESH "  Tekrar Tara", 304, 332, 222, 48, C_BLUE);
        lv_obj_add_event_cb(scan, wifi_scan_event, LV_EVENT_CLICKED, NULL);
        if (state == HIDROLAB_WIFI_CONNECTING || state == HIDROLAB_WIFI_SCANNING) {
            lv_obj_add_state(scan, LV_STATE_DISABLED);
        }
        lv_obj_t *offline = button(parent, "Çevrimdışı Devam Et", 538, 332, 230, 48, C_NAVY2);
        lv_obj_add_event_cb(offline, setup_offline_event, LV_EVENT_PRESSED, NULL);
    }

    int shown = count > 4 ? 4 : (int)count;
    for (int i = 0; i < shown; ++i) {
        hidrolab_wifi_network_t net = hidrolab_wifi_network_get((size_t)i);
        lv_obj_t *row = panel(parent, 304, 128 + i * 47, 464, 40, 0xFFFFFF);
        if (state != HIDROLAB_WIFI_CONNECTING && state != HIDROLAB_WIFI_SCANNING) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, wifi_network_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        } else {
            lv_obj_set_style_opa(row, (lv_opa_t)150, 0);
        }
        label(row, LV_SYMBOL_WIFI, 13, 9, net.rssi > -67 ? C_GREEN : C_BLUE, &lv_font_montserrat_14);
        lv_obj_t *name = label(row, net.ssid, 43, 9, C_TEXT, &lv_font_montserrat_14);
        lv_obj_set_width(name, 300);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        label(row, net.open ? "Açık" : "Kilitli", 370, 9, C_MUTED, &lv_font_montserrat_14);
    }
}

static void build_setup_step(void)
{
    if (!s_setup_mode) return;
    lv_obj_clean(s_screen);
    s_setup_name_ta = NULL;
    s_wifi_status_label = NULL;
    s_wifi_password_status = NULL;
    set_bg_gradient(s_screen, C_SPACE_TOP, C_SPACE_BOT);
    /* The RGB panel is refreshed in strips. Invalidating the whole root here
     * guarantees that no pixels from the previous setup/page survive a
     * transition, even when both screens use the same background color. */
    lv_obj_invalidate(s_screen);
    decorate_space_background(s_screen, 480);
    build_setup_progress(s_screen, (int)s_setup_step);

    if (s_setup_step == SETUP_WELCOME) {
        image(s_screen, &hidrolab_img_astronaut_guide, 72, 105);
        label(s_screen, "HybridLab Mini", 242, 104, C_SPACE_TEXT, &hidrolab_font_tr_32);
        label(s_screen, "Uzay bahçen seni bekliyor!", 244, 148, 0x69C9FF, &hidrolab_font_tr_32);
        lv_obj_t *txt = label(s_screen,
            "Bitkilerini büyüt, görevleri tamamla, oyunlarda rozetler kazan.\n"
            "Ben de yolculuk boyunca sana yardım edeceğim.",
            245, 205, C_SPACE_MUTE, &lv_font_montserrat_14);
        lv_obj_set_width(txt, 470);
        lv_label_set_long_mode(txt, LV_LABEL_LONG_WRAP);
        lv_obj_t *go = button(s_screen, "Göreve Başla  " LV_SYMBOL_RIGHT, 245, 300, 210, 50, C_GREEN);
        lv_obj_add_event_cb(go, setup_continue_event, LV_EVENT_CLICKED, NULL);
        centered_label(s_screen, "1 / 4  •  Hoş geldin", 0, 418, 800, C_SPACE_MUTE, &lv_font_montserrat_14);
        return;
    }

    if (s_setup_step == SETUP_NAME) {
        image(s_screen, &hidrolab_img_astronaut_guide, 42, 62);
        label(s_screen, "Sana nasıl hitap edelim?", 195, 67, C_SPACE_TEXT, &hidrolab_font_tr_32);
        label(s_screen, "Adın bahçenin ve görevlerinin üzerinde görünecek.", 198, 108,
              C_SPACE_MUTE, &lv_font_montserrat_14);

        s_setup_name_ta = lv_textarea_create(s_screen);
        lv_obj_set_pos(s_setup_name_ta, 194, 139);
        lv_obj_set_size(s_setup_name_ta, 410, 52);
        lv_textarea_set_one_line(s_setup_name_ta, true);
        lv_textarea_set_max_length(s_setup_name_ta, 24);
        lv_textarea_set_placeholder_text(s_setup_name_ta, "Adını yaz");
        lv_obj_set_style_text_font(s_setup_name_ta, &hidrolab_font_tr_14, 0);
        const char *existing = hidrolab_settings_get()->astronaut_name;
        if (existing[0] && strcmp(existing, "Astronot") != 0) {
            lv_textarea_set_text(s_setup_name_ta, existing);
        }

        lv_obj_t *go = button(s_screen, "Devam Et  " LV_SYMBOL_RIGHT, 620, 139, 148, 52, C_GREEN);
        lv_obj_add_event_cb(go, setup_continue_event, LV_EVENT_CLICKED, NULL);
        (void)create_stock_keyboard(s_screen, s_setup_name_ta, 0, 205, 800, 275);
        return;
    }

    if (s_setup_step == SETUP_ASTRONAUT) {
        centered_label(s_screen, "Astronotunu seç!", 0, 72, 800, C_SPACE_TEXT, &hidrolab_font_tr_32);
        centered_label(s_screen, "İstersen daha sonra değiştirebilirsin.", 0, 112, 800,
                       C_SPACE_MUTE, &lv_font_montserrat_14);
        const char *names[3] = {"Erkek Astronot", "Kız Astronot", "Uzaylı Astronot"};
        const uint32_t colors[3] = {0x103B68, 0x442947, 0x17463F};
        for (int i = 0; i < 3; ++i) {
            lv_obj_t *c = panel(s_screen, 55 + i * 245, 155, 210, 230, colors[i]);
            lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(c, setup_astronaut_event, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
            (void)profile_art(c, (hidrolab_astronaut_profile_t)i, 45, 24, 240);
            centered_label(c, names[i], 12, 170, 186, C_SPACE_TEXT, &lv_font_montserrat_14);
            centered_label(c, "Seç", 12, 198, 186, i == 0 ? C_BLUE : (i == 1 ? C_PINK : C_PURPLE),
                           &lv_font_montserrat_14);
        }
        centered_label(s_screen, "3 / 4  •  Karakter", 0, 426, 800, C_SPACE_MUTE, &lv_font_montserrat_14);
        return;
    }

    /* SETUP_WIFI */
    (void)profile_art(s_screen, hidrolab_settings_get()->astronaut_profile, 55, 103, 256);
    label(s_screen, "Bahçeni internete bağlayalım", 230, 66, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_t *tip = panel(s_screen, 34, 237, 230, 128, C_SPACE_CARD);
    label(tip, "Astronot İpucu", 18, 16, 0x69C9FF, &lv_font_montserrat_14);
    lv_obj_t *txt = label(tip,
        "Wi-Fi saatimizi yeniler.\nBağlanmak zorunda değilsin;\nbahçen internetsiz de çalışır.",
        18, 46, C_SPACE_TEXT, &lv_font_montserrat_14);
    lv_obj_set_width(txt, 195);
    build_setup_wifi_list(s_screen);
    centered_label(s_screen, "4 / 4  •  Wi-Fi veya çevrimdışı", 0, 438, 800, C_SPACE_MUTE, &lv_font_montserrat_14);

    if (!s_setup_wifi_scan_started && hidrolab_wifi_get_state() != HIDROLAB_WIFI_CONNECTING) {
        s_setup_wifi_scan_started = true;
        (void)hidrolab_wifi_scan_async();
    }
}

/* ---------------- Shell ---------------- */
static void badge_travel_completed(lv_anim_t *animation)
{
    if (!s_badge_travel_overlay) return;
    if (animation && animation->var == NULL) return;
    s_badge_travel_overlay = NULL;
    if (s_badge_travel_returning) {
        s_page = s_badge_return_page;
        s_base_home = s_badge_return_base_home;
        s_tasks_badges_view = false;
        s_badge_disembark_pending = s_page == PAGE_GARDEN && s_base_home;
    } else {
        s_page = PAGE_TASKS;
        s_tasks_badges_view = true;
    }
    s_tasks_academy_view = false;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
    request_page_render();
}

static void badge_portal_event(lv_event_t *e)
{
    (void)e;
    if (s_badge_travel_overlay || s_setup_mode || s_sleep_screen ||
        s_page_render_pending || s_page_render_retry || s_base_departing ||
        (s_page == PAGE_TASKS && s_tasks_badges_view)) return;

    s_badge_return_page = s_page;
    s_badge_travel_returning = false;
    s_badge_return_base_home = s_base_home;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);

    if (s_base_home && s_base_astronaut) {
        s_badge_boarding = true;
        s_base_departing = true;
        s_night_awake_until_us = esp_timer_get_time() + 60000000LL;
        if (s_base_companion_ui)
            lv_obj_add_flag(s_base_companion_ui, LV_OBJ_FLAG_HIDDEN);

        lv_anim_t board;
        lv_anim_init(&board);
        lv_anim_set_var(&board, s_base_astronaut);
        lv_anim_set_exec_cb(&board, badge_board_anim_exec);
        lv_anim_set_values(&board, 0, 1000);
        lv_anim_set_duration(&board, 600);
        lv_anim_set_path_cb(&board, lv_anim_path_ease_in_out);
        lv_anim_set_completed_cb(&board, badge_board_anim_completed);
        if (!lv_anim_start(&board)) {
            badge_board_anim_exec(s_base_astronaut, 1000);
            badge_board_anim_completed(NULL);
        }
        return;
    }
    badge_travel_start();
}

static void badge_travel_start(void)
{
    if (s_badge_travel_overlay || s_sleep_screen ||
        s_page_render_pending || s_page_render_retry) return;

    lv_obj_t *overlay = lv_obj_create(s_screen);
    s_badge_travel_overlay = overlay;
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    set_bg_gradient(overlay, 0x060D25, 0x172E50);
    s_badge_speed_bar = NULL;
    s_badge_flight_status = NULL;
    s_badge_destination_orb = NULL;

    lv_obj_t *field = lv_obj_create(overlay);
    lv_obj_set_pos(field, 0, 0);
    lv_obj_set_size(field, 800, 480);
    lv_obj_remove_flag(field, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(field, 0, 0);
    lv_obj_set_style_radius(field, 0, 0);
    lv_obj_set_style_border_width(field, 0, 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_TRANSP, 0);
    static const uint32_t warp_colors[5] = {
        0xF7FBFF, 0xC1EBFF, 0x8AD5F1, 0xE4F5FF, 0xA9C5FF,
    };
    for (int i = 0; i < BADGE_WARP_STAR_COUNT; ++i) {
        lv_obj_t *streak = lv_line_create(field);
        lv_obj_set_style_line_color(streak, lv_color_hex(warp_colors[i % 5]), 0);
        lv_obj_set_style_line_width(streak, i % 5 == 0 ? 2 : 1, 0);
        lv_obj_set_style_line_rounded(streak, true, 0);
    }
    badge_warp_anim_exec(field, 0);

    /* Thin cockpit corners leave the sky open; no center dot or equalizer. */
    static const lv_point_precise_t frame_left[] = {{0, 0}, {54, 18}, {12, 408}};
    static const lv_point_precise_t frame_right[] = {{800, 0}, {746, 18}, {788, 408}};
    for (int side = 0; side < 2; ++side) {
        lv_obj_t *rib = lv_line_create(overlay);
        lv_line_set_points(rib, side ? frame_right : frame_left, 3);
        lv_obj_set_style_line_color(rib, lv_color_hex(0x31536B), 0);
        lv_obj_set_style_line_width(rib, 5, 0);
    }
    lv_obj_t *console = panel(overlay, 160, 404, 480, 62, 0x12263F);
    lv_obj_set_style_radius(console, 20, 0);
    lv_obj_set_style_border_color(console, lv_color_hex(0x4B8299), 0);
    lv_obj_set_style_border_width(console, 1, 0);
    centered_label(console, s_badge_travel_returning ? "Ay üssüne dönüyoruz" :
                   "Rozet galaksisine gidiyoruz", 20, 12, 440, 0xC3EDFF,
                   &hidrolab_font_tr_14);
    s_badge_speed_bar = lv_bar_create(console);
    lv_obj_set_pos(s_badge_speed_bar, 105, 40);
    lv_obj_set_size(s_badge_speed_bar, 270, 4);
    lv_bar_set_range(s_badge_speed_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_badge_speed_bar, lv_color_hex(0x1D3B50), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_badge_speed_bar, lv_color_hex(0x6DDCCB), LV_PART_INDICATOR);

    lv_anim_t warp;
    lv_anim_init(&warp);
    lv_anim_set_var(&warp, field);
    lv_anim_set_exec_cb(&warp, badge_warp_anim_exec);
    lv_anim_set_values(&warp, 0, 1400);
    lv_anim_set_duration(&warp, 1400);
    lv_anim_set_path_cb(&warp, lv_anim_path_linear);
    lv_anim_set_completed_cb(&warp, badge_travel_completed);
    if (!lv_anim_start(&warp)) badge_travel_completed(NULL);
}

static void badge_portal_back_event(lv_event_t *e)
{
    (void)e;
    if (s_page_render_pending || s_page_render_retry || s_badge_travel_overlay) return;
    s_badge_travel_returning = true;
    badge_travel_start();
}

static void build_shell(void)
{
    s_badge_travel_overlay = NULL;
    lv_obj_clean(s_screen);
    set_bg_gradient(s_screen, C_SPACE_TOP, C_SPACE_TOP);
    lv_obj_invalidate(s_screen);

    /* Solid notification/status bar: no gradient, theme, translucent fill,
     * shadow or bottom overlay. A flat background avoids the visually split
     * light/dark bar reported on the 800x480 device. */
    lv_obj_t *top = lv_obj_create(s_screen);
    lv_obj_remove_style_all(top);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_size(top, 800, TOP_H);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_outline_width(top, 0, 0);
    lv_obj_set_style_shadow_width(top, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x0C1D36), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(top, LV_GRAD_DIR_NONE, 0);
    /* The brand image is extracted from the supplied HybridLab PDF. */
    image(top, &hidrolab_img_hybridlab_logo, 9, 1);
    label(top, "Mini", 172, 2, C_CYAN, &lv_font_montserrat_14);
    background_shape(top, 224, 5, 1, 14, 0x4D879F, 0, LV_OPA_40);
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    const char *astronaut_name = (cfg->astronaut_name[0] &&
                                  strcmp(cfg->astronaut_name, "Astronot") != 0) ?
                                 cfg->astronaut_name : "Astronot";
    const char *top_caption = (s_page == PAGE_GARDEN && s_base_home) ?
                              "Ay Üssü" : astronaut_name;
    lv_obj_t *top_name = label(top, top_caption, 235, 2, 0xFFFFFF,
                               &hidrolab_font_tr_14);
    lv_obj_set_width(top_name, 98);
    lv_label_set_long_mode(top_name, LV_LABEL_LONG_DOT);
    lv_obj_set_height(top_name, 20);
    background_shape(top, 343, 5, 1, 14, 0x4D879F, 0, LV_OPA_40);
    s_top_date = label(top, "", 349, 2, 0xA9D4E1, &hidrolab_font_tr_14);
    lv_obj_set_width(s_top_date, 279);
    lv_obj_set_style_text_align(s_top_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_top_date, LV_LABEL_LONG_DOT);
    lv_obj_set_height(s_top_date, 20);
    lv_label_set_text_fmt(s_top_date, "ÜS GÜNÜ %u  •  --.--.----",
                          (unsigned)cfg->season_day);
    background_shape(top, 638, 5, 1, 14, 0x4D879F, 0, LV_OPA_40);
    s_top_wifi = label(top, "", 650, 2, 0xB5D9EC, &lv_font_montserrat_14);
    s_top_clock = label(top, "--:--", 708, 2, 0xFFFFFF,
                        &lv_font_montserrat_14);
    lv_obj_set_width(s_top_clock, 72);
    lv_obj_set_height(s_top_clock, 20);
    lv_label_set_long_mode(s_top_clock, LV_LABEL_LONG_DOT);

    s_content = lv_obj_create(s_screen);
    lv_obj_set_pos(s_content, 0, CONTENT_Y);
    lv_obj_set_size(s_content, 800, CONTENT_H_FULL);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_radius(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    /* The Moon Base supplies its own full scene from SPIFFS. Other pages keep
     * the lightweight code-drawn space decoration. */
    if (s_page != PAGE_GARDEN || !s_base_home) {
        decorate_page_background(s_content);
    }

    /* The status bar (Ay Ussu / Us Gunu / Wi-Fi / clock) must be rendered
     * AFTER the content so no scene object can tint or overlap either half.
     * The bar itself has a fully opaque, single-color LVGL background. */
    lv_obj_move_foreground(top);
    lv_obj_invalidate(top);
}

/* ---------------- Garden ---------------- */
static const char *LEAF_OBSERVATIONS[5] = {
    "Seçilmedi", "Canlı yeşil", "Açık yeşil", "Sarı", "Kahverengi / lekeli",
};

static const char *ROOT_OBSERVATIONS[5] = {
    "Seçilmedi", "Beyaz / krem", "Açık sarı", "Kahverengi", "Yeşil / yosunlu",
};

static const uint32_t LEAF_OBSERVATION_COLORS[5] = {
    C_LOCK, 0x35B96F, 0x8FD36A, 0xF2C94C, 0x9B6847,
};

static const uint32_t ROOT_OBSERVATION_COLORS[5] = {
    C_LOCK, 0xF2E7C9, 0xE7C75D, 0x9B6847, 0x53A96B,
};

/* Short, child-friendly next steps.  These deliberately ask the child to
 * observe and verify conditions instead of diagnosing from color alone. */
static const char *LEAF_OBSERVATION_TIPS[5] = {
    "", "Harika! Yaprak iyi görünüyor.\nYarın yine bak.",
    "Yeni bir yaprak olabilir.\nYarın rengine yine bak.",
    "Bitkin biraz yardım istiyor.\nBirlikte pH ve EC'yi ölçün.",
    "Diğer yapraklara da bak.\nLeke büyüyor mu takip et.",
};

static const char *ROOT_OBSERVATION_TIPS[5] = {
    "", "Harika! Kökler temiz.\nYarın yine bak.",
    "Şimdilik izlemeyi sürdür.\nYarın rengi değişmiş mi bak.",
    "Kökler hava istiyor olabilir.\nPompayı birlikte kontrol edin.",
    "Su kabını ışıktan koru.\nYarın köklere yeniden bak.",
};

/* The first four options are built in, then saved names, then New species. */
#define SLOT_FIXED_TYPES 4U
static const hidrolab_slot_type_t SLOT_TYPES[] = {
    HIDROLAB_SLOT_LETTUCE,
    HIDROLAB_SLOT_ARUGULA,
    HIDROLAB_SLOT_MINT,
    HIDROLAB_SLOT_PARSLEY,
    HIDROLAB_SLOT_OTHER,
};

static const char *plant_stage(const hidrolab_plant_slot_t *slot, uint32_t *color)
{
    if (!slot || !slot->active) {
        if (color) *color = C_MUTED;
        return "Boş";
    }
    if (slot->growth_stage == HIDROLAB_STAGE_SEED) {
        if (color) *color = 0xFFD18B;
        return "Tohum";
    }
    if (slot->growth_stage == HIDROLAB_STAGE_SPROUT) {
        if (color) *color = 0x80E4AE;
        return "Filiz";
    }
    if (color) *color = 0x75D9EF;
    return "Bitki";
}

static void clear_slot_ui(void)
{
    s_garden_detail_modal = NULL;
    s_slot_modal = NULL;
    s_slot_delete_overlay = NULL;
    s_slot_error = NULL;
    s_slot_name_ta = NULL;
    s_slot_custom_ta = NULL;
    s_slot_custom_label = NULL;
    s_slot_delete_button = NULL;
    s_slot_keyboard = NULL;
    s_slot_type_dd = NULL;
    s_slot_stage_dd = NULL;
    s_slot_stage_label = NULL;
    memset(s_slot_stage_buttons, 0, sizeof(s_slot_stage_buttons));
    s_slot_saving = false;
}

static void close_slot_modal(lv_event_t *e)
{
    lv_obj_t *modal = (lv_obj_t *)lv_event_get_user_data(e);
    if (!modal || modal != s_slot_modal || s_slot_saving) return;
    clear_slot_ui();
    lv_obj_delete_async(modal);
}

static void slot_keyboard_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        save_slot_event(e);
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_CANCEL || !s_slot_modal || s_slot_saving) return;
    lv_obj_t *modal = s_slot_modal;
    clear_slot_ui();
    lv_obj_delete_async(modal);
}

static void slot_stage_event(lv_event_t *e)
{
    uint8_t stage = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (!s_slot_modal || s_slot_saving || stage > HIDROLAB_STAGE_PLANT) return;
    s_slot_stage_value = stage;
    for (int i = 0; i < 3; ++i) {
        if (!s_slot_stage_buttons[i]) continue;
        if (i == stage) lv_obj_add_state(s_slot_stage_buttons[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(s_slot_stage_buttons[i], LV_STATE_CHECKED);
    }
}

static void slot_type_event(lv_event_t *e)
{
    (void)e;
    if (!s_slot_type_dd || !s_slot_custom_ta) return;
    uint32_t choice = lv_dropdown_get_selected(s_slot_type_dd);
    bool custom = choice == SLOT_FIXED_TYPES +
                         hidrolab_settings_custom_species_count();
    if (custom) {
        lv_obj_remove_flag(s_slot_custom_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_slot_custom_ta, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_slot_custom_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_slot_custom_ta, LV_OBJ_FLAG_HIDDEN);
        if (s_slot_keyboard) lv_keyboard_set_textarea(s_slot_keyboard, s_slot_name_ta);
    }
    /* Growth always stays below the plant name. "Other" only reveals the
     * custom type directly below the type dropdown, so the screen never
     * changes columns when the child makes a selection. */
    if (s_slot_stage_label) lv_obj_set_pos(s_slot_stage_label, 24, 158);
    for (int i = 0; i < 3; ++i) {
        if (!s_slot_stage_buttons[i]) continue;
        lv_obj_set_pos(s_slot_stage_buttons[i], 24 + i * 112, 180);
        lv_obj_set_size(s_slot_stage_buttons[i], 104, 46);
    }
    if (s_slot_custom_label) lv_obj_set_pos(s_slot_custom_label, 388, 158);
    if (s_slot_custom_ta) {
        lv_obj_set_pos(s_slot_custom_ta, 388, 180);
        lv_obj_set_size(s_slot_custom_ta, 388, 46);
    }
    if (s_slot_delete_button) {
        lv_obj_set_pos(s_slot_delete_button, 388,
                       custom ? 228 : 180);
        lv_obj_set_size(s_slot_delete_button, 388,
                        custom ? 36 : 46);
    }
    if (s_slot_error) {
        lv_obj_set_pos(s_slot_error, custom && s_slot_delete_button ? 24 : 388, 236);
        lv_obj_set_width(s_slot_error, custom && s_slot_delete_button ? 340 : 388);
    }
    if (s_slot_error) lv_label_set_text(s_slot_error, "");
}

static void slot_textarea_focus_event(lv_event_t *e)
{
    if (s_slot_keyboard) {
        lv_keyboard_set_textarea(s_slot_keyboard, (lv_obj_t *)lv_event_get_target(e));
    }
}

static void slot_textarea_changed_event(lv_event_t *e)
{
    (void)e;
    if (s_slot_error) lv_label_set_text(s_slot_error, "");
}

static void save_slot_event(lv_event_t *e)
{
    (void)e;
    if (!s_slot_modal || s_slot_saving || !hidrolab_slot_is_available(s_edit_slot)) return;
    hidrolab_plant_slot_t slot = hidrolab_settings_get()->slots[s_edit_slot];
    bool was_active = slot.active;
    slot.active = true;
    const char *name = s_slot_name_ta ? lv_textarea_get_text(s_slot_name_ta) : "";
    /* The persisted name field is 17 bytes, not 17 Unicode characters. Never
     * truncate a Turkish letter in the middle or silently migrate the blob. */
    if (name && strlen(name) >= sizeof(slot.name)) {
        lv_label_set_text(s_slot_error, "Bu isim biraz uzun. Daha kısa bir isim seç.");
        return;
    }
    strlcpy(slot.name, name ? name : "", sizeof(slot.name));
    uint32_t sel = s_slot_type_dd ? lv_dropdown_get_selected(s_slot_type_dd) : 0;
    const size_t saved_species = hidrolab_settings_custom_species_count();
    if (sel >= SLOT_FIXED_TYPES + saved_species + 1) sel = 0;
    slot.type = sel < SLOT_FIXED_TYPES ? SLOT_TYPES[sel] : HIDROLAB_SLOT_OTHER;
    const char *custom_type = NULL;
    if (slot.type == HIDROLAB_SLOT_OTHER) {
        if (sel < SLOT_FIXED_TYPES + saved_species) {
            custom_type = hidrolab_settings_custom_species_name(sel - SLOT_FIXED_TYPES);
        } else {
            custom_type = s_slot_custom_ta ? lv_textarea_get_text(s_slot_custom_ta) : "";
        }
        if (!custom_type || !custom_type[0]) {
            lv_label_set_text(s_slot_error, "Diğer tür için önce tür adını yaz.");
            return;
        }
        if (strlen(custom_type) >= sizeof(slot.name)) {
            lv_label_set_text(s_slot_error, "Tür adı uzun. Daha kısa bir ad seç.");
            return;
        }
        /* Retain legacy Fesleğen/Ispanak IDs when the user only edits growth
         * or nickname; those choices are hidden for new plants, not erased. */
        hidrolab_slot_type_t old_type = hidrolab_settings_get()->slots[s_edit_slot].type;
        if ((old_type == HIDROLAB_SLOT_BASIL || old_type == HIDROLAB_SLOT_SPINACH) &&
            strcmp(custom_type, hidrolab_slot_type_name(old_type)) == 0) {
            slot.type = old_type;
            custom_type = NULL;
        }
    }
    if (!was_active) slot.planted_day = hidrolab_settings_get()->season_day;
    slot.growth_stage = s_slot_stage_value <= HIDROLAB_STAGE_PLANT ?
                        s_slot_stage_value : HIDROLAB_STAGE_SEED;
    if (!slot.name[0]) {
        strlcpy(slot.name, custom_type ? custom_type : hidrolab_slot_type_name(slot.type),
                sizeof(slot.name));
    }
    esp_err_t save_err = hidrolab_settings_set_slot(s_edit_slot, &slot, custom_type);
    if (save_err != ESP_OK) {
        lv_label_set_text(s_slot_error,
                          save_err == ESP_ERR_NO_MEM ?
                          "Tür listesi dolu. Kayıt yapılamadı." :
                          "Kaydedemedim. Bilgilerin burada; yeniden dene.");
        return;
    }
    s_slot_saving = true;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
    /* render_page() cleans the modal and the pressed Save button. Defer that
     * destruction until LVGL has finished dispatching this click event. */
    request_page_render();
}

static void delete_slot_event(lv_event_t *e)
{
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(e);
    if (!s_slot_modal || s_slot_saving || !hidrolab_slot_is_available(s_edit_slot)) return;
    if (!overlay || overlay != s_slot_delete_overlay) return;
    if (hidrolab_settings_clear_slot(s_edit_slot) != ESP_OK) {
        s_slot_delete_overlay = NULL;
        lv_obj_delete_async(overlay);
        lv_label_set_text(s_slot_error, "Yuvayı boşaltamadım. Yeniden deneyebilirsin.");
        return;
    }
    s_slot_saving = true;
    request_page_render();
}

static void cancel_slot_delete_event(lv_event_t *e)
{
    if (s_slot_saving) return;
    lv_obj_t *overlay = (lv_obj_t *)lv_event_get_user_data(e);
    if (!overlay || overlay != s_slot_delete_overlay) return;
    s_slot_delete_overlay = NULL;
    lv_obj_delete_async(overlay);
}

static void confirm_slot_delete_event(lv_event_t *e)
{
    (void)e;
    if (!s_slot_modal || s_slot_saving || s_slot_delete_overlay) return;
    lv_obj_t *overlay = panel(s_slot_modal, 0, 0, 800, 480, 0x020B17);
    s_slot_delete_overlay = overlay;
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_t *card = panel(overlay, 160, 136, 480, 208, 0x123644);
    lv_obj_set_style_border_color(card, lv_color_hex(0x75D9B3), 0);
    centered_label(card, "Bu yuvayı boşaltalım mı?", 12, 24, 456,
                   C_SPACE_TEXT, &hidrolab_font_tr_14);
    centered_label(card, "Bitkinin adı ve gelişim kaydı silinecek.", 12, 66, 456,
                   C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_t *keep = button(card, "Bitkim kalsın", 24, 130, 208, 50, 0x23664F);
    lv_obj_add_event_cb(keep, cancel_slot_delete_event, LV_EVENT_CLICKED, overlay);
    lv_obj_t *remove = button(card, "Yuvayı boşalt", 248, 130, 208, 50, 0x923F43);
    lv_obj_add_event_cb(remove, delete_slot_event, LV_EVENT_CLICKED, overlay);
}

static void open_slot_editor(void)
{
    const hidrolab_plant_slot_t *slot = &hidrolab_settings_get()->slots[s_edit_slot];
    clear_slot_ui();
    lv_obj_t *modal = lv_obj_create(s_screen);
    s_slot_modal = modal;
    lv_obj_set_pos(modal, 0, 0);
    lv_obj_set_size(modal, 800, 480);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    set_bg_gradient(modal, 0x071E2C, 0x123B42);

    label(modal, slot->active ? "Bitkim" : "Yeni Bir Bitki", 24, 14,
          C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_t *hint = label(modal, "", 26, 54, 0x94DDBB, &hidrolab_font_tr_14);
    lv_label_set_text_fmt(hint, "YUVA %u  •  Adını, türünü ve gelişimini seç.",
                           (unsigned)s_edit_slot + 1);
    lv_obj_t *cancel = button(modal, "Vazgeç", 674, 16, 102, 40, 0x294954);
    lv_obj_add_event_cb(cancel, close_slot_modal, LV_EVENT_CLICKED, modal);

    label(modal, "Bitkinin adı", 24, 80, C_SPACE_MUTE, &hidrolab_font_tr_14);
    s_slot_name_ta = lv_textarea_create(modal);
    lv_obj_set_pos(s_slot_name_ta, 24, 102);
    lv_obj_set_size(s_slot_name_ta, 340, 48);
    lv_textarea_set_one_line(s_slot_name_ta, true);
    lv_textarea_set_max_length(s_slot_name_ta, 16);
    lv_textarea_set_placeholder_text(s_slot_name_ta, "Örn. Minik Nane");
    lv_obj_set_style_text_font(s_slot_name_ta, &hidrolab_font_tr_14, 0);
    lv_obj_set_style_bg_color(s_slot_name_ta, lv_color_hex(0x102E3B), 0);
    lv_obj_set_style_text_color(s_slot_name_ta, lv_color_hex(C_SPACE_TEXT), 0);
    lv_obj_set_style_border_color(s_slot_name_ta, lv_color_hex(0x74DDA7), 0);
    if (slot->active) lv_textarea_set_text(s_slot_name_ta, slot->name);
    lv_obj_add_event_cb(s_slot_name_ta, slot_textarea_focus_event,
                        LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_slot_name_ta, slot_textarea_focus_event,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_slot_name_ta, slot_textarea_changed_event,
                        LV_EVENT_VALUE_CHANGED, NULL);

    label(modal, "Bitki türü", 388, 80, C_SPACE_MUTE, &hidrolab_font_tr_14);
    s_slot_type_dd = lv_dropdown_create(modal);
    lv_obj_set_pos(s_slot_type_dd, 388, 102);
    lv_obj_set_size(s_slot_type_dd, 190, 48);
    char type_options[512] = "Marul\nRoka\nNane\nMaydanoz";
    size_t species_count = hidrolab_settings_custom_species_count();
    for (size_t i = 0; i < species_count; ++i) {
        const char *item = hidrolab_settings_custom_species_name(i);
        if (!item) continue;
        if (strlen(type_options) + strlen(item) + 2 >= sizeof(type_options)) break;
        strlcat(type_options, "\n", sizeof(type_options));
        strlcat(type_options, item, sizeof(type_options));
    }
    strlcat(type_options, "\nYeni tür ekle...", sizeof(type_options));
    lv_dropdown_set_options(s_slot_type_dd, type_options);
    lv_obj_set_style_text_font(s_slot_type_dd, &hidrolab_font_tr_14, 0);
    lv_obj_set_style_bg_color(s_slot_type_dd, lv_color_hex(0x102E3B), 0);
    lv_obj_set_style_text_color(s_slot_type_dd, lv_color_hex(C_SPACE_TEXT), 0);
    lv_obj_set_style_border_color(s_slot_type_dd, lv_color_hex(0x427F80), 0);
    lv_obj_t *type_list = lv_dropdown_get_list(s_slot_type_dd);
    lv_obj_set_style_text_font(type_list, &hidrolab_font_tr_14, 0);
    lv_obj_set_style_bg_color(type_list, lv_color_hex(0x123B42), 0);
    lv_obj_set_style_text_color(type_list, lv_color_hex(C_SPACE_TEXT), 0);
    lv_obj_add_event_cb(s_slot_type_dd, slot_type_event, LV_EVENT_VALUE_CHANGED, NULL);
    if (slot->active) {
        uint32_t selected = SLOT_FIXED_TYPES + (uint32_t)species_count;
        if (slot->type == HIDROLAB_SLOT_OTHER) {
            const char *current = hidrolab_settings_get()->slot_custom_types[s_edit_slot];
            for (size_t i = 0; i < species_count; ++i) {
                const char *item = hidrolab_settings_custom_species_name(i);
                if (item && strcmp(current, item) == 0) {
                    selected = SLOT_FIXED_TYPES + (uint32_t)i;
                    break;
                }
            }
        } else {
            for (uint32_t i = 0; i < SLOT_FIXED_TYPES; ++i) {
                if (slot->type == SLOT_TYPES[i]) {
                    selected = i;
                    break;
                }
            }
        }
        lv_dropdown_set_selected(s_slot_type_dd, selected);
    }

    s_slot_custom_label = label(modal, "Yeni tür adı", 388, 158,
                                 C_SPACE_MUTE, &hidrolab_font_tr_14);
    s_slot_custom_ta = lv_textarea_create(modal);
    lv_obj_set_pos(s_slot_custom_ta, 388, 180);
    lv_obj_set_size(s_slot_custom_ta, 388, 46);
    lv_textarea_set_one_line(s_slot_custom_ta, true);
    lv_textarea_set_max_length(s_slot_custom_ta, 16);
    lv_textarea_set_placeholder_text(s_slot_custom_ta, "Örn. Çilek");
    lv_obj_set_style_text_font(s_slot_custom_ta, &hidrolab_font_tr_14, 0);
    lv_obj_set_style_bg_color(s_slot_custom_ta, lv_color_hex(0x102E3B), 0);
    lv_obj_set_style_text_color(s_slot_custom_ta, lv_color_hex(C_SPACE_TEXT), 0);
    lv_obj_set_style_border_color(s_slot_custom_ta, lv_color_hex(0x74DDA7), 0);
    if (slot->active && slot->type == HIDROLAB_SLOT_OTHER) {
        lv_textarea_set_text(s_slot_custom_ta,
                             hidrolab_settings_get()->slot_custom_types[s_edit_slot]);
    } else if (slot->active && (slot->type == HIDROLAB_SLOT_BASIL ||
                                slot->type == HIDROLAB_SLOT_SPINACH)) {
        lv_textarea_set_text(s_slot_custom_ta, hidrolab_slot_type_name(slot->type));
    }
    lv_obj_add_event_cb(s_slot_custom_ta, slot_textarea_focus_event,
                        LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_slot_custom_ta, slot_textarea_focus_event,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_slot_custom_ta, slot_textarea_changed_event,
                        LV_EVENT_VALUE_CHANGED, NULL);
    s_slot_stage_label = label(modal, "Gelişim", 24, 158,
                               C_SPACE_MUTE, &hidrolab_font_tr_14);
    static const char *stages[3] = {"Tohum", "Filiz", "Bitki"};
    s_slot_stage_value = slot->active && slot->growth_stage <= HIDROLAB_STAGE_PLANT ?
                        slot->growth_stage : HIDROLAB_STAGE_SEED;
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *stage = button(modal, stages[i], 24 + i * 112, 180, 104, 46,
                                 0x254654);
        lv_obj_set_style_bg_color(stage, lv_color_hex(0x278160), LV_STATE_CHECKED);
        lv_obj_set_style_border_width(stage, 2, LV_STATE_CHECKED);
        lv_obj_set_style_border_color(stage, lv_color_hex(0x9FF0C0), LV_STATE_CHECKED);
        s_slot_stage_buttons[i] = stage;
        if (i == s_slot_stage_value) lv_obj_add_state(stage, LV_STATE_CHECKED);
        lv_obj_add_event_cb(stage, slot_stage_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }

    lv_obj_t *save = button(modal, LV_SYMBOL_OK "  Kaydet", 600, 102, 176, 48, 0x237B57);
    lv_obj_add_event_cb(save, save_slot_event, LV_EVENT_CLICKED, modal);
    if (slot->active) {
        s_slot_delete_button = button(modal, LV_SYMBOL_TRASH "  Yuvayı boşalt",
                                      24, 180, 340, 46, 0x653A43);
        lv_obj_add_event_cb(s_slot_delete_button, confirm_slot_delete_event,
                            LV_EVENT_CLICKED, modal);
    }

    s_slot_error = label(modal, "", 388, 236, 0xFFD18B, &hidrolab_font_tr_14);
    lv_obj_set_width(s_slot_error, 388);
    slot_type_event(NULL);
    lv_obj_t *kb = create_stock_keyboard(modal, s_slot_name_ta, 0, 268, 800, 212);
    s_slot_keyboard = kb;
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x081D2A), 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x244451), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(C_SPACE_TEXT), LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, slot_keyboard_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(kb, slot_keyboard_event, LV_EVENT_READY, NULL);
}

static void slot_event(lv_event_t *e)
{
    if (s_slot_modal || s_garden_detail_modal || s_observation_modal ||
        s_page_render_pending || s_page_render_retry) return;
    s_edit_slot = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (!hidrolab_slot_is_available(s_edit_slot)) return;
    if (hidrolab_settings_get()->slots[s_edit_slot].active) {
        open_garden_detail();
    } else {
        open_slot_editor();
    }
}

/* Seed and generic mature plants stay lightweight; the frequently seen sprout
 * uses the polished garden sprite shared with the journal. */
static lv_obj_t *garden_art_shape(lv_obj_t *parent, int x, int y, int w, int h,
                                  uint32_t color, int radius, int rotation)
{
    lv_obj_t *shape = background_shape(parent, x, y, w, h, color, radius,
                                       LV_OPA_COVER);
    if (rotation) lv_obj_set_style_transform_rotation(shape, rotation, 0);
    lv_obj_move_foreground(shape);
    return shape;
}

static void garden_plant_art(lv_obj_t *parent, const hidrolab_plant_slot_t *slot,
                              int x, int y)
{
    if (slot->growth_stage == HIDROLAB_STAGE_SEED) {
        garden_art_shape(parent, x + 10, y + 53, 52, 7, 0x173B38, 4, 0);
        garden_art_shape(parent, x + 17, y + 47, 39, 9, 0x42665A, 5, 0);
        garden_art_shape(parent, x + 25, y + 27, 27, 20, 0x815839, 11, 330);
        garden_art_shape(parent, x + 27, y + 28, 23, 17, 0xE9BA70, 10, 330);
        garden_art_shape(parent, x + 30, y + 30, 9, 4, 0xFFE4A2, 3, 330);
        garden_art_shape(parent, x + 40, y + 34, 3, 7, 0xAF7846, 2, 330);
        garden_art_shape(parent, x + 54, y + 22, 4, 4, 0xA9F2D0, 2, 0);
    } else if (slot->growth_stage == HIDROLAB_STAGE_SPROUT) {
        (void)image(parent, &hidrolab_img_garden_sprout, x + 4, y + 4);
    } else {
        const lv_image_dsc_t *art = &hidrolab_img_memory_marul;
        int art_x = x;
        int art_y = y;
        switch (slot->type) {
            case HIDROLAB_SLOT_ARUGULA: art = &hidrolab_img_memory_roka; break;
            case HIDROLAB_SLOT_MINT: art = &hidrolab_img_memory_nane; break;
            case HIDROLAB_SLOT_PARSLEY: art = &hidrolab_img_memory_maydanoz; break;
            case HIDROLAB_SLOT_SPINACH: art = &hidrolab_img_memory_ispanak; break;
            case HIDROLAB_SLOT_BASIL: art = &hidrolab_img_memory_nane; break;
            case HIDROLAB_SLOT_OTHER:
                art = &hidrolab_img_garden_other_plant;
                art_x += 4;
                art_y += 4;
                break;
            default: break;
        }
        (void)image(parent, art, art_x, art_y);
    }
}

static void garden_detail_close_event(lv_event_t *e)
{
    (void)e;
    if (!s_garden_detail_modal) return;
    lv_obj_t *modal = s_garden_detail_modal;
    s_garden_detail_modal = NULL;
    lv_obj_delete_async(modal);
}

static void garden_detail_edit_event(lv_event_t *e)
{
    if (!s_garden_detail_modal || s_page_render_pending || s_page_render_retry) return;
    garden_detail_close_event(e);
    open_slot_editor();
}

static void garden_detail_observe_event(lv_event_t *e)
{
    if (!s_garden_detail_modal || s_page_render_pending || s_page_render_retry) return;
    garden_detail_close_event(e);
    /* The selected slot remains s_edit_slot. The color picker saves only to
     * this plant, then the Garden page is rendered afresh. */
    /* Pass the original event so its user_data still selects leaf or root. */
    observation_open_event(e);
}

static void garden_observation_art(lv_obj_t *parent, bool leaf,
                                   uint32_t color, int x, int y, int size)
{
    /* Reuse the polished garden sprites so observations speak the same
     * visual language as Seed / Sprout / Plant.  LVGL recoloring keeps the
     * original volume, veins, highlights and organic outline while showing
     * the child's selected condition. */
    const lv_image_dsc_t *art = leaf ? &hidrolab_img_garden_leaf :
                                        &hidrolab_img_garden_root;
    int source_size = 56;
    int inset = (size - source_size) / 2;
    lv_obj_t *img = image(parent, art, x + inset, y + inset);
    if (size != source_size) {
        lv_image_set_scale(img, (uint32_t)(size * 256 / source_size));
    }

    bool natural_color = (leaf && color == LEAF_OBSERVATION_COLORS[1]) ||
                         (!leaf && color == ROOT_OBSERVATION_COLORS[1]);
    if (!natural_color) {
        lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
        lv_obj_set_style_image_recolor_opa(img,
            leaf ? LV_OPA_80 : LV_OPA_70, 0);
    }
}

static void open_garden_detail(void)
{
    if (!hidrolab_slot_is_available(s_edit_slot) || s_garden_detail_modal) return;
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    const hidrolab_plant_slot_t *slot = &cfg->slots[s_edit_slot];
    if (!slot->active) return;
    const hidrolab_slot_observation_t *entry = &cfg->slot_observations[s_edit_slot];
    bool observation_available = slot->growth_stage != HIDROLAB_STAGE_SEED;
    bool today = entry->date_key == current_daily_key();
    uint8_t leaf = today && entry->leaf <= 4 ? entry->leaf : 0;
    uint8_t root = today && entry->root <= 4 ? entry->root : 0;

    lv_obj_t *modal = lv_obj_create(s_screen);
    s_garden_detail_modal = modal;
    lv_obj_set_pos(modal, 0, 0);
    lv_obj_set_size(modal, 800, 480);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    set_bg_gradient(modal, 0x071E2C, 0x123B42);

    label(modal, "Bitki Günlüğü", 24, 14, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_t *name = label(modal, slot->name, 26, 58, 0x94DDBB,
                            &hidrolab_font_tr_14);
    lv_obj_set_width(name, 520);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    const char *type_name = slot->type == HIDROLAB_SLOT_OTHER ?
        cfg->slot_custom_types[s_edit_slot] : hidrolab_slot_type_name(slot->type);
    char type_line[56];
    snprintf(type_line, sizeof(type_line), "Tür: %s", type_name[0] ? type_name : "Diğer");
    lv_obj_t *type_label = label(modal, type_line, 26, 80, C_SPACE_MUTE,
                                  &hidrolab_font_tr_14);
    lv_obj_set_width(type_label, 520);
    lv_label_set_long_mode(type_label, LV_LABEL_LONG_DOT);
    lv_obj_t *back = button(modal, LV_SYMBOL_LEFT "  Bahçe", 650, 18,
                             126, 42, 0x284156);
    lv_obj_add_event_cb(back, garden_detail_close_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *portrait = panel(modal, 24, 108, 214, 270, 0x143E43);
    lv_obj_add_flag(portrait, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(portrait, garden_detail_edit_event, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_translate_y(portrait, 2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(portrait, lv_color_hex(0x20585A), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(portrait, lv_color_hex(0x477E77), 0);
    centered_label(portrait, "BENİM BİTKİM", 12, 18, 190, 0x94DDBB,
                   &hidrolab_font_tr_14);
    garden_plant_art(portrait, slot, 72, 72);
    uint32_t stage_color = C_CYAN;
    const char *stage = plant_stage(slot, &stage_color);
    centered_label(portrait, stage, 14, 171, 186, stage_color,
                   &hidrolab_font_tr_14);
    centered_label(portrait, "Değiştirmek için dokun", 12, 210, 190,
                   C_SPACE_MUTE, &hidrolab_font_tr_14);

    if (!observation_available) {
        lv_obj_t *locked = panel(modal, 256, 108, 512, 196, 0x173044);
        lv_obj_remove_flag(locked, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(locked, lv_color_hex(0x526879), 0);
        garden_plant_art(locked, slot, 28, 54);
        background_shape(locked, 119, 91, 54, 4, 0x587383, 2,
                         LV_OPA_COVER);
        centered_label(locked, LV_SYMBOL_RIGHT, 130, 80, 32, 0x90A9B7,
                       &lv_font_montserrat_14);
        hidrolab_plant_slot_t sprout_slot = *slot;
        sprout_slot.growth_stage = HIDROLAB_STAGE_SPROUT;
        garden_plant_art(locked, &sprout_slot, 171, 54);
        label(locked, "GÖZLEM FİLİZLENİNCE AÇILIR", 265, 31, 0xFFD18B,
              &hidrolab_font_tr_14);
        lv_obj_t *locked_text = label(locked,
            "Tohum aşamasında görünür yaprak ve kök yoktur. Bu yüzden renk kaydı yapılmaz. Bitki filizlendiğinde iki gözlem kartı da açılır.",
            265, 68, 0xC5D8E3, &hidrolab_font_tr_14);
        lv_obj_set_width(locked_text, 222);
        lv_label_set_long_mode(locked_text, LV_LABEL_LONG_WRAP);
    } else for (int i = 0; i < 2; ++i) {
        bool is_leaf = i == 0;
        uint8_t selected = is_leaf ? leaf : root;
        lv_obj_t *card = panel(modal, 256 + i * 264, 108, 248, 208,
                               selected ? 0x1D4545 : 0x173044);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(card,
            lv_color_hex(selected ? 0x70C99B : 0x3D566A), 0);
        lv_obj_set_style_translate_y(card, 2, LV_STATE_PRESSED);
        lv_obj_add_event_cb(card, garden_detail_observe_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(is_leaf ? 1U : 0U));
        uint32_t observation_color = selected ?
            (is_leaf ? LEAF_OBSERVATION_COLORS[selected] :
                       ROOT_OBSERVATION_COLORS[selected]) :
            (is_leaf ? 0x48B879 : 0xE8DFBD);
        lv_obj_t *swatch = panel(card, 80, 14, 88, 88, 0x0D2734);
        lv_obj_remove_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(swatch,
                                      lv_color_hex(observation_color), 0);
        garden_observation_art(swatch, is_leaf, observation_color,
                               11, 9, 66);
        centered_label(card, is_leaf ? "Yaprak" : "Kök", 12, 111, 224,
                       C_SPACE_TEXT, &hidrolab_font_tr_14);
        centered_label(card, selected ?
            (is_leaf ? LEAF_OBSERVATIONS[selected] : ROOT_OBSERVATIONS[selected]) :
            "Bugün gözlemle", 12, 139, 224,
            selected ? 0x99D9B7 : C_SPACE_MUTE, &hidrolab_font_tr_14);
        centered_label(card, selected ?
            (is_leaf ? LEAF_OBSERVATION_TIPS[selected] :
                       ROOT_OBSERVATION_TIPS[selected]) :
            "Dokun ve renk seç", 12, 164, 224,
            selected ? 0xFFD18B : 0xA9C9D5, &hidrolab_font_tr_14);
    }
    centered_label(modal, observation_available ?
                   "Yaprak ve kök renklerini her bitki için ayrı kaydet." :
                   "Günlük yalnız gözleyebildiğimiz verileri kaydeder.",
                   256, 412, 512, 0xA9C9D5, &hidrolab_font_tr_14);
}

static void build_garden(void)
{
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    char title[96];
    garden_title(title, sizeof(title));
    int plants = active_plant_count();
    int fully_observed = 0;
    int observable_plants = 0;
    int seed_count = 0;
    int first_empty_slot = -1;
    int pending_slot = -1;
    int concern_slot = -1;
    uint32_t today = current_daily_key();
    bool water_reward = (cfg->activity_game_mask & ACTIVITY_WATER_PATH) != 0;
    label(s_content, "Ay Bahçesi", 20, 12, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_t *subtitle = label(s_content, title, 22, 53, 0x94DDBB, &hidrolab_font_tr_14);
    lv_obj_set_width(subtitle, 548);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    (void)base_return_button(s_content, 660, 15, 120);
    lv_obj_t *count = label(s_content, "", 440, 24, 0xA8EACC, &hidrolab_font_tr_14);
    lv_label_set_text_fmt(count, "%d / %d bitki", plants, HIDROLAB_GARDEN_SLOT_COUNT);

    /* Five physical X-shaped openings: A1, A3, B2, C1, C3.
     * Old 9-slot NVS positions stay stable; the four removed positions
     * remain archived but cannot be planted/edited from the UI. */
    static const uint8_t ring_col[HIDROLAB_SLOT_COUNT] = {0, 1, 2, 2, 2, 1, 0, 0, 1};
    static const uint8_t ring_row[HIDROLAB_SLOT_COUNT] = {0, 0, 0, 1, 2, 2, 2, 1, 1};
    for (uint8_t j = 0; j < HIDROLAB_GARDEN_SLOT_COUNT; ++j) {
        uint8_t i = hidrolab_garden_slot_at(j);
        int x = 58 + ring_col[i] * 175;
        int y = 91 + ring_row[i] * 122;
        const hidrolab_plant_slot_t *slot = &cfg->slots[i];
        uint32_t stage_color = C_CYAN;
        const char *stage = plant_stage(slot, &stage_color);
        lv_obj_t *c = panel(s_content, x, y, 112, 112,
                            slot->active ? 0x203E3D : C_SURFACE);
        set_bg_gradient(c, slot->active ? 0x203E3D : C_SURFACE,
                        slot->active ? 0x172D30 : C_SURFACE);
        lv_obj_set_style_border_width(c, slot->active ? 2 : 1, 0);
        lv_obj_set_style_border_color(c,
            lv_color_hex(slot->active ? (water_reward ? 0x7BC9CF : 0x648D7B) :
                                        C_BORDER), 0);
        lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_translate_y(c, 2, LV_STATE_PRESSED);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, slot_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        if (slot->active) {
            garden_plant_art(c, slot, 20, 0);
            const hidrolab_slot_observation_t *entry = &cfg->slot_observations[i];
            if (slot->growth_stage == HIDROLAB_STAGE_SEED) {
                seed_count++;
            }
            else observable_plants++;
            bool leaf_done = entry->date_key == today && entry->leaf >= 1 &&
                             entry->leaf <= 4;
            bool root_done = entry->date_key == today && entry->root >= 1 &&
                             entry->root <= 4;
            if (slot->growth_stage != HIDROLAB_STAGE_SEED &&
                leaf_done && root_done) fully_observed++;
            if (slot->growth_stage != HIDROLAB_STAGE_SEED &&
                (!leaf_done || !root_done) && pending_slot < 0) {
                pending_slot = i;
            }
            if (slot->growth_stage != HIDROLAB_STAGE_SEED &&
                entry->date_key == today &&
                (entry->leaf >= 3 || entry->root >= 3) && concern_slot < 0) {
                concern_slot = i;
            }
            if (slot->growth_stage != HIDROLAB_STAGE_SEED) {
                for (int mark = 0; mark < 2; ++mark) {
                    bool done = mark == 0 ? leaf_done : root_done;
                    lv_obj_t *badge = panel(c, mark == 0 ? 5 : 85, 45, 22, 22,
                                            done ? (mark == 0 ? 0x23865B : 0xAB7747) :
                                                   0x294854);
                    lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_border_color(badge,
                        lv_color_hex(done ? 0xE4FFE8 : 0x5C7580), 0);
                    centered_label(badge, mark == 0 ? "Y" : "K", 0, 3, 20,
                                   done ? C_SPACE_TEXT : 0x91AAB5,
                                   &lv_font_montserrat_14);
                }
            }
        } else {
            if (first_empty_slot < 0) first_empty_slot = i;
            background_shape(c, 33, 21, 46, 46, 0x244B50,
                             LV_RADIUS_CIRCLE, LV_OPA_COVER);
            centered_label(c, LV_SYMBOL_PLUS, 33, 33, 46, 0xA8EACC,
                           &lv_font_montserrat_14);
        }
        lv_obj_t *slot_name = centered_label(c,
            slot->active ? slot->name : "Bitki ekle",
            8, slot->active ? 67 : 75, 96,
            slot->active ? C_SPACE_TEXT : C_SPACE_MUTE,
            &hidrolab_font_tr_14);
        lv_label_set_long_mode(slot_name, LV_LABEL_LONG_DOT);
        if (slot->active) {
            centered_label(c, stage, 11, 86, 90, stage_color,
                           &hidrolab_font_tr_14);
        }
        /* Real reservoir opening ID, not its legacy NVS array index. */
        lv_obj_t *position_tag = panel(c, 5, 5, 31, 22, 0x122F39);
        lv_obj_remove_flag(position_tag, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(position_tag, 6, 0);
        lv_obj_set_style_border_width(position_tag, 0, 0);
        centered_label(position_tag, hidrolab_slot_position_name(i),
                       0, 3, 31, 0xB9F3DD, &lv_font_montserrat_14);
    }
    lv_obj_t *side = panel(s_content, 598, 90, 182, 306, C_SURFACE);
    lv_obj_add_flag(side, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_translate_y(side, 2, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(side, lv_color_hex(C_BORDER), 0);
    centered_label(side, "Bahçe notu", 10, 15, 162, C_SPACE_MUTE, &hidrolab_font_tr_14);
    int guide_slot;
    if (plants == 0) {
        guide_slot = first_empty_slot;
    } else if (concern_slot >= 0) {
        guide_slot = concern_slot;
    } else if (observable_plants == 0) {
        guide_slot = -1;
    } else if (pending_slot >= 0) {
        guide_slot = pending_slot;
    } else {
        guide_slot = -1;
    }
    /* Keep useful garden guidance, without an emotion/status badge. */
    (void)profile_art(side, cfg->astronaut_profile, 31, 53, 256);
    if (guide_slot >= 0) {
        lv_obj_add_event_cb(side, slot_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)guide_slot);
    } else {
        lv_obj_add_event_cb(side, garden_base_event, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *speech = panel(side, 12, 189, 158, 101, C_SURFACE);
    lv_obj_remove_flag(speech, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(speech, 0, 0);
    char speech_text[128];
    if (plants == 0) {
        strlcpy(speech_text,
                "İlk bitkini eklemek için bana dokun.",
                sizeof(speech_text));
    } else if (concern_slot >= 0) {
        strlcpy(speech_text,
                "Bir bitkinin kaydına yeniden bakalım. Bana dokun.",
                sizeof(speech_text));
    } else if (observable_plants == 0) {
        strlcpy(speech_text,
                "Tohumların büyüyor. Filiz çıkınca gözlem yapacağız.",
                sizeof(speech_text));
    } else if (fully_observed < observable_plants) {
        snprintf(speech_text, sizeof(speech_text),
                 "%d bitki için gözlem eksik. Sıradakini açmak için bana dokun.",
                 observable_plants - fully_observed);
    } else if (seed_count > 0) {
        snprintf(speech_text, sizeof(speech_text),
                 "Gözlemler tamam. %d tohumun filizlenmesini bekliyoruz.",
                 seed_count);
    } else {
        strlcpy(speech_text,
                "Bugünkü gözlemler tamam. Ay üssüne dönelim mi?",
                sizeof(speech_text));
    }
    lv_obj_t *words = centered_label(speech, speech_text, 8, 8, 142,
                                      C_SPACE_TEXT, &hidrolab_font_tr_14);
    lv_label_set_long_mode(words, LV_LABEL_LONG_WRAP);
}

/* ---------------- Tasks ---------------- */
static void observation_close_event(lv_event_t *e)
{
    (void)e;
    if (s_observation_modal) {
        lv_obj_t *modal = s_observation_modal;
        s_observation_modal = NULL;
        s_observation_error = NULL;
        lv_obj_delete_async(modal);
    }
}

static void observation_select_event(lv_event_t *e)
{
    if (!s_observation_modal || s_page_render_pending || s_page_render_retry) return;
    uintptr_t packed = (uintptr_t)lv_event_get_user_data(e);
    bool leaf = (packed & 0x100U) != 0;
    uint8_t observation = (uint8_t)(packed & 0xFFU);
    esp_err_t err = hidrolab_settings_record_slot_observation(
        s_edit_slot, leaf, observation, current_daily_key());
    if (err == ESP_OK) {
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
        s_observation_modal = NULL;
        s_observation_error = NULL;
        s_reopen_garden_detail = true;
        request_page_render();
    } else {
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        if (s_observation_error) {
            lv_label_set_text(s_observation_error,
                              "Kaydedemedim. Seçimin burada; yeniden dene.");
        }
    }
}

static void observation_open_event(lv_event_t *e)
{
    if (s_observation_modal || !hidrolab_slot_is_available(s_edit_slot) ||
        !hidrolab_settings_get()->slots[s_edit_slot].active ||
        s_page_render_pending || s_page_render_retry) return;
    if (hidrolab_settings_get()->slots[s_edit_slot].growth_stage ==
        HIDROLAB_STAGE_SEED) {
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        return;
    }
    bool leaf = (uintptr_t)lv_event_get_user_data(e) != 0;
    const char **names = leaf ? LEAF_OBSERVATIONS : ROOT_OBSERVATIONS;
    const char **notes = leaf ? LEAF_OBSERVATION_TIPS : ROOT_OBSERVATION_TIPS;
    const uint32_t *colors = leaf ? LEAF_OBSERVATION_COLORS : ROOT_OBSERVATION_COLORS;
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    const hidrolab_slot_observation_t *entry = &cfg->slot_observations[s_edit_slot];
    bool recorded = entry->date_key == current_daily_key();
    uint8_t selected = recorded ? (leaf ? entry->leaf : entry->root) : 0;
    recorded = recorded && selected >= 1 && selected <= 4;

    s_observation_modal = lv_obj_create(s_screen);
    lv_obj_set_pos(s_observation_modal, 0, 0);
    lv_obj_set_size(s_observation_modal, 800, 480);
    lv_obj_remove_flag(s_observation_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_observation_modal, 0, 0);
    lv_obj_set_style_pad_all(s_observation_modal, 0, 0);
    set_bg_gradient(s_observation_modal, 0x091A2C, 0x102A3E);

    label(s_observation_modal, leaf ? "Yaprak Gözlemi" : "Kök Gözlemi",
          20, 18, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_t *intro = label(s_observation_modal,
          cfg->slots[s_edit_slot].name,
          22, 59, 0xB8CBD8, &hidrolab_font_tr_14);
    lv_obj_set_width(intro, 570);
    lv_label_set_long_mode(intro, LV_LABEL_LONG_DOT);
    lv_obj_t *close = button(s_observation_modal, LV_SYMBOL_LEFT "  Bahçe",
                             630, 18, 150, 42, 0x284156);
    lv_obj_add_event_cb(close, observation_close_event, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < 4; ++i) {
        int col = i % 2;
        int row = i / 2;
        bool current = recorded && selected == i + 1;
        lv_obj_t *choice = panel(s_observation_modal,
                                 20 + col * 390, 108 + row * 148,
                                 370, 132, current ? 0x253E45 : 0x173044);
        lv_obj_add_flag(choice, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_width(choice, 2, 0);
        lv_obj_set_style_border_color(choice,
                                      lv_color_hex(current ? 0xD8AF67 : 0x3D566A), 0);
        lv_obj_set_style_translate_y(choice, 2, LV_STATE_PRESSED);
        lv_obj_add_event_cb(choice, observation_select_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)((leaf ? 0x100U : 0U) | (uint32_t)(i + 1)));
        lv_obj_t *swatch = panel(choice, 15, 12, 112, 106, 0x0D2734);
        lv_obj_remove_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(swatch, 18, 0);
        lv_obj_set_style_border_width(swatch, 2, 0);
        lv_obj_set_style_border_color(swatch, lv_color_hex(colors[i + 1]), 0);
        garden_observation_art(swatch, leaf, colors[i + 1], 20, 13, 78);
        lv_obj_t *choice_title = label(choice, names[i + 1], 148, 18,
                                        colors[i + 1], &hidrolab_font_tr_14);
        lv_obj_set_width(choice_title, 204);
        lv_obj_t *note = label(choice, notes[i + 1], 148, 47, 0xB8CBD8,
                               &hidrolab_font_tr_14);
        lv_obj_set_width(note, 204);
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
        label(choice, current ? LV_SYMBOL_OK "  BUGÜNÜN KAYDI" : "Seçmek için dokun",
              148, 103, current ? 0xF0CD8E : 0x93AEBC, &hidrolab_font_tr_14);
    }
    s_observation_error = centered_label(s_observation_modal, "",
                   20, 407, 760, 0xFFD18B, &hidrolab_font_tr_14);
    centered_label(s_observation_modal,
                   "Seçtiğin renk yalnız bu bitkinin bugünkü günlüğüne kaydedilir.",
                   20, 438, 760, 0xB8CBD8, &hidrolab_font_tr_14);
}

static void chemistry_slider_style(lv_obj_t *slider, uint32_t accent)
{
    /* LVGL's default slider theme can paint the filled and empty portions
     * with different colors (especially under PRESSED/FOCUSED state).
     * Remove the theme styles altogether, then draw one neutral track and
     * one solid accent-colored thumb. This does not change slider values. */
    lv_obj_remove_style_all(slider);

    /* Track: same color along its complete length. */
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x263E54), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(slider, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_MAIN);

    /* Suppress the entire 'filled' bar; only thumb position shows value. */
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(slider, LV_GRAD_DIR_NONE, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_INDICATOR);

    /* Thumb: opaque, non-gradient; consistent in idle and touched states. */
    lv_obj_set_style_bg_color(slider, lv_color_hex(accent), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_grad_dir(slider, LV_GRAD_DIR_NONE, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 7, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(0xE8F2F6), LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
}

static void chemistry_update_feedback(void)
{
    if (!s_chem_feedback_card || !s_chem_feedback_label) return;
    const char *message;
    uint32_t color;
    if (!s_chem_ph_touched) {
        message = "Önce pH değerini seç.";
        color = C_PURPLE;
    } else if (!s_chem_ec_touched) {
        message = "Şimdi EC değerini seç.";
        color = C_BLUE;
    } else {
        message = "Hazır! Kaydedebilirsin.";
        color = 0x63D69A;
    }
    lv_label_set_text(s_chem_feedback_label, message);
    lv_obj_set_style_text_color(s_chem_feedback_label, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(s_chem_feedback_card, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_chem_feedback_card,
                              lv_color_hex(s_chem_ph_touched &&
                                           s_chem_ec_touched ? 0x123F36 :
                                           0x102D43), 0);
}

static void chemistry_slider_value_event(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    bool ph = slider == s_ph_slider;
    int32_t step = ph ? 5 : 10;
    int32_t value = lv_slider_get_value(slider);
    int32_t snapped = ((value + step / 2) / step) * step;
    if (snapped != value) lv_slider_set_value(slider, snapped, LV_ANIM_OFF);

    if (ph) {
        s_chem_ph_touched = true;
        s_edit_ph_x100 = (uint16_t)snapped;
        if (s_ph_value_label) {
            lv_label_set_text_fmt(s_ph_value_label, "%u.%02u",
                                  (unsigned)(s_edit_ph_x100 / 100U),
                                  (unsigned)(s_edit_ph_x100 % 100U));
        }
    } else {
        s_chem_ec_touched = true;
        s_edit_ec_x100 = (uint16_t)snapped;
        if (s_ec_value_label) {
            lv_label_set_text_fmt(s_ec_value_label, "%u",
                                  (unsigned)s_edit_ec_x100);
        }
    }
    chemistry_update_feedback();
}


static void chemistry_save_event(lv_event_t *e)
{
    (void)e;
    if (!s_chem_ph_touched || !s_chem_ec_touched) {
        chemistry_update_feedback();
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        return;
    }
    esp_err_t save_err = hidrolab_settings_record_daily_chemistry(
        s_edit_ph_x100, s_edit_ec_x100, current_daily_key());
    if (save_err != ESP_OK) {
        if (s_chem_error) {
            lv_label_set_text(s_chem_error, "Kaydedemedim. Yeniden deneyelim.");
            lv_obj_set_style_text_color(s_chem_error, lv_color_hex(C_RED), 0);
        }
        if (s_chem_feedback_card) {
            lv_obj_set_style_border_color(s_chem_feedback_card,
                                          lv_color_hex(C_RED), 0);
        }
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        return;
    }
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
    s_ph_slider = NULL;
    s_ec_slider = NULL;
    s_ph_value_label = NULL;
    s_ec_value_label = NULL;
    s_chem_feedback_card = NULL;
    s_chem_feedback_label = NULL;
    s_chem_error = NULL;
    s_chem_ph_touched = false;
    s_chem_ec_touched = false;
    request_page_render();
}


static void chemistry_graphs_close_event(lv_event_t *e)
{
    lv_obj_t *modal = (lv_obj_t *)lv_event_get_user_data(e);
    if (modal) lv_obj_delete_async(modal);
}

static void chemistry_date_caption(uint32_t key, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (key >= 20240101U && key <= 20991231U) {
        snprintf(out, out_size, "%02u/%02u",
                 (unsigned)(key % 100U), (unsigned)((key / 100U) % 100U));
    } else {
        snprintf(out, out_size, "G%u", (unsigned)(key % 100U));
    }
}

/* One row per (date, planted slot); chemistry is shared by date, but
 * each plant's manually selected colors are recorded independently. */
typedef struct {
    uint32_t date_key;
    int8_t slot_index; /* -1 when only chemistry exists for that day */
    uint8_t leaf;
    uint8_t root;
    uint16_t ph_x100;
    uint16_t ec_us;
    bool has_chemistry;
} chemistry_history_row_t;

#define CHEM_TABLE_ROW_MAX (HIDROLAB_OBS_HISTORY_MAX + HIDROLAB_CHEM_HISTORY_MAX)

static void chemistry_history_table_header(lv_obj_t *parent)
{
    static const char *titles[] = {"Tarih", "Yuva", "pH", "EC", "Yaprak rengi", "Kök rengi"};
    static const int offsets[] = {12, 99, 166, 235, 337, 537};
    lv_obj_t *head = panel(parent, 22, 86, 756, 42, 0x203E59);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    for (int j = 0; j < 6; ++j) {
        label(head, titles[j], offsets[j], 11, 0xE9F7FF, &hidrolab_font_tr_14);
    }
}

static void chemistry_history_table_row(lv_obj_t *parent,
                                        const chemistry_history_row_t *entry,
                                        int row_index)
{
    const int y = row_index * 45;
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_pos(r, 1, y);
    lv_obj_set_size(r, 748, 43);
    lv_obj_set_style_radius(r, 7, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex((row_index % 2) ? 0x1C364E : 0x243F56), 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    char date[18];
    chemistry_date_caption(entry->date_key, date, sizeof(date));
    label(r, date, 12, 12, C_SPACE_TEXT, &hidrolab_font_tr_14);
    lv_obj_t *slot = label(r, "-", 99, 12, C_SPACE_MUTE, &hidrolab_font_tr_14);
    if (entry->slot_index >= 0) {
        lv_label_set_text(slot, hidrolab_slot_position_name((uint8_t)entry->slot_index));
    }
    lv_obj_t *ph = label(r, "-", 166, 12, C_SPACE_TEXT, &hidrolab_font_tr_14);
    lv_obj_t *ec = label(r, "-", 235, 12, C_SPACE_TEXT, &hidrolab_font_tr_14);
    if (entry->has_chemistry) {
        lv_label_set_text_fmt(ph, "%u.%02u", (unsigned)(entry->ph_x100 / 100U),
                              (unsigned)(entry->ph_x100 % 100U));
        lv_label_set_text_fmt(ec, "%u", (unsigned)entry->ec_us);
    }
    const char *leaf_name = entry->leaf >= 1 && entry->leaf <= 4 ?
        LEAF_OBSERVATIONS[entry->leaf] : "-";
    const char *root_name = entry->root >= 1 && entry->root <= 4 ?
        ROOT_OBSERVATIONS[entry->root] : "-";
    lv_obj_t *leaf_label = label(r, leaf_name, 337, 12,
        entry->leaf ? 0xA8EACC : C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_set_width(leaf_label, 194);
    lv_label_set_long_mode(leaf_label, LV_LABEL_LONG_DOT);
    lv_obj_t *root_label = label(r, root_name, 537, 12,
        entry->root ? 0xA8EACC : C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_set_width(root_label, 200);
    lv_label_set_long_mode(root_label, LV_LABEL_LONG_DOT);
}

static void chemistry_graphs_open_event(lv_event_t *e)
{
    (void)e;
    lv_obj_t *modal = lv_obj_create(s_screen);
    lv_obj_set_pos(modal, 0, 0);
    lv_obj_set_size(modal, 800, 480);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    set_bg_gradient(modal, C_SPACE_TOP, C_SPACE_BOT);
    decorate_space_background(modal, 480);
    label(modal, "Araştırma Geçmişi", 20, 12, C_SPACE_TEXT, &hidrolab_font_tr_32);
    label(modal, "pH ve EC: uS/cm; renkler gözlem kayıtlarıdır.",
          22, 53, C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_t *close = button(modal, LV_SYMBOL_CLOSE, 734, 14, 46, 40, C_NAVY2);
    lv_obj_add_event_cb(close, chemistry_graphs_close_event, LV_EVENT_CLICKED, modal);

    chemistry_history_row_t rows[CHEM_TABLE_ROW_MAX] = {0};
    size_t row_count = 0;
    const size_t obs_count = hidrolab_settings_observation_history_count();
    for (size_t i = 0; i < obs_count && row_count < CHEM_TABLE_ROW_MAX; ++i) {
        hidrolab_observation_history_entry_t entry;
        if (!hidrolab_settings_observation_history_get(i, &entry) ||
            !hidrolab_slot_is_available(entry.slot_index)) continue;
        rows[row_count++] = (chemistry_history_row_t) {
            .date_key = entry.date_key, .slot_index = (int8_t)entry.slot_index,
            .leaf = entry.leaf, .root = entry.root,
        };
    }
    size_t chem_count = hidrolab_settings_chemistry_history_count();
    for (size_t i = 0; i < chem_count; ++i) {
        hidrolab_chemistry_sample_t chem;
        if (!hidrolab_settings_chemistry_history_get(i, &chem)) continue;
        bool matched_date = false;
        for (size_t j = 0; j < row_count; ++j) {
            if (rows[j].date_key == chem.date_key) {
                rows[j].ph_x100 = chem.ph_x100;
                rows[j].ec_us = chem.ec_us;
                rows[j].has_chemistry = true;
                matched_date = true;
            }
        }
        if (!matched_date && row_count < CHEM_TABLE_ROW_MAX) {
            rows[row_count++] = (chemistry_history_row_t) {
                .date_key = chem.date_key, .slot_index = -1,
                .ph_x100 = chem.ph_x100, .ec_us = chem.ec_us,
                .has_chemistry = true,
            };
        }
    }
    /* Newest date first; within one date, order by garden slot. */
    for (size_t i = 1; i < row_count; ++i) {
        chemistry_history_row_t key = rows[i];
        size_t j = i;
        while (j > 0 && (rows[j - 1].date_key < key.date_key ||
               (rows[j - 1].date_key == key.date_key &&
                rows[j - 1].slot_index > key.slot_index))) {
            rows[j] = rows[j - 1];
            --j;
        }
        rows[j] = key;
    }
    chemistry_history_table_header(modal);
    lv_obj_t *scroll = lv_obj_create(modal);
    lv_obj_set_pos(scroll, 22, 130);
    lv_obj_set_size(scroll, 756, 310);
    lv_obj_set_style_pad_all(scroll, 2, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
    if (row_count == 0) {
        centered_label(scroll, "Henüz ölçüm veya renk kaydı yok.",
                       20, 110, 700, C_SPACE_MUTE, &hidrolab_font_tr_14);
    } else {
        /* Content height is determined by the last child; only the table scrolls. */
        for (size_t i = 0; i < row_count; ++i) {
            chemistry_history_table_row(scroll, &rows[i], (int)i);
        }
    }
    label(modal, "- : Bu tarihte değer girilmedi.  |  Aşağı kaydırarak eski kayıtlara bak.",
          24, 450, C_SPACE_MUTE, &hidrolab_font_tr_14);
}

static lv_obj_t *academy_art(lv_obj_t *parent, int x, int y, int size,
                             int kind, uint32_t accent)
{
    lv_obj_t *stage = panel(parent, x, y, size, size, 0x102B42);
    lv_obj_remove_flag(stage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(stage, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(stage, 2, 0);
    lv_obj_set_style_border_color(stage, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_width(stage, 8, 0);
    lv_obj_set_style_shadow_color(stage, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_opa(stage, LV_OPA_20, 0);

    int c = size / 2;
    int unit = size / 12;
    background_shape(stage, c - unit / 2, unit * 2, unit, unit,
                     0xF5FBFF, LV_RADIUS_CIRCLE, LV_OPA_COVER);
    background_shape(stage, unit * 2, c - unit / 2, unit, unit,
                     accent, LV_RADIUS_CIRCLE, LV_OPA_COVER);
    background_shape(stage, size - unit * 3, c + unit * 2, unit, unit,
                     0xFFD16F, LV_RADIUS_CIRCLE, LV_OPA_COVER);

    if (kind == 0) {
        background_shape(stage, c - 2, unit * 3, 4, unit * 4,
                         0x75D19A, 2, LV_OPA_COVER);
        background_shape(stage, c - unit * 2, unit * 3, unit * 2, unit,
                         0x75D19A, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        background_shape(stage, c, unit * 4, unit * 2, unit,
                         0x75D19A, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        background_shape(stage, c - unit * 3, unit * 7, unit * 6, unit * 2,
                         0x8D6549, unit, LV_OPA_COVER);
        for (int i = -1; i <= 1; ++i) {
            background_shape(stage, c + i * unit - 1, unit * 8, 3,
                             unit * (2 + (i == 0)), 0xE8F1D7, 2,
                             LV_OPA_COVER);
        }
    } else if (kind == 1) {
        lv_obj_t *core = panel(stage, c - unit * 3, c - unit * 3,
                               unit * 6, unit * 6, 0x233853);
        lv_obj_remove_flag(core, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(core, lv_color_hex(accent), 0);
        background_shape(core, unit * 2, unit * 2, unit * 2, unit * 2,
                         accent, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        background_shape(core, unit, unit * 4, unit, unit,
                         0x75D19A, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        background_shape(core, unit * 4, unit, unit, unit,
                         0xFFD16F, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        background_shape(core, unit * 4, unit * 4, unit, unit,
                         0xB99AF4, LV_RADIUS_CIRCLE, LV_OPA_COVER);
    } else if (kind == 2) {
        lv_obj_t *core = panel(stage, c - unit * 3, c - unit * 3,
                               unit * 6, unit * 6, 0x173C3E);
        lv_obj_remove_flag(core, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(core, lv_color_hex(accent), 0);
        lv_obj_t *ec = centered_label(core, "EC", 0, 0, unit * 6,
                                      accent, &hidrolab_font_tr_32);
        lv_obj_center(ec);
        background_shape(stage, unit * 2, c - 1, unit * 2, 3,
                         accent, 2, LV_OPA_COVER);
        background_shape(stage, size - unit * 4, c - 1, unit * 2, 3,
                         accent, 2, LV_OPA_COVER);
    } else if (kind == 3) {
        lv_obj_t *sun = background_shape(stage, c - unit * 2, unit * 2,
                                          unit * 4, unit * 4, 0xFFD16F,
                                          LV_RADIUS_CIRCLE, LV_OPA_COVER);
        lv_obj_set_style_shadow_width(sun, 8, 0);
        lv_obj_set_style_shadow_color(sun, lv_color_hex(0xFFD16F), 0);
        background_shape(stage, c - 2, unit * 6, 4, unit * 3,
                         0x76D29A, 2, LV_OPA_COVER);
        background_shape(stage, c - unit * 3, unit * 7, unit * 3, unit * 2,
                         0x76D29A, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        background_shape(stage, c, unit * 7, unit * 3, unit * 2,
                         0x76D29A, LV_RADIUS_CIRCLE, LV_OPA_COVER);
    } else {
        lv_obj_t *drop = centered_label(stage, LV_SYMBOL_TINT, 0, 0, size,
                                         accent, &hidrolab_font_tr_32);
        lv_obj_center(drop);
        background_shape(stage, c - unit * 3, unit * 8, unit * 6, unit,
                         0x75D19A, LV_RADIUS_CIRCLE, LV_OPA_COVER);
    }
    return stage;
}

/* The pilot lesson uses small scientific scenes instead of a decorative
 * topic badge.  Everything is drawn with LVGL primitives, so it remains fast
 * on the ESP32 and adds no bitmap payload to the nearly-full app partition. */
static lv_obj_t *academy_pilot_scene(lv_obj_t *parent, int x, int y,
                                     int width, int height, uint8_t step)
{
    lv_obj_t *scene = panel(parent, x, y, width, height, 0x0C2637);
    lv_obj_remove_flag(scene, LV_OBJ_FLAG_CLICKABLE);
    set_bg_gradient(scene, 0x153B4A, 0x0A2233);
    lv_obj_set_style_border_color(scene, lv_color_hex(0x77D99B), 0);
    lv_obj_set_style_border_width(scene, 2, 0);
    lv_obj_set_style_radius(scene, 20, 0);

    if (step == 0) {
        centered_label(scene, "OLAY YERİ", 8, 10, width - 16, 0x9CE7BB,
                       &lv_font_montserrat_14);
        /* Dim sun: light exists, but the dry sponge is the direct clue. */
        lv_obj_t *sun = background_shape(scene, width - 48, 40, 24, 24,
                                          0xFFD16F, LV_RADIUS_CIRCLE,
                                          LV_OPA_COVER);
        lv_obj_set_style_shadow_width(sun, 8, 0);
        lv_obj_set_style_shadow_color(sun, lv_color_hex(0xFFD16F), 0);
        background_shape(scene, width / 2 - 3, 70, 6, 78,
                         0x6EC48B, 3, LV_OPA_COVER);
        /* Drooping leaves intentionally point down. */
        background_shape(scene, width / 2 - 48, 82, 48, 22,
                         0x68C38B, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        background_shape(scene, width / 2 + 2, 105, 48, 22,
                         0x68C38B, LV_RADIUS_CIRCLE, LV_OPA_COVER);
        background_shape(scene, width / 2 - 46, 100, 4, 30,
                         0x68C38B, 2, LV_OPA_COVER);
        background_shape(scene, width / 2 + 43, 121, 4, 26,
                         0x68C38B, 2, LV_OPA_COVER);
        background_shape(scene, width / 2 - 49, 148, 98, 30,
                         0x9A704F, 10, LV_OPA_COVER);
        for (int i = 0; i < 5; ++i) {
            background_shape(scene, width / 2 - 37 + i * 18, 156,
                             10, 3, 0xD7BE8D, 2, LV_OPA_COVER);
        }
        centered_label(scene, "KURU SÜNGER", 12, height - 42, width - 24,
                       0xFFD18B, &lv_font_montserrat_14);
    } else if (step == 1) {
        centered_label(scene, "ADİL DENEY", 8, 10, width - 16, 0x9CE7BB,
                       &lv_font_montserrat_14);
        centered_label(scene, "AYNI TÜR  •  AYNI SU", 8, 36, width - 16,
                       0xB8D4E6, &lv_font_montserrat_14);
        for (int i = 0; i < 2; ++i) {
            int cx = width / 4 + i * width / 2;
            lv_obj_t *lamp = background_shape(scene, cx - 12, 67, 24, 24,
                                               i == 0 ? 0xFFE482 : 0x6D7892,
                                               LV_RADIUS_CIRCLE,
                                               LV_OPA_COVER);
            if (i == 0) {
                lv_obj_set_style_shadow_width(lamp, 10, 0);
                lv_obj_set_style_shadow_color(lamp,
                                               lv_color_hex(0xFFE482), 0);
            }
            background_shape(scene, cx - 2, 108, 4, 45,
                             0x75D19A, 2, LV_OPA_COVER);
            background_shape(scene, cx - 27, 116, 25, 13,
                             0x75D19A, LV_RADIUS_CIRCLE, LV_OPA_COVER);
            background_shape(scene, cx + 2, 126, 25, 13,
                             0x75D19A, LV_RADIUS_CIRCLE, LV_OPA_COVER);
            background_shape(scene, cx - 31, 153, 62, 22,
                             0x8D6549, 8, LV_OPA_COVER);
            centered_label(scene, i == 0 ? "ÇOK IŞIK" : "AZ IŞIK",
                           cx - width / 4, 184, width / 2,
                           i == 0 ? 0xFFE482 : 0xA9B7C8,
                           &lv_font_montserrat_14);
        }
        background_shape(scene, width / 2 - 1, 62, 2, 142,
                         0x315066, 1, LV_OPA_COVER);
    } else {
        centered_label(scene, "BİTKİ GÜNLÜĞÜ", 8, 10, width - 16,
                       0x9CE7BB, &lv_font_montserrat_14);
        static const char *row_title[3] = {"TARİH", "YAPRAK", "SU YOLU"};
        static const char *row_value[3] = {"14 EYL", "DİRİ", "AÇIK"};
        for (int i = 0; i < 3; ++i) {
            int ry = 46 + i * 49;
            lv_obj_t *row = panel(scene, 10, ry, width - 20, 39, 0x15364A);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_border_width(row, 0, 0);
            label(row, row_title[i], 10, 7, 0x91AFC2,
                  &lv_font_montserrat_14);
            centered_label(row, row_value[i], width - 98, 7, 68,
                           i == 0 ? 0xCFE8F4 : 0x8FE0AC,
                           &lv_font_montserrat_14);
        }
        centered_label(scene, "AYNI BİTKİYİ YİNE GÖZLE", 8, height - 38,
                       width - 16, 0xFFD18B, &lv_font_montserrat_14);
    }
    return scene;
}

static void tasks_academy_event(lv_event_t *e)
{
    (void)e;
    s_tasks_academy_view = true;
    s_academy_category = -1;
    s_academy_lesson = -1;
    s_academy_step = 0;
    s_academy_done_mask = 0;
    s_academy_choice = -1;
    s_academy_question_visible = false;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    request_page_render();
}

static void academy_tasks_back_event(lv_event_t *e)
{
    (void)e;
    if (s_academy_lesson >= 0) {
        s_academy_lesson = -1;
        s_academy_step = 0;
        s_academy_done_mask = 0;
        s_academy_choice = -1;
        s_academy_question_visible = false;
    } else if (s_academy_category >= 0) {
        s_academy_category = -1;
    } else {
        s_tasks_academy_view = false;
    }
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    request_page_render();
}

static void academy_lesson_event(lv_event_t *e)
{
    int lesson = (int)(uintptr_t)lv_event_get_user_data(e);
    if (lesson < 0 || lesson >= ACADEMY_LESSON_COUNT) return;
    s_academy_lesson = lesson;
    s_academy_step = 0;
    s_academy_done_mask = 0;
    s_academy_choice = -1;
    s_academy_question_visible = false;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    request_page_render();
}

static void academy_step_event(lv_event_t *e)
{
    int direction = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_academy_lesson < 0 || s_academy_lesson >= ACADEMY_LESSON_COUNT) return;
    if (direction < 0) {
        if (s_academy_step > 0) s_academy_step--;
        else {
            s_academy_lesson = -1;
            s_academy_done_mask = 0;
        }
        s_academy_choice = -1;
        s_academy_question_visible = false;
    } else if (!(s_academy_done_mask & (1U << s_academy_step))) {
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        return;
    } else if (s_academy_step < 2) {
        s_academy_step++;
        s_academy_choice = -1;
        s_academy_question_visible = false;
    } else {
        s_academy_lesson = -1;
        s_academy_step = 0;
        s_academy_done_mask = 0;
        s_academy_choice = -1;
        s_academy_question_visible = false;
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
        request_page_render();
        return;
    }
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    request_page_render();
}

static void academy_choice_event(lv_event_t *e)
{
    int choice = (int)(uintptr_t)lv_event_get_user_data(e);
    if (s_academy_lesson < 0 || s_academy_lesson >= ACADEMY_LESSON_COUNT ||
        choice < 0 || choice > 2 || !s_academy_question_visible) return;
    uint8_t step_bit = (uint8_t)(1U << s_academy_step);
    if (s_academy_done_mask & step_bit) return;
    const academy_interaction_t *interaction =
        &ACADEMY_INTERACTIONS[s_academy_lesson][s_academy_step];
    s_academy_choice = (int8_t)choice;
    if (choice == interaction->correct) {
        s_academy_done_mask |= step_bit;
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_CORRECT);
    } else {
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
    }
    request_page_render();
}

static void academy_begin_question_event(lv_event_t *e)
{
    (void)e;
    if (s_academy_lesson < 0 || s_academy_lesson >= ACADEMY_LESSON_COUNT) return;
    s_academy_question_visible = true;
    s_academy_choice = -1;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    request_page_render();
}

static void academy_return_to_explanation_event(lv_event_t *e)
{
    (void)e;
    s_academy_question_visible = false;
    s_academy_choice = -1;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    request_page_render();
}

static void academy_apply_garden_event(lv_event_t *e)
{
    (void)e;
    if (s_academy_lesson != 6 || s_academy_step != 2 ||
        !(s_academy_done_mask & (1U << 2))) return;
    s_tasks_academy_view = false;
    s_academy_category = -1;
    s_academy_lesson = -1;
    s_academy_step = 0;
    s_academy_done_mask = 0;
    s_academy_choice = -1;
    s_academy_question_visible = false;
    s_page = PAGE_GARDEN;
    s_base_home = false;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
    request_page_render();
}

static void academy_category_event(lv_event_t *e)
{
    int category = (int)(uintptr_t)lv_event_get_user_data(e);
    if (category < 0 || category >= ACADEMY_CATEGORY_COUNT) return;
    s_academy_category = category;
    s_academy_lesson = -1;
    s_academy_step = 0;
    s_academy_done_mask = 0;
    s_academy_choice = -1;
    s_academy_question_visible = false;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    request_page_render();
}

static void build_academy_lesson(void)
{
    const academy_lesson_t *lesson = &ACADEMY_LESSONS[s_academy_lesson];
    const academy_interaction_t *interaction =
        &ACADEMY_INTERACTIONS[s_academy_lesson][s_academy_step];
    bool step_done = (s_academy_done_mask & (1U << s_academy_step)) != 0;
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    label(s_content, "HybridLab Akademi", 20, 12, C_SPACE_TEXT,
          &hidrolab_font_tr_32);
    label(s_content, lesson->title, 22, 53, lesson->accent,
          &hidrolab_font_tr_14);
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Dersler",
                            520, 15, 126, 38, 0x29465E);
    lv_obj_add_event_cb(back, academy_tasks_back_event, LV_EVENT_CLICKED, NULL);
    (void)base_return_button(s_content, 660, 15, 120);

    if (!s_academy_question_visible) {
        lv_obj_t *lesson_card = panel(s_content, 20, 88, 760, 292, 0x142C42);
        lv_obj_remove_flag(lesson_card, LV_OBJ_FLAG_CLICKABLE);
        set_bg_gradient(lesson_card, 0x173A50, 0x0C2538);
        lv_obj_set_style_border_color(lesson_card, lv_color_hex(lesson->accent), 0);
        background_orb(lesson_card, -28, 80, 250, lesson->accent, LV_OPA_10);
        background_shape(lesson_card, 18, 247, 198, 16, 0x07192A,
                         LV_RADIUS_CIRCLE, LV_OPA_40);
        (void)profile_art(lesson_card, cfg->astronaut_profile, 28, 50, 400);

        /* A single bright speech surface reads as a character conversation,
         * not another dashboard nested inside the page. */
        background_shape(lesson_card, 202, 106, 30, 28, 0xEAF7FF,
                         7, LV_OPA_COVER);
        lv_obj_t *speech = panel(lesson_card, 220, 24, 354, 224, 0xEAF7FF);
        lv_obj_remove_flag(speech, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(speech, lv_color_hex(lesson->accent), 0);
        label(speech, "BİLİYOR MUSUN?", 18, 15, 0x477087,
              &lv_font_montserrat_14);
        label(speech, lesson->step_title[s_academy_step], 18, 43,
              0x16354A, &hidrolab_font_tr_32);
        lv_obj_t *explanation = label(speech,
            lesson->step_body[s_academy_step], 18, 91, 0x294D62,
            &hidrolab_font_tr_14);
        lv_obj_set_width(explanation, 318);
        lv_label_set_long_mode(explanation, LV_LABEL_LONG_WRAP);
        background_shape(speech, 18, 191, 204, 4, lesson->accent,
                         2, LV_OPA_COVER);
        label(speech, "Şimdi sıra sende!", 18, 199, 0x477087,
              &hidrolab_font_tr_14);

        if (s_academy_lesson == 6) {
            academy_pilot_scene(lesson_card, 590, 22, 148, 222,
                                s_academy_step);
        } else {
            academy_art(lesson_card, 602, 55, 116,
                        lesson->art_kind, lesson->accent);
            lv_obj_t *topic_chip = panel(lesson_card, 590, 190, 140, 34,
                                         0x0B2134);
            lv_obj_remove_flag(topic_chip, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_border_width(topic_chip, 0, 0);
            centered_label(topic_chip, lesson->grade, 4, 9, 132,
                           lesson->accent, &lv_font_montserrat_14);
        }
        for (int i = 0; i < 3; ++i) {
            background_shape(lesson_card, 621 + i * 28, 250, 18, 5,
                             i <= s_academy_step ? lesson->accent : 0x385269,
                             3, LV_OPA_COVER);
        }

        lv_obj_t *previous = button(s_content,
            s_academy_step == 0 ? LV_SYMBOL_LEFT "  Dersler" :
                                  LV_SYMBOL_LEFT "  Önceki konu",
            20, 398, 230, 44, 0x29465E);
        lv_obj_add_event_cb(previous, academy_step_event, LV_EVENT_CLICKED,
                            (void *)(intptr_t)-1);
        lv_obj_t *ready = button(s_content,
            "Soruyu aç  " LV_SYMBOL_RIGHT,
            500, 398, 280, 44, lesson->accent);
        lv_obj_add_event_cb(ready, academy_begin_question_event,
                            LV_EVENT_CLICKED, NULL);
        return;
    }

    lv_obj_t *card = panel(s_content, 20, 88, 760, 292, 0x142C42);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    set_bg_gradient(card, 0x18354D, 0x102438);
    lv_obj_set_style_border_color(card, lv_color_hex(lesson->accent), 0);
    lv_obj_t *art = s_academy_lesson == 6 ?
        academy_pilot_scene(card, 22, 38, 178, 226, s_academy_step) :
        academy_art(card, 30, 60, 160, lesson->art_kind, lesson->accent);
    if (step_done || s_academy_choice >= 0) {
        uint32_t result_color = step_done ? 0x2D8B66 : 0xA86642;
        lv_obj_t *result = panel(art,
                                 s_academy_lesson == 6 ? 132 : 112,
                                 s_academy_lesson == 6 ? 8 : 112,
                                 38, 38, result_color);
        lv_obj_remove_flag(result, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(result, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(result, 0, 0);
        lv_obj_t *result_mark = centered_label(result,
            step_done ? LV_SYMBOL_OK : LV_SYMBOL_REFRESH,
            0, 0, 38, 0xFFFFFF, &lv_font_montserrat_14);
        lv_obj_center(result_mark);
    }
    lv_obj_t *step_title = label(card, "", 220, 17, C_SPACE_TEXT,
                                 &hidrolab_font_tr_14);
    lv_label_set_text_fmt(step_title, "ADIM %u / 3  •  %s",
                          (unsigned)s_academy_step + 1U,
                          lesson->step_title[s_academy_step]);
    lv_obj_set_width(step_title, 340);
    lv_label_set_long_mode(step_title, LV_LABEL_LONG_WRAP);
    lv_obj_t *grade = panel(card, 570, 12, 160, 30, 0x0B2134);
    lv_obj_remove_flag(grade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(grade, 0, 0);
    centered_label(grade, lesson->grade, 0, 7, 160, lesson->accent,
                   &lv_font_montserrat_14);

    lv_obj_t *mission = panel(card, 220, 51, 510, 52, 0x0B2134);
    lv_obj_remove_flag(mission, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(mission, 0, 0);
    lv_obj_t *mission_text = centered_label(mission, interaction->prompt,
                                             8, 7, 494, lesson->accent,
                                             &hidrolab_font_tr_14);
    lv_label_set_long_mode(mission_text, LV_LABEL_LONG_WRAP);

    for (int i = 0; i < 3; ++i) {
        uint32_t choice_color = 0x29465E;
        if (step_done && i == interaction->correct) choice_color = 0x2D8B66;
        else if (s_academy_choice == i) choice_color = 0xA86642;
        lv_obj_t *choice = button(card, interaction->choice[i],
                                  220, 109 + i * 46, 510, 40,
                                  choice_color);
        lv_obj_set_style_border_width(choice,
            (step_done && i == interaction->correct) || s_academy_choice == i ? 2 : 0,
            0);
        lv_obj_set_style_border_color(choice,
            lv_color_hex(step_done ? 0x8FE0AC : 0xFFD18B), 0);
        lv_obj_add_event_cb(choice, academy_choice_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }

    const char *feedback_text = NULL;
    if (s_academy_lesson == 6 && s_academy_choice >= 0)
        feedback_text = ACADEMY_PILOT_FEEDBACK[s_academy_step]
                                               [s_academy_choice];
    if (!feedback_text) {
        feedback_text = step_done ? "Harika! Kanıtı doğru yorumladın." :
            s_academy_choice >= 0 ? "Bir kez daha düşün ve başka seçeneği dene." :
                                    "Seçimini yap; nedenini birlikte görelim.";
    }
    lv_obj_t *feedback = panel(card, 220, 245, 510, 40,
                               step_done ? 0x183F38 :
                               s_academy_choice >= 0 ? 0x493327 : 0x10283B);
    lv_obj_remove_flag(feedback, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(feedback, 0, 0);
    lv_obj_t *feedback_label = centered_label(feedback, feedback_text,
        6, 2, 498, step_done ? 0x8FE0AC :
                  s_academy_choice >= 0 ? 0xFFD18B : 0xA9C9E8,
        &lv_font_montserrat_14);
    lv_label_set_long_mode(feedback_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *previous = button(s_content, LV_SYMBOL_LEFT "  Anlatıma dön",
                                20, 398, 230, 44, 0x29465E);
    lv_obj_add_event_cb(previous, academy_return_to_explanation_event,
                        LV_EVENT_CLICKED, NULL);
    bool pilot_transfer = s_academy_lesson == 6 && s_academy_step == 2;
    lv_obj_t *next = button(s_content,
                            !step_done ? LV_SYMBOL_PLAY "  Önce deneyi yap" :
                            pilot_transfer ? LV_SYMBOL_HOME "  Bahçede Uygula" :
                            s_academy_step == 2 ? LV_SYMBOL_OK "  Keşfi Tamamla" :
                                                  "Sonraki deney  " LV_SYMBOL_RIGHT,
                            520, 398, 260, 44,
                            !step_done ? 0x354A5B :
                            s_academy_step == 2 ? 0x2D8B66 : lesson->accent);
    if (!step_done) lv_obj_add_state(next, LV_STATE_DISABLED);
    lv_obj_add_event_cb(next,
                        pilot_transfer ? academy_apply_garden_event :
                                         academy_step_event,
                        LV_EVENT_CLICKED,
                        pilot_transfer ? NULL : (void *)(intptr_t)1);
}

static void build_academy(void)
{
    if (s_academy_lesson >= 0 && s_academy_lesson < ACADEMY_LESSON_COUNT) {
        build_academy_lesson();
        return;
    }

    label(s_content, "HybridLab Akademi", 20, 12, C_SPACE_TEXT,
          &hidrolab_font_tr_32);
    label(s_content, s_academy_category < 0 ?
          "Öğrenme seviyeni seç; her keşif gerçek bahçeye bağlanır." :
          ACADEMY_CATEGORY_SUBTITLES[s_academy_category],
          22, 53, C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Görevler",
                            520, 15, 126, 38, C_SURFACE_ALT);
    lv_obj_add_event_cb(back, academy_tasks_back_event, LV_EVENT_CLICKED, NULL);
    (void)base_return_button(s_content, 660, 15, 120);

    if (s_academy_category < 0) {
        static const uint8_t category_art[ACADEMY_CATEGORY_COUNT] = {0, 1, 3, 2};
        for (int i = 0; i < ACADEMY_CATEGORY_COUNT; ++i) {
            int x = 20 + (i % 2) * 390;
            int y = 92 + (i / 2) * 160;
            lv_obj_t *card = panel(s_content, x, y, 370, 148, C_SURFACE);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            set_bg_gradient(card, C_SURFACE, C_SURFACE);
            lv_obj_set_style_border_color(card,
                                          lv_color_hex(ACADEMY_CATEGORY_ACCENTS[i]), 0);
            lv_obj_set_style_translate_y(card, 3, LV_STATE_PRESSED);
            lv_obj_add_event_cb(card, academy_category_event, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
            academy_art(card, 14, 20, 108, category_art[i],
                        ACADEMY_CATEGORY_ACCENTS[i]);
            centered_label(card, ACADEMY_CATEGORY_TITLES[i], 140, 24, 210,
                           C_SPACE_TEXT, &hidrolab_font_tr_14);
            lv_obj_t *subtitle = centered_label(card,
                ACADEMY_CATEGORY_SUBTITLES[i], 140, 57, 210,
                0xB8CBD8, &hidrolab_font_tr_14);
            lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
            centered_label(card, "2 keşif  " LV_SYMBOL_RIGHT, 140, 116, 210,
                           ACADEMY_CATEGORY_ACCENTS[i],
                           &lv_font_montserrat_14);
        }
    } else {
        int shown = 0;
        for (int i = 0; i < ACADEMY_LESSON_COUNT; ++i) {
            const academy_lesson_t *lesson = &ACADEMY_LESSONS[i];
            if (lesson->category != (uint8_t)s_academy_category) continue;
            int x = 20 + shown * 390;
            lv_obj_t *card = panel(s_content, x, 104, 370, 286, C_SURFACE);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            set_bg_gradient(card, C_SURFACE, C_SURFACE);
            lv_obj_set_style_border_color(card, lv_color_hex(lesson->accent), 0);
            lv_obj_set_style_translate_y(card, 3, LV_STATE_PRESSED);
            lv_obj_add_event_cb(card, academy_lesson_event, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
            academy_art(card, 123, 20, 124, lesson->art_kind, lesson->accent);
            centered_label(card, lesson->grade, 20, 157, 330, lesson->accent,
                           &lv_font_montserrat_14);
            lv_obj_t *title = centered_label(card, lesson->title, 20, 183, 330,
                                              C_SPACE_TEXT,
                                              &hidrolab_font_tr_14);
            lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
            lv_obj_t *unit = centered_label(card, lesson->unit, 30, 218, 310,
                                             0xB8CBD8,
                                             &hidrolab_font_tr_14);
            lv_label_set_long_mode(unit, LV_LABEL_LONG_WRAP);
            centered_label(card, "3 aşamalı keşif  " LV_SYMBOL_RIGHT,
                           20, 265, 330, lesson->accent,
                           &lv_font_montserrat_14);
            shown++;
        }
    }

    lv_obj_t *offline = panel(s_content, 174, 420, 452, 24, C_SPACE_TOP);
    lv_obj_remove_flag(offline, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(offline, 0, 0);
    centered_label(offline, "İnternet olmadan da keşfedebilirsin.",
                   8, 5, 436, C_SPACE_MUTE, &lv_font_montserrat_14);
}

static void simulation_open_event(lv_event_t *e)
{
    (void)e;
    hidrolab_simulation_ui_open(s_screen);
}

/* Fixed-width labels keep text within its own grid cell, including saved values. */
static lv_obj_t *research_label(lv_obj_t *parent, const char *text, int x, int y,
                                int width, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *object = label(parent, text, x, y, color, font);
    lv_obj_set_width(object, width);
    lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
    return object;
}

/* Style removal also removes LVGL's local x/y/width/height properties.
 * Apply the clean style FIRST and geometry LAST, never the reverse. */
static lv_obj_t *research_slider(lv_obj_t *parent, uint32_t color,
                                 int maximum, int value)
{
    lv_obj_t *slider = lv_slider_create(parent);
    chemistry_slider_style(slider, color);
    lv_obj_set_pos(slider, 26, 56);
    lv_obj_set_size(slider, 310, 12);
    lv_obj_set_ext_click_area(slider, 16);
    lv_slider_set_range(slider, 0, maximum);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, chemistry_slider_value_event,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, chemistry_slider_value_event,
                        LV_EVENT_PRESSED, NULL);
    return slider;
}

/* Research Center: 800x456 below the unchanged 24px status bar.
 * Left: simulation and academy. Right: physical measurements and history. */
static void build_tasks(void)
{
    uint32_t today = current_daily_key();
    uint32_t calendar_day = hidrolab_time_date_key();
    if (calendar_day) (void)hidrolab_settings_sync_daily_date(calendar_day);
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    bool chemistry_today = cfg->chemistry_date == today;

    research_label(s_content, "Araştırma Merkezi", 16, 10, 600,
                   C_SPACE_TEXT, &hidrolab_font_tr_32);
    research_label(s_content, "Deneyini keşfet, bahçenin ölçümlerini kaydet.",
                   18, 54, 600, C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_t *home = base_return_button(s_content, 656, 14, 128);
    lv_obj_set_height(home, 44);

    lv_obj_t *sim = panel(s_content, 16, 86, 360, 240, 0x12384E);
    lv_obj_remove_flag(sim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_color(sim, lv_color_hex(0x4C9A9D), 0);
    research_label(sim, "Ay Serası", 18, 14, 324, C_SPACE_TEXT,
                   &hidrolab_font_tr_32);
    research_label(sim, "NASA kaynaklı A/B simülasyonu", 20, 58, 320,
                   0xC3E4E3, &hidrolab_font_tr_14);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *room = panel(sim, 18 + i * 168, 94, 156, 64,
                               i ? 0x205043 : 0x20465B);
        lv_obj_remove_flag(room, LV_OBJ_FLAG_CLICKABLE);
        research_label(room, i ? "SERA B" : "SERA A", 12, 10, 68,
                       i ? 0xB1F1CF : 0xAEDDFF, &hidrolab_font_tr_14);
        research_label(room, i ? "Deney" : "Kontrol", 12, 34, 68,
                       C_SPACE_TEXT, &hidrolab_font_tr_14);
        (void)image(room, &hidrolab_img_memory_marul, 82, 2);
    }
    lv_obj_t *start = button(sim, LV_SYMBOL_PLAY "  Simülasyonu Aç",
                             18, 178, 324, 44, 0x238B71);
    lv_obj_add_event_cb(start, simulation_open_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *academy = panel(s_content, 16, 338, 360, 102, C_SURFACE);
    lv_obj_remove_flag(academy, LV_OBJ_FLAG_CLICKABLE);
    research_label(academy, "HybridLab Akademi", 18, 14, 324,
                   C_SPACE_TEXT, &hidrolab_font_tr_14);
    research_label(academy, "4 öğrenme yolu", 18, 52, 166,
                   C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_t *academy_open = button(academy, "Akademiyi Aç",
                                    196, 42, 146, 44, 0x276E83);
    lv_obj_add_event_cb(academy_open, tasks_academy_event,
                        LV_EVENT_CLICKED, NULL);

    s_edit_ph_x100 = chemistry_today ? cfg->ph_x100 : 600;
    s_edit_ec_x100 = chemistry_today ? cfg->ec_x100 : 1200;
    if (chemistry_today && s_edit_ec_x100 && s_edit_ec_x100 < 500)
        s_edit_ec_x100 = (uint16_t)(s_edit_ec_x100 * 10U);
    s_chem_ph_touched = chemistry_today && s_edit_ph_x100 <= 1400;
    s_chem_ec_touched = chemistry_today && s_edit_ec_x100 <= 5000;
    if (s_edit_ph_x100 > 1400) s_edit_ph_x100 = 600;
    if (s_edit_ec_x100 > 5000) s_edit_ec_x100 = 1200;

    lv_obj_t *chem = panel(s_content, 392, 86, 392, 354, C_SURFACE);
    lv_obj_remove_flag(chem, LV_OBJ_FLAG_CLICKABLE);
    research_label(chem, "Bahçenin pH / EC ölçümü", 16, 16, 358,
                   C_SPACE_TEXT, &hidrolab_font_tr_14);

    lv_obj_t *ph_box = panel(chem, 14, 50, 364, 96, 0x26323E);
    lv_obj_remove_flag(ph_box, LV_OBJ_FLAG_CLICKABLE);
    research_label(ph_box, "pH", 14, 6, 100, C_PURPLE, &hidrolab_font_tr_32);
    s_ph_value_label = research_label(ph_box, "", 194, 6, 154,
                                      C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_set_style_text_align(s_ph_value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_fmt(s_ph_value_label, "%u.%02u",
                          (unsigned)(s_edit_ph_x100 / 100U),
                          (unsigned)(s_edit_ph_x100 % 100U));
    s_ph_slider = research_slider(ph_box, C_PURPLE, 1400, s_edit_ph_x100);
    research_label(ph_box, "0", 18, 76, 60, C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_t *ph_max = research_label(ph_box, "14", 284, 76, 60,
                                      C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_set_style_text_align(ph_max, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *ec_box = panel(chem, 14, 154, 364, 96, 0x1D3743);
    lv_obj_remove_flag(ec_box, LV_OBJ_FLAG_CLICKABLE);
    research_label(ec_box, "EC", 14, 6, 58, C_BLUE, &hidrolab_font_tr_32);
    research_label(ec_box, "uS/cm", 78, 20, 90, C_SPACE_MUTE, &hidrolab_font_tr_14);
    s_ec_value_label = research_label(ec_box, "", 194, 6, 154,
                                      C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_set_style_text_align(s_ec_value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_fmt(s_ec_value_label, "%u", (unsigned)s_edit_ec_x100);
    s_ec_slider = research_slider(ec_box, C_BLUE, 5000, s_edit_ec_x100);
    research_label(ec_box, "0", 18, 76, 60, C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_t *ec_max = research_label(ec_box, "5000", 284, 76, 60,
                                      C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_set_style_text_align(ec_max, LV_TEXT_ALIGN_RIGHT, 0);

    s_chem_feedback_card = panel(chem, 14, 258, 364, 32, 0x102D43);
    lv_obj_remove_flag(s_chem_feedback_card, LV_OBJ_FLAG_CLICKABLE);
    s_chem_feedback_label = research_label(s_chem_feedback_card, "", 10, 6,
                                           340, C_SPACE_MUTE, &hidrolab_font_tr_14);
    s_chem_error = s_chem_feedback_label;
    lv_obj_t *save = button(chem, LV_SYMBOL_OK "  Kaydet",
                            14, 300, 178, 44, 0x236A58);
    lv_obj_add_event_cb(save, chemistry_save_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *history = button(chem, "Geçmiş Tablosu",
                               204, 300, 174, 44, 0x30415F);
    lv_obj_add_event_cb(history, chemistry_graphs_open_event,
                        LV_EVENT_CLICKED, NULL);
    chemistry_update_feedback();
}


/* ---------------- Achievements ---------------- */
static void evaluate_badges(void)
{
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    int plants = active_plant_count();
    uint32_t eligible = 0;
    if (plants >= 1) eligible |= 1UL << 0;
    if (plants >= 4) eligible |= 1UL << 1;
    if (plants >= HIDROLAB_GARDEN_SLOT_COUNT) eligible |= 1UL << 2;
    size_t chemistry_days = hidrolab_settings_chemistry_history_count();
    if (chemistry_days >= 1) eligible |= 1UL << 3;
    if (chemistry_days >= 3) eligible |= 1UL << 4;
    if (chemistry_days >= 7) eligible |= 1UL << 5;
    if (cfg->activity_game_mask & ACTIVITY_WATER_PATH) eligible |= 1UL << 6;
    if (cfg->activity_game_mask & ACTIVITY_BUG_ESCAPE) eligible |= 1UL << 7;
    if (cfg->activity_game_mask & ACTIVITY_ARCADE) eligible |= 1UL << 8;
    if (cfg->memory_wins >= 1) eligible |= 1UL << 9;
    if (cfg->memory_wins >= 3) eligible |= 1UL << 10;
    if (cfg->bubble_best >= 10) eligible |= 1UL << 11;
    if (cfg->bubble_best >= 20) eligible |= 1UL << 12;
    if (cfg->points >= 100) eligible |= 1UL << 13;
    if (cfg->points >= 250) eligible |= 1UL << 14;
    if (cfg->points >= 500) eligible |= 1UL << 15;
    if (cfg->setup_done) eligible |= 1UL << 16;
    if (hidrolab_wifi_get_state() == HIDROLAB_WIFI_CONNECTED) eligible |= 1UL << 17;
    if (cfg->season_day >= 7) eligible |= 1UL << 18;
    if (cfg->season_day >= 14) eligible |= 1UL << 19;

    uint32_t newly_unlocked = eligible & ~cfg->achievements;
    if (newly_unlocked &&
        hidrolab_settings_unlock_achievements(newly_unlocked) == ESP_OK) {
        s_pending_badges |= newly_unlocked;
    }
}

static void badge_popup_close_async_cb(void *unused)
{
    (void)unused;
    if (s_badge_overlay) {
        lv_obj_delete(s_badge_overlay);
        s_badge_overlay = NULL;
    }
    s_badge_popup_open = false;
    s_badge_close_pending = false;
    show_next_badge_popup();
}

static void badge_popup_close_event(lv_event_t *e)
{
    (void)e;
    if (s_badge_close_pending) return;
    s_badge_close_pending = true;
    if (lv_async_call(badge_popup_close_async_cb, NULL) != LV_RESULT_OK) {
        s_badge_close_pending = false;
    }
}

static uint32_t badge_accent(int index)
{
    if (index <= 2) return C_GREEN;
    if (index <= 5) return C_BLUE;
    if (index <= 8) return C_PURPLE;
    if (index <= 12) return C_ORANGE;
    if (index <= 15) return C_YELLOW;
    if (index <= 17) return C_CYAN;
    return C_PINK;
}

static void show_next_badge_popup(void)
{
    if (s_setup_mode || s_sleep_screen || s_base_departing || s_badge_popup_open ||
        s_pending_badges == 0 || !s_screen) return;

    int index = -1;
    for (int i = 0; i < BADGE_COUNT; ++i) {
        if (s_pending_badges & (1UL << i)) {
            index = i;
            break;
        }
    }
    if (index < 0) return;
    s_pending_badges &= ~(1UL << index);
    s_badge_popup_open = true;

    s_badge_overlay = lv_obj_create(s_screen);
    lv_obj_set_pos(s_badge_overlay, 0, 0);
    lv_obj_set_size(s_badge_overlay, 800, 480);
    lv_obj_remove_flag(s_badge_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_badge_overlay, 0, 0);
    lv_obj_set_style_radius(s_badge_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_badge_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_badge_overlay, lv_color_hex(0x001D34), 0);
    lv_obj_set_style_bg_opa(s_badge_overlay, LV_OPA_70, 0);

    uint32_t accent = badge_accent(index);
    lv_obj_t *box = panel(s_badge_overlay, 190, 74, 420, 330, C_SPACE_CARD);
    set_bg_gradient(box, C_SPACE_CARD, 0x0A2542);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(accent), 0);
    lv_obj_t *img = image(box, hidrolab_badge_images[index], 184, 38);
    lv_obj_set_style_shadow_width(box, 24, 0);
    lv_obj_set_style_shadow_color(box, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_opa(box, LV_OPA_50, 0);
    lv_image_set_scale(img, 430);
    centered_label(box, "YENİ ROZET KAZANDIN!", 20, 160, 380, accent,
                   &hidrolab_font_tr_14);
    centered_label(box, BADGES[index].name, 20, 190, 380, C_SPACE_TEXT,
                   &hidrolab_font_tr_32);
    centered_label(box, BADGES[index].desc, 35, 238, 350, C_SPACE_MUTE,
                   &hidrolab_font_tr_14);
    lv_obj_t *ok = button(box, "Harika!", 125, 282, 170, 38, C_GREEN);
    lv_obj_add_event_cb(ok, badge_popup_close_event, LV_EVENT_CLICKED, NULL);

    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_BADGE);
}

static void badge_detail_close_event(lv_event_t *e)
{
    lv_obj_t *modal = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_delete_async(modal);
}

static void badge_detail_event(lv_event_t *e)
{
    int index = (int)(uintptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= BADGE_COUNT) return;
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    bool got = (cfg->achievements & (1UL << index)) != 0;

    lv_obj_t *modal = lv_obj_create(s_screen);
    lv_obj_set_pos(modal, 0, 0);
    lv_obj_set_size(modal, 800, 480);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x071525), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_70, 0);

    uint32_t accent = badge_accent(index);
    lv_obj_t *box = panel(modal, 120, 86, 560, 308, 0x142C41);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x91794E), 0);
    label(box, "BAŞARIMIN ÖYKÜSÜ", 24, 21, 0xF0CD8E, &hidrolab_font_tr_14);
    lv_obj_t *medallion = panel(box, 24, 72, 112, 112, 0x213F51);
    lv_obj_remove_flag(medallion, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(medallion, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(medallion, 1, 0);
    lv_obj_set_style_border_color(medallion,
                                  lv_color_hex(got ? accent : 0x567082), 0);
    lv_obj_t *img = image(medallion, hidrolab_badge_images[index], 30, 30);
    if (!got) {
        lv_obj_set_style_image_recolor(img, lv_color_hex(C_LOCK), 0);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_70, 0);
    }
    lv_obj_t *title = label(box, BADGES[index].name, 164, 65,
                             C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_set_width(title, 372);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_t *desc = label(box, BADGES[index].desc, 164, 123, 0xB8CBD8,
                            &hidrolab_font_tr_14);
    lv_obj_set_width(desc, 372);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_t *reward = panel(box, 164, 174, 372, 31, 0x173B4A);
    lv_obj_remove_flag(reward, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(reward, 0, 0);
    centered_label(reward, BADGES[index].reward, 8, 8, 356,
                   0x9EDDEE, &hidrolab_font_tr_14);
    lv_obj_t *status = panel(box, 164, 213, 372, 31,
                             got ? 0x24493E : 0x2A3D4B);
    lv_obj_remove_flag(status, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(status, 0, 0);
    centered_label(status, got ? LV_SYMBOL_OK "  KOLEKSİYONUNDA" : "SIRADAKİ HEDEFİN",
                   8, 9, 356, got ? 0xACDFBD : 0xD2C8B2, &hidrolab_font_tr_14);
    lv_obj_t *close = button(box, "Başarımlara dön", 352, 254, 184, 36, 0x725A34);
    lv_obj_add_event_cb(close, badge_detail_close_event, LV_EVENT_CLICKED, modal);
}

static void badge_group_event(lv_event_t *e)
{
    if (s_page_render_pending || s_page_render_retry || s_badge_travel_overlay) return;
    uint8_t selected = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (selected >= 4 || selected == s_badge_group) return;
    s_badge_group = selected;
    request_page_render();
}

static void build_badges(void)
{
    evaluate_badges();
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    int earned = bit_count32(cfg->achievements & 0x000FFFFFUL);

    label(s_content, "Rozet Galaksisi", 20, 12, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  GERİ",
                            660, 15, 120, 38, 0x284156);
    lv_obj_add_event_cb(back, badge_portal_back_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *intro = label(s_content, "", 22, 53, 0xC5BCA6, &hidrolab_font_tr_14);
    lv_obj_set_width(intro, 758);
    lv_label_set_text_fmt(intro, "%d / 20 başarım tamamlandı. Dokun; hedefini ve üs ödülünü gör.", earned);
    lv_obj_t *progress = lv_bar_create(s_content);
    lv_obj_set_pos(progress, 22, 78);
    lv_obj_set_size(progress, 756, 6);
    lv_bar_set_range(progress, 0, BADGE_COUNT);
    lv_bar_set_value(progress, earned, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress, lv_color_hex(0x294253), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress, lv_color_hex(0xE4B85D), LV_PART_INDICATOR);

    static const char *groups[4] = {"Bahçe", "Su ve Ölçüm", "Oyunlar", "Uzay Yolculuğu"};
    static const uint8_t starts[5] = {0, 3, 6, 13, 20};
    if (s_badge_group >= 4) s_badge_group = 0;
    for (int group = 0; group < 4; ++group) {
        int complete = 0;
        for (int i = starts[group]; i < starts[group + 1]; ++i)
            if (cfg->achievements & (1UL << i)) ++complete;
        char caption[64];
        snprintf(caption, sizeof(caption), "%s  %d/%d", groups[group], complete,
                 starts[group + 1] - starts[group]);
        lv_obj_t *tab = button(s_content, caption, 20 + group * 193, 94, 181, 42,
                              group == s_badge_group ? 0x735A30 : 0x203D50);
        lv_obj_add_event_cb(tab, badge_group_event, LV_EVENT_CLICKED, (void *)(uintptr_t)group);
    }
    for (int i = starts[s_badge_group]; i < starts[s_badge_group + 1]; ++i) {
        int position = i - starts[s_badge_group];
        int col = position % 3;
        int row = position / 3;
        bool got = (cfg->achievements & (1UL << i)) != 0;
        lv_obj_t *c = panel(s_content, 20 + col * 258, 148 + row * 90, 244, 80,
                            got ? 0x1C3848 : 0x142B3E);
        lv_obj_set_style_radius(c, 12, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(got ? 0xAE8D50 : 0x3D5364), 0);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_translate_y(c, 2, LV_STATE_PRESSED);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, badge_detail_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        background_shape(c, 0, 0, 4, 80, badge_accent(i), 4,
                         got ? LV_OPA_COVER : LV_OPA_30);
        lv_obj_t *img = image(c, hidrolab_badge_images[i], 5, 14);
        if (!got) {
            lv_obj_set_style_image_recolor(img, lv_color_hex(C_LOCK), 0);
            lv_obj_set_style_image_recolor_opa(img, LV_OPA_80, 0);
        }
        lv_obj_t *name = label(c, BADGES[i].name, 64, 18,
                               got ? C_SPACE_TEXT : 0xACBDC9, &hidrolab_font_tr_14);
        lv_obj_set_width(name, 170);
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        if (got) {
            lv_obj_t *check = background_shape(c, 40, 34, 22, 22,
                                                0x286149, LV_RADIUS_CIRCLE, LV_OPA_COVER);
            lv_obj_t *mark = centered_label(check, LV_SYMBOL_OK, 0, 0, 22,
                                             0xD4F5DB, &hidrolab_font_tr_14);
            lv_obj_center(mark);
        }
    }
    label(s_content, "Renkli başarımlar tamamlandı; oyun ödülleri Ay Üssü ve Bahçe'de görünür.",
          22, 432, 0xB8CBD8, &hidrolab_font_tr_14);
}

/* ---------------- Games home ---------------- */
static void game_open_event(lv_event_t *e)
{
    if (s_page_render_pending || s_page_render_retry) return;
    game_view_t selected = (game_view_t)(uintptr_t)lv_event_get_user_data(e);
    if (selected != GAME_WATER_PATH && selected != GAME_BUG_ESCAPE &&
        selected != GAME_BUBBLE && selected != GAME_ARCADE && selected != GAME_MEMORY) return;
    s_game_view = selected;
    s_game_guide_visible = s_game_view != GAME_HOME;
    request_page_render();
}

static void game_prepare_selected(void)
{
    if (s_game_view == GAME_MEMORY) {
        s_memory_pairs = 0;
        s_memory_first = -1;
        s_memory_second = -1;
        s_memory_busy = false;
        shuffle_memory();
    } else if (s_game_view == GAME_BUBBLE) {
        s_bubble_score = 0;
        s_bubble_combo = 0;
        s_bubble_hits = 0;
        s_bubble_bonus_until = 0;
        s_bubble_finished = false;
        s_bubble_end_us = esp_timer_get_time() + 30000000LL;
    } else if (s_game_view == GAME_WATER_PATH) {
        s_water_level = 0;
        s_water_moves = 0;
        s_water_start_us = esp_timer_get_time();
        s_water_final_seconds = 0;
        s_water_finished = false;
        s_water_recorded = false;
        water_init_level();
    } else if (s_game_view == GAME_ARCADE) {
        s_arcade_score = 0;
        s_arcade_combo = 0;
        s_arcade_lives = 3;
        s_arcade_wave = 1;
        s_arcade_target = -1;
        s_arcade_finished = false;
        s_arcade_end_us = esp_timer_get_time() + 30000000LL;
        s_arcade_move_us = 0;
        s_arcade_target_since_us = 0;
    } else if (s_game_view == GAME_BUG_ESCAPE) {
        bug_begin_run();
    }
}

static void game_guide_start_event(lv_event_t *e)
{
    (void)e;
    if (!s_game_guide_visible || s_game_view == GAME_HOME) return;
    s_game_guide_visible = false;
    game_prepare_selected();
    request_page_render();
}

static void game_back_event(lv_event_t *e)
{
    (void)e;
    s_game_guide_visible = false;
    s_game_view = GAME_HOME;
    request_page_render();
}

static lv_obj_t *game_art_shape(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color, int radius)
{
    lv_obj_t *shape = lv_obj_create(parent);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, w, h);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(shape, 0, 0);
    lv_obj_set_style_border_width(shape, 0, 0);
    lv_obj_set_style_radius(shape, radius, 0);
    lv_obj_set_style_bg_color(shape, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
    return shape;
}

/* Lightweight, code-drawn cover art keeps the game hub lively without loading
 * eight more bitmaps into flash/RAM. */
static void __attribute__((unused)) game_card_art(lv_obj_t *card, int kind, uint32_t accent)
{
    int stage_x = (lv_obj_get_width(card) - 160) / 2;
    lv_obj_t *stage = game_art_shape(card, stage_x, 8, 160, 58, 0x0B1E3D, 13);
    lv_obj_set_style_bg_opa(stage, LV_OPA_70, 0);
    space_star(stage, 12 + kind * 7 % 80, 9, 3, 0xFFFFFF);
    space_star(stage, 132 - kind * 5 % 70, 16, 2, 0xBCEBFF);

    if (kind == 0) { /* Memory cards */
        for (int i = 0; i < 4; ++i) {
            lv_obj_t *tile = game_art_shape(stage, 35 + i * 24, 11 + (i % 2) * 7,
                                            20, 30, i < 2 ? accent : 0xFFFFFF, 6);
            lv_obj_set_style_border_width(tile, 1, 0);
            lv_obj_set_style_border_color(tile, lv_color_hex(0xBCEBFF), 0);
        }
    } else if (kind == 1) { /* Quiz planet */
        lv_obj_t *planet = game_art_shape(stage, 58, 6, 48, 48, accent, LV_RADIUS_CIRCLE);
        lv_obj_set_style_shadow_width(planet, 8, 0);
        lv_obj_set_style_shadow_color(planet, lv_color_hex(accent), 0);
        centered_label(planet, "?", 0, 13, 48, 0xFFFFFF, &lv_font_montserrat_14);
        game_art_shape(stage, 43, 27, 78, 5, 0xBCEBFF, 3);
    } else if (kind == 2) { /* Bubble cluster */
        const int bx[5] = {45, 76, 103, 62, 94};
        const int by[5] = {27, 8, 29, 35, 4};
        const int bs[5] = {22, 28, 18, 14, 11};
        for (int i = 0; i < 5; ++i) {
            lv_obj_t *bubble = game_art_shape(stage, bx[i], by[i], bs[i], bs[i],
                                              i == 1 ? accent : 0x82DBFF,
                                              LV_RADIUS_CIRCLE);
            lv_obj_set_style_bg_opa(bubble, i == 1 ? LV_OPA_COVER : LV_OPA_70, 0);
            lv_obj_set_style_border_width(bubble, 1, 0);
            lv_obj_set_style_border_color(bubble, lv_color_hex(0xFFFFFF), 0);
        }
    } else if (kind == 3) { /* Water path */
        for (int i = 0; i < 3; ++i) {
            game_art_shape(stage, 25 + i * 42, 12, 34, 34, 0x183D5D, 7);
        }
        game_art_shape(stage, 40, 27, 42, 7, accent, 4);
        game_art_shape(stage, 75, 27, 7, 19, accent, 4);
        game_art_shape(stage, 80, 39, 43, 7, accent, 4);
        lv_obj_t *drop = game_art_shape(stage, 130, 31, 15, 20, 0x55D9FF,
                                        LV_RADIUS_CIRCLE);
        start_float_animation(drop, 0, -5, 780, 0);
    } else if (kind == 4) { /* Root maze */
        for (int i = 0; i < 5; ++i) {
            game_art_shape(stage, 24 + i * 27, 8 + (i % 2) * 23,
                           20, 20, 0x284963, 5);
        }
        game_art_shape(stage, 34, 18, 70, 6, 0xF2E4C9, 3);
        game_art_shape(stage, 98, 18, 6, 27, 0xF2E4C9, 3);
        game_art_shape(stage, 98, 39, 37, 6, accent, 3);
        game_art_shape(stage, 128, 35, 13, 13, 0x55D9FF, LV_RADIUS_CIRCLE);
    } else if (kind == 5) { /* Growth timeline */
        game_art_shape(stage, 28, 37, 108, 4, 0x6E92B6, 2);
        for (int i = 0; i < 4; ++i) {
            lv_obj_t *node = game_art_shape(stage, 25 + i * 35, 27 - i * 4,
                                            16 + i * 2, 16 + i * 2,
                                            i == 3 ? accent : 0x58C77B,
                                            LV_RADIUS_CIRCLE);
            lv_obj_set_style_border_width(node, 2, 0);
            lv_obj_set_style_border_color(node, lv_color_hex(0xD8F4FF), 0);
        }
    } else if (kind == 6) { /* Meteor chase */
        lv_obj_t *meteor = game_art_shape(stage, 94, 13, 38, 38, 0xC56B52, LV_RADIUS_CIRCLE);
        game_art_shape(meteor, 8, 8, 8, 8, 0x7D4167, LV_RADIUS_CIRCLE);
        game_art_shape(stage, 35, 20, 32, 32, accent, LV_RADIUS_CIRCLE);
        game_art_shape(stage, 21, 31, 22, 5, 0x6BEAFF, 3);
        game_art_shape(stage, 24, 40, 14, 4, 0xA7F5FF, 2);
    } else if (kind == 7) { /* Badge galaxy */
        lv_obj_t *medal = game_art_shape(stage, 59, 5, 48, 48, accent, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(medal, 3, 0);
        lv_obj_set_style_border_color(medal, lv_color_hex(0xFFF2B6), 0);
        centered_label(medal, LV_SYMBOL_OK, 0, 13, 48, 0xFFFFFF, &lv_font_montserrat_14);
        game_art_shape(stage, 62, 44, 13, 13, 0xF28B43, 2);
        game_art_shape(stage, 91, 44, 13, 13, 0xE96DA6, 2);
    } else { /* Seed landing */
        game_art_shape(stage, 30, 15, 100, 4, 0x4B7598, 2);
        game_art_shape(stage, 46, 31, 70, 4, 0x4B7598, 2);
        lv_obj_t *pod = game_art_shape(stage, 70, 4, 25, 32, accent,
                                       LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(pod, 2, 0);
        lv_obj_set_style_border_color(pod, lv_color_hex(0xFFFFFF), 0);
        game_art_shape(stage, 39, 20, 19, 19, 0xA06458, LV_RADIUS_CIRCLE);
        game_art_shape(stage, 107, 37, 31, 12, 0x55C878, 6);
    }
}

/* Compact list artwork reserves a fixed 76 px column. Titles, descriptions and
 * completion marks therefore never share the same drawing area. */
static void __attribute__((unused)) game_list_art(lv_obj_t *card, int kind, uint32_t accent)
{
    lv_obj_t *icon = panel(card, 14, 12, 76, 72, C_SURFACE_ALT);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(icon, 14, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    if (kind == 0) {
        game_art_shape(icon, 5, 10, 31, 40, 0x27455D, 6);
        game_art_shape(icon, 39, 24, 31, 40, 0x27455D, 6);
        lv_obj_t *apple = image(icon, &hidrolab_img_memory_elma, 3, 16);
        lv_image_set_pivot(apple, 0, 0);
        lv_image_set_scale(apple, 128);
        lv_obj_t *cherry = image(icon, &hidrolab_img_memory_kiraz, 37, 30);
        lv_image_set_pivot(cherry, 0, 0);
        lv_image_set_scale(cherry, 128);
    } else if (kind == 1) {
        lv_obj_t *planet = game_art_shape(icon, 17, 10, 44, 44, accent, LV_RADIUS_CIRCLE);
        centered_label(planet, "?", 0, 13, 44, 0xFFFFFF, &lv_font_montserrat_14);
        game_art_shape(icon, 11, 49, 55, 5, 0xBCEBFF, 3);
    } else if (kind == 2) {
        const int bx[4] = {14, 34, 45, 23};
        const int by[4] = {31, 11, 35, 43};
        const int bs[4] = {20, 27, 17, 13};
        for (int i = 0; i < 4; ++i) {
            lv_obj_t *b = game_art_shape(icon, bx[i], by[i], bs[i], bs[i],
                                         i == 1 ? accent : 0x82DBFF, LV_RADIUS_CIRCLE);
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), 0);
        }
    } else if (kind == 3) {
        game_art_shape(icon, 8, 31, 60, 8, accent, 4);
        game_art_shape(icon, 31, 8, 8, 31, accent, 4);
        game_art_shape(icon, 52, 31, 8, 27, accent, 4);
        game_art_shape(icon, 27, 27, 16, 16, 0xB7F5FF, LV_RADIUS_CIRCLE);
    } else if (kind == 4) {
        lv_obj_t *bug = image(icon, &hidrolab_img_runner_beetle, 8, 16);
        lv_image_set_pivot(bug, 0, 0);
        lv_image_set_scale(bug, 384);
    } else if (kind == 5) {
        game_art_shape(icon, 9, 53, 58, 5, 0x6E92B6, 3);
        for (int i = 0; i < 3; ++i) {
            game_art_shape(icon, 12 + i * 21, 37 - i * 10, 15 + i * 2, 15 + i * 2,
                           i == 2 ? accent : 0x58C77B, LV_RADIUS_CIRCLE);
        }
    } else if (kind == 6) {
        lv_obj_t *meteor = image(icon, &hidrolab_img_arcade_meteor, 5, 4);
        lv_image_set_pivot(meteor, 0, 0);
        lv_image_set_scale(meteor, 224);
    } else {
        lv_obj_t *medal = game_art_shape(icon, 17, 9, 43, 43, accent, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(medal, 3, 0);
        lv_obj_set_style_border_color(medal, lv_color_hex(0xFFF2B6), 0);
        centered_label(medal, LV_SYMBOL_OK, 0, 12, 43, 0xFFFFFF, &lv_font_montserrat_14);
    }
}

static void game_guide_row(lv_obj_t *parent, int y, const char *heading,
                           const char *text, uint32_t accent)
{
    game_art_shape(parent, 22, y + 3, 4, 55, accent, 2);
    label(parent, heading, 40, y, accent, &lv_font_montserrat_14);
    lv_obj_t *body = label(parent, text, 40, y + 24, C_SPACE_TEXT,
                           &lv_font_montserrat_14);
    /* The 518px guide card is not laid out yet during construction. Reading
     * its measured width here would yield zero on the first frame. */
    lv_obj_set_width(body, 454);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
}

static void build_game_guide(void)
{
    const char *title = "Oyun";
    const char *goal = "Minik görevi tamamla.";
    const char *control = "Ekrandaki nesnelere dokun.";
    const char *tip = "Uzay arkadaşının söylediklerini izle.";
    uint32_t accent = C_CYAN;
    int art_kind = 3;

    switch (s_game_view) {
        case GAME_WATER_PATH:
            title = "Su Yolu";
            goal = "Depodan bitkiye kesintisiz bir su yolu kur.";
            control = "Boru parçalarına dokunarak yönlerini değiştir.";
            tip = "Her tur yeni bir rota gelir; süre ve hamle sayılır.";
            accent = C_CYAN;
            art_kind = 3;
            break;
        case GAME_BUG_ESCAPE:
            title = "Böceklerden Kaç";
            goal = "60 saniye boyunca böcekleri aş ve bitkini koru!";
            control = "Başlatınca bitki koşar; böcek yaklaşınca ekrana dokun.";
            tip = "Kelebek ve uğur böceği +1 can verir. Mor böcekten kaç!";
            accent = 0x78D98B;
            art_kind = 4;
            break;
        case GAME_BUBBLE:
            title = "Kabarcık Yakala";
            goal = "30 saniye içinde hareketli kabarcıkları yakala.";
            control = "Parlayan kabarcığa hızlıca dokun.";
            tip = "Seriyi bozmazsan kombon ve puanın artar.";
            accent = C_PURPLE;
            art_kind = 2;
            break;
        case GAME_ARCADE:
            title = "Meteor Arcade";
            goal = "Besin kapsüllerini yakala, meteorlardan kaç.";
            control = "Hedefin bulunduğu hücreye dokun.";
            tip = "Üç kalkanın var; dalgalar giderek hızlanır.";
            accent = 0xFF8879;
            art_kind = 6;
            break;
        case GAME_MEMORY:
            title = "Hasat Hafızası";
            goal = "Sekiz meyve ve sebze çiftinin tamamını eşleştir.";
            control = "İki karta sırayla dokun ve görselleri karşılaştır.";
            tip = "Yanlış çift kısa süre sonra yeniden kapanır.";
            accent = C_ORANGE;
            art_kind = 0;
            break;
        default:
            break;
    }

    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Oyunlar", 660, 15, 120, 38,
                            0x70404B);
    lv_obj_add_event_cb(back, game_back_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *heading = label(s_content, title, 20, 12, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_set_width(heading, 616);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
    label(s_content, "Kısa rehber • Hazır olduğunda başla.", 22, 53,
          C_SPACE_MUTE, &hidrolab_font_tr_14);

    lv_obj_t *card = panel(s_content, 20, 90, 518, 336, 0x142B41);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(accent), 0);
    game_guide_row(card, 24, "01  HEDEFİN", goal, accent);
    game_art_shape(card, 24, 115, 470, 1, C_BORDER, 0);
    game_guide_row(card, 132, "02  NASIL OYNANIR?", control, accent);
    game_art_shape(card, 24, 223, 470, 1, C_BORDER, 0);
    game_guide_row(card, 240, "03  KÜÇÜK İPUCU", tip, accent);

    lv_obj_t *guide = panel(s_content, 558, 90, 222, 260, 0x263244);
    lv_obj_remove_flag(guide, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_color(guide, lv_color_hex(0x746273), 0);
    centered_label(guide, "UZAY ARKADAŞIN", 12, 16, 198, 0xFFC4AC, &hidrolab_font_tr_14);
    (void)profile_art(guide, hidrolab_settings_get()->astronaut_profile, 51, 48, 256);
    centered_label(guide, "Birlikte deneyelim.\nHer tur yeni bir şans!", 14, 193, 194,
                   C_SPACE_TEXT, &hidrolab_font_tr_14);
    (void)art_kind;
    lv_obj_t *start = button(s_content, "BAŞLAYALIM!  " LV_SYMBOL_RIGHT,
                             558, 368, 222, 58, 0xB64E48);
    lv_obj_add_event_cb(start, game_guide_start_event, LV_EVENT_CLICKED, NULL);
}

/* Static miniature scenes stay inside their circular backgrounds geometrically.
 * clip_corner creates two ARGB8888 layers per circle (34,848 bytes EACH),
 * exhausting the device's LVGL heap. Never enable it on these parents. */
static void game_planet_art(lv_obj_t *target, int kind)
{
    const uint32_t sky[5] = {0x354C68, 0x302944, 0x285149, 0x24495B, 0x294568};
    lv_obj_t *globe = panel(target, 26, 0, 132, 132, sky[kind]);
    lv_obj_remove_flag(globe, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(globe, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(globe, false, 0);
    lv_obj_set_style_border_color(globe, lv_color_hex(0x7793AA), 0);
    lv_obj_set_style_border_width(globe, 2, 0);
    lv_obj_set_style_border_post(globe, true, 0);
    set_bg_gradient(globe, sky[kind], 0x16263C);
    for (int j = 0; j < 7; ++j) {
        int x = 14 + (j * 37) % 104;
        int y = 12 + (j * 23) % 94;
        if ((x - 66) * (x - 66) + (y - 66) * (y - 66) < 56 * 56)
            space_star(globe, x, y, j % 3 == 0 ? 2 : 1, 0xA4B9CB);
    }

    if (kind == 0) {
        game_art_shape(globe, 30, 96, 72, 23, 0x8B9AA6, LV_RADIUS_CIRCLE);
        game_art_shape(globe, 33, 110, 22, 6, 0x637986, LV_RADIUS_CIRCLE);
        game_art_shape(globe, 83, 103, 19, 6, 0x637986, LV_RADIUS_CIRCLE);
        game_art_shape(globe, 90, 21, 18, 18, 0xD3DFDB, LV_RADIUS_CIRCLE);
        lv_obj_t *plant = image(globe, &hidrolab_img_garden_sprout, 20, 39);
        lv_image_set_pivot(plant, 0, 0);
        lv_image_set_scale(plant, 208);
        (void)image(globe, &hidrolab_img_runner_beetle, 76, 83);
    } else if (kind == 1) {
        lv_obj_t *meteor = image(globe, &hidrolab_img_arcade_meteor, 25, 12);
        lv_image_set_pivot(meteor, 0, 0);
        lv_image_set_scale(meteor, 192);
        lv_obj_t *small = image(globe, &hidrolab_img_arcade_meteor, 76, 53);
        lv_image_set_pivot(small, 0, 0);
        lv_image_set_scale(small, 112);
        lv_obj_t *capsule = image(globe, &hidrolab_img_arcade_capsule, 29, 79);
        lv_image_set_pivot(capsule, 0, 0);
        lv_image_set_scale(capsule, 128);
    } else if (kind == 2) {
        const lv_image_dsc_t *fruit[4] = {
            &hidrolab_img_memory_elma, NULL, &hidrolab_img_memory_kiraz, &hidrolab_img_memory_elma
        };
        for (int j = 0; j < 4; ++j) {
            int x = 24 + (j % 2) * 44;
            int y = 20 + (j / 2) * 48;
            game_art_shape(globe, x, y, 38, 43, j == 1 ? 0x41636A : 0xE6EBCF, 7);
            if (fruit[j]) {
                lv_obj_t *f = image(globe, fruit[j], x + 2, y + 8);
                lv_image_set_pivot(f, 0, 0);
                lv_image_set_scale(f, 128);
            } else {
                lv_obj_t *back = game_art_shape(globe, x + 9, y + 10, 22, 25, 0x41636A, 4);
                lv_obj_set_style_border_width(back, 1, 0);
                lv_obj_set_style_border_color(back, lv_color_hex(0x91ACA7), 0);
            }
        }
    } else if (kind == 3) {
        game_art_shape(globe, 27, 22, 14, 47, 0x97C8CE, 7);
        game_art_shape(globe, 27, 58, 63, 14, 0x97C8CE, 7);
        game_art_shape(globe, 77, 58, 14, 42, 0x97C8CE, 7);
        game_art_shape(globe, 31, 25, 6, 42, 0x4ED4DF, 3);
        game_art_shape(globe, 31, 62, 55, 6, 0x4ED4DF, 3);
        game_art_shape(globe, 81, 62, 6, 36, 0x4ED4DF, 3);
        game_art_shape(globe, 24, 18, 20, 20, 0xBAEDF0, LV_RADIUS_CIRCLE);
        lv_obj_t *plant = image(globe, &hidrolab_img_garden_sprout, 65, 85);
        lv_image_set_pivot(plant, 0, 0);
        lv_image_set_scale(plant, 144);
    } else {
        const int bx[5] = {24, 62, 22, 89, 74};
        const int by[5] = {22, 46, 78, 20, 100};
        const int bs[5] = {33, 43, 25, 17, 15};
        for (int j = 0; j < 5; ++j) {
            lv_obj_t *bubble = game_art_shape(globe, bx[j], by[j], bs[j], bs[j],
                                              j == 1 ? 0xB98C72 : 0x648DB9, LV_RADIUS_CIRCLE);
            lv_obj_set_style_border_width(bubble, 1, 0);
            lv_obj_set_style_border_color(bubble, lv_color_hex(0xD9EAF7), 0);
            game_art_shape(bubble, bs[j] / 4, bs[j] / 5, bs[j] / 4, bs[j] / 7,
                           0xE8F4FA, LV_RADIUS_CIRCLE);
        }
    }
}

static void build_games_home(void)
{
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    label(s_content, "Oyunlar", 20, 12, C_SPACE_TEXT, &hidrolab_font_tr_32);
    label(s_content, "Rotanı seç! Her gezegen başka bir oyun.", 22, 53,
          C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_t *points = centered_label(s_content, "", 500, 27, 140, 0xFFD08A, &hidrolab_font_tr_14);
    lv_label_set_text_fmt(points, "%u yıldız", (unsigned)cfg->points);
    (void)base_return_button(s_content, 660, 15, 120);

    const char *titles[5] = {
        "Böceklerden Kaç", "Meteor Arcade", "Hasat Hafızası", "Su Yolu",
        "Kabarcık Yakala"
    };
    const game_view_t views[5] = {
        GAME_BUG_ESCAPE, GAME_ARCADE, GAME_MEMORY, GAME_WATER_PATH, GAME_BUBBLE
    };
    const int px[5] = {38, 308, 578, 173, 443};
    const int py[5] = {86, 86, 86, 269, 269};
    for (int j = 0; j < 24; ++j)
        space_star(s_content, 26 + (j * 137) % 748, 94 + (j * 67) % 336,
                   j % 5 == 0 ? 2 : 1, 0x617F96);

    for (int i = 0; i < 5; ++i) {
        /* One generous, non-overlapping target includes planet and name. */
        lv_obj_t *c = panel(s_content, px[i], py[i], 184, 168, C_SURFACE);
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, game_open_event, LV_EVENT_CLICKED, (void *)(uintptr_t)views[i]);
        lv_obj_set_style_translate_y(c, 2, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(c, LV_OPA_30, LV_STATE_PRESSED);
        game_planet_art(c, i);
        lv_obj_t *title = centered_label(c, titles[i], 0, 140, 184,
                                         C_SPACE_TEXT, &hidrolab_font_tr_14);
        lv_obj_set_style_bg_color(title, lv_color_hex(0x132638), 0);
        lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(title, 13, 0);
        lv_obj_set_style_pad_top(title, 5, 0);
        lv_obj_set_style_pad_bottom(title, 5, 0);
    }
}

/* ---------------- Memory game ---------------- */

static void memory_finish_round(void)
{
    (void)hidrolab_settings_record_memory_completion();
    if (s_memory_info) lv_label_set_text(s_memory_info, "Hepsini buldun!  +30 puan");
    evaluate_badges();

    lv_obj_t *result = panel(s_content, 196, 100, 408, 244, C_NAVY);
    set_bg_gradient(result, 0x082D56, 0x174B73);
    lv_obj_set_style_border_width(result, 2, 0);
    lv_obj_set_style_border_color(result, lv_color_hex(0x66C9FF), 0);
    centered_label(result, "Tüm Çiftleri Buldun!", 16, 24, 376, C_SPACE_TEXT,
                   &hidrolab_font_tr_32);
    centered_label(result, "+30 puan  •  8 eşleşme", 16, 82, 376, 0x7EE7FF,
                   &lv_font_montserrat_14);
    lv_obj_t *wins = centered_label(result, "", 16, 116, 376, C_SPACE_MUTE,
                                    &lv_font_montserrat_14);
    lv_label_set_text_fmt(wins, "Toplam tamamlanan tur: %u",
                          (unsigned)hidrolab_settings_get()->memory_wins);
    lv_obj_t *again = button(result, LV_SYMBOL_REFRESH "  Tekrar Oyna",
                             20, 178, 174, 46, C_BLUE);
    lv_obj_add_event_cb(again, memory_restart_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *exit = button(result, LV_SYMBOL_LEFT "  Oyunlara Dön",
                            210, 178, 174, 46, C_GREEN);
    lv_obj_add_event_cb(exit, game_back_event, LV_EVENT_CLICKED, NULL);
    lv_obj_fade_in(result, 160, 0);
    show_next_badge_popup();
}

static void memory_hide_timer(lv_timer_t *t)
{
    (void)t;
    s_memory_hide_timer = NULL;

    /* The timer can outlive a page transition. Never touch deleted LVGL objects. */
    if (s_page != PAGE_GAMES || s_game_view != GAME_MEMORY) {
        s_memory_first = -1;
        s_memory_second = -1;
        s_memory_busy = false;
        s_memory_timer_action = MEMORY_TIMER_NONE;
        return;
    }

    int picks[2] = {s_memory_first, s_memory_second};
    bool completed = false;
    for (int k = 0; k < 2; ++k) {
        int idx = picks[k];
        if (idx < 0 || idx >= MEMORY_CARDS) continue;

        if (s_memory_timer_action == MEMORY_TIMER_COMMIT_MATCH) {
            if (s_memory_cards[idx]) {
                set_bg_gradient(s_memory_cards[idx], 0x123F3C, 0x0A2B36);
                lv_obj_set_style_border_color(s_memory_cards[idx], lv_color_hex(C_GREEN), 0);
                centered_label(s_memory_cards[idx], LV_SYMBOL_OK, 145, 6, 25,
                               0x8DFFD0, &lv_font_montserrat_14);
            }
        } else {
            if (s_memory_images[idx]) {
                lv_obj_add_flag(s_memory_images[idx], LV_OBJ_FLAG_HIDDEN);
            }
            if (s_memory_back_labels[idx]) {
                lv_obj_remove_flag(s_memory_back_labels[idx], LV_OBJ_FLAG_HIDDEN);
            }
            if (s_memory_cards[idx]) {
                set_bg_gradient(s_memory_cards[idx], C_SURFACE_ALT, 0x102541);
                lv_obj_set_style_border_color(s_memory_cards[idx], lv_color_hex(C_BORDER), 0);
            }
            if (s_memory_back_labels[idx]) {
                lv_label_set_text(s_memory_back_labels[idx], "?");
                lv_obj_set_style_text_color(s_memory_back_labels[idx], lv_color_hex(C_CYAN), 0);
                lv_obj_set_y(s_memory_back_labels[idx], 22);
            }
        }
    }

    if (s_memory_timer_action == MEMORY_TIMER_COMMIT_MATCH && s_memory_pairs == 8) {
        completed = true;
    }

    s_memory_first = -1;
    s_memory_second = -1;
    s_memory_busy = false;
    s_memory_timer_action = MEMORY_TIMER_NONE;

    /* Finish after the final pair animation, outside the click callback. This
     * keeps the result and badge overlays from racing the card event. */
    if (completed) memory_finish_round();
}

static void memory_card_event(lv_event_t *e)
{
    if (s_memory_busy) return;
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= MEMORY_CARDS || idx == s_memory_first) return;

    lv_obj_remove_flag(s_memory_images[idx], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_memory_back_labels[idx], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_border_color(s_memory_cards[idx], lv_color_hex(C_BLUE), 0);
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);

    if (s_memory_first < 0) {
        s_memory_first = idx;
        return;
    }

    s_memory_second = idx;
    s_memory_busy = true;
    if (s_memory_hide_timer) {
        lv_timer_delete(s_memory_hide_timer);
        s_memory_hide_timer = NULL;
    }

    if (s_memory_values[s_memory_first] == s_memory_values[s_memory_second]) {
        int first = s_memory_first;
        int second = s_memory_second;
        s_memory_pairs++;
        lv_obj_remove_flag(s_memory_cards[first], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_memory_cards[second], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(s_memory_cards[first], lv_color_hex(C_GREEN), 0);
        lv_obj_set_style_border_color(s_memory_cards[second], lv_color_hex(C_GREEN), 0);
        if (s_memory_info) lv_label_set_text_fmt(s_memory_info, "Harika!  %d / 8 eşleşme", s_memory_pairs);
        (void)hidrolab_buzzer_beep(s_memory_pairs == 8 ? HIDROLAB_BEEP_SUCCESS : HIDROLAB_BEEP_CORRECT);
        s_memory_timer_action = MEMORY_TIMER_COMMIT_MATCH;
        s_memory_hide_timer = lv_timer_create(memory_hide_timer, 420, NULL);
    } else {
        if (s_memory_info) lv_label_set_text_fmt(s_memory_info, "Tekrar dene  •  %d / 8", s_memory_pairs);
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        s_memory_timer_action = MEMORY_TIMER_HIDE_MISMATCH;
        s_memory_hide_timer = lv_timer_create(memory_hide_timer, 680, NULL);
    }
    if (s_memory_hide_timer) {
        lv_timer_set_repeat_count(s_memory_hide_timer, 1);
    } else {
        /* Never leave the board permanently busy if LVGL cannot allocate a
         * short-lived timer. Immediate reveal/hide is still a valid round. */
        memory_hide_timer(NULL);
    }
}

static void shuffle_memory(void)
{
    int pool[MEMORY_POOL];
    for (int i = 0; i < MEMORY_POOL; ++i) pool[i] = i;
    for (int i = MEMORY_POOL - 1; i > 0; --i) {
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        int tmp = pool[i];
        pool[i] = pool[j];
        pool[j] = tmp;
    }
    for (int i = 0; i < 8; ++i) {
        s_memory_values[i * 2] = pool[i];
        s_memory_values[i * 2 + 1] = pool[i];
    }
    for (int i = MEMORY_CARDS - 1; i > 0; --i) {
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        int tmp = s_memory_values[i];
        s_memory_values[i] = s_memory_values[j];
        s_memory_values[j] = tmp;
    }
}

static void memory_restart_event(lv_event_t *e)
{
    (void)e;
    if (s_memory_hide_timer) {
        lv_timer_delete(s_memory_hide_timer);
        s_memory_hide_timer = NULL;
    }
    s_memory_timer_action = MEMORY_TIMER_NONE;
    s_memory_pairs = 0;
    s_memory_first = -1;
    s_memory_second = -1;
    s_memory_busy = false;
    shuffle_memory();
    request_page_render();
}

static void build_memory(void)
{
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Oyunlar", 16, 7, 112, 34, C_NAVY2);
    lv_obj_add_event_cb(back, game_back_event, LV_EVENT_CLICKED, NULL);
    label(s_content, "Hasat Hafızası", 146, 7, C_SPACE_TEXT, &hidrolab_font_tr_32);
    s_memory_info = label(s_content, "", 552, 15, C_GREEN, &lv_font_montserrat_14);
    lv_label_set_text_fmt(s_memory_info, "%d / 8 eşleşme", s_memory_pairs);
    (void)profile_art(s_content, hidrolab_settings_get()->astronaut_profile, 672, -3, 128);
    lv_obj_t *restart = button(s_content, LV_SYMBOL_REFRESH, 742, 7, 42, 34, C_BLUE);
    lv_obj_add_event_cb(restart, memory_restart_event, LV_EVENT_CLICKED, NULL);

    memset(s_memory_cards, 0, sizeof(s_memory_cards));
    memset(s_memory_images, 0, sizeof(s_memory_images));
    memset(s_memory_back_labels, 0, sizeof(s_memory_back_labels));

    for (int i = 0; i < MEMORY_CARDS; ++i) {
        int col = i % 4;
        int row = i / 4;
        lv_obj_t *c = panel(s_content, 14 + col * 196, 52 + row * 96, 178, 86,
                            C_SURFACE_ALT);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, memory_card_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_set_style_border_width(c, 2, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(C_BORDER), 0);
        /* Artwork is inset; masking children only wastes ARGB layer memory. */
        lv_obj_set_style_clip_corner(c, false, 0);
        set_bg_gradient(c, C_SURFACE_ALT, 0x102541);
        s_memory_cards[i] = c;

        int memory_value = s_memory_values[i];
        s_memory_images[i] = panel(c, 0, 0, 178, 86, C_SURFACE_ALT);
        lv_obj_remove_flag(s_memory_images[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(s_memory_images[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_memory_images[i], 0, 0);
        image(s_memory_images[i], MEMORY_IMAGES[memory_value], 53, 2);
        centered_label(s_memory_images[i], MEMORY_PLANTS[memory_value], 0, 64, 178,
                       C_SPACE_TEXT, &lv_font_montserrat_14);
        lv_obj_add_flag(s_memory_images[i], LV_OBJ_FLAG_HIDDEN);
        s_memory_back_labels[i] = centered_label(c, "?", 0, 22, 178, C_CYAN,
                                                  &hidrolab_font_tr_32);
    }
}

/* ---------------- Retired Mini Quiz ---------------- */
#if 0
static void quiz_choose_questions(int level)
{
    uint8_t avoid = s_quiz_prev_wrong_mask[level - 1];
    bool used[QUIZ_POOL] = {0};
    for (int slot = 0; slot < 3; ++slot) {
        int candidates[QUIZ_POOL];
        int n = 0;
        for (int q = 0; q < QUIZ_POOL; ++q) {
            if (!used[q] && !(avoid & (1U << q))) candidates[n++] = q;
        }
        if (n == 0) {
            for (int q = 0; q < QUIZ_POOL; ++q) {
                if (!used[q]) candidates[n++] = q;
            }
        }
        int pick = candidates[esp_random() % (uint32_t)n];
        used[pick] = true;
        s_quiz_pick[slot] = pick;
    }
}

static void quiz_retry_event(lv_event_t *e)
{
    (void)e;
    s_quiz_index = 0;
    s_quiz_score = 0;
    s_quiz_current_wrong_mask = 0;
    quiz_choose_questions(s_quiz_level);
    request_page_render();
}

static void quiz_next_event(lv_event_t *e)
{
    (void)e;
    if (s_quiz_level < QUIZ_LEVELS) s_quiz_level++;
    s_quiz_index = 0;
    s_quiz_score = 0;
    s_quiz_current_wrong_mask = 0;
    quiz_choose_questions(s_quiz_level);
    request_page_render();
}

static void quiz_finish(void)
{
    int level = s_quiz_level;
    int score = s_quiz_score;
    s_quiz_prev_wrong_mask[level - 1] = s_quiz_current_wrong_mask;
    (void)hidrolab_settings_record_quiz_completion((uint8_t)level, (uint8_t)score);
    if (score >= 2) {
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
    }
    evaluate_badges();
}

static void quiz_render_question(void)
{
    if (s_quiz_index >= 3) {
        quiz_finish();
        lv_obj_clean(s_content);
        memset(s_quiz_buttons, 0, sizeof(s_quiz_buttons));
        lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Bölümler", 18, 10, 120, 36, C_NAVY2);
        lv_obj_add_event_cb(back, quiz_levels_event, LV_EVENT_CLICKED, NULL);
        centered_label(s_content, s_quiz_score >= 2 ? "Görev Başarılı!" : "Tekrar Deneyelim", 0, 66,
                       800, s_quiz_score >= 2 ? C_GREEN : C_ORANGE, &hidrolab_font_tr_32);
        (void)profile_art(s_content, hidrolab_settings_get()->astronaut_profile, 80, 130, 256);
        lv_obj_t *box = panel(s_content, 250, 126, 470, 180, s_quiz_score >= 2 ? C_GREEN2 : 0xFFF0DF);
        lv_obj_fade_in(box, 160, 0);
        lv_obj_t *score = label(box, "", 25, 24, C_TEXT, &hidrolab_font_tr_32);
        lv_label_set_text_fmt(score, "%d / 3 doğru", s_quiz_score);
        lv_obj_t *msg = label(box,
            s_quiz_score >= 2 ? "Bir sonraki bölümün kilidi açıldı. Yıldızlar seni bekliyor!" :
                                "Bu kez farklı sorular gelecek. İpuçlarını düşün ve yeniden dene.",
            25, 78, C_TEXT, &lv_font_montserrat_14);
        lv_obj_set_width(msg, 415);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        bool has_next = s_quiz_score >= 2 && s_quiz_level < QUIZ_LEVELS;
        const char *action = has_next ? "Sonraki Bölüm" :
                             (s_quiz_score >= 2 ? "Bölümlere Dön" : "Tekrar Başla");
        lv_obj_t *again = button(box, action, 25, 126, 180, 40,
                                 s_quiz_score >= 2 ? C_GREEN : C_ORANGE);
        lv_obj_add_event_cb(again,
                            has_next ? quiz_next_event :
                            (s_quiz_score >= 2 ? quiz_levels_event : quiz_retry_event),
                            LV_EVENT_CLICKED, NULL);
        show_next_badge_popup();
        return;
    }

    const quiz_q_t *q = &QUIZ_BANK[s_quiz_level - 1][s_quiz_pick[s_quiz_index]];
    lv_label_set_text_fmt(s_quiz_info, "Bölüm %d  •  Soru %d/3  •  %d doğru",
                          s_quiz_level, s_quiz_index + 1, s_quiz_score);
    lv_label_set_text(s_quiz_question, q->q);
    const char *opts[3] = {q->a, q->b, q->c};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *t = lv_obj_get_child(s_quiz_buttons[i], 0);
        lv_label_set_text(t, opts[i]);
    }
}

static void quiz_feedback_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_quiz_feedback_timer = NULL;
    if (s_page != PAGE_GAMES || s_game_view != GAME_QUIZ || s_quiz_level <= 0) return;

    static const uint32_t colors[3] = {C_BLUE, C_GREEN, C_PURPLE};
    for (int i = 0; i < 3; ++i) {
        if (s_quiz_buttons[i]) {
            lv_obj_set_style_bg_color(s_quiz_buttons[i], lv_color_hex(colors[i]), 0);
            lv_obj_add_flag(s_quiz_buttons[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }
    s_quiz_index++;
    quiz_render_question();
}

static void quiz_option_event(lv_event_t *e)
{
    if (s_quiz_feedback_timer) return;
    int choice = (int)(uintptr_t)lv_event_get_user_data(e);
    const quiz_q_t *q = &QUIZ_BANK[s_quiz_level - 1][s_quiz_pick[s_quiz_index]];
    if (choice == q->ans) {
        s_quiz_score++;
        game_react(s_quiz_score >= 2 ? "Yıldızlara yaklaşıyorsun!" : "Doğru cevap!", true);
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_CORRECT);
    } else {
        s_quiz_current_wrong_mask |= (uint8_t)(1U << s_quiz_pick[s_quiz_index]);
        game_react("Sorun değil, doğru cevabı incele.", false);
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
    }

    for (int i = 0; i < 3; ++i) {
        lv_obj_remove_flag(s_quiz_buttons[i], LV_OBJ_FLAG_CLICKABLE);
        if (i == q->ans) {
            lv_obj_set_style_bg_color(s_quiz_buttons[i], lv_color_hex(C_GREEN), 0);
        } else if (i == choice) {
            lv_obj_set_style_bg_color(s_quiz_buttons[i], lv_color_hex(C_RED), 0);
        }
    }
    lv_label_set_text(s_quiz_info, choice == q->ans ? "Doğru!  Yeni soru hazırlanıyor..." :
                                                     "Olmadı; doğru cevap yeşil renkte.");
    s_quiz_feedback_timer = lv_timer_create(quiz_feedback_timer_cb, 520, NULL);
    if (s_quiz_feedback_timer) {
        lv_timer_set_repeat_count(s_quiz_feedback_timer, 1);
    } else {
        /* Do not rebuild/clean the active button tree from inside its click
         * callback. On allocation pressure, keep the current question usable. */
        static const uint32_t colors[3] = {C_BLUE, C_GREEN, C_PURPLE};
        for (int i = 0; i < 3; ++i) {
            lv_obj_set_style_bg_color(s_quiz_buttons[i], lv_color_hex(colors[i]), 0);
            lv_obj_add_flag(s_quiz_buttons[i], LV_OBJ_FLAG_CLICKABLE);
        }
        lv_label_set_text(s_quiz_info, "Devam edilemedi; cevaba yeniden dokun.");
    }
}

static void quiz_level_event(lv_event_t *e)
{
    s_quiz_level = (int)(uintptr_t)lv_event_get_user_data(e);
    s_quiz_index = 0;
    s_quiz_score = 0;
    s_quiz_current_wrong_mask = 0;
    quiz_choose_questions(s_quiz_level);
    request_page_render();
}

static void build_quiz(void)
{
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    if (s_quiz_level <= 0) {
        lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Oyunlar", 16, 8, 112, 36, C_NAVY2);
        lv_obj_add_event_cb(back, game_back_event, LV_EVENT_CLICKED, NULL);
        label(s_content, "Mini Quiz", 146, 8, C_SPACE_TEXT, &hidrolab_font_tr_32);
        make_guide(s_content, "Her bölüm biraz daha zor. En az 2 doğru ile sonraki bölümü aç!", 16, 54, 768);

        for (int i = 0; i < QUIZ_LEVELS; ++i) {
            int level = i + 1;
            int col = i % 5;
            int row = i / 5;
            bool unlocked = level <= cfg->quiz_unlocked;
            bool done = (cfg->quiz_completed_mask & (1U << i)) != 0;
            bool perfect = (cfg->quiz_perfect_mask & (1U << i)) != 0;
            lv_obj_t *c = panel(s_content, 20 + col * 154, 128 + row * 88, 140, 80,
                                unlocked ? (done ? C_GREEN2 : C_BLUE2) : 0xE8EFF3);
            if (unlocked) {
                lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(c, quiz_level_event, LV_EVENT_CLICKED, (void *)(uintptr_t)level);
            }
            centered_label(c, unlocked ? (perfect ? "★★★" : (done ? "✓" : "★")) : LV_SYMBOL_CLOSE,
                           0, 7, 140, unlocked ? (perfect ? C_YELLOW : C_BLUE) : C_LOCK,
                           &lv_font_montserrat_14);
            lv_obj_t *n = centered_label(c, "", 0, 30, 140, C_TEXT, &lv_font_montserrat_14);
            lv_label_set_text_fmt(n, "Bölüm %d", level);
            centered_label(c, unlocked ? (done ? "Tamamlandı" : "Başla") : "Kilitli", 0, 54, 140,
                           unlocked ? (done ? C_GREEN : C_BLUE) : C_LOCK, &lv_font_montserrat_14);
        }
        return;
    }

    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Bölümler", 16, 8, 120, 36, C_NAVY2);
    lv_obj_add_event_cb(back, quiz_levels_event, LV_EVENT_CLICKED, NULL);
    s_quiz_info = label(s_content, "", 160, 16, 0xD9C7FF, &lv_font_montserrat_14);
    game_companion(s_content, "Düşün, seç ve birlikte öğrenelim!", 626, 61);
    lv_obj_t *qbox = panel(s_content, 24, 61, 582, 112, 0xFFFFFF);
    s_quiz_question = centered_label(qbox, "", 20, 22, 542, C_TEXT, &hidrolab_font_tr_32);
    lv_obj_set_style_text_font(s_quiz_question, &hidrolab_font_tr_14, 0);
    lv_label_set_long_mode(s_quiz_question, LV_LABEL_LONG_WRAP);

    for (int i = 0; i < 3; ++i) {
        s_quiz_buttons[i] = button(s_content, "", 30, 190 + i * 73, 566, 56,
                                   i == 0 ? C_BLUE : (i == 1 ? C_GREEN : C_PURPLE));
        lv_obj_add_event_cb(s_quiz_buttons[i], quiz_option_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }
    quiz_render_question();
}
#endif

/* ---------------- Bubble game ---------------- */
static void bubble_move(void)
{
    if (!s_bubble || !s_bubble_area) return;
    static const uint32_t colors[5] = {0x3EB9F1, 0x8B65D7, 0x2DD5C4, 0xE96DA6, 0x53C7FF};
    int size = 46 + (int)(esp_random() % 25U);
    int x = 180 + (int)(esp_random() % (unsigned)(500 - size));
    int y = 72 + (int)(esp_random() % (unsigned)(220 - size));
    lv_obj_set_size(s_bubble, size, size);
    lv_obj_set_style_radius(s_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_bubble,
                              lv_color_hex(colors[esp_random() % 5U]), 0);
    lv_obj_set_pos(s_bubble, x, y);
}

static void bubble_event(lv_event_t *e)
{
    (void)e;
    if (esp_timer_get_time() >= s_bubble_end_us) return;
    s_bubble_combo++;
    s_bubble_hits++;
    int reward = 4 + (s_bubble_combo > 6 ? 6 : s_bubble_combo);
    s_bubble_score += reward;
    if (s_bubble_info) {
        lv_label_set_text_fmt(s_bubble_info, "%d puan  •  x%d kombo",
                              s_bubble_score, s_bubble_combo);
    }
    game_react(s_bubble_combo >= 8 ? "Galaktik kombo!" :
               s_bubble_combo >= 4 ? "Süper seri!" : "Parlayan hedef!", true);
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    bubble_move();
    if (s_bubble_bonus && s_bubble_hits % 4 == 0) {
        s_bubble_bonus_until = s_bubble_hits + 2;
        lv_obj_set_x(s_bubble_bonus, 580 + (int)(esp_random() % 90U));
        lv_obj_remove_flag(s_bubble_bonus, LV_OBJ_FLAG_HIDDEN);
    } else if (s_bubble_bonus && s_bubble_bonus_until &&
               s_bubble_hits >= s_bubble_bonus_until) {
        s_bubble_bonus_until = 0;
        lv_obj_add_flag(s_bubble_bonus, LV_OBJ_FLAG_HIDDEN);
    }
}

static void bubble_bonus_event(lv_event_t *e)
{
    (void)e;
    if (!s_bubble_bonus || s_bubble_finished ||
        lv_obj_has_flag(s_bubble_bonus, LV_OBJ_FLAG_HIDDEN) ||
        esp_timer_get_time() >= s_bubble_end_us) return;
    s_bubble_score += 12;
    s_bubble_bonus_until = 0;
    lv_obj_add_flag(s_bubble_bonus, LV_OBJ_FLAG_HIDDEN);
    if (s_bubble_info) {
        lv_label_set_text_fmt(s_bubble_info, "%d puan  •  x%d kombo",
                              s_bubble_score, s_bubble_combo);
    }
    game_react("Altın kabarcık! +12 puan", true);
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_CORRECT);
}

static void bubble_restart_event(lv_event_t *e)
{
    (void)e;
    s_bubble_score = 0;
    s_bubble_combo = 0;
    s_bubble_hits = 0;
    s_bubble_bonus_until = 0;
    s_bubble_finished = false;
    s_bubble_end_us = esp_timer_get_time() + 30000000LL;
    request_page_render();
}

static void bubble_show_result(void)
{
    if (!s_bubble_area) return;

    lv_obj_t *result = panel(s_bubble_area, 176, 70, 404, 236, C_NAVY);
    set_bg_gradient(result, 0x082D56, 0x174B73);
    lv_obj_set_style_border_width(result, 2, 0);
    lv_obj_set_style_border_color(result, lv_color_hex(0x66C9FF), 0);
    lv_obj_fade_in(result, 160, 0);

    centered_label(result, "Süre Doldu!", 16, 20, 372, 0xFFFFFF, &hidrolab_font_tr_32);
    lv_obj_t *score = centered_label(result, "", 16, 72, 372, 0x7EE7FF, &hidrolab_font_tr_32);
    lv_label_set_text_fmt(score, "%d puan", s_bubble_score);

    lv_obj_t *best = centered_label(result, "", 16, 113, 372, C_SPACE_MUTE,
                                     &lv_font_montserrat_14);
    lv_label_set_text_fmt(best, "En iyi skor: %u", (unsigned)hidrolab_settings_get()->bubble_best);

    lv_obj_t *again = button(result, LV_SYMBOL_REFRESH "  Tekrar Oyna", 20, 166, 174, 48, C_BLUE);
    lv_obj_add_event_cb(again, bubble_restart_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *exit = button(result, LV_SYMBOL_LEFT "  Oyunlara Dön", 210, 166, 174, 48, C_GREEN);
    lv_obj_add_event_cb(exit, game_back_event, LV_EVENT_CLICKED, NULL);
}

static void build_bubble(void)
{
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Oyunlar", 16, 8, 112, 36, C_NAVY2);
    lv_obj_add_event_cb(back, game_back_event, LV_EVENT_CLICKED, NULL);
    label(s_content, "Kabarcık Yakala", 146, 8, C_SPACE_TEXT, &hidrolab_font_tr_32);
    s_bubble_info = label(s_content, "0 puan  •  x0 kombo", 520, 16, 0xD9C7FF,
                          &lv_font_montserrat_14);
    lv_obj_t *restart = button(s_content, LV_SYMBOL_REFRESH, 742, 8, 42, 36, C_BLUE);
    lv_obj_add_event_cb(restart, bubble_restart_event, LV_EVENT_CLICKED, NULL);

    s_bubble_area = panel(s_content, 22, 58, 756, 360, 0xDDF5FF);
    set_bg_gradient(s_bubble_area, 0xEAFBFF, 0x9ADCF7);
    game_companion(s_bubble_area, "Kabarcıkları birlikte yakalayalım!", 10, 16);
    centered_label(s_bubble_area, "Renkli kabarcıkları yakala, kombo yap!", 176, 20, 520,
                   C_NAVY, &lv_font_montserrat_14);
    s_bubble_progress = lv_bar_create(s_bubble_area);
    lv_obj_set_pos(s_bubble_progress, 180, 50);
    lv_obj_set_size(s_bubble_progress, 420, 10);
    lv_bar_set_range(s_bubble_progress, 0, 300);
    lv_bar_set_value(s_bubble_progress, 300, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bubble_progress, lv_color_hex(0xC4E9F8), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bubble_progress, lv_color_hex(C_BLUE), LV_PART_INDICATOR);
    s_bubble_time = label(s_bubble_area, "30 sn", 608, 42, C_NAVY, &lv_font_montserrat_14);

    /* Static ambient bubbles preserve the depth without five always-running
     * alpha animations competing with touch handling. */
    static const uint32_t ambient_colors[8] = {
        0xFFFFFF, 0xA4EDFF, 0xC6B2F4, 0xFFE7A2,
        0xB0F4DF, 0xF5B9D5, 0xB8D9FF, 0xFFFFFF,
    };
    for (int i = 0; i < 8; ++i) {
        int size = 14 + (i % 3) * 7;
        lv_obj_t *ambient = game_art_shape(s_bubble_area,
                                           198 + (i * 83) % 475,
                                           98 + (i * 61) % 190,
                                           size, size, ambient_colors[i],
                                           LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(ambient, LV_OPA_40, 0);
        lv_obj_set_style_border_width(ambient, 1, 0);
        lv_obj_set_style_border_color(ambient, lv_color_hex(0xFFFFFF), 0);
    }
    centered_label(s_bubble_area, "Altın kabarcık +12 puan!", 188, 329, 360,
                   0x315B77, &lv_font_montserrat_14);
    s_bubble = lv_button_create(s_bubble_area);
    lv_obj_set_size(s_bubble, 58, 58);
    lv_obj_set_style_radius(s_bubble, 29, 0);
    lv_obj_set_style_border_width(s_bubble, 3, 0);
    lv_obj_set_style_border_color(s_bubble, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(s_bubble, lv_color_hex(0x4BB8F2), 0);
    lv_obj_set_style_bg_opa(s_bubble, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(s_bubble, 12, 0);
    lv_obj_set_style_shadow_color(s_bubble, lv_color_hex(0xFFFFFF), 0);
    lv_obj_t *drop_icon = label(s_bubble, LV_SYMBOL_TINT, 0, 0,
                                0xFFFFFF, &lv_font_montserrat_14);
    lv_obj_center(drop_icon);
    lv_obj_t *shine = game_art_shape(s_bubble, 12, 10, 10, 10, 0xFFFFFF,
                                     LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(shine, LV_OPA_70, 0);
    lv_obj_add_event_cb(s_bubble, bubble_event, LV_EVENT_CLICKED, NULL);
    s_bubble_bonus = lv_button_create(s_bubble_area);
    lv_obj_set_pos(s_bubble_bonus, 590, 296);
    lv_obj_set_size(s_bubble_bonus, 48, 48);
    lv_obj_set_style_radius(s_bubble_bonus, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_bubble_bonus, lv_color_hex(0xF5BC45), 0);
    lv_obj_set_style_border_width(s_bubble_bonus, 3, 0);
    lv_obj_set_style_border_color(s_bubble_bonus, lv_color_hex(0xFFF4C9), 0);
    lv_obj_set_style_shadow_width(s_bubble_bonus, 10, 0);
    lv_obj_set_style_shadow_color(s_bubble_bonus, lv_color_hex(0xFFD16B), 0);
    lv_obj_t *bonus_text = label(s_bubble_bonus, "+12", 0, 0,
                                  0x623F23, &lv_font_montserrat_14);
    lv_obj_center(bonus_text);
    lv_obj_add_event_cb(s_bubble_bonus, bubble_bonus_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_bubble_bonus, LV_OBJ_FLAG_HIDDEN);
    if (s_bubble_end_us <= esp_timer_get_time()) s_bubble_end_us = esp_timer_get_time() + 30000000LL;
    bubble_move();
}

/* ---------------- Su Yolu ---------------- */
#define PIPE_N 0x01U
#define PIPE_E 0x02U
#define PIPE_S 0x04U
#define PIPE_W 0x08U

static uint8_t water_direction_bit(int from, int to)
{
    if (to == from - 3) return PIPE_N;
    if (to == from + 1) return PIPE_E;
    if (to == from + 3) return PIPE_S;
    return PIPE_W;
}

static bool water_path_dfs(uint8_t path[9], bool visited[9], int depth)
{
    int current = path[depth - 1];
    if (depth == 9) return current % 3 == 2;

    uint8_t order[4] = {0, 1, 2, 3};
    for (int i = 3; i > 0; --i) {
        int j = (int)(esp_random() % (unsigned)(i + 1));
        uint8_t swap = order[i];
        order[i] = order[j];
        order[j] = swap;
    }
    static const int8_t delta[4] = {-3, 1, 3, -1};
    for (int k = 0; k < 4; ++k) {
        int dir = order[k];
        int next = current + delta[dir];
        if (next < 0 || next >= 9) continue;
        if ((dir == 1 && current % 3 == 2) || (dir == 3 && current % 3 == 0)) continue;
        if (visited[next]) continue;
        if (depth == 8 && next % 3 != 2) continue;
        visited[next] = true;
        path[depth] = (uint8_t)next;
        if (water_path_dfs(path, visited, depth + 1)) return true;
        visited[next] = false;
    }
    return false;
}

static uint32_t water_solution_signature(void)
{
    uint32_t signature = 2166136261U;
    for (int i = 0; i < 9; ++i) {
        signature ^= s_water_solution[i];
        signature *= 16777619U;
    }
    signature ^= (uint32_t)s_water_start_index << 8;
    signature ^= (uint32_t)s_water_end_index << 16;
    return signature;
}

static void water_build_solution(const uint8_t path[9])
{
    memset(s_water_solution, 0, sizeof(s_water_solution));
    s_water_start_index = path[0];
    s_water_end_index = path[8];
    s_water_solution[path[0]] |= PIPE_W;
    s_water_solution[path[8]] |= PIPE_E;
    for (int i = 0; i < 8; ++i) {
        int from = path[i];
        int to = path[i + 1];
        s_water_solution[from] |= water_direction_bit(from, to);
        s_water_solution[to] |= water_direction_bit(to, from);
    }
}

static void water_generate_solution(void)
{
    uint8_t path[9] = {0};
    bool made = false;
    for (int attempt = 0; attempt < 64; ++attempt) {
        bool visited[9] = {false};
        path[0] = (uint8_t)((esp_random() % 3U) * 3U);
        visited[path[0]] = true;
        if (!water_path_dfs(path, visited, 1)) continue;
        water_build_solution(path);
        uint32_t signature = water_solution_signature();
        if (signature != s_water_last_signature) {
            s_water_last_signature = signature;
            made = true;
            break;
        }
    }
    if (!made) {
        static const uint8_t fallback_a[9] = {0, 3, 6, 7, 4, 1, 2, 5, 8};
        static const uint8_t fallback_b[9] = {6, 3, 0, 1, 4, 7, 8, 5, 2};
        water_build_solution(fallback_a);
        if (water_solution_signature() == s_water_last_signature) {
            water_build_solution(fallback_b);
        }
        s_water_last_signature = water_solution_signature();
    }
}

static uint8_t water_rotated_mask(int index)
{
    uint8_t mask = s_water_solution[index];
    for (int turn = 0; turn < s_water_rot[index]; ++turn) {
        mask = (uint8_t)(((mask << 1) | (mask >> 3)) & 0x0FU);
    }
    return mask;
}

static bool water_is_solved(void)
{
    for (int i = 0; i < 9; ++i) {
        if (water_rotated_mask(i) != s_water_solution[i]) return false;
    }
    return true;
}

static void water_init_level(void)
{
    water_generate_solution();
    int wrong = 0;
    for (int shuffle = 0; shuffle < 12; ++shuffle) {
        wrong = 0;
        for (int i = 0; i < 9; ++i) {
            s_water_rot[i] = (uint8_t)(esp_random() % 4U);
            if (water_rotated_mask(i) != s_water_solution[i]) wrong++;
        }
        if (wrong >= 5) break;
    }
    if (wrong == 0) s_water_rot[0] = 1;
}

static int water_elapsed_seconds(void)
{
    if (s_water_start_us <= 0) return 0;
    int64_t elapsed_us = esp_timer_get_time() - s_water_start_us;
    if (elapsed_us < 0) elapsed_us = 0;
    return (int)(elapsed_us / 1000000LL);
}

static void water_update_hud(void)
{
    if (!s_water_status) return;
    int seconds = s_water_finished ? s_water_final_seconds : water_elapsed_seconds();
    char text[64];
    snprintf(text, sizeof(text), "HAT %u/3  •  %d hamle  •  %d sn",
             (unsigned)s_water_level + 1U, s_water_moves, seconds);
    label_text_changed(s_water_status, text);
}

static void water_refresh_tile(int index)
{
    if (index < 0 || index >= 9 || !s_water_tiles[index]) return;
    uint8_t mask = water_rotated_mask(index);
    const uint8_t bits[4] = {PIPE_N, PIPE_E, PIPE_S, PIPE_W};
    for (int arm = 0; arm < 4; ++arm) {
        if (!s_water_arms[index][arm]) continue;
        lv_obj_set_style_bg_opa(s_water_arms[index][arm],
                                (mask & bits[arm]) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    /* No correctness glow: the child must reason about the complete route. */
    lv_obj_set_style_border_color(s_water_tiles[index], lv_color_hex(0x557694), 0);
    lv_obj_set_style_shadow_width(s_water_tiles[index], 0, 0);
}

static void water_tile_event(lv_event_t *e)
{
    int index = (int)(uintptr_t)lv_event_get_user_data(e);
    if (s_water_finished || index < 0 || index >= 9) return;
    s_water_rot[index] = (uint8_t)((s_water_rot[index] + 1U) % 4U);
    s_water_moves++;
    water_refresh_tile(index);
    water_update_hud();
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
}

static void water_check_event(lv_event_t *e)
{
    (void)e;
    if (!water_is_solved()) {
        game_react("Henüz kaçak var; bağlantı yönlerini tekrar kontrol et!", false);
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        return;
    }
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_CORRECT);
    if (s_water_level < 2) {
        s_water_level++;
        water_init_level();
        request_page_render();
        return;
    }
    s_water_finished = true;
    s_water_final_seconds = water_elapsed_seconds();
    if (!s_water_recorded) {
        (void)hidrolab_settings_record_activity_game(ACTIVITY_WATER_PATH, 20, 0);
        s_water_recorded = true;
    }
    request_page_render();
}

static void water_restart_event(lv_event_t *e)
{
    (void)e;
    s_water_level = 0;
    s_water_moves = 0;
    s_water_start_us = esp_timer_get_time();
    s_water_final_seconds = 0;
    s_water_finished = false;
    s_water_recorded = false;
    water_init_level();
    request_page_render();
}

static void build_water_path(void)
{
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Oyunlar", 16, 8, 112, 36, C_NAVY2);
    lv_obj_add_event_cb(back, game_back_event, LV_EVENT_CLICKED, NULL);
    label(s_content, "Su Yolu", 146, 8, C_SPACE_TEXT, &hidrolab_font_tr_32);

    if (s_water_finished) {
        game_companion(s_content, "Suyu bitkiye ulaştırdın; sistem nefes aldı!", 626, 62);
        lv_obj_t *result = panel(s_content, 74, 91, 520, 264, C_SPACE_CARD);
        set_bg_gradient(result, C_SURFACE_ALT, 0x0A2542);
        lv_obj_set_style_border_width(result, 2, 0);
        lv_obj_set_style_border_color(result, lv_color_hex(C_CYAN), 0);
        centered_label(result, "Su Akışı Tamam!", 20, 30, 480, C_CYAN,
                       &hidrolab_font_tr_32);
        lv_obj_t *summary = centered_label(result, "", 20, 94, 480,
                                            C_SPACE_TEXT, &hidrolab_font_tr_14);
        lv_label_set_text_fmt(summary, "3/3 hat  •  %d hamle  •  %d sn  •  +20 puan",
                              s_water_moves, s_water_final_seconds);
        lv_obj_t *again = button(result, LV_SYMBOL_REFRESH "  Tekrar", 62, 184, 170, 48, C_BLUE);
        lv_obj_add_event_cb(again, water_restart_event, LV_EVENT_CLICKED, NULL);
        lv_obj_t *exit = button(result, LV_SYMBOL_LEFT "  Oyunlara Dön", 280, 184, 178, 48, C_GREEN);
        lv_obj_add_event_cb(exit, game_back_event, LV_EVENT_CLICKED, NULL);
        return;
    }

    lv_obj_t *hud = panel(s_content, 328, 8, 278, 42, 0x163659);
    s_water_status = centered_label(hud, "", 8, 12, 262, 0xD9F1FF,
                                    &lv_font_montserrat_14);
    water_update_hud();

    lv_obj_t *area = panel(s_content, 20, 62, 586, 340, 0x102E50);
    set_bg_gradient(area, 0x102E50, 0x16476B);
    centered_label(area, "Parçalara dokun, su yolunu kesintisiz bağla", 10, 10, 566,
                   0xCDEEFF, &hidrolab_font_tr_14);
    int start_row = s_water_start_index / 3;
    int end_row = s_water_end_index / 3;
    lv_obj_t *source = game_art_shape(area, 13, 63 + start_row * 88, 38, 38,
                                      0x45D7FF, LV_RADIUS_CIRCLE);
    centered_label(source, LV_SYMBOL_TINT, 0, 10, 38, 0xFFFFFF, &lv_font_montserrat_14);
    centered_label(area, "DEPO", 5, 104 + start_row * 88, 54, 0x8BDFFF,
                   &lv_font_montserrat_14);
    game_art_shape(area, 43, 77 + start_row * 88, 23, 8, 0x45D7FF, 4);

    for (int i = 0; i < 9; ++i) {
        int col = i % 3;
        int row = i / 3;
        lv_obj_t *tile = panel(area, 62 + col * 84, 42 + row * 88, 76, 80, 0x173F61);
        lv_obj_set_style_radius(tile, 12, 0);
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, water_tile_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        s_water_tiles[i] = tile;
        s_water_arms[i][0] = game_art_shape(tile, 33, 0, 10, 35, 0x55D9FF, 4);
        s_water_arms[i][1] = game_art_shape(tile, 38, 35, 38, 10, 0x55D9FF, 4);
        s_water_arms[i][2] = game_art_shape(tile, 33, 40, 10, 40, 0x55D9FF, 4);
        s_water_arms[i][3] = game_art_shape(tile, 0, 35, 38, 10, 0x55D9FF, 4);
        lv_obj_t *joint = game_art_shape(tile, 28, 30, 20, 20, 0xA6F3FF,
                                         LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(joint, 2, 0);
        lv_obj_set_style_border_color(joint, lv_color_hex(0xFFFFFF), 0);
        water_refresh_tile(i);
    }
    game_art_shape(area, 306, 77 + end_row * 88, 34, 8, 0x45D7FF, 4);
    lv_obj_t *plant = panel(area, 334, 46 + end_row * 88, 58, 68, 0x173F61);
    game_art_shape(plant, 26, 25, 7, 34, 0x54CA78, 4);
    lv_obj_t *leaf_a = game_art_shape(plant, 7, 12, 25, 20, 0x65D987, LV_RADIUS_CIRCLE);
    lv_obj_set_style_transform_rotation(leaf_a, 330, 0);
    lv_obj_t *leaf_b = game_art_shape(plant, 28, 5, 25, 20, 0x48BD70, LV_RADIUS_CIRCLE);
    lv_obj_set_style_transform_rotation(leaf_b, 420, 0);
    centered_label(area, "BİTKİ", 326, 118 + end_row * 88, 72, 0x92E9AB,
                   &lv_font_montserrat_14);

    lv_obj_t *mission = panel(area, 405, 48, 164, 174, 0xF5FBFF);
    centered_label(mission, "SUYU BİTKİYE GÖTÜR", 8, 14, 148, C_BLUE,
                   &hidrolab_font_tr_14);
    lv_obj_t *mission_text = centered_label(mission,
        "Depodan çıkan bütün bağlantıları bitkiye kadar kesintisiz kur.",
        14, 50, 136, C_TEXT, &lv_font_montserrat_14);
    lv_label_set_long_mode(mission_text, LV_LABEL_LONG_WRAP);
    lv_obj_t *check = button(area, LV_SYMBOL_OK "  Suyu Gönder", 405, 244,
                             164, 52, C_BLUE);
    lv_obj_add_event_cb(check, water_check_event, LV_EVENT_CLICKED, NULL);
    game_companion(s_content, "Her tur yeni bir hat: depodan bitkiye ulaştır!", 626, 66);
}

/* ---------------- Kök Labirenti ---------------- */
static const uint8_t MAZE_BLOCKS[3][30] = {
    {1,1,0,0,1,1, 1,0,0,0,1,1, 1,1,1,0,0,1, 1,0,1,0,0,1, 1,1,1,1,0,0},
    {1,1,0,0,1,1, 1,1,0,0,1,1, 1,0,0,1,1,1, 1,0,0,0,1,1, 1,1,1,0,0,0},
    {1,0,0,1,1,1, 1,0,0,1,1,1, 1,0,0,0,1,1, 1,1,1,0,0,1, 1,1,1,1,0,0},
};

static void maze_init_level(void)
{
    memset(s_maze_visited, 0, sizeof(s_maze_visited));
    s_maze_path[0] = 2;
    s_maze_path_len = 1;
    s_maze_visited[2] = true;
}

static void maze_refresh(void)
{
    int current = s_maze_path_len ? s_maze_path[s_maze_path_len - 1] : 2;
    for (int i = 0; i < 30; ++i) {
        if (!s_maze_cells[i]) continue;
        bool blocked = MAZE_BLOCKS[s_maze_level][i] != 0;
        uint32_t bg = blocked ? 0x334C62 : 0xEDF8F3;
        if (i == 29) bg = 0xDDF7FF;
        if (s_maze_visited[i]) bg = i == current ? 0x72DF92 : 0xC8F2D5;
        lv_obj_set_style_bg_color(s_maze_cells[i], lv_color_hex(bg), 0);
        lv_obj_set_style_border_color(s_maze_cells[i],
                                      lv_color_hex(i == current ? C_GREEN : 0x86AABE), 0);
        lv_obj_set_style_shadow_width(s_maze_cells[i], i == current ? 8 : 0, 0);
        if (s_maze_nodes[i]) {
            lv_obj_set_style_bg_opa(s_maze_nodes[i], s_maze_visited[i] ? LV_OPA_COVER : LV_OPA_20, 0);
            lv_obj_set_style_bg_color(s_maze_nodes[i], lv_color_hex(i == current ? 0xFFFFFF : 0xF1E4C9), 0);
        }
    }
    if (s_maze_status) {
        lv_label_set_text_fmt(s_maze_status, "LABİRENT %u/3  •  %u adım",
                              (unsigned)s_maze_level + 1U,
                              (unsigned)(s_maze_path_len - 1U));
    }
}

static void maze_cell_event(lv_event_t *e)
{
    int next = (int)(uintptr_t)lv_event_get_user_data(e);
    if (s_maze_finished || next < 0 || next >= 30 || MAZE_BLOCKS[s_maze_level][next]) return;
    int current = s_maze_path[s_maze_path_len - 1];
    if (s_maze_path_len >= 2 && next == s_maze_path[s_maze_path_len - 2]) {
        s_maze_visited[current] = false;
        s_maze_path_len--;
        maze_refresh();
        game_react("Geri aldık; başka bir dal deneyebiliriz.", true);
        return;
    }
    int row_delta = next / 6 - current / 6;
    int col_delta = next % 6 - current % 6;
    if ((row_delta < 0 ? -row_delta : row_delta) +
        (col_delta < 0 ? -col_delta : col_delta) != 1 || s_maze_visited[next]) {
        game_react("Kök yalnızca komşu ve açık hücreye uzayabilir.", false);
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        return;
    }
    s_maze_path[s_maze_path_len++] = (uint8_t)next;
    s_maze_visited[next] = true;
    maze_refresh();
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    if (next != 29) {
        game_react(s_maze_path_len > 6 ? "Su kokusu yakında; devam et!" : "Kök büyüyor!", true);
        return;
    }
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_CORRECT);
    if (s_maze_level < 2) {
        s_maze_level++;
        maze_init_level();
        request_page_render();
        return;
    }
    s_maze_finished = true;
    if (!s_maze_recorded) {
        (void)hidrolab_settings_record_activity_game(ACTIVITY_ROOT_MAZE, 20, 0);
        s_maze_recorded = true;
    }
    request_page_render();
}

static void maze_reset_event(lv_event_t *e)
{
    (void)e;
    maze_init_level();
    maze_refresh();
    game_react("Labirent sıfırlandı; yeni yolu dene.", true);
}

static void maze_restart_event(lv_event_t *e)
{
    (void)e;
    s_maze_level = 0;
    s_maze_finished = false;
    s_maze_recorded = false;
    maze_init_level();
    request_page_render();
}

static void __attribute__((unused)) build_root_maze(void)
{
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Oyunlar", 16, 8, 112, 36, C_NAVY2);
    lv_obj_add_event_cb(back, game_back_event, LV_EVENT_CLICKED, NULL);
    label(s_content, "Kök Labirenti", 146, 8, C_SPACE_TEXT, &hidrolab_font_tr_32);

    if (s_maze_finished) {
        game_companion(s_content, "Üç kök de suya ulaştı; harika yön buldun!", 626, 62);
        lv_obj_t *result = panel(s_content, 74, 91, 520, 264, 0xFFFFFF);
        centered_label(result, "Kökler Suya Ulaştı!", 20, 30, 480, C_GREEN,
                       &hidrolab_font_tr_32);
        centered_label(result, "3 / 3 labirent  •  +20 puan", 20, 94, 480,
                       C_PURPLE, &hidrolab_font_tr_14);
        lv_obj_t *again = button(result, LV_SYMBOL_REFRESH "  Tekrar", 62, 184, 170, 48, C_BLUE);
        lv_obj_add_event_cb(again, maze_restart_event, LV_EVENT_CLICKED, NULL);
        lv_obj_t *exit = button(result, LV_SYMBOL_LEFT "  Oyunlara Dön", 280, 184, 178, 48, C_GREEN);
        lv_obj_add_event_cb(exit, game_back_event, LV_EVENT_CLICKED, NULL);
        return;
    }

    lv_obj_t *hud = panel(s_content, 408, 8, 198, 42, 0x163659);
    s_maze_status = centered_label(hud, "", 8, 12, 182, 0xD9F1FF,
                                   &lv_font_montserrat_14);
    lv_obj_t *area = panel(s_content, 20, 62, 586, 340, 0xEAF6F1);
    set_bg_gradient(area, 0xF6FBF8, 0xDCEFE8);
    centered_label(area, "Komşu karelere dokun • önceki kareye dokunarak geri al", 8, 8, 570,
                   C_NAVY, &lv_font_montserrat_14);

    for (int i = 0; i < 30; ++i) {
        int col = i % 6;
        int row = i / 6;
        bool blocked = MAZE_BLOCKS[s_maze_level][i] != 0;
        lv_obj_t *cell = panel(area, 18 + col * 74, 38 + row * 56, 66, 48,
                               blocked ? 0x334C62 : 0xEDF8F3);
        lv_obj_set_style_radius(cell, 9, 0);
        lv_obj_set_style_border_width(cell, 2, 0);
        if (!blocked) {
            lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(cell, maze_cell_event, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
        } else {
            for (int rock = 0; rock < 2; ++rock) {
                game_art_shape(cell, 10 + rock * 26, 14 - rock * 4, 20, 20,
                               rock ? 0x45647B : 0x5B7588, LV_RADIUS_CIRCLE);
            }
        }
        s_maze_cells[i] = cell;
        if (!blocked) {
            s_maze_nodes[i] = game_art_shape(cell, 23, 14, 20, 20, 0xF1E4C9,
                                              LV_RADIUS_CIRCLE);
        }
        if (i == 2) centered_label(cell, "FİLİZ", 0, 16, 66, C_GREEN, &lv_font_montserrat_14);
        if (i == 29) centered_label(cell, LV_SYMBOL_TINT, 0, 16, 66, C_BLUE, &lv_font_montserrat_14);
    }
    lv_obj_t *reset = button(area, LV_SYMBOL_REFRESH "  Sıfırla", 466, 235, 104, 52, C_PURPLE);
    lv_obj_add_event_cb(reset, maze_reset_event, LV_EVENT_CLICKED, NULL);
    centered_label(area, "HEDEF", 466, 294, 104, C_BLUE, &lv_font_montserrat_14);
    maze_refresh();
    game_companion(s_content, "Kökü kayanın içinden değil, açık yoldan suya götür!", 626, 66);
}

/* ---------------- Tohum İnişi ---------------- */
static void landing_prepare_round(void)
{
    s_landing_row = 0;
    s_landing_col = (int)(esp_random() % 5U);
    s_landing_target_col = (int)(esp_random() % 5U);
    for (int i = 0; i < 4; ++i) {
        s_landing_obstacles[i] = (int)(esp_random() % 5U);
    }
    if (s_landing_obstacles[0] == s_landing_col) {
        s_landing_obstacles[0] = (s_landing_obstacles[0] + 2) % 5;
    }
}

static void landing_init_game(void)
{
    s_landing_score = 0;
    s_landing_lives = 3;
    s_landing_round = 1;
    s_landing_finished = false;
    s_landing_recorded = false;
    landing_prepare_round();
}

static void landing_refresh(void)
{
    if (s_landing_pod) {
        lv_obj_set_pos(s_landing_pod, 40 + s_landing_col * 82,
                       32 + s_landing_row * 48);
    }
    if (s_landing_target) lv_obj_set_x(s_landing_target, 34 + s_landing_target_col * 82);
    for (int i = 0; i < 4; ++i) {
        if (s_landing_rocks[i]) lv_obj_set_x(s_landing_rocks[i], 43 + s_landing_obstacles[i] * 82);
    }
    if (s_landing_info) {
        lv_label_set_text_fmt(s_landing_info, "UÇUŞ %d/8  •  %d iniş  •  Kalkan %d",
                              s_landing_round, s_landing_score, s_landing_lives);
    }
}

static void landing_move_event(lv_event_t *e)
{
    if (s_landing_finished) return;
    int direction = (int)(intptr_t)lv_event_get_user_data(e);
    int next = s_landing_col + direction;
    if (next < 0 || next > 4) {
        game_react("Şerit bitti; diğer yöne dön!", false);
        return;
    }
    s_landing_col = next;
    landing_refresh();
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
}

static void landing_finish_game(void)
{
    s_landing_finished = true;
    if (s_landing_timer) lv_timer_pause(s_landing_timer);
    if (!s_landing_recorded) {
        (void)hidrolab_settings_record_activity_game(ACTIVITY_LANDING, 20, 0);
        s_landing_recorded = true;
    }
    request_page_render();
}

static void landing_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_page != PAGE_GAMES || s_game_view != GAME_SEED_LANDING || s_landing_finished) return;
    s_landing_row++;
    if (s_landing_row >= 1 && s_landing_row <= 4 &&
        s_landing_col == s_landing_obstacles[s_landing_row - 1]) {
        s_landing_lives--;
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        game_react("Meteor çarptı! Yeni kapsül hazırlanıyor.", false);
        if (s_landing_lives <= 0 || s_landing_round >= 8) {
            landing_finish_game();
            return;
        }
        s_landing_round++;
        landing_prepare_round();
        request_page_render();
        return;
    }
    if (s_landing_row >= 5) {
        bool landed = s_landing_col == s_landing_target_col;
        if (landed) {
            s_landing_score++;
            (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_CORRECT);
            game_react("Yumuşak iniş! Tohum yuvada.", true);
        } else {
            (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
            game_react("Yuvayı kaçırdık; sıradaki uçuşa odaklan!", false);
        }
        if (s_landing_round >= 8) {
            landing_finish_game();
            return;
        }
        s_landing_round++;
        landing_prepare_round();
        request_page_render();
        return;
    }
    landing_refresh();
}

static void landing_restart_event(lv_event_t *e)
{
    (void)e;
    landing_init_game();
    request_page_render();
}

static void __attribute__((unused)) build_seed_landing(void)
{
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Oyunlar", 16, 8, 112, 36, C_NAVY2);
    lv_obj_add_event_cb(back, game_back_event, LV_EVENT_CLICKED, NULL);
    label(s_content, "Tohum İnişi", 146, 8, C_SPACE_TEXT, &hidrolab_font_tr_32);

    if (s_landing_finished) {
        game_companion(s_content, s_landing_score >= 5 ?
                       "Usta pilotsun; seraya yeni tohumlar geldi!" :
                       "Görev bitti; bir tur daha bizi ustalaştırır!", 626, 62);
        lv_obj_t *result = panel(s_content, 74, 91, 520, 264, 0xFFFFFF);
        centered_label(result, "İniş Görevi Tamam!", 20, 30, 480, C_ORANGE,
                       &hidrolab_font_tr_32);
        lv_obj_t *score = centered_label(result, "", 20, 94, 480, C_PURPLE,
                                         &hidrolab_font_tr_14);
        lv_label_set_text_fmt(score, "%d / 8 başarılı iniş  •  +20 puan", s_landing_score);
        lv_obj_t *again = button(result, LV_SYMBOL_REFRESH "  Tekrar", 62, 184, 170, 48, C_BLUE);
        lv_obj_add_event_cb(again, landing_restart_event, LV_EVENT_CLICKED, NULL);
        lv_obj_t *exit = button(result, LV_SYMBOL_LEFT "  Oyunlara Dön", 280, 184, 178, 48, C_GREEN);
        lv_obj_add_event_cb(exit, game_back_event, LV_EVENT_CLICKED, NULL);
        return;
    }

    lv_obj_t *hud = panel(s_content, 360, 8, 246, 42, 0x163659);
    s_landing_info = centered_label(hud, "", 6, 12, 234, 0xD9F1FF,
                                    &lv_font_montserrat_14);
    s_landing_area = panel(s_content, 20, 62, 586, 340, 0x111C42);
    set_bg_gradient(s_landing_area, 0x111C42, 0x2B2354);
    centered_label(s_landing_area, "Oklarla tohumu yönlendir • meteorlardan kaç • yeşil yuvaya in",
                   8, 7, 570, 0xD9E9FF, &lv_font_montserrat_14);
    for (int i = 0; i < 5; ++i) {
        game_art_shape(s_landing_area, 56 + i * 82, 35, 2, 270, 0x415480, 1);
    }
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *rock = game_art_shape(s_landing_area, 43, 80 + i * 48, 30, 30,
                                        0xB86E62, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(rock, 2, 0);
        lv_obj_set_style_border_color(rock, lv_color_hex(0xF3A17F), 0);
        game_art_shape(rock, 7, 7, 8, 8, 0x76485E, LV_RADIUS_CIRCLE);
        s_landing_rocks[i] = rock;
    }
    s_landing_target = panel(s_landing_area, 34, 292, 50, 30, 0x38B96A);
    lv_obj_set_style_radius(s_landing_target, 7, 0);
    lv_obj_set_style_border_width(s_landing_target, 2, 0);
    lv_obj_set_style_border_color(s_landing_target, lv_color_hex(0x9BFFC0), 0);
    centered_label(s_landing_target, "YUVA", 0, 8, 50, 0xFFFFFF, &lv_font_montserrat_14);

    s_landing_pod = panel(s_landing_area, 40, 32, 34, 38, 0xE9A64D);
    lv_obj_set_style_radius(s_landing_pod, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_landing_pod, 3, 0);
    lv_obj_set_style_border_color(s_landing_pod, lv_color_hex(0xFFF3C6), 0);
    game_art_shape(s_landing_pod, 12, 8, 10, 16, 0x8C5A36, LV_RADIUS_CIRCLE);
    game_art_shape(s_landing_pod, 4, 28, 26, 5, 0x72E6FF, 3);

    lv_obj_t *controls = panel(s_landing_area, 458, 65, 112, 238, 0x182F59);
    centered_label(controls, "PİLOT", 6, 15, 100, 0x8BDFFF, &hidrolab_font_tr_14);
    centered_label(controls, "Tohum her an aşağı iner.", 12, 52, 88, 0xFFFFFF,
                   &lv_font_montserrat_14);
    lv_obj_t *left = button(controls, LV_SYMBOL_LEFT, 8, 116, 44, 58, C_BLUE);
    lv_obj_add_event_cb(left, landing_move_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)-1);
    lv_obj_t *right = button(controls, LV_SYMBOL_RIGHT, 60, 116, 44, 58, C_BLUE);
    lv_obj_add_event_cb(right, landing_move_event, LV_EVENT_CLICKED,
                        (void *)(intptr_t)1);
    centered_label(controls, "3 KALKAN", 6, 198, 100, 0xFFD58B, &lv_font_montserrat_14);
    landing_refresh();
    game_companion(s_content, "Tohumu sağa sola sür; yeşil yuvaya yumuşak indir!", 626, 66);
    s_landing_timer = lv_timer_create(landing_timer_cb, 650, NULL);
}

/* ---------------- Böceklerden Kaç ---------------- */
/* Scrolling lane markers in one small 764x8 redraw area.
 * The larger lunar craters now also move as actual LVGL objects. */
static void bug_road_draw(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(obj, &bounds);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(0x747D88);
    dsc.bg_opa = LV_OPA_80;
    dsc.radius = 2;
    const int phase = (s_bug_road_phase_q8 >> 8) % 135;
    for (int x = 18 - phase; x < 764; x += 135) {
        int left = x < 0 ? 0 : x;
        int right = (x + 50) > 764 ? 764 : (x + 50);
        if (left >= right) continue;
        lv_area_t rect = {bounds.x1 + left, bounds.y1,
                          bounds.x1 + right - 1, bounds.y1 + 4};
        lv_draw_rect(layer, &dsc, &rect);
    }
}

static void bug_spawn_obstacle(void)
{
    /* One obstacle at a time, with a generous random approach distance.
     * Even at maximum speed this leaves more than one full jump cycle. */
    s_bug_x = 720 + (int)(esp_random() % 121U);
    s_bug_x_q8 = s_bug_x << 8;
    s_bug_variant = (int)(esp_random() % 3U);
    s_bug_pack = s_bug_score < 3 ? 1 : 1 + (int)(esp_random() % (s_bug_score < 8 ? 2U : 3U));
    s_bug_collision_checked = false;
    if (s_bug_object) {
        /* DIRECT redraw needs the complete sprite inside its object bounds. */
        lv_obj_set_width(s_bug_object, 40 + (s_bug_pack - 1) * 36);
        for (int i = 0; i < 2; ++i) {
            if (!s_bug_pack_extra[i]) continue;
            if (i + 1 < s_bug_pack) lv_obj_remove_flag(s_bug_pack_extra[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(s_bug_pack_extra[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_pos(s_bug_object, s_bug_x - 5, 275 - s_bug_variant);
    }
}

static void bug_init_game(void)
{
    s_bug_air_active = false;
    s_bug_air_due_us = 0;
    s_bug_safe_until_us = 0;
    s_bug_hud_lives = -1;
    s_bug_score = 0;
    s_bug_lives = 1;
    s_bug_jumps = 0;
    s_bug_speed = 180;
    s_bug_final_seconds = 0;
    s_bug_start_us = 0;
    s_bug_jump_start_us = 0;
    s_bug_jump_until_us = 0;
    s_bug_jump_cooldown_us = 0;
    s_bug_jump_duration_us = 800000LL;
    s_bug_jump_queued = false;
    s_bug_game_now_us = 0;
    s_bug_last_tick_us = 0;
    s_bug_air_draw_x = -10000;
    s_bug_hud_score = -1;
    s_bug_hud_seconds = -1;
    s_bug_hud_speed = -1;
    s_bug_draw_plant_y = -10000;
    s_bug_draw_object_x = -10000;
    s_bug_draw_shadow_opa = LV_OPA_TRANSP;
    s_bug_road_phase_q8 = 0;
    s_bug_road_draw_phase = -1;
    for (unsigned i = 0; i < BUG_GROUND_CRATERS; ++i)
        s_bug_crater_draw_x[i] = -10000;
    s_bug_started = false;
    s_bug_finished = false;
    s_bug_recorded = false;
    bug_spawn_obstacle();
}

static void bug_begin_run(void)
{
    bug_init_game();
    s_bug_started = true;
    s_bug_start_us = esp_timer_get_time();
    s_bug_game_now_us = s_bug_start_us;
    s_bug_last_tick_us = s_bug_start_us;
}

static int bug_jump_height(int64_t now)
{
    const int64_t duration_us = s_bug_jump_duration_us;
    if (s_bug_jump_start_us <= 0 || now < s_bug_jump_start_us ||
        now >= s_bug_jump_until_us) return 0;
    int64_t elapsed = now - s_bug_jump_start_us;
    int32_t t = (int32_t)((elapsed * 1024LL) / duration_us);
    if (t < 0) t = 0;
    if (t > 1024) t = 1024;
    return (int)((4LL * t * (1024 - t) * 120LL) / (1024LL * 1024LL));
}

static void bug_update_hud(void)
{
    if (!s_bug_started) {
        if (s_bug_info) label_text_changed(s_bug_info, "Ay yüzeyine dokun ve koşuya başla");
        if (s_bug_time) label_text_changed(s_bug_time, "60 sn kaldı");
        return;
    }
    int64_t elapsed = s_bug_game_now_us - s_bug_start_us;
    if (elapsed < 0) elapsed = 0;
    int seconds = (int)(elapsed / 1000000LL);
    if (s_bug_info && (s_bug_hud_score != s_bug_score || s_bug_hud_lives != s_bug_lives)) {
        char text[64];
        snprintf(text, sizeof(text), "SKOR %d  •  %d CAN", s_bug_score, s_bug_lives);
        label_text_changed(s_bug_info, text);
        s_bug_hud_score = s_bug_score;
        s_bug_hud_lives = s_bug_lives;
    }
    if (s_bug_time && s_bug_hud_seconds != seconds) {
        char text[32];
        int remaining = BUG_TARGET_SECONDS - seconds;
        if (remaining < 0) remaining = 0;
        snprintf(text, sizeof(text), "%d sn kaldı", remaining);
        label_text_changed(s_bug_time, text);
        s_bug_hud_seconds = seconds;
    }
    if (s_bug_progress && s_bug_hud_speed != s_bug_speed) {
        lv_bar_set_value(s_bug_progress, s_bug_speed, LV_ANIM_OFF);
        s_bug_hud_speed = s_bug_speed;
    }
}

static void bug_refresh(void)
{
    int64_t now = s_bug_game_now_us;
    int height = bug_jump_height(now);
    int plant_y = 236 - height;
    if (s_bug_plant && plant_y != s_bug_draw_plant_y) {
        lv_obj_set_y(s_bug_plant, plant_y);
        s_bug_draw_plant_y = plant_y;
    }
    /* Keep the shadow at a fixed size: repeatedly changing its position,
     * width and opacity caused 3 needless LVGL invalidations per frame. */
    if (s_bug_shadow) {
        lv_opa_t shadow_opa = height > 60 ? LV_OPA_20 : LV_OPA_40;
        if (shadow_opa != s_bug_draw_shadow_opa) {
            lv_obj_set_style_bg_opa(s_bug_shadow, shadow_opa, 0);
            s_bug_draw_shadow_opa = shadow_opa;
        }
    }
    if (s_bug_object && s_bug_x != s_bug_draw_object_x) {
        lv_obj_set_x(s_bug_object, s_bug_x - 5);
        s_bug_draw_object_x = s_bug_x;
    }
    if (s_bug_air && s_bug_air_active) {
        int air_x = (s_bug_air_x_q8 >> 8) - 4;
        if (air_x != s_bug_air_draw_x) {
            lv_obj_set_x(s_bug_air, air_x);
            s_bug_air_draw_x = air_x;
        }
    }
    /* Animate the visible large crater features too. Previously these
     * remained fixed while only a 3px strip moved, so the ground looked
     * stationary. Each crater is a tiny dirty rectangle in DIRECT mode. */
    const int distance = s_bug_road_phase_q8 >> 8;
    static const int start_x[BUG_GROUND_CRATERS] = {214, 514, 690};
    static const int widths[BUG_GROUND_CRATERS] = {61, 43, 33};
    for (unsigned i = 0; i < BUG_GROUND_CRATERS; ++i) {
        if (!s_bug_craters[i]) continue;
        const int period = 764 + widths[i];
        int x = start_x[i] - (distance % period);
        if (x < -widths[i]) x += period;
        if (x != s_bug_crater_draw_x[i]) {
            lv_obj_set_x(s_bug_craters[i], x);
            s_bug_crater_draw_x[i] = x;
        }
    }
    /* Move the lane markers in a single small draw strip. */
    int road_phase = distance;
    if (s_bug_road_strip && road_phase != s_bug_road_draw_phase) {
        lv_obj_invalidate(s_bug_road_strip);
        s_bug_road_draw_phase = road_phase;
    }
    bug_update_hud();
}

/* One common game clock drives jumps, insects, collisions and 60 s target.
 * Air time shortens modestly as the tempo increases, but peak height stays
 * high enough to clear a full three-insect pack. */
static void bug_begin_jump(int64_t now)
{
    int speed = s_bug_speed;
    if (speed < 180) speed = 180;
    if (speed > 350) speed = 350;
    s_bug_jump_duration_us = 800000LL - ((int64_t)(speed - 180) * 120000LL / 170LL);
    s_bug_jump_start_us = now;
    s_bug_jump_until_us = now + s_bug_jump_duration_us;
    s_bug_jump_cooldown_us = s_bug_jump_until_us;
    s_bug_jump_queued = false;
    ++s_bug_jumps;
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
}

static void bug_jump_event(lv_event_t *e)
{
    (void)e;
    if (s_bug_finished || hidrolab_buzzer_alarm_active()) return;
    if (!s_bug_started) {
        bug_begin_run();
    }
    int64_t now = s_bug_game_now_us;
    if (now < s_bug_jump_until_us) {
        /* Pressing shortly before landing queues one next jump. */
        if (s_bug_jump_until_us - now <= 140000LL) s_bug_jump_queued = true;
        return;
    }
    if (now < s_bug_jump_cooldown_us) return;
    bug_begin_jump(now);
    bug_refresh();
}

static void bug_finish_game(void)
{
    if (s_bug_finished) return;
    s_bug_finished = true;
    s_bug_final_seconds = (int)((s_bug_game_now_us - s_bug_start_us) / 1000000LL);
    if (s_bug_final_seconds > BUG_TARGET_SECONDS)
        s_bug_final_seconds = BUG_TARGET_SECONDS;
    if (s_bug_lives > 0)
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
    if (s_bug_timer) lv_timer_pause(s_bug_timer);
    if (!s_bug_recorded) {
        (void)hidrolab_settings_record_activity_game(ACTIVITY_BUG_ESCAPE, 20, 0);
        s_bug_recorded = true;
    }
    request_page_render();
}

static int bug_speed_for_progress(int elapsed_seconds, int cleared_bugs)
{
    if (elapsed_seconds < 0) elapsed_seconds = 0;
    if (cleared_bugs < 0) cleared_bugs = 0;
    /* Gradual curve; keep the late game playable on a touchscreen. */
    int speed = 180 + elapsed_seconds * 2 + cleared_bugs * 3;
    return speed > 350 ? 350 : speed;
}

static bool bug_collision_at(int bug_x, int jump_height)
{
    /* Slightly inset hitboxes; avoid punishing transparent sprite edges. */
    const int plant_hit_left = 101;
    const int plant_hit_right = 115;
    const int bug_hit_left = bug_x + 8;
    const int bug_hit_right = bug_x + 22 + (s_bug_pack - 1) * 36;
    return bug_hit_left < plant_hit_right && bug_hit_right > plant_hit_left &&
           jump_height < 22;
}

static bool bug_passed_at(int bug_x)
{
    return bug_x + 26 + (s_bug_pack - 1) * 36 < 96;
}

static void bug_air_spawn(int64_t now)
{
    if (!s_bug_air) return;
    static const lv_image_dsc_t *const sprites[] = {
        &hidrolab_img_runner_butterfly, &hidrolab_img_runner_ladybug,
        &hidrolab_img_runner_wasp
    };
    s_bug_air_kind = (int)(esp_random() % 3U);
    s_bug_air_x_q8 = 780 * 256;
    s_bug_air_active = true;
    lv_image_set_src(s_bug_air, sprites[s_bug_air_kind]);
    lv_obj_remove_flag(s_bug_air, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_bug_air, 776, 165);
    s_bug_air_draw_x = 776;
    s_bug_air_due_us = now + 6500000LL + (esp_random() % 2500000U);
}

static void bug_air_tick(int64_t now, int64_t delta_us, int height)
{
    if (!s_bug_air_due_us) s_bug_air_due_us = now + 4500000LL;
    if (!s_bug_air_active && now >= s_bug_air_due_us) bug_air_spawn(now);
    if (!s_bug_air_active || !s_bug_air) return;
    s_bug_air_x_q8 -= (int32_t)((int64_t)(s_bug_speed + 20) * delta_us * 256 / 1000000);
    int x = s_bug_air_x_q8 >> 8;
    bool touches = x + 30 > 96 && x + 5 < 118 &&
                   236 - height < 195 && 292 - height > 170;
    if (touches) {
        if (s_bug_air_kind < 2) {
            if (s_bug_lives < 3) ++s_bug_lives;
            (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_CORRECT);
        } else if (now >= s_bug_safe_until_us) {
            --s_bug_lives;
            s_bug_safe_until_us = now + 700000LL;
            (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        }
    }
    if (touches || x < -40) {
        s_bug_air_active = false;
        lv_obj_add_flag(s_bug_air, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Advance GAME LOGIC using short fixed maximum steps, but update LVGL
 * objects only once per displayed frame. This prevents collisions from
 * tunneling through a hitbox if the LCD misses a frame. */
static void bug_simulate_step(int64_t step_us)
{
    s_bug_game_now_us += step_us;
    int64_t now = s_bug_game_now_us;
    int elapsed_seconds = (int)((now - s_bug_start_us) / 1000000LL);
    s_bug_speed = bug_speed_for_progress(elapsed_seconds, s_bug_score);
    if (s_bug_jump_queued && now >= s_bug_jump_until_us) {
        bug_begin_jump(now);
    }
    int32_t step_q8 = (int32_t)(((int64_t)s_bug_speed * step_us * 256LL + 500000LL) / 1000000LL);
    s_bug_x_q8 -= step_q8;
    s_bug_x = s_bug_x_q8 >> 8;
    /* Accumulate travelled distance for smooth ground scroll. The 60-second
     * session stays well within int32_t even at the fastest game speed. */
    s_bug_road_phase_q8 += step_q8;
    int jump_height = bug_jump_height(now);
    bug_air_tick(now, step_us, jump_height);
    if (!s_bug_collision_checked && bug_collision_at(s_bug_x, jump_height)) {
        s_bug_collision_checked = true;
        if (now >= s_bug_safe_until_us) {
            --s_bug_lives;
            s_bug_safe_until_us = now + 900000LL;
            (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        }
    }
    if (s_bug_lives <= 0) return;
    if (!s_bug_collision_checked && bug_passed_at(s_bug_x)) {
        s_bug_collision_checked = true;
        s_bug_score += s_bug_pack;
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_CORRECT);
    }
    if (s_bug_x < -36) bug_spawn_obstacle();
}

static void bug_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_page != PAGE_GAMES || s_game_view != GAME_BUG_ESCAPE ||
        s_bug_finished || !s_bug_started) return;
    int64_t wall_now = esp_timer_get_time();
    int64_t delta_us = s_bug_last_tick_us > 0 ? wall_now - s_bug_last_tick_us : 33333LL;
    s_bug_last_tick_us = wall_now;
    if (hidrolab_buzzer_alarm_active()) return;
    if (delta_us < 0) delta_us = 0;
    /* Avoid seconds-long teleportation after a blocking event. Under normal
     * load use real elapsed time, not an artificially slowed game clock. */
    if (delta_us > 160000LL) delta_us = 160000LL;
    while (delta_us > 0 && s_bug_lives > 0) {
        int64_t step = delta_us > 20000LL ? 20000LL : delta_us;
        bug_simulate_step(step);
        delta_us -= step;
    }
    if (s_bug_lives <= 0) {
        bug_finish_game();
        return;
    }
    int elapsed_seconds = (int)((s_bug_game_now_us - s_bug_start_us) / 1000000LL);
    if (elapsed_seconds >= BUG_TARGET_SECONDS) {
        bug_finish_game();
        return;
    }
    bug_refresh();
}

static void bug_restart_event(lv_event_t *e)
{
    (void)e;
    bug_begin_run();
    request_page_render();
}

static void build_bug_escape(void)
{
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Oyunlar", 16, 8, 112, 36, C_NAVY2);
    lv_obj_add_event_cb(back, game_back_event, LV_EVENT_CLICKED, NULL);
    label(s_content, "Böceklerden Kaç", 146, 8, C_SPACE_TEXT, &hidrolab_font_tr_32);

    if (s_bug_finished) {
        game_companion(s_content, s_bug_lives > 0 ?
                       "Başardın! Bitkini bitişe ulaştırdın!" :
                       "Bu kez böceğe takıldık. Yeniden deneyelim!", 626, 62);
        lv_obj_t *result = panel(s_content, 62, 80, 544, 286, C_SPACE_CARD);
        set_bg_gradient(result, C_SURFACE_ALT, 0x0A2542);
        lv_obj_set_style_border_width(result, 2, 0);
        lv_obj_set_style_border_color(result, lv_color_hex(C_GREEN), 0);
        centered_label(result, s_bug_lives > 0 ? "KAZANDIN!" : "KOŞU BİTTİ", 20, 25, 504, 0x85A8BE,
                       &hidrolab_font_tr_14);
        lv_obj_t *score = centered_label(result, "", 20, 58, 504, 0x8DFFD0,
                                         &hidrolab_font_tr_32);
        lv_label_set_text_fmt(score, "%d", s_bug_score);
        lv_obj_t *detail = centered_label(result, "", 20, 116, 504, 0xD9F1FF,
                                          &hidrolab_font_tr_14);
        lv_label_set_text_fmt(detail, "%d saniye  •  %d zıplayış  •  +20 puan",
                              s_bug_final_seconds, s_bug_jumps);
        centered_label(result, s_bug_lives > 0 ?
                       "60 saniyeyi tamamladın. Bitkin güvende!" :
                       "Hedef: 60 saniye boyunca böceklerden kaç!",
                       20, 150, 504, 0x9EC1D3, &hidrolab_font_tr_14);
        lv_obj_t *again = button(result, LV_SYMBOL_REFRESH "  Tekrar", 68, 207, 176, 50, C_BLUE);
        lv_obj_add_event_cb(again, bug_restart_event, LV_EVENT_CLICKED, NULL);
        lv_obj_t *exit = button(result, LV_SYMBOL_LEFT "  Oyunlara Dön", 292, 207, 184, 50, C_GREEN);
        lv_obj_add_event_cb(exit, game_back_event, LV_EVENT_CLICKED, NULL);
        return;
    }

    lv_obj_t *hud = panel(s_content, 458, 8, 210, 42, 0x163659);
    s_bug_info = centered_label(hud, "", 6, 12, 198, 0xD9F1FF,
                                &lv_font_montserrat_14);
    (void)profile_art(s_content, hidrolab_settings_get()->astronaut_profile,
                      708, -4, 120);
    s_bug_area = panel(s_content, 18, 58, 764, 382, 0x122946);
    lv_obj_set_style_bg_color(s_bug_area, lv_color_hex(0x0A1022), 0);
    lv_obj_set_style_border_color(s_bug_area, lv_color_hex(0x354665), 0);
    lv_obj_add_flag(s_bug_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_bug_area, bug_jump_event, LV_EVENT_PRESSED, NULL);
    lv_obj_t *jump_hint = panel(s_bug_area, 300, 14, 164, 34, 0x173B5B);
    lv_obj_remove_flag(jump_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(jump_hint, 1, 0);
    lv_obj_set_style_border_color(jump_hint, lv_color_hex(0x54769A), 0);
    centered_label(jump_hint, "DOKUN  •  ZIPLA", 6, 10, 152,
                   0xE2F3FF, &lv_font_montserrat_14);

    /* Flat 2D Moon scene: sparse sky, Earth and a low-contrast parallax ridge. */
    const int star_x[7] = {42, 166, 258, 378, 526, 610, 724};
    const int star_y[7] = {76, 130, 83, 116, 72, 137, 91};
    for (int i = 0; i < 7; ++i) {
        lv_obj_t *star = game_art_shape(s_bug_area, star_x[i], star_y[i],
                                        i == 3 ? 4 : 2, i == 3 ? 4 : 2,
                                        0xE9F4FF, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(star, i == 3 ? LV_OPA_80 : LV_OPA_50, 0);
    }
    lv_obj_t *earth = game_art_shape(s_bug_area, 654, 43, 67, 67,
                                      0x3C91D0, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(earth, 2, 0);
    lv_obj_set_style_border_color(earth, lv_color_hex(0xB8E7FF), 0);
    game_art_shape(earth, 8, 19, 30, 16, 0x67C187, LV_RADIUS_CIRCLE);
    game_art_shape(earth, 34, 39, 23, 12, 0x78CF91, LV_RADIUS_CIRCLE);

    lv_obj_t *ridge = game_art_shape(s_bug_area, -28, 242, 350, 72,
                                     0x303B55, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(ridge, LV_OPA_50, 0);
    ridge = game_art_shape(s_bug_area, 286, 250, 420, 64,
                           0x26344D, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(ridge, LV_OPA_50, 0);

    game_art_shape(s_bug_area, 0, 300, 764, 82, 0xBFC4CC, 0);
    game_art_shape(s_bug_area, 0, 298, 764, 3, 0xE8EAED, 1);
    /* The ground's large recognizable features now scroll with the game.
     * Keep them as 3 small shapes, not one 764x82 invalidated bitmap. */
    s_bug_craters[0] = game_art_shape(s_bug_area, 214, 326, 61, 20,
                                     0x88919E, LV_RADIUS_CIRCLE);
    s_bug_craters[1] = game_art_shape(s_bug_area, 514, 349, 43, 14,
                                     0x88919E, LV_RADIUS_CIRCLE);
    s_bug_craters[2] = game_art_shape(s_bug_area, 690, 333, 33, 11,
                                     0x88919E, LV_RADIUS_CIRCLE);
    for (unsigned i = 0; i < BUG_GROUND_CRATERS; ++i)
        lv_obj_set_style_bg_opa(s_bug_craters[i], LV_OPA_70, 0);
    s_bug_road_strip = lv_obj_create(s_bug_area);
    lv_obj_set_pos(s_bug_road_strip, 0, 313);
    lv_obj_set_size(s_bug_road_strip, 764, 8);
    lv_obj_remove_style_all(s_bug_road_strip);
    lv_obj_remove_flag(s_bug_road_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_bug_road_strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_bug_road_strip, bug_road_draw, LV_EVENT_DRAW_MAIN, NULL);
    s_bug_road_draw_phase = -1;

    s_bug_object = panel(s_bug_area, 715, 275, 40, 28, 0xE9564A);
    lv_obj_set_style_bg_opa(s_bug_object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bug_object, 0, 0);
    lv_obj_set_style_radius(s_bug_object, 0, 0);
    lv_obj_remove_flag(s_bug_object, LV_OBJ_FLAG_CLICKABLE);
    /* All beetle images now stay INSIDE the parent. Without this, DIRECT
     * mode may leave old pixels/trails outside the invalidated rectangle. */
    (void)image(s_bug_object, &hidrolab_img_runner_beetle, 0, 0);
    for (int i = 0; i < 2; ++i) {
        s_bug_pack_extra[i] = image(s_bug_object, &hidrolab_img_runner_beetle,
                                    36 * (i + 1), 0);
    }
    s_bug_air = image(s_bug_area, &hidrolab_img_runner_butterfly, 776, 165);
    lv_obj_remove_flag(s_bug_air, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_bug_air, LV_OBJ_FLAG_HIDDEN);
    s_bug_air_active = false;
    centered_label(s_bug_area, "Kelebek / uğur böceği: +1 can    Mor böcek: -1 can", 130, 349,
                   500, 0x142635, &hidrolab_font_tr_14);

    s_bug_shadow = game_art_shape(s_bug_area, 76, 296, 60, 8,
                                   0x626975, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(s_bug_shadow, LV_OPA_40, 0);
    s_bug_plant = image(s_bug_area, &hidrolab_img_garden_sprout, 74, 236);
    lv_obj_remove_flag(s_bug_plant, LV_OBJ_FLAG_CLICKABLE);

    s_bug_time = centered_label(s_bug_area, "60 sn kaldı", 18, 20, 120, 0xFFFFFF,
                                &hidrolab_font_tr_14);
    label(s_bug_area, "TEMPO", 536, 23, 0x85A8BE, &lv_font_montserrat_14);
    s_bug_progress = lv_bar_create(s_bug_area);
    lv_obj_set_pos(s_bug_progress, 590, 27);
    lv_obj_set_size(s_bug_progress, 132, 8);
    lv_bar_set_range(s_bug_progress, 180, 350);
    lv_obj_set_style_radius(s_bug_progress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bug_progress, lv_color_hex(0x243C58), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bug_progress, lv_color_hex(0x65DFA4),
                              LV_PART_INDICATOR);

    /* High-frequency dirty-area updates are affordable in DIRECT mode.
     * Other screens remain in full-refresh mode to preserve status bar. */
    bug_display_fast_mode(true);
    s_bug_timer = lv_timer_create(bug_timer_cb, 33, NULL);
    bug_spawn_obstacle();
    bug_refresh();
}

/* Tohumdan Hasada was retired to reserve flash for future interactive games. */
#if 0
/* ---------------- Visual Tohumdan Hasada game ---------------- */
static const char *lesson_title(void)
{
    return "Tohumdan Hasada";
}

static uint8_t lesson_activity_bit(void)
{
    return ACTIVITY_SEED;
}

static int lesson_total(void)
{
    return 4;
}

static void lesson_feedback_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_lesson_feedback_timer = NULL;
    if (s_page != PAGE_GAMES || s_game_view != GAME_SEED_TO_HARVEST) return;
    s_lesson_index++;
    request_page_render();
}

static void lesson_visual_choice_event(lv_event_t *e)
{
    if (s_lesson_feedback_timer || s_lesson_index >= lesson_total()) return;
    int choice = (int)(uintptr_t)lv_event_get_user_data(e);
    int correct_choice = s_lesson_index;
    bool correct = choice == correct_choice;
    lv_obj_t *picked = lv_event_get_target(e);
    int picked_w = lv_obj_get_width(picked);
    lv_obj_t *mark = centered_label(picked, correct ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE,
                                     picked_w - 40, 8, 28,
                                     correct ? C_GREEN : C_RED,
                                     &lv_font_montserrat_14);
    lv_obj_move_foreground(mark);
    if (correct) {
        s_lesson_score++;
        lv_obj_set_style_border_width(picked, 4, 0);
        lv_obj_set_style_border_color(picked, lv_color_hex(C_GREEN), 0);
        game_react(s_lesson_score == lesson_total() ? "Görev tamam!" : "Doğru sıra!", true);
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_CORRECT);
    } else {
        lv_obj_set_style_border_width(picked, 4, 0);
        lv_obj_set_style_border_color(picked, lv_color_hex(C_RED), 0);
        lv_obj_remove_flag(picked, LV_OBJ_FLAG_CLICKABLE);
        game_react("Sırayı düşün: tohumdan başla.", false);
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (s_lesson_buttons[i]) lv_obj_remove_flag(s_lesson_buttons[i], LV_OBJ_FLAG_CLICKABLE);
    }
    if (s_lesson_info) lv_label_set_text(s_lesson_info, "Harika! Sıradaki sahne hazırlanıyor...");
    s_lesson_feedback_timer = lv_timer_create(lesson_feedback_timer_cb, 520, NULL);
    if (s_lesson_feedback_timer) lv_timer_set_repeat_count(s_lesson_feedback_timer, 1);
}

static lv_obj_t *visual_choice_card(lv_obj_t *parent, int x, int y, int w, int h,
                                    uint32_t bg, int choice)
{
    lv_obj_t *card = panel(parent, x, y, w, h, bg);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xB8DEF4), 0);
    lv_obj_set_style_translate_y(card, 3, LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, lesson_visual_choice_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)choice);
    if (choice >= 0 && choice < 4) s_lesson_buttons[choice] = card;
    return card;
}

static void lesson_restart_event(lv_event_t *e)
{
    (void)e;
    s_lesson_index = 0;
    s_lesson_score = 0;
    s_lesson_recorded = false;
    for (int i = 3; i > 0; --i) {
        int j = (int)(esp_random() % (unsigned)(i + 1));
        int swap = s_seed_order[i];
        s_seed_order[i] = s_seed_order[j];
        s_seed_order[j] = swap;
    }
    request_page_render();
}

static void __attribute__((unused)) build_root_doctor(lv_obj_t *area)
{
    background_orb(area, -30, 230, 120, 0x62D6FF, LV_OPA_20);
    background_orb(area, 500, -30, 110, C_GREEN, LV_OPA_20);
    centered_label(area, "Koyu ve yorgun kökü bul, üzerine dokun", 18, 16, 550,
                   C_NAVY, &hidrolab_font_tr_14);
    lv_obj_t *plant_card = panel(area, 206, 48, 174, 90, 0xFFFFFF);
    lv_obj_t *plant = image(plant_card,
                            hidrolab_memory_plant_images[(s_lesson_index + 2) % 8], 13, 10);
    lv_obj_set_style_radius(plant, 12, 0);
    lv_obj_t *cup = panel(area, 249, 125, 88, 38, 0x394C60);
    lv_obj_set_style_radius(cup, 8, 0);

    int target = (s_lesson_index * 2 + 1) % 3;
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *card = visual_choice_card(area, 24 + i * 181, 175, 160, 154,
                                            0xF4FBFF, i);
        bool tired = i == target;
        lv_obj_t *root = panel(card, 64, 14, 32, 108, tired ? 0xA96E45 : 0xE8F5ED);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
        start_float_animation(root, -3, 4, 720 + i * 120, i * 90);
        lv_obj_set_style_radius(root, 16, 0);
        lv_obj_set_style_border_color(root, lv_color_hex(tired ? 0x70442E : 0xB9DDC8), 0);
        for (int node = 0; node < 3; ++node) {
            lv_obj_t *dot = panel(card, node % 2 ? 42 : 98, 35 + node * 30, 15, 15,
                                  tired ? 0xC98A5C : 0xFFFFFF);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        }
        centered_label(card, "Kök", 0, 127, 160, C_MUTED, &lv_font_montserrat_14);
    }
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *bubble = panel(area, 116 + i * 104, 274 - (i % 2) * 28,
                                 13 + (i % 2) * 5, 13 + (i % 2) * 5, 0xBCEEFF);
        lv_obj_remove_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(bubble, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_60, 0);
        start_float_animation(bubble, 22, -82, 900 + i * 130, i * 110);
    }
}

static void __attribute__((unused)) build_leaf_detective(lv_obj_t *area)
{
    background_orb(area, 480, 225, 125, C_YELLOW, LV_OPA_20);
    centered_label(area, "Sarı lekeli bitkiyi yakala", 18, 14, 550,
                   C_NAVY, &hidrolab_font_tr_14);
    int target = (s_lesson_index * 3 + 2) % 4;
    for (int i = 0; i < 4; ++i) {
        int col = i % 2;
        int row = i / 2;
        lv_obj_t *card = visual_choice_card(area, 24 + col * 273, 50 + row * 142,
                                            250, 126, 0xF3FBFF, i);
        lv_obj_t *plant = image(card,
                                hidrolab_memory_plant_images[(s_lesson_index * 3 + i) % 8],
                                51, 18);
        if (i == target) {
            lv_obj_set_style_image_recolor(plant, lv_color_hex(C_YELLOW), 0);
            lv_obj_set_style_image_recolor_opa(plant, LV_OPA_60, 0);
            for (int spot = 0; spot < 3; ++spot) {
                lv_obj_t *dot = panel(card, 92 + spot * 28, 38 + (spot % 2) * 22,
                                      11, 11, 0xA85B32);
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_border_width(dot, 0, 0);
            }
        }
        centered_label(card, "Dokun", 0, 98, 250, C_MUTED, &lv_font_montserrat_14);
    }
    lv_obj_t *scanner = panel(area, 10, 48, 566, 5, 0x45D7FF);
    lv_obj_remove_flag(scanner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(scanner, 0, 0);
    lv_obj_set_style_bg_opa(scanner, LV_OPA_50, 0);
    start_float_animation(scanner, 0, 258, 1250, 0);
}

static void build_stage_icon(lv_obj_t *card, int stage)
{
    static const char *names[4] = {"Tohum", "Filiz", "Bitki", "Hasat"};
    lv_obj_t *halo = game_art_shape(card, 51, 4, 108, 88,
                                     stage == 0 ? 0xF6E5BD : 0xDDF6E8,
                                     LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(halo, LV_OPA_50, 0);
    lv_obj_t *stage_image = image(card, hidrolab_growth_stage_images[stage], 57, 4);
    start_float_animation(stage_image, 1, -3, 920 + stage * 70, stage * 60);
    centered_label(card, names[stage], 0, 94, 210, C_TEXT, &hidrolab_font_tr_14);
}

static void build_seed_to_harvest(lv_obj_t *area)
{
    centered_label(area, "Karışan evreleri doğru sırayla seç", 18, 12, 550,
                   C_NAVY, &hidrolab_font_tr_14);
    game_art_shape(area, 188, 45, 192, 4, 0xB8D6E8, 2);
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *step = game_art_shape(area, 180 + i * 64, 35, 22, 22,
                                        i < s_lesson_index ? C_GREEN :
                                        i == s_lesson_index ? C_BLUE : 0xD5E5EF,
                                        LV_RADIUS_CIRCLE);
        centered_label(step, i < s_lesson_index ? LV_SYMBOL_OK : "", 0, 5, 22,
                       0xFFFFFF, &lv_font_montserrat_14);
    }
    for (int pos = 0; pos < 4; ++pos) {
        int stage = s_seed_order[pos];
        int col = pos % 2;
        int row = pos / 2;
        lv_obj_t *card = visual_choice_card(area, 66 + col * 258, 64 + row * 132,
                                            210, 122,
                                            stage < s_lesson_index ? C_GREEN2 : 0xF7FBFF, stage);
        build_stage_icon(card, stage);
        if (stage < s_lesson_index) {
            lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_border_color(card, lv_color_hex(C_GREEN), 0);
        }
    }
}

static void build_lesson_game(void)
{
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Oyunlar", 16, 8, 112, 36, C_NAVY2);
    lv_obj_add_event_cb(back, game_back_event, LV_EVENT_CLICKED, NULL);
    label(s_content, lesson_title(), 146, 8, C_SPACE_TEXT, &hidrolab_font_tr_32);

    if (s_lesson_index >= lesson_total()) {
        if (!s_lesson_recorded) {
            (void)hidrolab_settings_record_activity_game(lesson_activity_bit(), 20, 0);
            s_lesson_recorded = true;
        }
        game_companion(s_content, "Sen gerçek bir uzay botanikçisisin!", 626, 62);
        lv_obj_t *result = panel(s_content, 74, 91, 520, 264, 0xFFFFFF);
        centered_label(result, "Görsel Görev Tamamlandı!",
                       20, 28, 480, C_GREEN,
                       &hidrolab_font_tr_32);
        lv_obj_t *score = centered_label(result, "", 20, 91, 480, C_PURPLE,
                                         &hidrolab_font_tr_32);
        lv_label_set_text_fmt(score, "%d / %d adım  •  +20 puan",
                              s_lesson_score, lesson_total());
        lv_obj_t *again = button(result, LV_SYMBOL_REFRESH "  Tekrar", 62, 184, 170, 48, C_BLUE);
        lv_obj_add_event_cb(again, lesson_restart_event, LV_EVENT_CLICKED, NULL);
        lv_obj_t *exit = button(result, LV_SYMBOL_LEFT "  Oyunlara Dön", 280, 184, 178, 48, C_GREEN);
        lv_obj_add_event_cb(exit, game_back_event, LV_EVENT_CLICKED, NULL);
        lv_obj_fade_in(result, 150, 0);
        return;
    }

    lv_obj_t *mission_hud = panel(s_content, 430, 8, 176, 42, 0x163659);
    lv_obj_set_style_border_color(mission_hud, lv_color_hex(0x4CA9DF), 0);
    s_lesson_info = centered_label(mission_hud, "", 8, 12, 160, 0xD9F1FF,
                                   &lv_font_montserrat_14);
    lv_label_set_text_fmt(s_lesson_info, "GÖREV %d / %d", s_lesson_index + 1,
                          lesson_total());
    lv_obj_t *area = panel(s_content, 20, 62, 586, 340, 0xECF8FF);
    set_bg_gradient(area, 0xF8FCFF, 0xDDEEFF);
    build_seed_to_harvest(area);
    game_companion(s_content, "Büyüme sırasını birlikte kuralım!", 626, 66);
}

#endif

/* ---------------- Separate arcade: Meteor Arcade ---------------- */
static void arcade_style_cell(int index, bool target)
{
    if (index < 0 || index >= 6 || !s_arcade_cells[index]) return;
    lv_obj_set_style_bg_color(s_arcade_cells[index],
                              lv_color_hex(target ? 0x123D49 : 0x1C213E), 0);
    lv_obj_set_style_border_width(s_arcade_cells[index], target ? 3 : 1, 0);
    lv_obj_set_style_border_color(s_arcade_cells[index],
                                  lv_color_hex(target ? 0x63E4C1 : 0x5C5275), 0);
    if (s_arcade_orbs[index]) {
        if (target) lv_obj_remove_flag(s_arcade_orbs[index], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_arcade_orbs[index], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_arcade_meteors[index]) {
        if (target) lv_obj_add_flag(s_arcade_meteors[index], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(s_arcade_meteors[index], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_arcade_names[index]) {
        label_text_changed(s_arcade_names[index], target ? "ENERJİ KAPSÜLÜ" : "METEOR");
        lv_obj_set_style_text_color(s_arcade_names[index],
                                    lv_color_hex(target ? 0x8DFFD0 : 0xC1A5B5), 0);
    }
}

static int arcade_interval_us_for(int wave, int combo)
{
    if (wave < 1) wave = 1;
    if (combo < 0) combo = 0;
    int interval = 1150000 - (wave - 1) * 110000 -
                   (combo > 8 ? 8 : combo) * 45000;
    return interval < 420000 ? 420000 : interval;
}

static int arcade_points_for_hit(int combo, int64_t reaction_us)
{
    int points = 1;
    if (combo >= 4) points++;
    if (combo >= 8) points++;
    if (reaction_us >= 0 && reaction_us <= 450000LL) points++;
    return points;
}

static void arcade_shuffle(void)
{
    int previous = s_arcade_target;
    int next = (int)(esp_random() % 6U);
    if (next == s_arcade_target) next = (next + 1) % 6;
    s_arcade_target = next;
    int64_t now = esp_timer_get_time();
    s_arcade_target_since_us = now;
    s_arcade_move_us = now + arcade_interval_us_for(s_arcade_wave, s_arcade_combo);
    /* On later shuffles only two cells change. Re-styling all six used to
     * invalidate the full playfield several times per second. */
    if (previous < 0) {
        for (int i = 0; i < 6; ++i) arcade_style_cell(i, i == next);
    } else {
        arcade_style_cell(previous, false);
        arcade_style_cell(next, true);
    }
}

static void arcade_update_hud(void)
{
    if (s_arcade_info) {
        char text[72];
        snprintf(text, sizeof(text), "SKOR %d  •  x%d  •  %d KALKAN  •  D%d",
                 s_arcade_score, s_arcade_combo, s_arcade_lives, s_arcade_wave);
        label_text_changed(s_arcade_info, text);
    }
}

static void arcade_cell_event(lv_event_t *e)
{
    if (s_arcade_finished || esp_timer_get_time() >= s_arcade_end_us) return;
    int index = (int)(uintptr_t)lv_event_get_user_data(e);
    if (index == s_arcade_target) {
        int64_t reaction_us = esp_timer_get_time() - s_arcade_target_since_us;
        s_arcade_combo++;
        int gained = arcade_points_for_hit(s_arcade_combo, reaction_us);
        s_arcade_score += gained;
        s_arcade_wave = 1 + s_arcade_score / 8;
        if (s_arcade_wave > 5) s_arcade_wave = 5;
        game_react(gained >= 4 ? "Süper hızlı! +4" :
                   s_arcade_combo >= 8 ? "Işık hızında kombo!" :
                   s_arcade_combo >= 4 ? "Süper pilotluk!" : "Kapsül gemide!", true);
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_CORRECT);
    } else {
        if (s_arcade_score > 0) s_arcade_score--;
        s_arcade_combo = 0;
        if (s_arcade_lives > 0) s_arcade_lives--;
        game_react(s_arcade_lives > 0 ? "Kalkan darbe aldı!" : "Kalkanlar tükendi!", false);
        (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_WRONG);
    }
    arcade_update_hud();
    if (s_arcade_lives <= 0) {
        s_arcade_end_us = esp_timer_get_time();
        return;
    }
    arcade_shuffle();
}

static void arcade_restart_event(lv_event_t *e)
{
    (void)e;
    s_arcade_score = 0;
    s_arcade_combo = 0;
    s_arcade_lives = 3;
    s_arcade_wave = 1;
    s_arcade_target = -1;
    s_arcade_finished = false;
    s_arcade_end_us = esp_timer_get_time() + 30000000LL;
    s_arcade_move_us = 0;
    s_arcade_target_since_us = 0;
    request_page_render();
}

static void arcade_show_result(void)
{
    if (!s_arcade_area) return;
    lv_obj_t *result = panel(s_arcade_area, 86, 54, 414, 244, C_NAVY);
    set_bg_gradient(result, 0x132A46, 0x392238);
    lv_obj_set_style_border_width(result, 2, 0);
    lv_obj_set_style_border_color(result, lv_color_hex(0xF06455), 0);
    centered_label(result, "Meteor Görevi Tamam!", 18, 26, 378, 0xFFFFFF,
                   &hidrolab_font_tr_32);
    lv_obj_t *score = centered_label(result, "", 18, 88, 378, 0xFFB36A,
                                     &hidrolab_font_tr_32);
    lv_label_set_text_fmt(score, "%d puan  •  Dalga %d", s_arcade_score, s_arcade_wave);
    lv_obj_t *best = centered_label(result, "", 18, 130, 378, C_SPACE_MUTE,
                                    &lv_font_montserrat_14);
    lv_label_set_text_fmt(best, "En iyi skor: %u", (unsigned)hidrolab_settings_get()->arcade_best);
    lv_obj_t *again = button(result, LV_SYMBOL_REFRESH "  Tekrar", 22, 178, 170, 46, C_BLUE);
    lv_obj_add_event_cb(again, arcade_restart_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *exit = button(result, LV_SYMBOL_LEFT "  Oyunlara Dön", 210, 178, 180, 46, C_GREEN);
    lv_obj_add_event_cb(exit, game_back_event, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(result);
}

static void build_arcade(void)
{
    lv_obj_t *back = button(s_content, LV_SYMBOL_LEFT "  Oyunlar", 16, 8, 112, 36, C_NAVY2);
    lv_obj_add_event_cb(back, game_back_event, LV_EVENT_CLICKED, NULL);
    label(s_content, "Meteor Arcade", 146, 8, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_t *hud = panel(s_content, 430, 8, 350, 42, 0x163659);
    lv_obj_set_style_border_color(hud, lv_color_hex(0xF06455), 0);
    s_arcade_info = centered_label(hud, "", 7, 12, 336, 0xFFE1D6,
                                   &lv_font_montserrat_14);
    s_arcade_area = panel(s_content, 20, 62, 586, 360, 0x191A43);
    set_bg_gradient(s_arcade_area, 0x101B38, 0x3A202E);
    static const int16_t field_stars[][3] = {
        {18, 42, 2}, {92, 29, 3}, {173, 40, 2}, {261, 19, 3},
        {354, 42, 2}, {449, 24, 3}, {548, 41, 2}, {35, 330, 3},
        {211, 320, 2}, {391, 335, 3}, {555, 315, 2},
    };
    for (size_t i = 0; i < sizeof(field_stars) / sizeof(field_stars[0]); ++i) {
        space_star(s_arcade_area, field_stars[i][0], field_stars[i][1],
                   field_stars[i][2], i % 3 == 0 ? 0xFFF2B6 : 0xBCEBFF);
    }
    s_arcade_progress = lv_bar_create(s_arcade_area);
    lv_obj_set_pos(s_arcade_progress, 20, 18);
    lv_obj_set_size(s_arcade_progress, 470, 10);
    lv_bar_set_range(s_arcade_progress, 0, 300);
    lv_bar_set_value(s_arcade_progress, 300, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_arcade_progress, lv_color_hex(0x443245), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_arcade_progress, lv_color_hex(0xF06455), LV_PART_INDICATOR);
    s_arcade_time = label(s_arcade_area, "30 sn", 506, 9, 0xFFFFFF, &lv_font_montserrat_14);
    for (int i = 0; i < 6; ++i) {
        int col = i % 3;
        int row = i / 3;
        lv_obj_t *cell = lv_button_create(s_arcade_area);
        lv_obj_set_pos(cell, 20 + col * 183, 49 + row * 137);
        lv_obj_set_size(cell, 164, 118);
        lv_obj_set_style_radius(cell, 18, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x1C213E), 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0x5C5275), 0);
        lv_obj_set_style_translate_y(cell, 3, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(cell, 0, 0);
        s_arcade_cells[i] = cell;
        s_arcade_meteors[i] = image(cell, &hidrolab_img_arcade_meteor, 44, 0);
        s_arcade_orbs[i] = image(cell, &hidrolab_img_arcade_capsule, 50, 0);
        s_arcade_names[i] = centered_label(cell, "", 8, 76, 148,
                                            0x8DFFD0, &lv_font_montserrat_14);
        lv_obj_add_event_cb(s_arcade_cells[i], arcade_cell_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }
    game_companion(s_content, "Yeşil enerji kapsülünü seç; meteorlara dokunma!", 626, 66);
    if (s_arcade_end_us <= esp_timer_get_time()) s_arcade_end_us = esp_timer_get_time() + 30000000LL;
    arcade_update_hud();
    arcade_shuffle();
}

/* ---------------- Wi-Fi manager ---------------- */
static void refresh_wifi_manager_panel(void);

static void wifi_manager_close_event(lv_event_t *e)
{
    (void)e;
    if (s_wifi_manager_overlay) {
        lv_obj_t *overlay = s_wifi_manager_overlay;
        s_wifi_manager_overlay = NULL;
        s_wifi_manager_panel = NULL;
        lv_obj_delete_async(overlay);
    }
}

static void wifi_manager_scan_event(lv_event_t *e)
{
    (void)e;
    s_wifi_manager_last_count = (size_t)-1;
    s_wifi_manager_last_state = (hidrolab_wifi_state_t)-1;
    (void)hidrolab_wifi_scan_async();
    /* The scan button belongs to the panel being rebuilt. Let ui_tick refresh
     * it after the click dispatch finishes instead of deleting the target from
     * inside its own event callback. */
}

static void refresh_wifi_manager_panel(void)
{
    if (!s_wifi_manager_panel) return;
    lv_obj_clean(s_wifi_manager_panel);

    hidrolab_wifi_state_t state = hidrolab_wifi_get_state();
    size_t count = hidrolab_wifi_network_count();
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    s_wifi_manager_last_state = state;
    s_wifi_manager_last_count = count;

    label(s_wifi_manager_panel, "Wi-Fi Ağları", 20, 18, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_t *close = button(s_wifi_manager_panel, LV_SYMBOL_CLOSE, 550, 14, 44, 44, 0x36415D);
    lv_obj_add_event_cb(close, wifi_manager_close_event, LV_EVENT_CLICKED, NULL);

    char status[96];
    if (state == HIDROLAB_WIFI_CONNECTED) {
        const char *ssid = hidrolab_wifi_connected_ssid();
        snprintf(status, sizeof(status), "Bağlı: %s", (ssid && ssid[0]) ? ssid : cfg->wifi_ssid);
    } else if (state == HIDROLAB_WIFI_CONNECTING) {
        strlcpy(status, "Bağlanıyor...", sizeof(status));
    } else if (state == HIDROLAB_WIFI_SCANNING) {
        strlcpy(status, "Yakındaki ağlar taranıyor...", sizeof(status));
    } else if (state == HIDROLAB_WIFI_FAILED) {
        strlcpy(status, "Bağlantı kurulamadı. Şifreyi kontrol et.", sizeof(status));
    } else if (cfg->wifi_ssid[0]) {
        snprintf(status, sizeof(status), "Kayıtlı ağ: %s", cfg->wifi_ssid);
    } else {
        strlcpy(status, "Henüz bir ağa bağlı değilsin.", sizeof(status));
    }
    lv_obj_t *st = label(s_wifi_manager_panel, status, 22, 68,
                         state == HIDROLAB_WIFI_CONNECTED ? C_GREEN : C_SPACE_MUTE,
                         &hidrolab_font_tr_14);
    lv_obj_set_width(st, 572);
    lv_label_set_long_mode(st, LV_LABEL_LONG_DOT);

    lv_obj_t *scan = button(s_wifi_manager_panel, LV_SYMBOL_REFRESH "  Tekrar Tara", 428, 358, 166, 42, 0x665B9D);
    lv_obj_add_event_cb(scan, wifi_manager_scan_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_note = label(s_wifi_manager_panel, "Bağlanmak için bir ağa dokun.",
                                 22, 371, C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_set_width(scan_note, 390);
    lv_label_set_long_mode(scan_note, LV_LABEL_LONG_DOT);
    if (state == HIDROLAB_WIFI_SCANNING || state == HIDROLAB_WIFI_CONNECTING) {
        lv_obj_add_state(scan, LV_STATE_DISABLED);
        lv_label_set_text(scan_note, "Lütfen kısa bir süre bekle.");
    }

    if (state == HIDROLAB_WIFI_SCANNING) {
        centered_label(s_wifi_manager_panel, LV_SYMBOL_WIFI "  Ağlar aranıyor...",
                       22, 194, 572, 0xC5BCEA, &hidrolab_font_tr_14);
        return;
    }

    if (count == 0) {
        centered_label(s_wifi_manager_panel, "Ağ bulunamadı. Tekrar taramayı deneyebilirsin.",
                       22, 194, 572, C_SPACE_MUTE, &hidrolab_font_tr_14);
        return;
    }

    int shown = count > 5 ? 5 : (int)count;
    for (int i = 0; i < shown; ++i) {
        hidrolab_wifi_network_t net = hidrolab_wifi_network_get((size_t)i);
        lv_obj_t *row = panel(s_wifi_manager_panel, 20, 108 + i * 49, 572, 42,
                              i % 2 ? 0x1F304A : 0x233650);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x435473), 0);
        if (state != HIDROLAB_WIFI_CONNECTING && state != HIDROLAB_WIFI_SCANNING) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, wifi_network_event, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
        } else {
            lv_obj_set_style_opa(row, (lv_opa_t)150, 0);
        }
        label(row, LV_SYMBOL_WIFI, 13, 12, net.rssi > -67 ? C_GREEN : 0xB9AEF4, &lv_font_montserrat_14);
        lv_obj_t *name = label(row, net.ssid, 43, 12, C_SPACE_TEXT, &hidrolab_font_tr_14);
        lv_obj_set_width(name, 418);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        centered_label(row, net.open ? "Açık" : "Kilitli", 477, 12, 82, C_SPACE_MUTE, &hidrolab_font_tr_14);
    }
}

static void wifi_manager_open_event(lv_event_t *e)
{
    (void)e;
    if (s_wifi_manager_overlay) return;

    s_wifi_manager_overlay = lv_obj_create(s_screen);
    lv_obj_set_pos(s_wifi_manager_overlay, 0, 0);
    lv_obj_set_size(s_wifi_manager_overlay, 800, 480);
    lv_obj_remove_flag(s_wifi_manager_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_wifi_manager_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_wifi_manager_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_wifi_manager_overlay, lv_color_hex(0x001D34), 0);
    lv_obj_set_style_bg_opa(s_wifi_manager_overlay, LV_OPA_70, 0);

    s_wifi_manager_panel = panel(s_wifi_manager_overlay, 92, 34, 616, 412, 0x17273E);
    set_bg_gradient(s_wifi_manager_panel, 0x1C2E48, 0x101E33);
    lv_obj_set_style_border_color(s_wifi_manager_panel, lv_color_hex(0x6F6698), 0);
    s_wifi_manager_last_state = (hidrolab_wifi_state_t)-1;
    s_wifi_manager_last_count = (size_t)-1;
    refresh_wifi_manager_panel();

    if (hidrolab_wifi_get_state() != HIDROLAB_WIFI_CONNECTING &&
        hidrolab_wifi_get_state() != HIDROLAB_WIFI_SCANNING) {
        (void)hidrolab_wifi_scan_async();
    }
}

static void factory_reset_confirm_event(lv_event_t *e)
{
    lv_obj_t *modal = (lv_obj_t *)lv_event_get_user_data(e);
    (void)modal;
    (void)hidrolab_wifi_disconnect();
    if (hidrolab_settings_factory_reset() == ESP_OK) {
        (void)hidrolab_control_set_air_pump(false);
        (void)hidrolab_control_set_fan(false);
        esp_restart();
    }
}

static void modal_delete_event(lv_event_t *e)
{
    lv_obj_t *modal = (lv_obj_t *)lv_event_get_user_data(e);
    if (modal) lv_obj_delete_async(modal);
}

static void factory_reset_open_event(lv_event_t *e)
{
    (void)e;
    lv_obj_t *modal = lv_obj_create(s_screen);
    lv_obj_set_pos(modal, 0, 0);
    lv_obj_set_size(modal, 800, 480);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x001D34), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_70, 0);

    lv_obj_t *box = panel(modal, 190, 116, 420, 248, C_SURFACE);
    set_bg_gradient(box, 0x39243B, 0x14263F);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(C_RED), 0);
    centered_label(box, "Her şeyi sıfırla?", 20, 24, 380, 0xFF9CA3,
                   &hidrolab_font_tr_32);
    lv_obj_t *txt = centered_label(box,
        "İsim, astronot seçimi, Wi-Fi, bitkiler, puanlar, oyunlar ve tüm rozetler silinecek.",
        34, 80, 352, C_SPACE_MUTE, &lv_font_montserrat_14);
    lv_obj_set_width(txt, 352);
    lv_label_set_long_mode(txt, LV_LABEL_LONG_WRAP);
    lv_obj_t *cancel = button(box, "Vazgeç", 48, 178, 140, 42, C_NAVY2);
    lv_obj_t *confirm = button(box, "Sıfırla", 232, 178, 140, 42, C_RED);
    lv_obj_add_event_cb(cancel, modal_delete_event, LV_EVENT_CLICKED, modal);
    lv_obj_add_event_cb(confirm, factory_reset_confirm_event, LV_EVENT_CLICKED, modal);
}

/* ---------------- Settings ---------------- */
static void refresh_control_labels(void);

static void control_auto_event(lv_event_t *e)
{
    hidrolab_channel_t ch = (hidrolab_channel_t)(uintptr_t)lv_event_get_user_data(e);
    esp_err_t err = hidrolab_scheduler_set_auto(ch, true, 0, 0);
    s_motor_command_error[ch] = err;
    (void)hidrolab_buzzer_beep(err == ESP_OK ? HIDROLAB_BEEP_TOUCH : HIDROLAB_BEEP_WRONG);
    refresh_control_labels();
}

static void motor_command_event(lv_event_t *e)
{
    uintptr_t command = (uintptr_t)lv_event_get_user_data(e);
    hidrolab_channel_t ch = (hidrolab_channel_t)(command >> 1);
    bool on = (command & 1U) != 0;
    if ((unsigned)ch > HIDROLAB_CHANNEL_FAN) return;
    esp_err_t err = hidrolab_scheduler_manual_set(ch, on);
    s_motor_command_error[ch] = err;
    (void)hidrolab_buzzer_beep(err == ESP_OK ? HIDROLAB_BEEP_TOUCH : HIDROLAB_BEEP_WRONG);
    refresh_control_labels();
}

static void motors_stop_event(lv_event_t *e)
{
    (void)e;
    /* Attempt both independently, even when the first command fails. */
    s_motor_command_error[0] = hidrolab_scheduler_manual_set(HIDROLAB_CHANNEL_PUMP, false);
    s_motor_command_error[1] = hidrolab_scheduler_manual_set(HIDROLAB_CHANNEL_FAN, false);
    (void)hidrolab_buzzer_beep(s_motor_command_error[0] == ESP_OK &&
                              s_motor_command_error[1] == ESP_OK ? HIDROLAB_BEEP_TOUCH : HIDROLAB_BEEP_WRONG);
    refresh_control_labels();
}

static void make_control_card(hidrolab_channel_t ch, int x, const char *title,
                              const char *icon_text, uint32_t accent)
{
    lv_obj_t *c = panel(s_content, x, 158, 370, 208, 0x182B44);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    set_bg_gradient(c, C_SURFACE, C_SURFACE);
    lv_obj_set_style_border_color(c, lv_color_hex(C_BORDER), 0);
    lv_obj_t *circle = panel(c, 304, 14, 44, 44, 0x2A3B58);
    lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(circle, 14, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    centered_label(circle, icon_text, 0, 13, 44, accent, &lv_font_montserrat_14);
    lv_obj_t *heading = label(c, title, 20, 16, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_set_width(heading, 275);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);

    lv_obj_t *state = label(c, "", 20, 65, C_SPACE_MUTE,
                            &hidrolab_font_tr_14);
    lv_obj_set_width(state, 330);
    lv_label_set_long_mode(state, LV_LABEL_LONG_WRAP);
    lv_obj_t *on = button(c, "AÇ", 20, 108, 156, 44, 0x286D61);
    lv_obj_t *off = button(c, "KAPAT", 194, 108, 156, 44, 0x344B61);
    lv_obj_add_event_cb(on, motor_command_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)(((unsigned)ch << 1) | 1U));
    lv_obj_add_event_cb(off, motor_command_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)((unsigned)ch << 1));
    lv_obj_set_style_border_width(on, 2, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(on, lv_color_hex(0xC8FFF0), LV_STATE_CHECKED);
    lv_obj_set_style_border_width(off, 2, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(off, lv_color_hex(0xC9DCE8), LV_STATE_CHECKED);
    /* Override theme checked colors too: OFF is calm slate, not danger red. */
    lv_obj_set_style_bg_color(on, lv_color_hex(0x286D61), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(off, lv_color_hex(0x344B61), LV_STATE_CHECKED);
    lv_obj_t *auto_btn = button(c, "Şimdi otomatiğe dön", 20, 162, 330, 34, 0x2A3B58);
    lv_obj_add_event_cb(auto_btn, control_auto_event, LV_EVENT_CLICKED, (void *)(uintptr_t)ch);

    if (ch == HIDROLAB_CHANNEL_PUMP) {
        s_pump_on_button = on;
        s_pump_off_button = off;
        s_pump_state_label = state;
    } else {
        s_fan_on_button = on;
        s_fan_off_button = off;
        s_fan_state_label = state;
    }
}

static void refresh_settings_wifi_label(void)
{
    if (!s_settings_wifi_label) return;
    hidrolab_wifi_state_t state = hidrolab_wifi_get_state();
    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    char text[96];
    uint32_t color = C_SPACE_MUTE;
    if (state == HIDROLAB_WIFI_CONNECTED) {
        const char *ssid = hidrolab_wifi_connected_ssid();
        snprintf(text, sizeof(text), "Bağlı • %s", (ssid && ssid[0]) ? ssid : cfg->wifi_ssid);
        color = C_GREEN;
    } else if (state == HIDROLAB_WIFI_CONNECTING) {
        strlcpy(text, "Bağlanıyor...", sizeof(text));
        color = 0xB9AEF4;
    } else if (cfg->wifi_ssid[0]) {
        snprintf(text, sizeof(text), "Bağlı değil • Kayıtlı: %s", cfg->wifi_ssid);
    } else {
        strlcpy(text, "Wi-Fi ayarlanmadı", sizeof(text));
    }
    label_status_changed(s_settings_wifi_label, text, color);
}

static void settings_tab_event(lv_event_t *e)
{
    s_settings_view = (settings_view_t)(uintptr_t)lv_event_get_user_data(e);
    request_page_render();
}

static void refresh_control_labels(void);

static void sleep_hours_close_event(lv_event_t *e)
{
    (void)e;
    if (!s_sleep_modal) return;
    lv_obj_t *modal = s_sleep_modal;
    s_sleep_modal = NULL;
    s_sleep_start_dd = NULL;
    s_sleep_end_dd = NULL;
    s_sleep_error = NULL;
    lv_obj_delete_async(modal);
}

static void sleep_hours_save_event(lv_event_t *e)
{
    (void)e;
    if (!s_sleep_modal || !s_sleep_start_dd || !s_sleep_end_dd ||
        s_page_render_pending || s_page_render_retry) return;
    uint8_t start = (uint8_t)lv_dropdown_get_selected(s_sleep_start_dd);
    uint8_t end = (uint8_t)lv_dropdown_get_selected(s_sleep_end_dd);
    if (start == end) {
        lv_label_set_text(s_sleep_error, "Başlangıç ve uyanış saati farklı olmalı.");
        return;
    }
    if (hidrolab_settings_set_sleep_hours(start, end) != ESP_OK) {
        lv_label_set_text(s_sleep_error, "Kaydedemedim. Yeniden deneyebilirsin.");
        return;
    }
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
    request_page_render();
}

static void sleep_hours_open_event(lv_event_t *e)
{
    (void)e;
    if (s_sleep_modal || s_page_render_pending || s_page_render_retry) return;
    s_sleep_modal = lv_obj_create(s_screen);
    lv_obj_set_pos(s_sleep_modal, 0, 0);
    lv_obj_set_size(s_sleep_modal, 800, 480);
    lv_obj_remove_flag(s_sleep_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_sleep_modal, 0, 0);
    lv_obj_set_style_pad_all(s_sleep_modal, 0, 0);
    set_bg_gradient(s_sleep_modal, 0x091A2C, 0x102A3E);
    label(s_sleep_modal, "Uyku Saatleri", 24, 18, C_SPACE_TEXT,
          &hidrolab_font_tr_32);
    lv_obj_t *back = button(s_sleep_modal, LV_SYMBOL_LEFT "  Ayarlar",
                            636, 18, 144, 42, 0x284156);
    lv_obj_add_event_cb(back, sleep_hours_close_event, LV_EVENT_CLICKED, NULL);
    centered_label(s_sleep_modal,
                   "Bu saatlerde ekran dinlenir; pompa ve fan kapalı kalır.",
                   30, 75, 740, 0xB8CBD8, &hidrolab_font_tr_14);

    static const char hours[] =
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n"
        "12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
    uint16_t saved = hidrolab_settings_get()->sleep_hours;
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *card = panel(s_sleep_modal, 96 + i * 314, 134, 294, 160,
                               i == 0 ? 0x1C3850 : 0x193E45);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
        centered_label(card, i == 0 ? "Uyku başlasın" : "Astronot uyansın",
                       15, 22, 264, C_SPACE_TEXT, &hidrolab_font_tr_14);
        lv_obj_t *dd = lv_dropdown_create(card);
        lv_obj_set_pos(dd, 77, 74);
        lv_obj_set_size(dd, 140, 56);
        lv_dropdown_set_options(dd, hours);
        lv_dropdown_set_selected(dd, i == 0 ? (saved >> 8) : (saved & 0xFFU));
        lv_obj_set_style_text_font(dd, &hidrolab_font_tr_14, 0);
        lv_obj_set_style_bg_color(dd, lv_color_hex(0x102E3B), 0);
        lv_obj_set_style_text_color(dd, lv_color_hex(C_SPACE_TEXT), 0);
        lv_obj_set_style_border_color(dd, lv_color_hex(0x78C4BD), 0);
        lv_obj_t *list = lv_dropdown_get_list(dd);
        lv_obj_set_style_text_font(list, &hidrolab_font_tr_14, 0);
        lv_obj_set_style_bg_color(list, lv_color_hex(0x123B42), 0);
        lv_obj_set_style_text_color(list, lv_color_hex(C_SPACE_TEXT), 0);
        if (i == 0) s_sleep_start_dd = dd;
        else s_sleep_end_dd = dd;
    }
    centered_label(s_sleep_modal, "Saatler kaydedilir; Wi-Fi olmadan RTC ile çalışır.",
                   80, 313, 640, C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_t *save = button(s_sleep_modal, LV_SYMBOL_OK "  Saatleri Kaydet",
                            220, 363, 360, 54, 0x247B62);
    lv_obj_add_event_cb(save, sleep_hours_save_event, LV_EVENT_CLICKED, NULL);
    s_sleep_error = centered_label(s_sleep_modal, "", 80, 430, 640,
                                    0xFFD18B, &hidrolab_font_tr_14);
}

static void alarm_close_event(lv_event_t *e)
{
    (void)e;
    if (!s_alarm_modal) return;
    lv_obj_t *modal = s_alarm_modal;
    s_alarm_modal = NULL;
    s_alarm_hour_dd = NULL;
    s_alarm_minute_dd = NULL;
    s_alarm_error = NULL;
    lv_obj_delete_async(modal);
}

static void alarm_save_event(lv_event_t *e)
{
    (void)e;
    if (!s_alarm_modal || !s_alarm_hour_dd || !s_alarm_minute_dd ||
        s_page_render_pending || s_page_render_retry) return;
    uint8_t hour = (uint8_t)lv_dropdown_get_selected(s_alarm_hour_dd);
    uint8_t minute = (uint8_t)lv_dropdown_get_selected(s_alarm_minute_dd);
    if (hidrolab_settings_set_alarm(true, hour, minute) != ESP_OK) {
        lv_label_set_text(s_alarm_error, "Alarm kaydedilemedi. Tekrar deneyebilirsin.");
        return;
    }
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_SUCCESS);
    request_page_render();
}

static void alarm_open_event(lv_event_t *e)
{
    (void)e;
    if (s_alarm_modal || s_page_render_pending || s_page_render_retry) return;
    s_alarm_modal = lv_obj_create(s_screen);
    lv_obj_set_pos(s_alarm_modal, 0, 0);
    lv_obj_set_size(s_alarm_modal, 800, 480);
    lv_obj_remove_flag(s_alarm_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_alarm_modal, 0, 0);
    lv_obj_set_style_pad_all(s_alarm_modal, 0, 0);
    set_bg_gradient(s_alarm_modal, 0x091A2C, 0x102A3E);
    label(s_alarm_modal, "Günlük Alarm", 24, 18, C_SPACE_TEXT,
          &hidrolab_font_tr_32);
    lv_obj_t *back = button(s_alarm_modal, LV_SYMBOL_LEFT "  Ayarlar",
                            636, 18, 144, 42, 0x284156);
    lv_obj_add_event_cb(back, alarm_close_event, LV_EVENT_CLICKED, NULL);
    centered_label(s_alarm_modal,
                   "Seçtiğin saatte kısa bir sesli hatırlatma çalar.",
                   30, 75, 740, 0xB8CBD8, &hidrolab_font_tr_14);

    static const char hours[] =
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n"
        "12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
    static char minutes[180];
    if (!minutes[0]) {
        size_t used = 0;
        for (int i = 0; i < 60; ++i) {
            int written = snprintf(minutes + used, sizeof(minutes) - used,
                                   i == 59 ? "%02d" : "%02d\n", i);
            if (written < 0 || (size_t)written >= sizeof(minutes) - used) break;
            used += (size_t)written;
        }
    }
    uint32_t saved = hidrolab_settings_get()->alarm_config;
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *card = panel(s_alarm_modal, 96 + i * 314, 134, 294, 160,
                               i == 0 ? 0x1C3850 : 0x193E45);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
        centered_label(card, i == 0 ? "Saat" : "Dakika", 15, 22, 264,
                       C_SPACE_TEXT, &hidrolab_font_tr_14);
        lv_obj_t *dd = lv_dropdown_create(card);
        lv_obj_set_pos(dd, 77, 74);
        lv_obj_set_size(dd, 140, 56);
        lv_dropdown_set_options(dd, i == 0 ? hours : minutes);
        lv_dropdown_set_selected(dd, i == 0 ? (saved >> 8) & 0xFFU : saved & 0xFFU);
        lv_obj_set_style_text_font(dd, &hidrolab_font_tr_14, 0);
        lv_obj_set_style_bg_color(dd, lv_color_hex(0x102E3B), 0);
        lv_obj_set_style_text_color(dd, lv_color_hex(C_SPACE_TEXT), 0);
        lv_obj_set_style_border_color(dd, lv_color_hex(0x78C4BD), 0);
        lv_obj_t *list = lv_dropdown_get_list(dd);
        lv_obj_set_style_text_font(list, &hidrolab_font_tr_14, 0);
        lv_obj_set_style_bg_color(list, lv_color_hex(0x123B42), 0);
        lv_obj_set_style_text_color(list, lv_color_hex(C_SPACE_TEXT), 0);
        if (i == 0) s_alarm_hour_dd = dd;
        else s_alarm_minute_dd = dd;
    }
    centered_label(s_alarm_modal,
                   "Her gün seçtiğin saatte, Durdur'a basana kadar çalar.",
                   60, 313, 680, C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_t *save = button(s_alarm_modal, LV_SYMBOL_OK "  Kaydet ve Aç",
                            220, 363, 360, 54, 0x247B62);
    lv_obj_add_event_cb(save, alarm_save_event, LV_EVENT_CLICKED, NULL);
    s_alarm_error = centered_label(s_alarm_modal, "", 80, 430, 640,
                                    0xFFD18B, &hidrolab_font_tr_14);
}

static void alarm_switch_event(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    uint32_t saved = hidrolab_settings_get()->alarm_config;
    if (hidrolab_settings_set_alarm(enabled, (saved >> 8) & 0xFFU,
                                   saved & 0xFFU) != ESP_OK) {
        if (enabled) lv_obj_remove_state(sw, LV_STATE_CHECKED);
        else lv_obj_add_state(sw, LV_STATE_CHECKED);
        if (s_alarm_state_label) lv_label_set_text(s_alarm_state_label,
                                                   "Kaydedilemedi; tekrar dene.");
        return;
    }
    (void)hidrolab_buzzer_beep(HIDROLAB_BEEP_TOUCH);
    request_page_render();
}

static void build_settings(void)
{
    label(s_content, "Ayarlar", 20, 12, C_SPACE_TEXT, &hidrolab_font_tr_32);
    lv_obj_t *subtitle = label(s_content, "Üssünün çalışma, dinlenme ve bağlantı ayarları.",
                               22, 53, C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_set_width(subtitle, 520);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    (void)base_return_button(s_content, 660, 15, 120);

    lv_obj_t *tabs = panel(s_content, 20, 90, 760, 52, C_SURFACE);
    lv_obj_remove_flag(tabs, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(tabs, 16, 0);
    lv_obj_set_style_border_color(tabs, lv_color_hex(C_BORDER), 0);
    lv_obj_t *motors_tab = button(tabs, LV_SYMBOL_SETTINGS "  Pompa / Fan", 5, 5, 244, 42,
                                  s_settings_view == SETTINGS_MOTORS ? 0x2C5C67 : C_SURFACE);
    lv_obj_t *sleep_tab = button(tabs, LV_SYMBOL_PAUSE "  Uyku / Alarm", 258, 5, 244, 42,
                                 s_settings_view == SETTINGS_SLEEP_ALARM ? 0x2C5C67 : C_SURFACE);
    lv_obj_t *wifi_tab = button(tabs, LV_SYMBOL_WIFI "  Wi-Fi / Cihaz", 511, 5, 244, 42,
                                s_settings_view == SETTINGS_WIFI ? 0x2C5C67 : C_SURFACE);
    lv_obj_add_event_cb(motors_tab, settings_tab_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)SETTINGS_MOTORS);
    lv_obj_add_event_cb(sleep_tab, settings_tab_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)SETTINGS_SLEEP_ALARM);
    lv_obj_add_event_cb(wifi_tab, settings_tab_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)SETTINGS_WIFI);

    if (s_settings_view == SETTINGS_MOTORS) {
        make_control_card(HIDROLAB_CHANNEL_PUMP, 20, "Pompa", LV_SYMBOL_TINT, 0x8CBFF6);
        make_control_card(HIDROLAB_CHANNEL_FAN, 410, "Hava Fanı", LV_SYMBOL_LOOP, 0xB7ABEE);
        lv_obj_t *schedule = panel(s_content, 20, 382, 760, 56, 0x192A43);
        lv_obj_remove_flag(schedule, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(schedule, lv_color_hex(0x455271), 0);
        label(schedule, "Gündüz: pompa saatte 5 dk • fan sürekli açık", 18, 8, 0xC5BCEA,
              &hidrolab_font_tr_14);
        label(schedule, "Manuel seçim 2 dk sürer, sonra otomatiğe döner.", 18, 30, C_SPACE_MUTE, &hidrolab_font_tr_14);
        lv_obj_t *stop = button(schedule, "2 dk ara ver", 550, 7, 196, 42, 0x344B61);
        lv_obj_add_event_cb(stop, motors_stop_event, LV_EVENT_CLICKED, NULL);
        refresh_control_labels();
        return;
    }

    if (s_settings_view == SETTINGS_SLEEP_ALARM) {
        const hidrolab_settings_t *cfg = hidrolab_settings_get();
        uint16_t hours = cfg->sleep_hours;
        uint32_t alarm = cfg->alarm_config;
        bool enabled = (alarm & (1UL << 16)) != 0;

        lv_obj_t *sleep = panel(s_content, 20, 158, 370, 232, 0x192F43);
        lv_obj_remove_flag(sleep, LV_OBJ_FLAG_CLICKABLE);
        set_bg_gradient(sleep, C_SURFACE, C_SURFACE);
        lv_obj_set_style_border_color(sleep, lv_color_hex(C_BORDER), 0);
        label(sleep, "Uyku saatleri", 22, 20, 0xA8DDE8, &hidrolab_font_tr_14);
        lv_obj_t *sleep_time = label(sleep, "", 22, 57, C_SPACE_TEXT,
                                      &hidrolab_font_tr_32);
        lv_label_set_text_fmt(sleep_time, "%02u:00 - %02u:00",
                              (unsigned)(hours >> 8), (unsigned)(hours & 0xFFU));
        lv_obj_t *sleep_hint = label(sleep,
              "Bu saatlerde otomatik çalışma durur. Elle kontrol kullanılabilir.",
              22, 111, 0xC7D7DE, &hidrolab_font_tr_14);
        lv_obj_set_width(sleep_hint, 326);
        lv_label_set_long_mode(sleep_hint, LV_LABEL_LONG_WRAP);
        lv_obj_t *sleep_edit = button(sleep, "Uyku Saatlerini Ayarla",
                                      20, 171, 330, 44, 0x286D80);
        lv_obj_add_event_cb(sleep_edit, sleep_hours_open_event, LV_EVENT_CLICKED, NULL);

        lv_obj_t *alarm_card = panel(s_content, 410, 158, 370, 232, 0x2A3044);
        lv_obj_remove_flag(alarm_card, LV_OBJ_FLAG_CLICKABLE);
        set_bg_gradient(alarm_card, C_SURFACE, C_SURFACE);
        lv_obj_set_style_border_color(alarm_card, lv_color_hex(C_BORDER), 0);
        label(alarm_card, "Günlük alarm", 22, 20, 0xF0CD8E,
              &hidrolab_font_tr_14);
        lv_obj_t *alarm_time = label(alarm_card, "", 22, 57, C_SPACE_TEXT,
                                      &hidrolab_font_tr_32);
        lv_label_set_text_fmt(alarm_time, "%02u:%02u",
                              (unsigned)((alarm >> 8) & 0xFFU),
                              (unsigned)(alarm & 0xFFU));
        s_alarm_state_label = label(alarm_card, enabled ?
                                     "Açık • Her gün kısa hatırlatma" :
                                     "Kapalı • Saati seçip açabilirsin",
                                     22, 111, enabled ? 0xA6E6BA : 0xC7D7DE,
                                     &hidrolab_font_tr_14);
        s_alarm_switch = lv_switch_create(alarm_card);
        lv_obj_set_pos(s_alarm_switch, 269, 64);
        lv_obj_set_size(s_alarm_switch, 78, 42);
        if (enabled) lv_obj_add_state(s_alarm_switch, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(s_alarm_switch, lv_color_hex(0x4A5569), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_alarm_switch, lv_color_hex(0xD9A86B),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(s_alarm_switch, alarm_switch_event, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_t *alarm_edit = button(alarm_card, "Alarm Saatini Ayarla",
                                      20, 171, 330, 44, 0x2C5C67);
        lv_obj_add_event_cb(alarm_edit, alarm_open_event, LV_EVENT_CLICKED, NULL);

        lv_obj_t *hint = panel(s_content, 20, 406, 760, 32, 0x182D40);
        lv_obj_remove_flag(hint, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_width(hint, 0, 0);
        centered_label(hint, "RTC çevrimdışı çalışır • Alarm motorları açmaz",
                       10, 6, 740, C_SPACE_MUTE, &hidrolab_font_tr_14);
        return;
    }

    lv_obj_t *wifi = panel(s_content, 20, 158, 760, 144, 0x182B44);
    lv_obj_remove_flag(wifi, LV_OBJ_FLAG_CLICKABLE);
    set_bg_gradient(wifi, 0x1D304B, 0x132238);
    lv_obj_set_style_border_color(wifi, lv_color_hex(0x455677), 0);
    lv_obj_t *wifi_icon = panel(wifi, 20, 23, 54, 54, 0x2A3B58);
    lv_obj_remove_flag(wifi_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(wifi_icon, 0, 0);
    centered_label(wifi_icon, LV_SYMBOL_WIFI, 0, 18, 54, 0xC5BCEA,
                   &lv_font_montserrat_14);
    label(wifi, "Wi-Fi", 92, 23, C_SPACE_TEXT, &hidrolab_font_tr_32);
    s_settings_wifi_label = label(wifi, "", 94, 70, C_SPACE_MUTE,
                                  &hidrolab_font_tr_14);
    lv_obj_set_width(s_settings_wifi_label, 438);
    lv_label_set_long_mode(s_settings_wifi_label, LV_LABEL_LONG_DOT);
    lv_obj_t *wifi_btn = button(wifi, "Ağları Yönet", 558, 32, 180, 48, 0x665B9D);
    lv_obj_add_event_cb(wifi_btn, wifi_manager_open_event, LV_EVENT_CLICKED, NULL);
    refresh_settings_wifi_label();
    background_shape(wifi, 22, 99, 716, 1, 0x3C4D69, 0, LV_OPA_COVER);
    lv_obj_t *wifi_hint = label(wifi,
        "Bahçen çevrimdışı da çalışır. Wi-Fi, saati otomatik günceller.",
        22, 116, C_SPACE_MUTE, &hidrolab_font_tr_14);
    lv_obj_set_width(wifi_hint, 716);
    lv_label_set_long_mode(wifi_hint, LV_LABEL_LONG_DOT);

    lv_obj_t *danger = panel(s_content, 20, 318, 760, 120, 0x2B263A);
    lv_obj_remove_flag(danger, LV_OBJ_FLAG_CLICKABLE);
    set_bg_gradient(danger, 0x302B40, 0x22263A);
    lv_obj_set_style_border_color(danger, lv_color_hex(0x715064), 0);
    label(danger, "Cihazı Sıfırla", 22, 20, 0xF2B4BF, &hidrolab_font_tr_14);
    lv_obj_t *reset_hint = label(danger,
        "Bitkiler, ölçümler, bağlantı ve oyun kayıtları silinir.\nDevam etmeden önce onayın istenir.",
        22, 52, 0xD0BBC9, &hidrolab_font_tr_14);
    lv_obj_set_width(reset_hint, 492);
    lv_label_set_long_mode(reset_hint, LV_LABEL_LONG_WRAP);
    lv_obj_t *reset = button(danger, "Her Şeyi Sıfırla", 540, 36, 198, 48, 0x84495D);
    lv_obj_add_event_cb(reset, factory_reset_open_event, LV_EVENT_CLICKED, NULL);
}

static void refresh_control_labels(void)
{
    hidrolab_schedule_status_t pump = hidrolab_scheduler_get_status(HIDROLAB_CHANNEL_PUMP);
    hidrolab_schedule_status_t fan = hidrolab_scheduler_get_status(HIDROLAB_CHANNEL_FAN);
    bool pump_on = hidrolab_control_air_pump_is_on();
    bool fan_on = hidrolab_control_fan_is_on();
    lv_obj_t *states[2] = {s_pump_state_label, s_fan_state_label};
    lv_obj_t *on_buttons[2] = {s_pump_on_button, s_fan_on_button};
    lv_obj_t *off_buttons[2] = {s_pump_off_button, s_fan_off_button};
    bool outputs[2] = {pump_on, fan_on};
    bool automatic[2] = {pump.automatic, fan.automatic};
    for (int i = 0; i < 2; ++i) {
        hidrolab_control_snapshot_t io = hidrolab_control_snapshot(i == 1);
        if (states[i]) {
            char text[128];
            bool mismatch = io.initialized && io.level != (outputs[i] ? 1 : 0);
            if (s_motor_command_error[i] != ESP_OK) {
                snprintf(text, sizeof(text), "İşlem yapılamadı\nYeniden deneyebilirsin.");
            } else if (mismatch) {
                snprintf(text, sizeof(text), "Bağlantıyı kontrol edelim\nÇıkış seçiminle uyuşmuyor.");
            } else if (!automatic[i]) {
                uint32_t left = i == 0 ? pump.remaining_seconds : fan.remaining_seconds;
                snprintf(text, sizeof(text), "%s • Manuel\nOtomatiğe dönüş: %02lu:%02lu",
                         outputs[i] ? "Açık" : "Kapalı",
                         (unsigned long)(left / 60), (unsigned long)(left % 60));
            } else {
                snprintf(text, sizeof(text), "%s • Otomatik\n%s",
                         outputs[i] ? "Açık" : "Kapalı",
                         i == 0 ? "Gündüz her saat başı 5 dakika" : "Gündüz açık, gece dinlenir");
            }
            label_status_changed(states[i], text, s_motor_command_error[i] != ESP_OK || mismatch ?
                                 C_RED : outputs[i] ? 0xA8EACC : C_SPACE_MUTE);
        }
        if (on_buttons[i]) {
            if (outputs[i]) lv_obj_add_state(on_buttons[i], LV_STATE_CHECKED);
            else lv_obj_remove_state(on_buttons[i], LV_STATE_CHECKED);
        }
        if (off_buttons[i]) {
            if (!outputs[i]) lv_obj_add_state(off_buttons[i], LV_STATE_CHECKED);
            else lv_obj_remove_state(off_buttons[i], LV_STATE_CHECKED);
        }
    }
    refresh_settings_wifi_label();
}

/* ---------------- Page rendering ---------------- */
static void render_game(void)
{
    if (s_game_guide_visible && s_game_view != GAME_HOME) {
        build_game_guide();
        return;
    }
    switch (s_game_view) {
        case GAME_MEMORY:
            build_memory();
            break;
        case GAME_BUBBLE:
            build_bubble();
            break;
        case GAME_WATER_PATH:
            build_water_path();
            break;
        case GAME_ARCADE:
            build_arcade();
            break;
        case GAME_BUG_ESCAPE:
            build_bug_escape();
            break;
        case GAME_HOME:
        default:
            build_games_home();
            break;
    }
}

static void render_page(void)
{
    /* Restore the reliable full redraw for every non-runner page/transition. */
    bug_display_fast_mode(false);
    /* A queued badge close callback may otherwise retain an object pointer
     * after build_shell() replaces the screen tree. */
    lv_async_call_cancel(badge_popup_close_async_cb, NULL);
    s_badge_overlay = NULL;
    s_badge_popup_open = false;
    s_badge_close_pending = false;
    clear_slot_ui();
    s_sleep_modal = NULL;
    s_sleep_start_dd = NULL;
    s_sleep_end_dd = NULL;
    s_sleep_error = NULL;
    s_alarm_modal = NULL;
    s_alarm_hour_dd = NULL;
    s_alarm_minute_dd = NULL;
    s_alarm_error = NULL;
    s_alarm_switch = NULL;
    s_alarm_state_label = NULL;
    s_observation_modal = NULL;
    s_observation_error = NULL;
    /* Cancel delayed game callbacks before LVGL objects are destroyed. */
    cancel_base_walk();
    s_page_render_retry = false;
    cancel_game_timers();
    s_memory_timer_action = MEMORY_TIMER_NONE;
    s_memory_busy = false;
    s_memory_first = -1;
    s_memory_second = -1;
    memset(s_memory_cards, 0, sizeof(s_memory_cards));
    memset(s_memory_images, 0, sizeof(s_memory_images));
    memset(s_memory_back_labels, 0, sizeof(s_memory_back_labels));
    memset(s_water_tiles, 0, sizeof(s_water_tiles));
    memset(s_water_arms, 0, sizeof(s_water_arms));
    memset(s_maze_cells, 0, sizeof(s_maze_cells));
    memset(s_maze_nodes, 0, sizeof(s_maze_nodes));
    memset(s_landing_rocks, 0, sizeof(s_landing_rocks));
    s_bug_road_strip = NULL;
    memset(s_bug_craters, 0, sizeof(s_bug_craters));
    memset(s_arcade_cells, 0, sizeof(s_arcade_cells));
    memset(s_arcade_orbs, 0, sizeof(s_arcade_orbs));
    memset(s_arcade_names, 0, sizeof(s_arcade_names));
    memset(s_arcade_meteors, 0, sizeof(s_arcade_meteors));
    s_water_status = NULL;
    s_maze_status = NULL;
    s_landing_area = NULL;
    s_landing_pod = NULL;
    s_landing_target = NULL;
    s_landing_info = NULL;
    s_bug_area = NULL;
    s_bug_plant = NULL;
    s_bug_shadow = NULL;
    s_bug_object = NULL;
    s_bug_air = NULL;
    s_bug_pack_extra[0] = NULL;
    s_bug_pack_extra[1] = NULL;
    s_bug_info = NULL;
    s_bug_time = NULL;
    s_bug_progress = NULL;
    s_game_astronaut = NULL;
    s_game_reaction = NULL;
    s_game_reaction_card = NULL;
    s_wifi_manager_overlay = NULL;
    s_wifi_manager_panel = NULL;
    s_settings_wifi_label = NULL;
    s_slot_name_ta = NULL;
    s_slot_type_dd = NULL;
    s_slot_stage_dd = NULL;
    s_ph_slider = NULL;
    s_ec_slider = NULL;
    s_ph_value_label = NULL;
    s_ec_value_label = NULL;
    s_chem_feedback_card = NULL;
    s_chem_feedback_label = NULL;
    s_chem_error = NULL;
    s_chem_ph_touched = false;
    s_chem_ec_touched = false;
    s_bubble = NULL;
    s_bubble_bonus = NULL;
    s_bubble_info = NULL;
    s_bubble_area = NULL;
    s_bubble_progress = NULL;
    s_bubble_time = NULL;
    s_arcade_info = NULL;
    s_arcade_time = NULL;
    s_arcade_progress = NULL;
    s_arcade_area = NULL;

    build_shell();
    s_pump_on_button = s_pump_off_button = NULL;
    s_fan_on_button = s_fan_off_button = NULL;
    s_pump_state_label = NULL;
    s_fan_state_label = NULL;

    switch (s_page) {
        case PAGE_GARDEN:
            if (s_base_home) build_moon_base();
            else {
                build_garden();
                if (s_reopen_garden_detail) {
                    s_reopen_garden_detail = false;
                    open_garden_detail();
                }
            }
            break;
        case PAGE_TASKS:
            if (s_tasks_badges_view) build_badges();
            else if (s_tasks_academy_view) build_academy();
            else build_tasks();
            break;
        case PAGE_GAMES:
            render_game();
            break;
        case PAGE_SETTINGS:
            build_settings();
            break;
        default:
            break;
    }
    if (s_page != PAGE_GARDEN || s_base_home) s_reopen_garden_detail = false;
    evaluate_badges();
    show_next_badge_popup();
}

static void update_top_bar(void)
{
    if (!s_top_wifi || !s_top_date || !s_top_clock) return;
    hidrolab_wifi_state_t state = hidrolab_wifi_get_state();
    label_text_changed(s_top_wifi, state == HIDROLAB_WIFI_CONNECTED ? LV_SYMBOL_WIFI : "");

    if (hidrolab_time_is_synced()) {
        time_t now = 0;
        struct tm local = {0};
        char buf[8] = "--:--";
        char date_buf[12] = "--.--.----";
        time(&now);
        localtime_r(&now, &local);
        strftime(buf, sizeof(buf), "%H:%M", &local);
        strftime(date_buf, sizeof(date_buf), "%d.%m.%Y", &local);
        char day_and_date[40];
        snprintf(day_and_date, sizeof(day_and_date), "ÜS GÜNÜ %u  •  %s",
                 (unsigned)hidrolab_settings_get()->season_day, date_buf);
        label_text_changed(s_top_date, day_and_date);
        label_text_changed(s_top_clock, buf);
    } else {
        char day_and_date[40];
        snprintf(day_and_date, sizeof(day_and_date), "ÜS GÜNÜ %u  •  --.--.----",
                 (unsigned)hidrolab_settings_get()->season_day);
        label_text_changed(s_top_date, day_and_date);
        label_text_changed(s_top_clock, "--:--");
    }
}

static void alarm_dismiss_event(lv_event_t *e)
{
    (void)e;
    hidrolab_buzzer_alarm_stop();
    if (s_alarm_ringing_overlay) {
        lv_obj_delete_async(s_alarm_ringing_overlay);
        s_alarm_ringing_overlay = NULL;
    }
    s_night_awake_until_us = esp_timer_get_time() + 60000000LL;
}

static void show_ringing_alarm(void)
{
    if (s_alarm_ringing_overlay) return;
    /* Top layer survives screen rebuilds and remains usable over night mode. */
    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    s_alarm_ringing_overlay = overlay;
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 800, 480);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    set_bg_gradient(overlay, 0x061322, 0x183C52);
    centered_label(overlay, "GÜNLÜK ALARM", 40, 65, 720, 0xFFD78B,
                   &hidrolab_font_tr_32);
    centered_label(overlay, "Belirlediğin saat geldi!", 40, 136, 720,
                   C_SPACE_TEXT, &hidrolab_font_tr_32);
    centered_label(overlay, "Sesi kapatmak için Durdur'a dokun.", 40, 213, 720,
                   0xB6D8E8, &hidrolab_font_tr_14);
    lv_obj_t *stop = button(overlay, "DURDUR", 220, 294, 360, 82, 0x287E82);
    lv_obj_add_event_cb(stop, alarm_dismiss_event, LV_EVENT_CLICKED, NULL);
}

static void ui_tick(lv_timer_t *t)
{
    (void)t;
    if (hidrolab_buzzer_alarm_active()) {
        show_ringing_alarm();
        return;
    }
    /* A full-screen simulation covers the dashboard; defer hidden redraws. */
    if (hidrolab_simulation_ui_is_open()) return;
    if (s_setup_mode && s_setup_build_retry) {
        request_setup_build();
    }
    if (s_page_render_retry) {
        request_page_render();
        /* Persistent allocation pressure must not starve the sleep/clock
         * checks below. Sleep can also release the current scene's memory. */
        if (s_page_render_pending) return;
    }
    hidrolab_wifi_state_t state = hidrolab_wifi_get_state();
    size_t network_count = hidrolab_wifi_network_count();

    if (s_setup_mode) {
        if (s_setup_step == SETUP_WIFI &&
            (state == HIDROLAB_WIFI_CONNECTED || strcmp(hidrolab_wifi_ip(), "-") != 0)) {
            finish_setup_to_garden(false);
            return;
        }

        if (s_setup_step == SETUP_WIFI &&
            (state != s_last_wifi_state || network_count != s_last_network_count)) {
            /* Never destroy the pressed Wi-Fi/offline object before LVGL has
             * delivered its release/click sequence. Refresh on the next tick. */
            if (any_input_pressed()) return;
            s_last_wifi_state = state;
            s_last_network_count = network_count;
            build_setup_step();
            return;
        }
        s_last_wifi_state = state;
        s_last_network_count = network_count;
        return;
    }

    uint32_t calendar_day = hidrolab_time_date_key();
    if (calendar_day) {
        uint32_t previous_day = hidrolab_settings_get()->task_date;
        if (previous_day != calendar_day) {
            bool rollover = previous_day != 0;
            if (hidrolab_settings_sync_daily_date(calendar_day) == ESP_OK &&
                rollover && !s_sleep_screen) {
                request_page_render();
                return;
            }
        }
    }

    bool night = hidrolab_time_is_night();
    int64_t now_us = esp_timer_get_time();
    if (s_sleep_screen) {
        if (!night) {
            s_sleep_screen = false;
            s_night_awake_until_us = 0;
            request_page_render();
        } else {
            update_sleep_clock();
        }
        return;
    }
    if (night) {
        if (any_input_pressed()) s_night_awake_until_us = now_us + 60000000LL;
        if (s_night_awake_until_us == 0 || now_us >= s_night_awake_until_us) {
            if (!any_input_pressed()) {
                s_page = PAGE_GARDEN;
                s_game_view = GAME_HOME;
                build_sleep_screen();
            }
            return;
        }
    } else {
        s_night_awake_until_us = 0;
    }

    update_top_bar();
    if (s_page == PAGE_SETTINGS) refresh_control_labels();

    if (s_wifi_manager_panel &&
        (state != s_wifi_manager_last_state || network_count != s_wifi_manager_last_count)) {
        s_wifi_manager_last_state = state;
        s_wifi_manager_last_count = network_count;
        refresh_wifi_manager_panel();
    }

    if (s_page == PAGE_GAMES && s_game_view == GAME_BUBBLE && s_bubble_info && !s_bubble_finished) {
        int64_t remaining = s_bubble_end_us - esp_timer_get_time();
        if (remaining <= 0) {
            s_bubble_finished = true;
            uint8_t score = (uint8_t)(s_bubble_score > 255 ? 255 : s_bubble_score);
            (void)hidrolab_settings_record_bubble_completion(score);
            if (s_bubble) lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
            if (s_bubble_bonus) lv_obj_add_flag(s_bubble_bonus, LV_OBJ_FLAG_HIDDEN);
            if (s_bubble_progress) lv_bar_set_value(s_bubble_progress, 0, LV_ANIM_OFF);
            if (s_bubble_time) lv_label_set_text(s_bubble_time, "0 sn");
            lv_label_set_text_fmt(s_bubble_info, "Bitti! %d puan", s_bubble_score);
            bubble_show_result();
            evaluate_badges();
            show_next_badge_popup();
        } else {
            int tenths = (int)((remaining + 99999LL) / 100000LL);
            int seconds = (tenths + 9) / 10;
            if (s_bubble_progress && lv_bar_get_value(s_bubble_progress) != tenths) {
                lv_bar_set_value(s_bubble_progress, tenths, LV_ANIM_OFF);
            }
            if (s_bubble_time) {
                char text[16];
                snprintf(text, sizeof(text), "%d sn", seconds);
                label_text_changed(s_bubble_time, text);
            }
        }
    }

    if (s_page == PAGE_GAMES && s_game_view == GAME_ARCADE && s_arcade_info &&
        !s_arcade_finished) {
        int64_t remaining = s_arcade_end_us - esp_timer_get_time();
        if (remaining <= 0) {
            s_arcade_finished = true;
            uint8_t score = (uint8_t)(s_arcade_score > 255 ? 255 : s_arcade_score);
            (void)hidrolab_settings_record_activity_game(ACTIVITY_ARCADE, 15, score);
            if (s_arcade_progress) lv_bar_set_value(s_arcade_progress, 0, LV_ANIM_OFF);
            if (s_arcade_time) lv_label_set_text(s_arcade_time, "0 sn");
            if (s_arcade_info) lv_label_set_text_fmt(s_arcade_info, "Bitti! %d puan", s_arcade_score);
            game_react(s_arcade_score >= 12 ? "Muhteşem pilotluk!" : "Görev tamamlandı!", true);
            arcade_show_result();
            evaluate_badges();
            show_next_badge_popup();
        } else {
            int tenths = (int)((remaining + 99999LL) / 100000LL);
            int seconds = (tenths + 9) / 10;
            if (s_arcade_progress && lv_bar_get_value(s_arcade_progress) != tenths) {
                lv_bar_set_value(s_arcade_progress, tenths, LV_ANIM_OFF);
            }
            if (s_arcade_time) {
                char text[16];
                snprintf(text, sizeof(text), "%d sn", seconds);
                label_text_changed(s_arcade_time, text);
            }
            if (esp_timer_get_time() >= s_arcade_move_us) arcade_shuffle();
        }
    }

    if (s_page == PAGE_GAMES && s_game_view == GAME_WATER_PATH &&
        s_water_status && !s_water_finished) {
        water_update_hud();
    }
}

void hidrolab_ui_init(void)
{
    s_screen = lv_screen_active();
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);

    const hidrolab_settings_t *cfg = hidrolab_settings_get();
    s_setup_mode = !cfg->setup_done;
    s_base_home = true;
    s_setup_step = SETUP_WELCOME;
    s_setup_wifi_scan_started = false;
    s_setup_build_pending = false;
    s_setup_build_retry = false;
    s_game_guide_visible = false;
    strlcpy(s_setup_name_buf, cfg->astronaut_name, sizeof(s_setup_name_buf));

    shuffle_memory();
    s_memory_pairs = 0;
    s_memory_first = -1;
    s_memory_second = -1;
    s_memory_busy = false;
    s_bubble_end_us = 0;
    s_bubble_finished = false;
    s_water_level = 0;
    s_water_moves = 0;
    s_water_start_us = 0;
    s_water_final_seconds = 0;
    s_water_last_signature = 0;
    s_water_finished = false;
    s_water_recorded = false;
    s_landing_timer = NULL;
    s_bug_timer = NULL;
    s_bug_fast_display = false;
    bug_init_game();
    s_arcade_target = -1;
    s_arcade_end_us = 0;
    s_arcade_target_since_us = 0;
    s_arcade_finished = false;
    s_sleep_screen = false;
    s_night_awake_until_us = 0;

    /* Preload once so the base appears instantly after waking and boot logs
     * can verify the independent art partition even during night mode. */
    if (!s_setup_mode) (void)load_moon_base_art();

    if (s_setup_mode) build_setup_step();
    else if (hidrolab_time_is_night()) build_sleep_screen();
    else render_page();

    s_tick_timer = lv_timer_create(ui_tick, 500, NULL);
    if (!s_tick_timer) {
        ESP_LOGE(UI_TAG, "UI tick timer could not be created");
    }
}
