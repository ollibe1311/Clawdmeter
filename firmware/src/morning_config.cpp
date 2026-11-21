#include "morning_config.h"
#include <Preferences.h>

static Preferences prefs;
static DressRules dress_rules;
static ScreenSchedule schedule;
static float weather_lat = -33.87;
static float weather_lon = 151.21;

// Default dress rules
static const DressRules DEFAULT_DRESS = {
    .shorts_threshold = 22.0f,
    .pants_threshold = 16.0f,
    .rain_threshold = 40
};

// Default schedule: 06:00-09:00, 15:00-20:30
static const ScreenSchedule DEFAULT_SCHEDULE = {
    .morning_start_hour = 6,
    .morning_start_min = 0,
    .morning_end_hour = 9,
    .morning_end_min = 0,
    .afternoon_start_hour = 15,
    .afternoon_start_min = 0,
    .afternoon_end_hour = 20,
    .afternoon_end_min = 30
};

void morning_config_init() {
    prefs.begin("morning", false);
    
    // Load dress rules with defaults
    dress_rules.shorts_threshold = prefs.getFloat("dress_shorts", DEFAULT_DRESS.shorts_threshold);
    dress_rules.pants_threshold = prefs.getFloat("dress_pants", DEFAULT_DRESS.pants_threshold);
    dress_rules.rain_threshold = prefs.getInt("dress_rain", DEFAULT_DRESS.rain_threshold);
    
    // Load schedule with defaults
    schedule.morning_start_hour = prefs.getInt("sched_msh", DEFAULT_SCHEDULE.morning_start_hour);
    schedule.morning_start_min = prefs.getInt("sched_msm", DEFAULT_SCHEDULE.morning_start_min);
    schedule.morning_end_hour = prefs.getInt("sched_meh", DEFAULT_SCHEDULE.morning_end_hour);
    schedule.morning_end_min = prefs.getInt("sched_mem", DEFAULT_SCHEDULE.morning_end_min);
    schedule.afternoon_start_hour = prefs.getInt("sched_ash", DEFAULT_SCHEDULE.afternoon_start_hour);
    schedule.afternoon_start_min = prefs.getInt("sched_asm", DEFAULT_SCHEDULE.afternoon_start_min);
    schedule.afternoon_end_hour = prefs.getInt("sched_aeh", DEFAULT_SCHEDULE.afternoon_end_hour);
    schedule.afternoon_end_min = prefs.getInt("sched_aem", DEFAULT_SCHEDULE.afternoon_end_min);
    
    // Load weather location
    weather_lat = prefs.getFloat("weather_lat", -33.87);
    weather_lon = prefs.getFloat("weather_lon", 151.21);
}

bool morning_config_get_wifi_ssid(char* buf, size_t len) {
    return prefs.getString("wifi_ssid", buf, len) > 0;
}

bool morning_config_get_wifi_pass(char* buf, size_t len) {
    return prefs.getString("wifi_pass", buf, len) > 0;
}

void morning_config_set_wifi_ssid(const char* ssid) {
    prefs.putString("wifi_ssid", ssid);
}

void morning_config_set_wifi_pass(const char* pass) {
    prefs.putString("wifi_pass", pass);
}

bool morning_config_get_feed_url(char* buf, size_t len) {
    return prefs.getString("feed_url", buf, len) > 0;
}

void morning_config_set_feed_url(const char* url) {
    prefs.putString("feed_url", url);
}

DressRules morning_config_get_dress_rules() {
    return dress_rules;
}

void morning_config_set_dress_rules(const DressRules& rules) {
    dress_rules = rules;
    prefs.putFloat("dress_shorts", rules.shorts_threshold);
    prefs.putFloat("dress_pants", rules.pants_threshold);
    prefs.putInt("dress_rain", rules.rain_threshold);
}

ScreenSchedule morning_config_get_schedule() {
    return schedule;
}

void morning_config_set_schedule(const ScreenSchedule& sched) {
    schedule = sched;
    prefs.putInt("sched_msh", sched.morning_start_hour);
    prefs.putInt("sched_msm", sched.morning_start_min);
    prefs.putInt("sched_meh", sched.morning_end_hour);
    prefs.putInt("sched_mem", sched.morning_end_min);
    prefs.putInt("sched_ash", sched.afternoon_start_hour);
    prefs.putInt("sched_asm", sched.afternoon_start_min);
    prefs.putInt("sched_aeh", sched.afternoon_end_hour);
    prefs.putInt("sched_aem", sched.afternoon_end_min);
}

void morning_config_get_weather_location(float* lat, float* lon) {
    *lat = weather_lat;
    *lon = weather_lon;
}

void morning_config_set_weather_location(float lat, float lon) {
    weather_lat = lat;
    weather_lon = lon;
    prefs.putFloat("weather_lat", lat);
    prefs.putFloat("weather_lon", lon);
}

void morning_config_clear() {
    prefs.clear();
}
