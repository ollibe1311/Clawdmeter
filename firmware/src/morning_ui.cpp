#ifdef BOARD_MORNING_BOARD
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include "morning_ui.h"
#include "morning_config.h"
#include "morning_fonts.h"
#include "splash.h"
#include <time.h>

// Colors - warm off-white background, kid accents
#define COLOR_BG         0xF7F3EC  // Warm off-white
#define COLOR_TEXT       0x1A1A1A  // Dark gray
#define COLOR_ARLO       0x2E9E5B  // Green
#define COLOR_WILLOW     0xFF2E88  // Hot pink
#define COLOR_PARENT     0xFF6A13  // Orange

// Declare fonts (we'll generate these later)
LV_FONT_DECLARE(morning_font_48);  // Name
LV_FONT_DECLARE(morning_font_28);  // Items
LV_FONT_DECLARE(morning_font_20);  // Details

static MorningScreen current_screen = MORNING_SCREEN_CLAWD;
static int current_day_offset = 0;  // 0 = today, 1-6 = future days
static const FeedData* current_feed = nullptr;
static const WeatherData* current_weather = nullptr;
static uint32_t last_activity_ms = 0;
static bool screen_is_on = true;
static const uint32_t IDLE_TIMEOUT_MS = 120000;  // 2 minutes

static lv_obj_t* clawd_container = nullptr;
static lv_obj_t* kid1_container = nullptr;
static lv_obj_t* kid2_container = nullptr;
static lv_obj_t* parents_container = nullptr;

// Forward declarations
static void create_clawd_screen();
static void create_kid_screen(lv_obj_t** container, bool is_arlo);
static void create_parents_screen();
static void update_kid_screen(lv_obj_t* container, const KidDay* kid, 
                              const char* name, uint32_t color, 
                              float max_temp, int precip_prob);
static void update_parents_screen();
static const char* get_day_name(int day_of_week);
static bool should_show_tonight();
static int get_arlo_first_offset();

void morning_ui_init() {
    lv_obj_t* screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    
    // Create all screen containers (hidden by default)
    create_clawd_screen();
    create_kid_screen(&kid1_container, true);
    create_kid_screen(&kid2_container, false);
    create_parents_screen();
    
    // Start on Clawd screen
    morning_ui_show_screen(MORNING_SCREEN_CLAWD);
    last_activity_ms = millis();
}

