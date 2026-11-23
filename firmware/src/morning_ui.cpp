#ifdef BOARD_MORNING_BOARD
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include "morning_ui.h"
#include "morning_config.h"
#include "morning_fonts.h"
#include "splash.h"
#include "morning_weather_icons.h"
#include "morning_item_icons.h"
#include "boards/waveshare_lcd_4/io_expander.h"
#include <ctype.h>
#include <time.h>

// Colors - warm off-white background, kid accents
#define COLOR_BG         lv_color_hex(0xF7F3EC)
#define COLOR_TEXT       lv_color_hex(0x1A1A1A)
#define COLOR_ARLO       lv_color_hex(0x2E9E5B)
#define COLOR_WILLOW     lv_color_hex(0xFF2E88)
#define COLOR_PARENT     lv_color_hex(0xFF6A13)
#define COLOR_LIGHT_GRAY lv_color_hex(0xCCCCCC)


static MorningScreen current_screen = MORNING_SCREEN_CLAWD;
static int current_day_offset = 0;  // 0 = today, 1-6 = future days
static const FeedData* current_feed = nullptr;
static const WeatherData* current_weather = nullptr;
static uint32_t last_activity_ms = 0;
static bool screen_is_on = true;
static const uint32_t IDLE_TIMEOUT_MS = 120000;  // 2 minutes

// Scheduled animation system
static int last_fired_slot = -1;  // Track last fired slot (hour * 2 + (minute == 30 ? 1 : 0))
static bool animation_playing = false;
static int clips_played = 0;
static int animation_sequence[3] = {-1, -1, -1};  // Indices of clips to play
static uint32_t animation_start_ms = 0;

static lv_obj_t* clawd_container = nullptr;
static lv_obj_t* kid1_container = nullptr;
static lv_obj_t* kid2_container = nullptr;
static lv_obj_t* parents_container = nullptr;
static lv_obj_t* clock_container = nullptr;
static lv_obj_t* clock_digits = nullptr;
static lv_obj_t* clock_info = nullptr;
static char clock_text[8] = "--:--";
static int clock_last_min = -1;

// Slide animations (container x/y on the one screen; no extra screens to leak)
#define SLIDE_MS 250
static bool anim_busy = false;
static lv_obj_t* slide_old = nullptr;
static int slide_day_dir = 0;
static void anim_set_x(void* obj, int32_t v) { lv_obj_set_x((lv_obj_t*)obj, v); }
static void anim_set_y(void* obj, int32_t v) { lv_obj_set_y((lv_obj_t*)obj, v); }
static lv_obj_t* container_for(MorningScreen s);
static void stop_slides();
static void slide_to_page(MorningScreen to, int dir);
static void slide_day(int dir);

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
static const lv_image_dsc_t* get_item_icon(const Item* item);
static int add_item_row(lv_obj_t* parent, const lv_image_dsc_t* icon, const char* text, int y);
static int add_birthday_rows(lv_obj_t* parent, const DayData* day, int y);
static const lv_image_dsc_t* get_weather_icon(int weathercode);
static void handle_touch_event(lv_event_t* e);
static void handle_clawd_tap(lv_event_t* e);
static void go_idle();
static void create_clock_screen();
static void update_clock(bool force);

