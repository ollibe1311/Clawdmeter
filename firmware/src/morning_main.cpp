#ifdef BOARD_MORNING_BOARD
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <lvgl.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "morning_config.h"
#include "morning_net.h"
#include "morning_ui.h"
#include "morning_data.h"
#include "morning_fonts.h"

#include "hal/board_caps.h"
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include "hal/input_hal.h"
#include "hal/power_hal.h"
#include "boards/waveshare_lcd_4/io_expander.h"  // For backlight control

// LVGL setup
#define BUF_LINES 40
static uint16_t* buf1 = nullptr;
static uint16_t* buf2 = nullptr;

// OTA update state (must be before setup() for lambda capture)
static bool ota_in_progress = false;
static lv_obj_t* ota_label = nullptr;
static char ota_password[33] = {0};  // Never logged

static uint32_t my_tick(void) { return millis(); }

static void my_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    display_hal_draw_bitmap(area->x1, area->y1, w, h, (uint16_t*)px_map);
    lv_display_flush_ready(disp);
}

static void rounder_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    display_hal_round_area(&area->x1, &area->y1, &area->x2, &area->y2);
}

static void my_touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    uint16_t x, y;
    bool pressed;
    touch_hal_read(&x, &y, &pressed);
    
    if (pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        morning_ui_wake();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Feed update callback
static void on_feed_update(const FeedData* data, bool success) {
    if (success) {
        Serial.println("Feed updated successfully");
        morning_ui_update_feed(data);
    } else {
        Serial.println("Feed update failed");
    }
}

// Weather update callback
static void on_weather_update(const WeatherData* data, bool success) {
    if (success) {
        Serial.println("Weather updated successfully");
        morning_ui_update_weather(data);
    } else {
        Serial.println("Weather update failed");
    }
}

extern "C" void board_init(void);

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== Morning Board ===");
    
    // Initialize board hardware
    board_init();
    
    // Initialize display and touch
    display_hal_init();
    display_hal_begin();
    display_hal_set_brightness(255);  // Full brightness
    
    touch_hal_init();
    input_hal_init();
    power_hal_init();
    
    // Initialize LVGL
    const int W = board_caps().width;
    const int H = board_caps().height;
    
    lv_init();
    lv_tick_set_cb(my_tick);
    
    buf1 = (uint16_t*)heap_caps_malloc(W * BUF_LINES * 2, MALLOC_CAP_SPIRAM);
    buf2 = (uint16_t*)heap_caps_malloc(W * BUF_LINES * 2, MALLOC_CAP_SPIRAM);
    
    lv_display_t* disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, W * BUF_LINES * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touch_cb);
    
    // Initialize config and networking
    morning_config_init();
    morning_net_init();
    
    // Initialize UI
    morning_ui_init();
    
    // Show provisioning status only if WiFi not configured
    if (!morning_net_is_wifi_configured()) {
        Serial.println("\n*** PROVISIONING MODE ***");
        Serial.println("WiFi not configured. Send these commands over serial (115200 baud):");
        Serial.println("  SET WIFI_SSID=<your wifi name>");
        Serial.println("  SET WIFI_PASS=<your wifi password>");
        Serial.println("  SET FEED_URL=<your feed JSON URL>");
        Serial.println("  SHOW");
        Serial.println("  REBOOT");
        Serial.println("Provisioning window: first 5 seconds after boot.");
        Serial.println("***\n");
    } else if (morning_net_in_provision_window()) {
        Serial.println("Provisioning window active (5 seconds). Send STATUS to check config.");
    }
    
    // Initial fetch if connected
    if (morning_net_is_connected()) {
        morning_net_fetch_feed(on_feed_update);
        morning_net_fetch_weather(on_weather_update);
    } else {
        // Register the UI callbacks now (these return immediately when offline)
        // so fetches after a later reconnect still refresh the screen.
        morning_net_fetch_feed(on_feed_update);
        morning_net_fetch_weather(on_weather_update);
        // Load cached data
        morning_ui_update_feed(morning_net_get_cached_feed());
        morning_ui_update_weather(morning_net_get_cached_weather());
    }
    
    // Setup ArduinoOTA with password from NVS
    if (!morning_config_has_ota_password()) {
        // Generate random password on first boot
        const char* chars = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789";
        for (int i = 0; i < 16; i++) {
            ota_password[i] = chars[esp_random() % strlen(chars)];
        }
        ota_password[16] = '\0';
        morning_config_set_ota_password(ota_password);
        Serial.println("Generated new OTA password (use 'OTA' command to view)");
    } else {
        char buf[64];
        morning_config_get_ota_password(buf, sizeof(buf));
        strlcpy(ota_password, buf, sizeof(ota_password));
    }
    
    ArduinoOTA.setHostname("morningboard");
    ArduinoOTA.setPassword(ota_password);
    
    ArduinoOTA.onStart([]() {
        ota_in_progress = true;
        Serial.println("OTA: Update starting");
        
        // Show updating screen
        lv_obj_t* screen = lv_scr_act();
        lv_obj_clean(screen);
        
        ota_label = lv_label_create(screen);
        lv_label_set_text(ota_label, "Updating...\n0%");
        lv_obj_set_style_text_font(ota_label, &morning_font_28, 0);
        lv_obj_center(ota_label);
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int percent = (progress * 100) / total;
        if (ota_label && percent % 10 == 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Updating...\n%d%%", percent);
            lv_label_set_text(ota_label, buf);
            lv_timer_handler();
        }
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("OTA: Update complete");
        if (ota_label) {
            lv_label_set_text(ota_label, "Update complete!\nRestarting...");
            lv_timer_handler();
        }
        delay(1000);
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA: Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
        
        ota_in_progress = false;
        ESP.restart();
    });
    
    ArduinoOTA.begin();
    
    Serial.printf("Morning Board ready (%dx%d)\n", W, H);
    Serial.printf("Web settings: http://morningboard.local or http://%s\n", 
                  WiFi.localIP().toString().c_str());
    Serial.println("OTA ready on morningboard.local (use 'OTA' command for password)");
}

