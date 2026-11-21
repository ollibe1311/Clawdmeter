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
#define COLOR_BG         lv_color_hex(0xF7F3EC)
#define COLOR_TEXT       lv_color_hex(0x1A1A1A)
#define COLOR_ARLO       lv_color_hex(0x2E9E5B)
#define COLOR_WILLOW     lv_color_hex(0xFF2E88)
#define COLOR_PARENT     lv_color_hex(0xFF6A13)
#define COLOR_LIGHT_GRAY lv_color_hex(0xCCCCCC)

// LVGL symbols for icons
#define ICON_EVENT     LV_SYMBOL_BELL      // LVGL 9 has no CALENDAR symbol
#define ICON_BRING     LV_SYMBOL_HOME
#define ICON_ACTION    LV_SYMBOL_WARNING
#define ICON_UNIFORM   LV_SYMBOL_IMAGE
#define ICON_ACTIVITY  LV_SYMBOL_PLAY
#define ICON_WEATHER   ""                  // LVGL 9 has no CLOUD symbol
#define ICON_PARTY     LV_SYMBOL_CHARGE

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

// Swipe gesture tracking
static lv_point_t swipe_start = {0, 0};
static bool swipe_active = false;
#define SWIPE_THRESHOLD 50

// Forward declarations
static void create_clawd_screen();
static void create_kid_screen(lv_obj_t** container);
static void create_parents_screen();
static void update_kid_screen(lv_obj_t* container, const KidDay* kid, 
                              const char* name, lv_color_t color, 
                              float max_temp, int precip_prob, const char* date_str);
static void update_parents_screen(const char* date_str);
static const char* get_day_name(int day_of_week);
static const char* get_month_name(int month);
static bool should_show_tonight();
static int get_arlo_first_offset();
static const char* get_item_icon(ItemType type);
static void handle_touch_event(lv_event_t* e);
static void handle_clawd_tap(lv_event_t* e);

void morning_ui_init() {
    lv_obj_t* screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    
    // Create all screen containers (hidden by default)
    create_clawd_screen();
    create_kid_screen(&kid1_container);
    create_kid_screen(&kid2_container);
    create_parents_screen();
    
    // Start on Clawd screen
    morning_ui_show_screen(MORNING_SCREEN_CLAWD);
    last_activity_ms = millis();
}