void morning_ui_init() {
    lv_obj_t* screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    // Pages slide off-screen during animations; keep the screen itself from
    // becoming scrollable (or showing scrollbars) while they are out of bounds.
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    
    // Create all screen containers (hidden by default)
    create_clawd_screen();
    create_kid_screen(&kid1_container);
    create_kid_screen(&kid2_container);
    create_parents_screen();
    create_clock_screen();
    
    // Clips only change under the scheduled sequence's control
    splash_set_auto_rotate(false);

    // Boot: show the day view for the 2-minute idle window, then the clock
    morning_ui_show_screen(MORNING_SCREEN_KID1);
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
    } else if (code == LV_EVENT_RELEASED && swipe_active && anim_busy) {
        swipe_active = false;   // ignore gestures while a slide is running
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
                if (current_day_offset > 0) slide_day(-1);
            } else {
                // Swipe left - next day
                if (current_day_offset < 6) slide_day(+1);
            }
        } else if (abs(dy) > abs(dx) && abs(dy) > SWIPE_THRESHOLD) {
            // Vertical swipe - change page
            if (dy > 0) {
                // Swipe down - previous page
                switch (current_screen) {
                    case MORNING_SCREEN_KID2:
                        slide_to_page(MORNING_SCREEN_KID1, -1);
                        break;
                    case MORNING_SCREEN_PARENTS:
                        slide_to_page(MORNING_SCREEN_KID2, -1);
                        break;
                    default:
                        break;
                }
            } else {
                // Swipe up - next page
                switch (current_screen) {
                    case MORNING_SCREEN_KID1:
                        slide_to_page(MORNING_SCREEN_KID2, +1);
                        break;
                    case MORNING_SCREEN_KID2:
                        slide_to_page(MORNING_SCREEN_PARENTS, +1);
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
    stop_slides();
    // Hide all
    if (screen != MORNING_SCREEN_CLAWD) splash_hide();
    if (clawd_container) lv_obj_add_flag(clawd_container, LV_OBJ_FLAG_HIDDEN);
    if (kid1_container) lv_obj_add_flag(kid1_container, LV_OBJ_FLAG_HIDDEN);
    if (kid2_container) lv_obj_add_flag(kid2_container, LV_OBJ_FLAG_HIDDEN);
    if (parents_container) lv_obj_add_flag(parents_container, LV_OBJ_FLAG_HIDDEN);
    if (clock_container) lv_obj_add_flag(clock_container, LV_OBJ_FLAG_HIDDEN);
    
    // Show requested
    current_screen = screen;
    switch (screen) {
        case MORNING_SCREEN_CLAWD:
            splash_set_dance_mode(morning_ui_dance_day());
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
        case MORNING_SCREEN_CLOCK:
            if (clock_container) {
                update_clock(true);
                lv_obj_clear_flag(clock_container, LV_OBJ_FLAG_HIDDEN);
            }
            break;
    }
    // (last_activity_ms is touch time only; programmatic screen changes
    //  must not keep the screen awake)
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
    if (current_screen == MORNING_SCREEN_CLOCK) update_clock(true);
}

MorningScreen morning_ui_get_current_screen() {
    return current_screen;
}

void morning_ui_tick() {
    splash_tick();
    if (current_screen == MORNING_SCREEN_CLOCK) update_clock(false);   // redraws once a minute
    
    // Handle animation sequence playback
    if (animation_playing && current_screen == MORNING_SCREEN_CLAWD) {
        // Check if current clip has played long enough (20-30 seconds per clip)
        uint32_t elapsed = millis() - animation_start_ms;
        uint32_t clip_duration = 25000;  // 25 seconds per clip
        
        if (elapsed > (clips_played + 1) * clip_duration) {
            clips_played++;
            
            if (clips_played >= 3) {
                // Sequence complete: back to the clock is decided below (one place)
                animation_playing = false;
                Serial.println("Animation: Sequence complete");
            } else {
                // Switch to next clip
                splash_set_anim(animation_sequence[clips_played]);
                Serial.printf("Animation: Clip %d/%d\n", clips_played + 1, 3);
            }
        }
    }
    
    // Single source of truth for active vs idle:
    //   active = touched within IDLE_TIMEOUT_MS OR scheduled sequence playing
    //   idle   = the pixel clock, backlight on (no blanking when idle)
    bool touched = (millis() - last_activity_ms) < IDLE_TIMEOUT_MS;
    bool visible = touched || animation_playing;
    if (visible != screen_is_on) {
        screen_is_on = visible;
        if (visible) {
            // Woken by a touch while idle (clock / paused Clawd) -> day view
            if (!animation_playing && (current_screen == MORNING_SCREEN_CLAWD ||
                                       current_screen == MORNING_SCREEN_CLOCK))
                morning_ui_show_screen(MORNING_SCREEN_KID1);
            morning_ui_set_blank(false);
            Serial.println("Screen: ON");
        } else {
            go_idle();
            Serial.println("Screen: idle (clock)");
        }
    }
}

// Friday (all scheduled hours: mornings and afternoons) or a family birthday
// today -> Clawd mostly dances on the splash.
bool morning_ui_dance_day() {
    time_t now; time(&now);
    if (now < 1700000000) return false;            // clock not set yet
    struct tm t; localtime_r(&now, &t);
    if (t.tm_wday == 5) return true;
    if (current_feed && current_feed->valid && current_feed->day_count > 0) {
        char today[16];
        strftime(today, sizeof(today), "%Y-%m-%d", &t);
        for (int d = 0; d < current_feed->day_count; d++) {
            if (strcmp(current_feed->days[d].date, today) == 0)
                return current_feed->days[d].birthday_count > 0;
        }
    }
    return false;
}

void morning_ui_check_schedule() {
    // Only dance mode now; blank/visible is decided in morning_ui_tick().
    splash_set_dance_mode(morning_ui_dance_day());
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
    last_activity_ms = millis();
    
    // Stop any scheduled animation in progress
    if (animation_playing) {
        morning_ui_stop_animation();
        // Show day view when woken during animation
        if (current_screen == MORNING_SCREEN_CLAWD) {
            morning_ui_show_screen(MORNING_SCREEN_KID1);
        }
    }
}

static lv_obj_t* container_for(MorningScreen s) {
    switch (s) {
        case MORNING_SCREEN_CLAWD:   return clawd_container;
        case MORNING_SCREEN_KID1:    return kid1_container;
        case MORNING_SCREEN_KID2:    return kid2_container;
        case MORNING_SCREEN_PARENTS: return parents_container;
        case MORNING_SCREEN_CLOCK:   return nullptr;   // no slides on the clock
    }
    return nullptr;
}

static void stop_slides() {
    lv_obj_t* all[] = {clawd_container, kid1_container, kid2_container, parents_container};
    for (lv_obj_t* c : all) {
        if (!c) continue;
        lv_anim_delete(c, anim_set_x);
        lv_anim_delete(c, anim_set_y);
        lv_obj_set_pos(c, 0, 0);
    }
    slide_old = nullptr;
    slide_day_dir = 0;
    anim_busy = false;
}

static void start_anim(lv_obj_t* obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to,
                       lv_anim_completed_cb_t done) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (done) lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

static void page_slide_done(lv_anim_t* a) {
    (void)a;
    if (slide_old) {
        lv_obj_add_flag(slide_old, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(slide_old, 0, 0);
        slide_old = nullptr;
    }
    anim_busy = false;
}

// Vertical page change: dir +1 = next page comes up from below.
static void slide_to_page(MorningScreen to, int dir) {
    lv_obj_t* from_c = container_for(current_screen);
    lv_obj_t* to_c = container_for(to);
    if (!from_c || !to_c || from_c == to_c || anim_busy) {
        morning_ui_show_screen(to);
        return;
    }
    anim_busy = true;
    slide_old = from_c;
    current_screen = to;
    last_activity_ms = millis();
    lv_obj_set_pos(to_c, 0, dir * 480);
    lv_obj_clear_flag(to_c, LV_OBJ_FLAG_HIDDEN);
    start_anim(from_c, anim_set_y, 0, -dir * 480, nullptr);
    start_anim(to_c, anim_set_y, dir * 480, 0, page_slide_done);
}

static void day_slide_in_done(lv_anim_t* a) { (void)a; anim_busy = false; }

static void day_slide_out_done(lv_anim_t* a) {
    lv_obj_t* c = (lv_obj_t*)a->var;
    int dir = slide_day_dir;
    slide_day_dir = 0;
    current_day_offset += dir;
    morning_ui_update_feed(current_feed);       // rebuilds page contents
    lv_obj_set_x(c, dir * 480);
    start_anim(c, anim_set_x, dir * 480, 0, day_slide_in_done);
}

// Horizontal day change: dir +1 = next day (content moves left).
static void slide_day(int dir) {
    lv_obj_t* c = container_for(current_screen);
    if (!c || anim_busy || current_screen == MORNING_SCREEN_CLAWD || current_screen == MORNING_SCREEN_CLOCK) {
        current_day_offset += dir;
        morning_ui_update_feed(current_feed);
        return;
    }
    anim_busy = true;
    slide_day_dir = dir;
    last_activity_ms = millis();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, anim_set_x);
    lv_anim_set_values(&a, 0, -dir * 480);
    lv_anim_set_duration(&a, SLIDE_MS / 2);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, day_slide_out_done);
    lv_anim_start(&a);
}

// Idle state: day 0, the pixel clock, splash stopped, backlight on.
// (morning_ui_set_blank(true) still exists but is not used when idle.)
static void go_idle() {
    current_day_offset = 0;
    morning_ui_update_feed(current_feed);          // rebuild pages for today
    splash_hide();                                 // no clip cycling behind the clock
    morning_ui_show_screen(MORNING_SCREEN_CLOCK);
    morning_ui_set_blank(false);                   // overlay hidden, 0x05 = 0
}

// "Done" (top-strip tap, button): back to the clock now instead of waiting 2 minutes.
void morning_ui_reset_to_clawd() {
    if (animation_playing) morning_ui_stop_animation();
    last_activity_ms = millis() - IDLE_TIMEOUT_MS - 1;
    screen_is_on = false;
    go_idle();
}

// Screen-off: a black, clickable overlay on the top layer plus the backlight
// off (CH32 PWM 0x05 = 255, inverted). Touch still works with the backlight
// off, so tapping the overlay wakes the board. Every blank/unblank (idle
// timeout, touch-to-wake, the :00/:30 sequence, BOOT button) goes through
// morning_ui_set_blank, the only place that switches the backlight.
static lv_obj_t* blank_overlay = nullptr;
static void blank_tap_cb(lv_event_t* e) {
    (void)e;
    morning_ui_wake();
    morning_ui_set_blank(false);  // backlight on, then hide the overlay
    if (current_screen == MORNING_SCREEN_CLAWD || current_screen == MORNING_SCREEN_CLOCK)
        morning_ui_show_screen(MORNING_SCREEN_KID1);
    screen_is_on = true;
}
void morning_ui_set_blank(bool blank) {
    if (!blank_overlay) {
        blank_overlay = lv_obj_create(lv_layer_top());
        lv_obj_set_size(blank_overlay, 480, 480);
        lv_obj_set_pos(blank_overlay, 0, 0);
        lv_obj_set_style_bg_color(blank_overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(blank_overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(blank_overlay, 0, 0);
        lv_obj_set_style_radius(blank_overlay, 0, 0);
        lv_obj_set_style_pad_all(blank_overlay, 0, 0);
        lv_obj_clear_flag(blank_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(blank_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(blank_overlay, blank_tap_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_flag(blank_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    bool hidden = lv_obj_has_flag(blank_overlay, LV_OBJ_FLAG_HIDDEN);
    if (blank && hidden) {
        lv_obj_clear_flag(blank_overlay, LV_OBJ_FLAG_HIDDEN);  // black overlay first
        io_expander_set_backlight(false);                      // then 0x05 = 255
    } else if (!blank && !hidden) {
        io_expander_set_backlight(true);                       // 0x05 = 0 first
        lv_obj_add_flag(blank_overlay, LV_OBJ_FLAG_HIDDEN);    // then hide overlay
    }
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
    
    // Weather - show icon + min/max temps for selected day
    char weather_str[48];
    float min_temp = (current_weather && current_weather->valid && current_day_offset < 7) ? 
                     current_weather->min_temp[current_day_offset] : 15.0f;
    int weathercode = (current_weather && current_weather->valid && current_day_offset < 7) ?
                      current_weather->weathercode[current_day_offset] : 1;
    snprintf(weather_str, sizeof(weather_str), "%.0f\xC2\xB0 / %.0f\xC2\xB0",
             min_temp, max_temp);
    lv_obj_t* weather_label = lv_label_create(top_bar);
    lv_label_set_text(weather_label, weather_str);
    lv_obj_set_style_text_font(weather_label, &morning_font_16, 0);
    lv_obj_align(weather_label, LV_ALIGN_RIGHT_MID, 0, 0);
    // 28px pixel-art weather icon left of the temps (fonts have no weather glyphs)
    lv_obj_t* w_img = lv_image_create(top_bar);
    lv_image_set_src(w_img, get_weather_icon(weathercode));
    lv_obj_update_layout(weather_label);
    lv_obj_align_to(w_img, weather_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    
    // Day dots (1 of 7) - fixed to fit all 7 dots properly
    lv_obj_t* dots = lv_obj_create(container);
    lv_obj_clear_flag(dots, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_size(dots, 130, 12);  // Wider to ensure all 7 dots fit
    lv_obj_align(dots, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(dots, COLOR_BG, 0);
    lv_obj_set_style_border_width(dots, 0, 0);
    lv_obj_set_style_pad_all(dots, 2, 0);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    for (int i = 0; i < 7; i++) {
        lv_obj_t* dot = lv_obj_create(dots);
        lv_obj_clear_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_set_style_margin_left(dot, 1, 0);
        lv_obj_set_style_margin_right(dot, 1, 0);
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
    
    // Birthdays (cake) above the items
    const DayData* bday_day = (current_feed && current_day_offset < current_feed->day_count)
                              ? &current_feed->days[current_day_offset] : nullptr;
    if (bday_day) y_pos = add_birthday_rows(container, bday_day, y_pos);

    // Items list
    if (kid->item_count == 0 && !(bday_day && bday_day->birthday_count > 0)) {
        lv_obj_t* empty = lv_label_create(container);
        lv_label_set_text(empty, "Nothing on today!");
        lv_obj_set_style_text_font(empty, &morning_font_28, 0);
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 0, y_pos);
    } else {
        for (int i = 0; i < kid->item_count && i < 8 && y_pos < 380; i++) {
            const Item* item = &kid->items[i];
            
            char text[MAX_TEXT_LEN + 20];
            
            if (item->type == ITEM_ACTION && item->deadline[0]) {
                // Parse deadline and show day
                struct tm tm_deadline = {0};
                sscanf(item->deadline, "%d-%d-%d", &tm_deadline.tm_year, &tm_deadline.tm_mon, &tm_deadline.tm_mday);
                tm_deadline.tm_year -= 1900;
                tm_deadline.tm_mon -= 1;
                mktime(&tm_deadline);
                snprintf(text, sizeof(text), "%s (%s %d)", item->text, get_day_name(tm_deadline.tm_wday), tm_deadline.tm_mday);
            } else {
                strlcpy(text, item->text, sizeof(text));
            }
            y_pos = add_item_row(container, get_item_icon(item), text, y_pos);
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
    
    // Day dots (1 of 7) - fixed to fit all 7 dots properly
    lv_obj_t* dots = lv_obj_create(parents_container);
    lv_obj_clear_flag(dots, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_size(dots, 130, 12);  // Wider to ensure all 7 dots fit
    lv_obj_align(dots, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(dots, COLOR_BG, 0);
    lv_obj_set_style_border_width(dots, 0, 0);
    lv_obj_set_style_pad_all(dots, 2, 0);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    for (int i = 0; i < 7; i++) {
        lv_obj_t* dot = lv_obj_create(dots);
        lv_obj_clear_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_set_style_margin_left(dot, 1, 0);
        lv_obj_set_style_margin_right(dot, 1, 0);
        if (i == current_day_offset) {
            lv_obj_set_style_bg_color(dot, COLOR_TEXT, 0);
        } else {
            lv_obj_set_style_bg_color(dot, COLOR_LIGHT_GRAY, 0);
        }
    }
    
    // Title
    lv_obj_t* title = lv_label_create(parents_container);
    lv_label_set_text(title, "Amy and Olli");
    lv_obj_set_style_text_font(title, &morning_font_48, 0);
    lv_obj_set_style_text_color(title, COLOR_PARENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 65);
    
    // Items
    int y_pos = 125;
    y_pos = add_birthday_rows(parents_container, day, y_pos);
    if (day->parents.item_count == 0 && day->birthday_count == 0) {
        lv_obj_t* empty = lv_label_create(parents_container);
        lv_label_set_text(empty, "Nothing scheduled");
        lv_obj_set_style_text_font(empty, &morning_font_28, 0);
        lv_obj_align(empty, LV_ALIGN_TOP_LEFT, 0, y_pos);
    } else {
        for (int i = 0; i < day->parents.item_count && i < 10 && y_pos < 440; i++) {
            const Item* item = &day->parents.items[i];
            char text[MAX_TEXT_LEN + 20];
            if (item->type == ITEM_ACTION && item->deadline[0]) {
                struct tm td = {0};
                sscanf(item->deadline, "%d-%d-%d", &td.tm_year, &td.tm_mon, &td.tm_mday);
                td.tm_year -= 1900; td.tm_mon -= 1; mktime(&td);
                snprintf(text, sizeof(text), "%s (%s %d)", item->text, get_day_name(td.tm_wday), td.tm_mday);
            } else {
                strlcpy(text, item->text, sizeof(text));
            }
            y_pos = add_item_row(parents_container, get_item_icon(item), text, y_pos);
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

// Case-insensitive whole-word-ish match: `w` must start at a word boundary.
static bool has_word(const char* text, const char* w) {
    size_t n = strlen(w);
    for (const char* p = text; *p; p++) {
        if ((p == text || !isalpha((unsigned char)p[-1])) && strncasecmp(p, w, n) == 0) return true;
    }
    return false;
}

// Per-activity icon from the item text, or nullptr if nothing matches.
static const lv_image_dsc_t* activity_icon_for_text(const char* t) {
    if (has_word(t, "birthday")) return &mb_ic_cake;
    if (has_word(t, "party")) return &mb_ic_balloon;
    if (has_word(t, "sail")) return &mb_ic_sailboat;
    if (has_word(t, "swim") || has_word(t, "surf")) return &mb_ic_wave;
    if (has_word(t, "trombone") || has_word(t, "violin") || has_word(t, "piano") ||
        has_word(t, "choir") || has_word(t, "music") || has_word(t, "ensemble") ||
        has_word(t, "band")) return &mb_ic_note;
    if (has_word(t, "art") || has_word(t, "paint") || has_word(t, "pottery") ||
        has_word(t, "craft")) return &mb_ic_brush;
    if (has_word(t, "dance") || has_word(t, "ballet")) return &mb_ic_shoe;
    if (has_word(t, "football") || has_word(t, "soccer") || has_word(t, "footy") ||
        has_word(t, "sport") || has_word(t, "netball")) return &mb_ic_ball;
    return nullptr;
}

static const lv_image_dsc_t* get_item_icon(const Item* item) {
    switch (item->type) {
        case ITEM_BRING:   return &mb_ic_bag;
        case ITEM_ACTION:  return &mb_ic_warning;
        case ITEM_UNIFORM: return &mb_ic_shirt;
        case ITEM_ACTIVITY: {
            const lv_image_dsc_t* a = activity_icon_for_text(item->text);
            return a ? a : &mb_ic_ball;
        }
        case ITEM_EVENT:
        case ITEM_OTHER:
        default: {
            const lv_image_dsc_t* a = activity_icon_for_text(item->text);
            return a ? a : &mb_ic_calendar;
        }
    }
}

// One item row: 24px icon + wrapped label. Returns the next y.
static int add_item_row(lv_obj_t* parent, const lv_image_dsc_t* icon, const char* text, int y) {
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, icon);
    lv_obj_align(img, LV_ALIGN_TOP_LEFT, 0, y + 1);
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &morning_font_20, 0);
    lv_obj_set_width(label, 388);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 32, y);
    lv_obj_update_layout(label);
    int h = lv_obj_get_height(label);
    return y + (h + 6 > 30 ? h + 6 : 30);
}

static int add_birthday_rows(lv_obj_t* parent, const DayData* day, int y) {
    for (int b = 0; b < day->birthday_count && b < MAX_BIRTHDAYS; b++) {
        char text[MAX_NAME_LEN + 16];
        snprintf(text, sizeof(text), "%s's birthday", day->birthdays[b]);
        y = add_item_row(parent, &mb_ic_cake, text, y);
    }
    return y;
}

static const lv_image_dsc_t* get_weather_icon(int weathercode) {
    // WMO weather interpretation codes (Open-Meteo "weathercode")
    if (weathercode == 0 || weathercode == 1) return &mb_wx_sun;      // clear / mainly clear
    if (weathercode == 2) return &mb_wx_partly;                        // partly cloudy
    if (weathercode == 45 || weathercode == 48) return &mb_wx_fog;     // fog
    if ((weathercode >= 51 && weathercode <= 67) ||
        (weathercode >= 80 && weathercode <= 82)) return &mb_wx_rain;  // drizzle / rain / showers
    if (weathercode >= 95) return &mb_wx_storm;                        // thunderstorm
    return &mb_wx_cloud;                                               // 3 overcast, snow, unknown
}

// Scheduled animation system: play 3 random clips at :00 and :30
static void start_animation_sequence() {
    int anim_count = splash_get_anim_count();
    if (anim_count < 3) return;
    
    // Pick 3 unique random clips
    bool used[20] = {false};  // Max 20 animations
    for (int i = 0; i < 3; i++) {
        int idx;
        int attempts = 0;
        do {
            idx = esp_random() % anim_count;
            attempts++;
        } while ((used[idx] || !splash_anim_in_random_pool(idx)) && attempts < 100);
        
        animation_sequence[i] = idx;
        used[idx] = true;
    }
    
    clips_played = 0;
    animation_playing = true;
    animation_start_ms = millis();
    
    // Switch to Clawd screen and play first clip
    morning_ui_show_screen(MORNING_SCREEN_CLAWD);
    morning_ui_set_blank(false);  // Make sure screen is visible
    
    // Start first clip
    splash_set_anim(animation_sequence[0]);
    
    Serial.printf("Animation: Starting sequence (3 clips)\n");
}

void morning_ui_check_animation_slot() {
    // Get current time
    struct tm timeinfo;
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Check if time is valid
    if (timeinfo.tm_year < (2024 - 1900)) {
        // Time not synced yet
        return;
    }
    
    // Calculate current slot: hour * 2 + (minute == 30 ? 1 : 0)
    // Only fire at exactly :00 or :30
    if (timeinfo.tm_min != 0 && timeinfo.tm_min != 30) {
        return;
    }
    
    int current_slot = timeinfo.tm_hour * 2 + (timeinfo.tm_min == 30 ? 1 : 0);
    
    // Skip if this slot already fired
    if (current_slot == last_fired_slot) {
        return;
    }
    
    // Skip if screen is in use (touched within last 2 minutes)
    if (millis() - last_activity_ms < IDLE_TIMEOUT_MS) {
        Serial.printf("Animation: Skipping slot %02d:%02d (screen in use)\n", 
                     timeinfo.tm_hour, timeinfo.tm_min);
        last_fired_slot = current_slot;
        return;
    }
    
    // Fire this slot
    last_fired_slot = current_slot;
    Serial.printf("Animation: Firing slot %02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min);
    start_animation_sequence();
}

void morning_ui_stop_animation() {
    if (animation_playing) {
        animation_playing = false;
        clips_played = 0;
        Serial.println("Animation: Stopped by user");
    }
}

// ---- Idle pixel clock ------------------------------------------------------
// 5x7 block font (glyphs from Oliver's approved mockup, tools: mockups/clock.py)
// drawn as separate square "pixels" in Clawd orange on black. Drawn by one
// lv_obj's draw callback (no per-block objects), refreshed once a minute.
#define CLOCK_ORANGE lv_color_hex(0xD97757)
#define CLOCK_GREY   lv_color_hex(0xB0AEA5)
#define CLOCK_GAP    3
struct ClockGlyph { char c; uint8_t w; const char* rows[7]; };
static const ClockGlyph CLOCK_FONT[] = {
    {'0', 5, {"01110","10001","10011","10101","11001","10001","01110"}},
    {'1', 5, {"00100","01100","00100","00100","00100","00100","01110"}},
    {'2', 5, {"01110","10001","00001","00010","00100","01000","11111"}},
    {'3', 5, {"11111","00010","00100","00010","00001","10001","01110"}},
    {'4', 5, {"00010","00110","01010","10010","11111","00010","00010"}},
    {'5', 5, {"11111","10000","11110","00001","00001","10001","01110"}},
    {'6', 5, {"00110","01000","10000","11110","10001","10001","01110"}},
    {'7', 5, {"11111","00001","00010","00100","01000","01000","01000"}},
    {'8', 5, {"01110","10001","10001","01110","10001","10001","01110"}},
    {'9', 5, {"01110","10001","10001","01111","00001","00010","01100"}},
    {':', 1, {"0","1","1","0","1","1","0"}},
    {'-', 5, {"00000","00000","00000","11111","00000","00000","00000"}},
};
static const ClockGlyph* clock_glyph(char c) {
    for (size_t i = 0; i < sizeof(CLOCK_FONT) / sizeof(CLOCK_FONT[0]); i++)
        if (CLOCK_FONT[i].c == c) return &CLOCK_FONT[i];
    return nullptr;
}

// Block size: 21 px (as the mockup) for "7:42"; "10:42"-"12:59" would be
// 25 columns x 21 = 525 px (wider than the panel), so 5-char times use 18 px.
static void clock_draw_cb(lv_event_t* e) {
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
    lv_area_t co; lv_obj_get_coords(obj, &co);
    int n = (int)strlen(clock_text);
    int cols = 0;
    for (int i = 0; i < n; i++) { const ClockGlyph* g = clock_glyph(clock_text[i]); if (g) cols += g->w + 1; }
    if (cols == 0) return;
    cols -= 1;
    int b = (cols * 21 <= 470) ? 21 : 18;
    int x = co.x1 + (480 - cols * b) / 2;
    int y = co.y1 + (lv_area_get_height(&co) - 7 * b) / 2;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = CLOCK_ORANGE; d.bg_opa = LV_OPA_COVER; d.radius = 0; d.border_width = 0;
    for (int i = 0; i < n; i++) {
        const ClockGlyph* g = clock_glyph(clock_text[i]);
        if (!g) continue;
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < g->w; k++)
                if (g->rows[r][k] == '1') {
                    lv_area_t a;
                    a.x1 = x + k * b; a.y1 = y + r * b;
                    a.x2 = a.x1 + b - CLOCK_GAP - 1; a.y2 = a.y1 + b - CLOCK_GAP - 1;
                    lv_draw_rect(layer, &d, &a);
                }
        x += (g->w + 1) * b;
    }
}

static void clock_tap_cb(lv_event_t* e) {
    (void)e;
    if (current_screen != MORNING_SCREEN_CLOCK) return;
    Serial.println("UI: clock tapped -> kid page");
    morning_ui_wake();
    morning_ui_show_screen(MORNING_SCREEN_KID1);
}

static void create_clock_screen() {
    clock_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(clock_container, 480, 480);
    lv_obj_set_pos(clock_container, 0, 0);
    lv_obj_set_style_bg_color(clock_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(clock_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(clock_container, 0, 0);
    lv_obj_set_style_pad_all(clock_container, 0, 0);
    lv_obj_set_style_radius(clock_container, 0, 0);
    lv_obj_clear_flag(clock_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(clock_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(clock_container, clock_tap_cb, LV_EVENT_CLICKED, NULL);

    // Digits band: 7 rows x 21 px = 147 px, top at y=120 (as the mockup)
    clock_digits = lv_obj_create(clock_container);
    lv_obj_remove_style_all(clock_digits);
    lv_obj_set_size(clock_digits, 480, 147);
    lv_obj_set_pos(clock_digits, 0, 120);
    lv_obj_clear_flag(clock_digits, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(clock_digits, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(clock_digits, clock_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    clock_info = lv_label_create(clock_container);
    lv_obj_set_style_text_font(clock_info, &morning_font_28, 0);
    lv_obj_set_style_text_color(clock_info, CLOCK_GREY, 0);
    lv_obj_clear_flag(clock_info, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(clock_info, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_label_set_text(clock_info, "");
    lv_obj_align(clock_info, LV_ALIGN_TOP_MID, 0, 296);

    lv_obj_add_flag(clock_container, LV_OBJ_FLAG_HIDDEN);
}

static void update_clock(bool force) {
    if (!clock_container) return;
    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    bool valid = now > 1700000000;
    int min_key = valid ? t.tm_hour * 60 + t.tm_min : -2;
    if (!force && min_key == clock_last_min) return;
    clock_last_min = min_key;

    if (valid) {
        int h = t.tm_hour % 12; if (h == 0) h = 12;      // 12-hour, no am/pm
        snprintf(clock_text, sizeof(clock_text), "%d:%02d", h, t.tm_min);
        // Atkinson has no middle dot (U+00B7), so two spaces separate the parts
        char info[48];
        int n = snprintf(info, sizeof(info), "%s %d %s", get_day_name(t.tm_wday), t.tm_mday,
                         get_month_name(t.tm_mon));
        if (current_weather && current_weather->valid && n > 0 && n < (int)sizeof(info))
            snprintf(info + n, sizeof(info) - n, "  %d\xC2\xB0", (int)lroundf(current_weather->max_temp[0]));
        lv_label_set_text(clock_info, info);
    } else {
        strlcpy(clock_text, "--:--", sizeof(clock_text));
        lv_label_set_text(clock_info, "");
    }
    lv_obj_align(clock_info, LV_ALIGN_TOP_MID, 0, 296);
    if (clock_digits) lv_obj_invalidate(clock_digits);
}

#endif  // BOARD_MORNING_BOARD