static uint32_t last_schedule_check = 0;
static uint32_t last_fetch_check = 0;
static uint32_t last_animation_check = 0;

// OTA info, printed for the "OTA" serial command. Called from
// morning_net_process_serial(): that function drains every byte on the USB
// serial port, so a separate reader here never saw the command.
void morning_print_ota_info() {
    Serial.println("\n=== OTA Configuration ===");
    Serial.printf("Hostname: morningboard.local\n");
    Serial.printf("Password: %s\n", ota_password);
    Serial.println("\nUpload command:");
    Serial.println("  export OTA_PASSWORD='<password>'");
    Serial.println("  pio run -e morning_board -t upload --upload-port morningboard.local");
    Serial.println("========================\n");
}

void loop() {
    // Skip normal operations during OTA update
    if (ota_in_progress) {
        ArduinoOTA.handle();
        delay(10);
        return;
    }
    
    ArduinoOTA.handle();
    lv_timer_handler();
    morning_net_tick();
    morning_ui_tick();
    power_hal_tick();
    display_hal_tick();
    
    // Process serial provisioning commands. Always accepted over USB serial:
    // gating on the provision window closed it as soon as WIFI_SSID was set,
    // so WIFI_PASS / FEED_URL / REBOOT that followed were silently dropped.
    morning_net_process_serial();   // also handles "OTA"
    
    // Check scheduled animations every 30 seconds
    if (millis() - last_animation_check > 30000) {
        last_animation_check = millis();
        morning_ui_check_animation_slot();
    }
    
    // Check screen schedule every 10 seconds and control backlight
    if (millis() - last_schedule_check > 10000) {
        last_schedule_check = millis();
        morning_ui_check_schedule();
        
        // Blank/visible is decided in morning_ui_tick() (single source of truth).
    }
    
    // Periodic data refresh check (networking module handles timing)
    if (millis() - last_fetch_check > 60000) {  // Check every minute
        last_fetch_check = millis();
        if (morning_net_is_connected() && morning_ui_should_be_on()) {
            // Fetches will happen automatically via morning_net_tick()
        }
    }
    
    // Handle button input
    static bool btn_was = false;
    bool btn_now = input_hal_is_held(INPUT_BTN_PRIMARY);
    if (btn_now && !btn_was) {
        // Tap to wake or cycle screens
        // Button: wake to the day view when idle/Clawd, otherwise go blank
        if (!morning_ui_is_screen_on() || morning_ui_get_current_screen() == MORNING_SCREEN_CLAWD) {
            morning_ui_wake();
            morning_ui_show_screen(MORNING_SCREEN_KID1);
        } else {
            morning_ui_reset_to_clawd();
        }
    }
    btn_was = btn_now;
    
    delay(5);
}
#endif  // BOARD_MORNING_BOARD
