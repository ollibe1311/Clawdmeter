#pragma once
#include "morning_data.h"
#include <lvgl.h>

enum MorningScreen {
    MORNING_SCREEN_CLAWD,
    MORNING_SCREEN_KID1,
    MORNING_SCREEN_KID2,
    MORNING_SCREEN_PARENTS
};

// Initialize UI system
void morning_ui_init();

// Update with new feed/weather data
void morning_ui_update_feed(const FeedData* feed);
void morning_ui_update_weather(const WeatherData* weather);

// Show a specific screen
void morning_ui_show_screen(MorningScreen screen);

// Get current screen
MorningScreen morning_ui_get_current_screen();

// Call from main loop
void morning_ui_tick();

// Handle screen schedule (auto on/off)
void morning_ui_check_schedule();

// Check if screen should be on right now
bool morning_ui_should_be_on();

// Wake screen (from touch or button)
void morning_ui_wake();

// Return to Clawd after timeout
void morning_ui_reset_to_clawd();