static void create_clawd_screen() {
    clawd_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(clawd_container, 480, 480);
    lv_obj_set_style_bg_color(clawd_container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(clawd_container, 0, 0);
    // Default theme gives lv_obj padding + radius; the splash is placed at
    // (0,0) of the content area, so the padding pushed it right/down and left
    // an off-white L strip on the top/left edges. Zero them.
    lv_obj_set_style_pad_all(clawd_container, 0, 0);
    lv_obj_set_style_radius(clawd_container, 0, 0);
    lv_obj_set_pos(clawd_container, 0, 0);
    lv_obj_clear_flag(clawd_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Initialize splash animation system
    splash_init(clawd_container);
    
    // Add touch handler to move to pages
    lv_obj_add_flag(clawd_container, LV_OBJ_FLAG_CLICKABLE);
}

static void create_kid_screen(lv_obj_t** container, bool is_arlo) {
    *container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(*container, 480, 480);
    lv_obj_set_style_bg_color(*container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(*container, 0, 0);
    lv_obj_set_style_radius(*container, 0, 0);
    lv_obj_set_style_pad_all(*container, 20, 0);
    lv_obj_add_flag(*container, LV_OBJ_FLAG_HIDDEN);
}

static void create_parents_screen() {
    parents_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(parents_container, 480, 480);
    lv_obj_set_style_bg_color(parents_container, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(parents_container, 0, 0);
    lv_obj_set_style_radius(parents_container, 0, 0);
    lv_obj_set_style_pad_all(parents_container, 20, 0);
    lv_obj_add_flag(parents_container, LV_OBJ_FLAG_HIDDEN);
}

void morning_ui_show_screen(MorningScreen screen) {
    // Hide all
    if (clawd_container) lv_obj_add_flag(clawd_container, LV_OBJ_FLAG_HIDDEN);
    if (kid1_container) lv_obj_add_flag(kid1_container, LV_OBJ_FLAG_HIDDEN);
    if (kid2_container) lv_obj_add_flag(kid2_container, LV_OBJ_FLAG_HIDDEN);
    if (parents_container) lv_obj_add_flag(parents_container, LV_OBJ_FLAG_HIDDEN);
    
    // Show requested
    current_screen = screen;
    switch (screen) {
        case MORNING_SCREEN_CLAWD:
            if (clawd_container) {
                lv_obj_clear_flag(clawd_container, LV_OBJ_FLAG_HIDDEN);
                splash_show();
            }
            break;
        case MORNING_SCREEN_KID1:
            if (kid1_container) lv_obj_clear_flag(kid1_container, LV_OBJ_FLAG_HIDDEN);
            break;
        case MORNING_SCREEN_KID2:
            if (kid2_container) lv_obj_clear_flag(kid2_container, LV_OBJ_FLAG_HIDDEN);
            break;
        case MORNING_SCREEN_PARENTS:
            if (parents_container) lv_obj_clear_flag(parents_container, LV_OBJ_FLAG_HIDDEN);
            break;
    }
    
    last_activity_ms = millis();
}

void morning_ui_update_feed(const FeedData* feed) {
    current_feed = feed;
    
    if (!feed || !feed->valid) return;
    
    // Update all kid screens with current day data
    if (current_day_offset < feed->day_count) {
        const DayData* day = &feed->days[current_day_offset];
        float max_temp = current_weather && current_weather->valid ? 
                         current_weather->max_temp[current_day_offset] : 20.0f;
        int precip = current_weather && current_weather->valid ? 
                     current_weather->precip_prob[current_day_offset] : 0;
        
        // Determine which kid is first (alternates daily)
        int arlo_first = get_arlo_first_offset();
        
        if (arlo_first == 0) {
            update_kid_screen(kid1_container, &day->arlo, "Arlo", COLOR_ARLO, max_temp, precip);
            update_kid_screen(kid2_container, &day->willow, "Willow", COLOR_WILLOW, max_temp, precip);
        } else {
            update_kid_screen(kid1_container, &day->willow, "Willow", COLOR_WILLOW, max_temp, precip);
            update_kid_screen(kid2_container, &day->arlo, "Arlo", COLOR_ARLO, max_temp, precip);
        }
        
        update_parents_screen();
    }
}

void morning_ui_update_weather(const WeatherData* weather) {
    current_weather = weather;
    // Re-update displays with new weather
    morning_ui_update_feed(current_feed);
}

MorningScreen morning_ui_get_current_screen() {
    return current_screen;
}

void morning_ui_tick() {
    splash_tick();
    
    // Check for idle timeout
    if (millis() - last_activity_ms > IDLE_TIMEOUT_MS) {
        if (current_screen != MORNING_SCREEN_CLAWD) {
            morning_ui_reset_to_clawd();
        }
    }
}

void morning_ui_check_schedule() {
    bool should_be_on = morning_ui_should_be_on();
    
    if (should_be_on && !screen_is_on) {
        // Auto wake
        screen_is_on = true;
        // Turn on backlight via display HAL
    } else if (!should_be_on && screen_is_on) {
        // Auto sleep (unless recently touched)
        if (millis() - last_activity_ms > IDLE_TIMEOUT_MS) {
            screen_is_on = false;
            // Turn off backlight via display HAL
        }
    }
}

bool morning_ui_should_be_on() {
    struct tm timeinfo;
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    ScreenSchedule sched = morning_config_get_schedule();
    int hour = timeinfo.tm_hour;
    int min = timeinfo.tm_min;
    int time_mins = hour * 60 + min;
    
    int morning_start = sched.morning_start_hour * 60 + sched.morning_start_min;
    int morning_end = sched.morning_end_hour * 60 + sched.morning_end_min;
    int afternoon_start = sched.afternoon_start_hour * 60 + sched.afternoon_start_min;
    int afternoon_end = sched.afternoon_end_hour * 60 + sched.afternoon_end_min;
    
    return (time_mins >= morning_start && time_mins < morning_end) ||
           (time_mins >= afternoon_start && time_mins < afternoon_end);
}

void morning_ui_wake() {
    screen_is_on = true;
    last_activity_ms = millis();
}

void morning_ui_reset_to_clawd() {
    current_day_offset = 0;
    morning_ui_show_screen(MORNING_SCREEN_CLAWD);
}

// Helper functions

static void update_kid_screen(lv_obj_t* container, const KidDay* kid, 
                              const char* name, uint32_t color,
                              float max_temp, int precip_prob) {
    // Clear existing content
    lv_obj_clean(container);
    
    // Title bar with day/time
    lv_obj_t* title = lv_label_create(container);
    lv_label_set_text(title, "Monday 23 Nov");  // TODO: actual date
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Kid name (big, with accent color)
    lv_obj_t* name_label = lv_label_create(container);
    lv_label_set_text(name_label, name);
    // lv_obj_set_style_text_font(name_label, &morning_font_48, 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(color), 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 40);
    
    // Items list (simplified - full version would iterate and format)
    int y_pos = 100;
    if (kid->item_count == 0) {
        lv_obj_t* empty = lv_label_create(container);
        lv_label_set_text(empty, "Nothing on today!");  // TODO: emoji support
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 0, y_pos);
    } else {
        for (int i = 0; i < kid->item_count && i < 5; i++) {
            lv_obj_t* item = lv_label_create(container);
            lv_label_set_text(item, kid->items[i].text);
            lv_obj_align(item, LV_ALIGN_TOP_LEFT, 0, y_pos);
            y_pos += 35;
        }
    }
    
    // Dress advice at bottom
    lv_obj_t* dress = lv_label_create(container);
    char dress_text[128];
    
    if (kid->uniform != UNIFORM_NONE) {
        const char* uniform_name = "Uniform";
        if (kid->uniform == UNIFORM_SPORT) uniform_name = "Sport uniform";
        else if (kid->uniform == UNIFORM_FORMAL) uniform_name = "Formal uniform";
        else if (kid->uniform == UNIFORM_MUFTI) uniform_name = "Mufti";
        
        snprintf(dress_text, sizeof(dress_text), "%s", uniform_name);
    } else {
        DressRules rules = morning_config_get_dress_rules();
        const char* clothes = "";
        
        if (max_temp >= rules.shorts_threshold) {
            clothes = strcmp(name, "Arlo") == 0 ? "Shorts + short sleeves" : "Skort + short sleeves";
        } else if (max_temp >= rules.pants_threshold) {
            clothes = "Pants + short sleeves + jumper";
        } else {
            clothes = "Pants + long sleeves + jumper";
        }
        
        if (precip_prob >= rules.rain_threshold) {
            snprintf(dress_text, sizeof(dress_text), "%s + RAIN JACKET", clothes);
        } else {
            strlcpy(dress_text, clothes, sizeof(dress_text));
        }
    }
    
    lv_label_set_text(dress, dress_text);
    lv_obj_align(dress, LV_ALIGN_BOTTOM_LEFT, 0, -10);
}

static void update_parents_screen() {
    if (!current_feed || !current_feed->valid) return;
    if (current_day_offset >= current_feed->day_count) return;
    
    lv_obj_clean(parents_container);
    
    const DayData* day = &current_feed->days[current_day_offset];
    
    // Title
    lv_obj_t* title = lv_label_create(parents_container);
    lv_label_set_text(title, "Parents");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_PARENT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 40);
    
    // Items
    int y_pos = 100;
    if (day->parents.item_count == 0) {
        lv_obj_t* empty = lv_label_create(parents_container);
        lv_label_set_text(empty, "Nothing scheduled");
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 0, y_pos);
    } else {
        for (int i = 0; i < day->parents.item_count && i < 10; i++) {
            lv_obj_t* item = lv_label_create(parents_container);
            lv_label_set_text(item, day->parents.items[i].text);
            lv_obj_align(item, LV_ALIGN_TOP_LEFT, 0, y_pos);
            y_pos += 35;
        }
    }
}

static const char* get_day_name(int day_of_week) {
    const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return names[day_of_week % 7];
}

static bool should_show_tonight() {
    struct tm timeinfo;
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_hour >= 15;
}

static int get_arlo_first_offset() {
    // Alternates daily based on days since epoch
    time_t now;
    time(&now);
    struct tm epoch = {0};
    epoch.tm_year = 126;  // 2026
    epoch.tm_mon = 0;     // January
    epoch.tm_mday = 1;
    time_t epoch_time = mktime(&epoch);
    
    int days_since = (now - epoch_time) / 86400;
    return days_since % 2;
}
#endif  // BOARD_MORNING_BOARD
