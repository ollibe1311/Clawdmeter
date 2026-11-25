#pragma once
#include "morning_data.h"

// Initialize networking (WiFi, NTP, mDNS, web server)
void morning_net_init();

// Call periodically from main loop
void morning_net_tick();

// WiFi connection status
bool morning_net_is_connected();

// Get current time (Australia/Sydney)
bool morning_net_get_time(struct tm* timeinfo);

// Fetch feed data (non-blocking, returns immediately, result via callback)
void morning_net_fetch_feed(void (*callback)(const FeedData* data, bool success));

// Fetch weather data (non-blocking, returns immediately, result via callback)
void morning_net_fetch_weather(void (*callback)(const WeatherData* data, bool success));

// Get last cached feed/weather data
const FeedData* morning_net_get_cached_feed();
const WeatherData* morning_net_get_cached_weather();

// Check if we're in a provisioning window (first 5s after boot or config missing)
bool morning_net_in_provision_window();

// Check if WiFi is configured (credentials exist)
bool morning_net_is_wifi_configured();

// Process serial provisioning commands
void morning_net_process_serial();