static void create_clawd_screen() {
    clawd_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(clawd_container, 480, 480);
    lv_obj_set_style_bg_color(clawd_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(clawd_container, 0, 0);
    lv_obj_set_style_pad_all(clawd_container, 0, 0);
    lv_obj_set_style_radius(clawd_container, 0, 0);
    lv_obj_set_pos(clawd_container, 0, 0);
    lv_obj_clear_flag(clawd_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(clawd_container, LV_OBJ_FLAG_CLICKABLE);
    
    // Initialize splash animation system
    splash_init(clawd_container);
    
    // splash_init() puts a full-screen lv_obj on top of clawd_container.
    // lv_obj_create() makes objects CLICKABLE by default, so that overlay was
    // swallowing every tap and clawd_container's CLICKED handler never ran.
    // Let taps fall through / bubble to the page, and also hook the overlay
    // directly so a tap works whichever object LVGL picks.
    lv_obj_t* splash_root = splash_get_root();
    if (splash_root) {
        lv_obj_add_flag(splash_root, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(splash_root, handle_clawd_tap, LV_EVENT_CLICKED, NULL);
        for (uint32_t i = 0; i < lv_obj_get_child_count(splash_root); i++) {
            lv_obj_t* ch = lv_obj_get_child(splash_root, i);
            lv_obj_clear_flag(ch, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(ch, LV_OBJ_FLAG_EVENT_BUBBLE);
        }
    }

    // Tap Clawd -> first kid page (agreed spec)
    lv_obj_add_event_cb(clawd_container, handle_clawd_tap, LV_EVENT_CLICKED, NULL);
}

static void handle_clawd_tap(lv_event_t* e) {
    // Bubbling can deliver the same click to both the overlay and the page;
    // the first call switches screens, so the second is a no-op.
    if (current_screen != MORNING_SCREEN_CLAWD) return;
    Serial.println("UI: Clawd tapped -> kid page");
    morning_ui_wake();
    morning_ui_show_screen(MORNING_SCREEN_KID1);
}

static void create_kid_screen(lv_obj_t** container) {
    *container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(*container, 480, 480);
    lv_obj_set_style_bg_color(*container, COLOR_BG, 0);
    lv_obj_set_style_border_width(*container, 0, 0);
    lv_obj_set_style_radius(*container, 0, 0);
    lv_obj_set_style_pad_all(*container, 20, 0);
    lv_obj_add_flag(*container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(*container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(*container, LV_OBJ_FLAG_SCROLLABLE);  // LVGL scroll would eat vertical swipes
    
    // Add swipe/touch handler
    lv_obj_add_event_cb(*container, handle_touch_event, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(*container, handle_touch_event, LV_EVENT_RELEASED, NULL);
}

static void create_parents_screen() {
    parents_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(parents_container, 480, 480);
    lv_obj_set_style_bg_color(parents_container, COLOR_BG, 0);
    lv_obj_set_style_border_width(parents_container, 0, 0);
    lv_obj_set_style_radius(parents_container, 0, 0);
    lv_obj_set_style_pad_all(parents_container, 20, 0);
    lv_obj_add_flag(parents_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(parents_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(parents_container, LV_OBJ_FLAG_SCROLLABLE);  // LVGL scroll would eat vertical swipes
    
    // Add swipe/touch handler
    lv_obj_add_event_cb(parents_container, handle_touch_event, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(parents_container, handle_touch_event, LV_EVENT_RELEASED, NULL);
}

static void handle_touch_event(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    
    if (code == LV_EVENT_PRESSING) {
        if (!swipe_active) {
            swipe_start = point;
            swipe_active = true;
        }
    } else if (code == LV_EVENT_RELEASED && swipe_active) {
        int dx = point.x - swipe_start.x;
        int dy = point.y - swipe_start.y;
        swipe_active = false;
        Serial.printf("UI: gesture start=(%d,%d) dx=%d dy=%d screen=%d\n",
                      (int)swipe_start.x, (int)swipe_start.y, dx, dy, (int)current_screen);
        
        // Tap on top strip (y < 60) returns to Clawd
        if (swipe_start.y < 60 && abs(dx) < 20 && abs(dy) < 20) {
            morning_ui_reset_to_clawd();
            return;
        }
        
        // Determine swipe direction
        if (abs(dx) > abs(dy) && abs(dx) > SWIPE_THRESHOLD) {
            // Horizontal swipe - change day
            if (dx > 0) {
                // Swipe right - previous day
                if (current_day_offset > 0) {
                    current_day_offset--;
                    morning_ui_update_feed(current_feed);
                }
            } else {
                // Swipe left - next day
                if (current_day_offset < 6) {
                    current_day_offset++;
                    morning_ui_update_feed(current_feed);
                }
            }
        } else if (abs(dy) > abs(dx) && abs(dy) > SWIPE_THRESHOLD) {
            // Vertical swipe - change page
            if (dy > 0) {
                // Swipe down - previous page
                switch (current_screen) {
                    case MORNING_SCREEN_KID2:
                        morning_ui_show_screen(MORNING_SCREEN_KID1);
                        break;
                    case MORNING_SCREEN_PARENTS:
                        morning_ui_show_screen(MORNING_SCREEN_KID2);
                        break;
                    default:
                        break;
                }
            } else {
                // Swipe up - next page
                switch (current_screen) {
                    case MORNING_SCREEN_KID1:
                        morning_ui_show_screen(MORNING_SCREEN_KID2);
                        break;
                    case MORNING_SCREEN_KID2:
                        morning_ui_show_screen(MORNING_SCREEN_PARENTS);
                        break;
                    default:
                        break;
                }
            }
        }
        
        morning_ui_wake();
    }
}

void morning_ui_show_screen(MorningScreen screen) {
    // Hide all
    if (screen != MORNING_SCREEN_CLAWD) splash_hide();
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
    
    if (!feed || !feed->valid || current_day_offset >= feed->day_count) return;
    
    const DayData* day = &feed->days[current_day_offset];
    float max_temp = (current_weather && current_weather->valid && current_day_offset < 7) ? 
                     current_weather->max_temp[current_day_offset] : 20.0f;
    int precip = (current_weather && current_weather->valid && current_day_offset < 7) ? 
                 current_weather->precip_prob[current_day_offset] : 0;
    
    // Format date string
    char date_str[32];
    struct tm tm_date = {0};
    sscanf(day->date, "%d-%d-%d", &tm_date.tm_year, &tm_date.tm_mon, &tm_date.tm_mday);
    tm_date.tm_year -= 1900;
    tm_date.tm_mon -= 1;
    mktime(&tm_date);
    snprintf(date_str, sizeof(date_str), "%s %d %s", 
             get_day_name(tm_date.tm_wday), 
             tm_date.tm_mday,
             get_month_name(tm_date.tm_mon));
    
    // Determine which kid is first
    int arlo_first = get_arlo_first_offset();
    
    if (arlo_first == 0) {
        update_kid_screen(kid1_container, &day->arlo, "Arlo", COLOR_ARLO, max_temp, precip, date_str);
        update_kid_screen(kid2_container, &day->willow, "Willow", COLOR_WILLOW, max_temp, precip, date_str);
    } else {
        update_kid_screen(kid1_container, &day->willow, "Willow", COLOR_WILLOW, max_temp, precip, date_str);
        update_kid_screen(kid2_container, &day->arlo, "Arlo", COLOR_ARLO, max_temp, precip, date_str);
    }
    
    update_parents_screen(date_str);
}

void morning_ui_update_weather(const WeatherData* weather) {
    current_weather = weather;
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
        } else if (!morning_ui_should_be_on()) {
            // Outside scheduled hours - sleep
            screen_is_on = false;
        }
    }
}

void morning_ui_check_schedule() {
    bool should_be_on = morning_ui_should_be_on();
    
    if (should_be_on && !screen_is_on) {
        // Auto wake
        screen_is_on = true;
        // Backlight control handled in morning_main
    } else if (!should_be_on && screen_is_on) {
        // Auto sleep (unless recently touched)
        if (millis() - last_activity_ms > IDLE_TIMEOUT_MS) {
            screen_is_on = false;
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

bool morning_ui_is_screen_on() {
    return screen_is_on;
}

// Helper functions

static void update_kid_screen(lv_obj_t* container, const KidDay* kid, 
                              const char* name, lv_color_t color,
                              float max_temp, int precip_prob, const char* date_str) {
    lv_obj_clean(container);
    
    // Top bar
    lv_obj_t* top_bar = lv_obj_create(container);
    lv_obj_clear_flag(top_bar, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));  // let touches reach the page for swipes
    lv_obj_set_size(top_bar, 440, 40);
    lv_obj_align(top_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(top_bar, COLOR_BG, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    
    // Date
    lv_obj_t* date_label = lv_label_create(top_bar);
    lv_label_set_text(date_label, date_str);
    lv_obj_set_style_text_font(date_label, &morning_font_20, 0);
    lv_obj_align(date_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Weather
    char weather_str[32];
    snprintf(weather_str, sizeof(weather_str), "%s%.0f\xC2\xB0", ICON_WEATHER, max_temp);  // UTF-8 degree sign
    lv_obj_t* weather_label = lv_label_create(top_bar);
    lv_label_set_text(weather_label, weather_str);
    lv_obj_set_style_text_font(weather_label, &morning_font_16, 0);
    lv_obj_align(weather_label, LV_ALIGN_RIGHT_MID, 0, 0);
    
    // Day dots (1 of 7)
    lv_obj_t* dots = lv_obj_create(container);
    lv_obj_clear_flag(dots, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));  // let touches reach the page for swipes
    lv_obj_set_size(dots, 100, 10);
    lv_obj_align(dots, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(dots, COLOR_BG, 0);
    lv_obj_set_style_border_width(dots, 0, 0);
    lv_obj_set_style_pad_all(dots, 0, 0);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    for (int i = 0; i < 7; i++) {
        lv_obj_t* dot = lv_obj_create(dots);
        lv_obj_clear_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));  // let touches reach the page for swipes
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        if (i == current_day_offset) {
            lv_obj_set_style_bg_color(dot, COLOR_TEXT, 0);
        } else {
            lv_obj_set_style_bg_color(dot, COLOR_LIGHT_GRAY, 0);
        }
    }
    
    // Kid name
    lv_obj_t* name_label = lv_label_create(container);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_font(name_label, &morning_font_48, 0);
    lv_obj_set_style_text_color(name_label, color, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 65);
    
    int y_pos = 125;
    
    // Tonight section (from 3pm)
    if (should_show_tonight() && kid->tonight_count > 0) {
        lv_obj_t* tonight_label = lv_label_create(container);
        lv_label_set_text(tonight_label, "Tonight:");
        lv_obj_set_style_text_font(tonight_label, &morning_font_20, 0);
        lv_obj_set_style_text_color(tonight_label, color, 0);
        lv_obj_align(tonight_label, LV_ALIGN_TOP_LEFT, 0, y_pos);
        y_pos += 30;
        
        for (int i = 0; i < kid->tonight_count && i < 3; i++) {
            lv_obj_t* item = lv_label_create(container);
            char text[MAX_TEXT_LEN + 4];
            snprintf(text, sizeof(text), "  %s", kid->tonight[i]);
            lv_label_set_text(item, text);
            lv_obj_set_style_text_font(item, &morning_font_16, 0);
            lv_obj_align(item, LV_ALIGN_TOP_LEFT, 10, y_pos);
            y_pos += 25;
        }
        y_pos += 10;
    }
    
    // Items list
    if (kid->item_count == 0) {
        lv_obj_t* empty = lv_label_create(container);
        lv_label_set_text(empty, "Nothing on today!");
        lv_obj_set_style_text_font(empty, &morning_font_28, 0);
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 0, y_pos);
    } else {
        for (int i = 0; i < kid->item_count && i < 8 && y_pos < 380; i++) {
            const Item* item = &kid->items[i];
            
            lv_obj_t* item_label = lv_label_create(container);
            char text[MAX_TEXT_LEN + 20];
            const char* icon = get_item_icon(item->type);
            
            if (item->type == ITEM_ACTION && item->deadline[0]) {
                // Parse deadline and show day
                struct tm tm_deadline = {0};
                sscanf(item->deadline, "%d-%d-%d", &tm_deadline.tm_year, &tm_deadline.tm_mon, &tm_deadline.tm_mday);
                tm_deadline.tm_year -= 1900;
                tm_deadline.tm_mon -= 1;
                mktime(&tm_deadline);
                snprintf(text, sizeof(text), "%s %s (%s %d)", icon, item->text, get_day_name(tm_deadline.tm_wday), tm_deadline.tm_mday);
            } else {
                snprintf(text, sizeof(text), "%s %s", icon, item->text);
            }
            
            lv_label_set_text(item_label, text);
            lv_obj_set_style_text_font(item_label, &morning_font_20, 0);
            lv_obj_set_width(item_label, 420);
            lv_label_set_long_mode(item_label, LV_LABEL_LONG_WRAP);
            lv_obj_align(item_label, LV_ALIGN_TOP_LEFT, 0, y_pos);
            y_pos += 30;
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
        
        if (precip_prob >= morning_config_get_dress_rules().rain_threshold) {
            snprintf(dress_text, sizeof(dress_text), "%s + RAIN JACKET", uniform_name);
        } else {
            strlcpy(dress_text, uniform_name, sizeof(dress_text));
        }
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
    lv_obj_set_style_text_font(dress, &morning_font_16, 0);
    lv_obj_set_width(dress, 420);
    lv_label_set_long_mode(dress, LV_LABEL_LONG_WRAP);
    lv_obj_align(dress, LV_ALIGN_BOTTOM_LEFT, 0, -10);
}

static void update_parents_screen(const char* date_str) {
    if (!current_feed || !current_feed->valid || current_day_offset >= current_feed->day_count) return;
    
    lv_obj_clean(parents_container);
    
    const DayData* day = &current_feed->days[current_day_offset];
    
    // Top bar
    lv_obj_t* top_bar = lv_obj_create(parents_container);
    lv_obj_clear_flag(top_bar, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));  // let touches reach the page for swipes
    lv_obj_set_size(top_bar, 440, 40);
    lv_obj_align(top_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(top_bar, COLOR_BG, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    
    // Date
    lv_obj_t* date_label = lv_label_create(top_bar);
    lv_label_set_text(date_label, date_str);
    lv_obj_set_style_text_font(date_label, &morning_font_20, 0);
    lv_obj_align(date_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Day dots
    lv_obj_t* dots = lv_obj_create(parents_container);
    lv_obj_clear_flag(dots, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));  // let touches reach the page for swipes
    lv_obj_set_size(dots, 100, 10);
    lv_obj_align(dots, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(dots, COLOR_BG, 0);
    lv_obj_set_style_border_width(dots, 0, 0);
    lv_obj_set_style_pad_all(dots, 0, 0);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    for (int i = 0; i < 7; i++) {
        lv_obj_t* dot = lv_obj_create(dots);
        lv_obj_clear_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));  // let touches reach the page for swipes
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        if (i == current_day_offset) {
            lv_obj_set_style_bg_color(dot, COLOR_TEXT, 0);
        } else {
            lv_obj_set_style_bg_color(dot, COLOR_LIGHT_GRAY, 0);
        }
    }
    
    // Title
    lv_obj_t* title = lv_label_create(parents_container);
    lv_label_set_text(title, "Parents");
    lv_obj_set_style_text_font(title, &morning_font_48, 0);
    lv_obj_set_style_text_color(title, COLOR_PARENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 65);
    
    // Items
    int y_pos = 125;
    if (day->parents.item_count == 0) {
        lv_obj_t* empty = lv_label_create(parents_container);
        lv_label_set_text(empty, "Nothing scheduled");
        lv_obj_set_style_text_font(empty, &morning_font_28, 0);
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 0, y_pos);
    } else {
        for (int i = 0; i < day->parents.item_count && i < 10 && y_pos < 440; i++) {
            const Item* item = &day->parents.items[i];
            lv_obj_t* item_label = lv_label_create(parents_container);
            
            char text[MAX_TEXT_LEN + 20];
            const char* icon = get_item_icon(item->type);
            snprintf(text, sizeof(text), "%s %s", icon, item->text);
            
            lv_label_set_text(item_label, text);
            lv_obj_set_style_text_font(item_label, &morning_font_20, 0);
            lv_obj_set_width(item_label, 420);
            lv_label_set_long_mode(item_label, LV_LABEL_LONG_WRAP);
            lv_obj_align(item_label, LV_ALIGN_TOP_LEFT, 0, y_pos);
            y_pos += 30;
        }
    }
}

static const char* get_day_name(int day_of_week) {
    const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return names[day_of_week % 7];
}

static const char* get_month_name(int month) {
    const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return names[month % 12];
}

static bool should_show_tonight() {
    struct tm timeinfo;
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_hour >= 15;
}

static int get_arlo_first_offset() {
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

static const char* get_item_icon(ItemType type) {
    switch (type) {
        case ITEM_EVENT:    return ICON_EVENT;
        case ITEM_BRING:    return ICON_BRING;
        case ITEM_ACTION:   return ICON_ACTION;
        case ITEM_UNIFORM:  return ICON_UNIFORM;
        case ITEM_ACTIVITY: return ICON_ACTIVITY;
        default:            return "";
    }
}

#endif  // BOARD_MORNING_BOARD
