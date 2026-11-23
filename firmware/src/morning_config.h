#pragma once
#include "morning_data.h"

// Initialize config system (loads from NVS)
void morning_config_init();

// WiFi credentials
bool morning_config_get_wifi_ssid(char* buf, size_t len);
bool morning_config_get_wifi_pass(char* buf, size_t len);
void morning_config_set_wifi_ssid(const char* ssid);
void morning_config_set_wifi_pass(const char* pass);

// Feed URL
bool morning_config_get_feed_url(char* buf, size_t len);
void morning_config_set_feed_url(const char* url);

// Dress thresholds (with defaults)
DressRules morning_config_get_dress_rules();
void morning_config_set_dress_rules(const DressRules& rules);

// Screen schedule (with defaults)
ScreenSchedule morning_config_get_schedule();
void morning_config_set_schedule(const ScreenSchedule& sched);

// Weather location
void morning_config_get_weather_location(float* lat, float* lon);
void morning_config_set_weather_location(float lat, float lon);

// OTA password (generated randomly on first boot if not set)
bool morning_config_get_ota_password(char* buf, size_t len);
void morning_config_set_ota_password(const char* password);
bool morning_config_has_ota_password();

// Clear all config (factory reset)
void morning_config_clear();
