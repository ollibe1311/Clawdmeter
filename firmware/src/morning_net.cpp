#ifdef BOARD_MORNING_BOARD
#include "morning_net.h"
#include "morning_config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "boards/waveshare_lcd_4/io_expander.h"

static const char* TZ_STRING = "AEST-10AEDT,M10.1.0,M4.1.0/3";
static const char* NTP_SERVER = "pool.ntp.org";
static const uint32_t PROVISION_WINDOW_MS = 5000;
static const uint32_t FEED_INTERVAL_MS = 15 * 60 * 1000;  // 15 minutes
static const uint32_t WEATHER_INTERVAL_MS = 15 * 60 * 1000;  // 15 minutes (match feed)
static const uint32_t WIFI_RECONNECT_INTERVALS[] = {5000, 10000, 20000, 40000, 60000};  // backoff
static const int WIFI_RECONNECT_INTERVALS_COUNT = 5;

void morning_print_ota_info();   // defined in morning_main.cpp
static WebServer server(80);
static uint32_t boot_time_ms = 0;
static bool wifi_connected = false;
static bool time_synced = false;
static bool wifi_configured = false;
static uint32_t last_wifi_attempt = 0;
static int wifi_reconnect_index = 0;

// FeedData is ~100 KB. As a static it sat in internal RAM and starved mbedTLS
// ("SSL - Memory allocation failed" on every HTTPS fetch). Keep it in PSRAM.
static FeedData* cached_feed_p = nullptr;
static int      last_feed_code = 0;        // HTTP code (or -N client error) of the last feed fetch
static uint32_t last_feed_ok_ms = 0;       // millis() of the last successful feed fetch
#define cached_feed (*cached_feed_p)
static WeatherData cached_weather = {};
static uint32_t last_feed_fetch = 0;
static bool force_fetch = false;   // fetch right after a (re)connect
static uint32_t last_weather_fetch = 0;

static void (*feed_callback)(const FeedData*, bool) = nullptr;
static void (*weather_callback)(const WeatherData*, bool) = nullptr;

// Forward declarations
static void connect_wifi();
static void setup_mdns();
static void setup_webserver();
static bool fetch_feed_sync(FeedData* data);
static bool fetch_weather_sync(WeatherData* data);
static bool load_cached_feed();
static bool save_cached_feed();

void morning_net_init() {
    boot_time_ms = millis();
    if (!cached_feed_p) {
        cached_feed_p = (FeedData*)heap_caps_calloc(1, sizeof(FeedData), MALLOC_CAP_SPIRAM);
        if (!cached_feed_p) cached_feed_p = (FeedData*)calloc(1, sizeof(FeedData));
    }
    
    // Initialize LittleFS for caching
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
    }
    
    // Try to load cached feed
    load_cached_feed();
    
    // Connect to WiFi
    connect_wifi();
    
    // Set up time
    if (wifi_connected) {
        configTzTime(TZ_STRING, NTP_SERVER);
        Serial.println("NTP time sync started");
        
        // Set up mDNS
        setup_mdns();
        
        // Set up web server
        setup_webserver();
        server.begin();
        Serial.println("Web server started on port 80");
    }
}

static void connect_wifi() {
    char ssid[64], pass[64];
    if (!morning_config_get_wifi_ssid(ssid, sizeof(ssid)) ||
        !morning_config_get_wifi_pass(pass, sizeof(pass))) {
        Serial.println("WiFi credentials not configured");
        wifi_configured = false;
        return;
    }
    
    wifi_configured = true;
    Serial.printf("Connecting to WiFi: %s\n", ssid);
    WiFi.mode(WIFI_STA);
    // No modem sleep (WIFI_PS_NONE): with power save the radio wakes in bursts
    // for beacons, and Wi-Fi/lwIP buffers live in PSRAM in this build, which
    // competes with the RGB panel's bounce-buffer refills (left-edge slivers).
    WiFi.setSleep(false);
    WiFi.begin(ssid, pass);
    
    // Wait up to 10 seconds
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
        wifi_reconnect_index = 0;
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("WiFi connection failed - will retry with backoff");
        last_wifi_attempt = millis();
    }
}

static void setup_mdns() {
    if (MDNS.begin("morningboard")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS responder started: morningboard.local");
    } else {
        Serial.println("mDNS setup failed");
    }
}

static void setup_webserver() {
    // Serve settings form
    server.on("/", []() {
        DressRules rules = morning_config_get_dress_rules();
        ScreenSchedule sched = morning_config_get_schedule();
        
        String html = "<!DOCTYPE html><html><head><title>Morning Board Settings</title>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>body{font-family:Arial;max-width:600px;margin:20px auto;padding:0 20px}";
        html += "input,button{font-size:16px;padding:8px;margin:5px 0;width:100%;box-sizing:border-box}";
        html += "button{background:#4CAF50;color:white;border:none;cursor:pointer}";
        html += "button:hover{background:#45a049}h2{border-bottom:2px solid #ddd;padding-bottom:10px}</style></head><body>";
        html += "<h1>Morning Board Settings</h1>";
        
        html += "<h2>Dress Rules</h2><form action='/settings' method='POST'>";
        html += "<label>Shorts/Skort Temperature (&deg;C):</label>";
        html += "<input type='number' step='0.1' name='shorts' value='" + String(rules.shorts_threshold, 1) + "'>";
        html += "<label>Pants/Long Sleeves Temperature (&deg;C):</label>";
        html += "<input type='number' step='0.1' name='pants' value='" + String(rules.pants_threshold, 1) + "'>";
        html += "<label>Rain Jacket Probability (%):</label>";
        html += "<input type='number' name='rain' value='" + String(rules.rain_threshold) + "'>";
        
        html += "<h2>Screen Schedule</h2>";
        html += "<label>Morning Start:</label>";
        html += "<input type='time' name='morning_start' value='";
        html += String(sched.morning_start_hour < 10 ? "0" : "") + String(sched.morning_start_hour) + ":";
        html += String(sched.morning_start_min < 10 ? "0" : "") + String(sched.morning_start_min) + "'>";
        html += "<label>Morning End:</label>";
        html += "<input type='time' name='morning_end' value='";
        html += String(sched.morning_end_hour < 10 ? "0" : "") + String(sched.morning_end_hour) + ":";
        html += String(sched.morning_end_min < 10 ? "0" : "") + String(sched.morning_end_min) + "'>";
        html += "<label>Afternoon Start:</label>";
        html += "<input type='time' name='afternoon_start' value='";
        html += String(sched.afternoon_start_hour < 10 ? "0" : "") + String(sched.afternoon_start_hour) + ":";
        html += String(sched.afternoon_start_min < 10 ? "0" : "") + String(sched.afternoon_start_min) + "'>";
        html += "<label>Afternoon End:</label>";
        html += "<input type='time' name='afternoon_end' value='";
        html += String(sched.afternoon_end_hour < 10 ? "0" : "") + String(sched.afternoon_end_hour) + ":";
        html += String(sched.afternoon_end_min < 10 ? "0" : "") + String(sched.afternoon_end_min) + "'>";
        
        html += "<button type='submit'>Save Settings</button></form>";
        html += "<p style='margin-top:40px;color:#666;font-size:14px'>Morning Board &middot; ";
        html += "IP: " + WiFi.localIP().toString() + "</p></body></html>";
        
        server.send(200, "text/html", html);
    });
    
    // Handle settings update
    // Backlight polarity test hook: /bl?v=N writes N (0-255) to CH32 reg 0x05;
    // /bl with no args reads the register back and reports the boot value.
    server.on("/bl", []() {
        char msg[320];
        if (server.hasArg("v")) {
            int v = server.arg("v").toInt();
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            bool ok = io_expander_raw_pwm((uint8_t)v);
            snprintf(msg, sizeof(msg), "BL wrote %d (%s); readback=%d; boot=%d\n",
                     v, ok ? "ack" : "NACK/n.a.", io_expander_read_pwm(), io_expander_boot_pwm());
        } else {
            snprintf(msg, sizeof(msg),
                     "BL readback=%d; boot=%d; int_free=%u; int_largest=%u; feed_code=%d; "
                     "feed_ok_age_s=%ld; feed_first=%s; feed_generated=%s\n",
                     io_expander_read_pwm(), io_expander_boot_pwm(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                     last_feed_code,
                     last_feed_ok_ms ? (long)((millis() - last_feed_ok_ms) / 1000) : -1L,
                     cached_feed.day_count > 0 ? cached_feed.days[0].date : "-",
                     cached_feed.generated);
        }
        Serial.print(msg);
        server.send(200, "text/plain", msg);
    });

    server.on("/settings", HTTP_POST, []() {
        DressRules rules;
        rules.shorts_threshold = server.arg("shorts").toFloat();
        rules.pants_threshold = server.arg("pants").toFloat();
        rules.rain_threshold = server.arg("rain").toInt();
        morning_config_set_dress_rules(rules);
        
        ScreenSchedule sched;
        sscanf(server.arg("morning_start").c_str(), "%d:%d", 
               &sched.morning_start_hour, &sched.morning_start_min);
        sscanf(server.arg("morning_end").c_str(), "%d:%d", 
               &sched.morning_end_hour, &sched.morning_end_min);
        sscanf(server.arg("afternoon_start").c_str(), "%d:%d", 
               &sched.afternoon_start_hour, &sched.afternoon_start_min);
        sscanf(server.arg("afternoon_end").c_str(), "%d:%d", 
               &sched.afternoon_end_hour, &sched.afternoon_end_min);
        morning_config_set_schedule(sched);
        
        String html = "<!DOCTYPE html><html><head><title>Settings Saved</title>";
        html += "<meta http-equiv='refresh' content='2;url=/'></head><body>";
        html += "<h2>Settings saved! Redirecting...</h2></body></html>";
        server.send(200, "text/html", html);
    });
}

void morning_net_tick() {
    // WiFi reconnect with exponential backoff
    if (!wifi_connected && wifi_configured) {
        uint32_t interval = WIFI_RECONNECT_INTERVALS[wifi_reconnect_index];
        if (millis() - last_wifi_attempt >= interval) {
            last_wifi_attempt = millis();
            Serial.printf("WiFi reconnect attempt (backoff %lums)\n", interval);
            
            char ssid[64], pass[64];
            if (morning_config_get_wifi_ssid(ssid, sizeof(ssid)) &&
                morning_config_get_wifi_pass(pass, sizeof(pass))) {
                WiFi.setSleep(false);
                WiFi.begin(ssid, pass);
                
                // Quick check (don't block)
                for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
                    delay(100);
                }
                
                if (WiFi.status() == WL_CONNECTED) {
                    wifi_connected = true;
                    wifi_reconnect_index = 0;
                    Serial.printf("WiFi reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
                    force_fetch = true;  // don't wait 15/60 min for first data
                    
                    // Re-setup services
                    if (!time_synced) configTzTime(TZ_STRING, NTP_SERVER);
                    setup_mdns();
                    if (!server.uri()) {
                        setup_webserver();
                        server.begin();
                    }
                } else {
                    wifi_reconnect_index = min(wifi_reconnect_index + 1, WIFI_RECONNECT_INTERVALS_COUNT - 1);
                }
            }
        }
    } else if (wifi_connected && WiFi.status() != WL_CONNECTED) {
        // Detected disconnect
        wifi_connected = false;
        last_wifi_attempt = millis();
        wifi_reconnect_index = 0;
        Serial.println("WiFi disconnected");
    }
    
    if (wifi_connected) {
        server.handleClient();
        
        // Trad pushes the feed around 06:15: fetch again at 06:20 and 06:30
        // local time (once each per day) on top of the 15-minute refresh.
        {
            static int done_key = -1;
            time_t now = time(nullptr);
            if (now > 1700000000) {
                struct tm t; localtime_r(&now, &t);
                int key = t.tm_yday * 2 + (t.tm_min >= 30 ? 1 : 0);
                if (t.tm_hour == 6 && (t.tm_min == 20 || t.tm_min == 30) && done_key != key) {
                    done_key = key;
                    Serial.printf("Feed: scheduled %02d:%02d refetch\n", t.tm_hour, t.tm_min);
                    morning_net_fetch_feed(nullptr);
                }
            }
        }
        // Periodic feed fetch (every 15 min when connected)
        if (force_fetch || millis() - last_feed_fetch > FEED_INTERVAL_MS) {
            morning_net_fetch_feed(nullptr);
        }
        
        // Periodic weather fetch (every 15 min when connected)
        if (force_fetch || millis() - last_weather_fetch > WEATHER_INTERVAL_MS) {
            morning_net_fetch_weather(nullptr);
        }
        force_fetch = false;
    }
}

bool morning_net_is_connected() {
    return wifi_connected && WiFi.status() == WL_CONNECTED;
}

bool morning_net_get_time(struct tm* timeinfo) {
    if (!time_synced) {
        time_t now;
        time(&now);
        if (now > 1000000000) {  // Sanity check (after year 2001)
            time_synced = true;
        }
    }
    if (!time_synced) return false;
    
    time_t now;
    time(&now);
    localtime_r(&now, timeinfo);
    return true;
}

static bool fetch_feed_sync(FeedData* data) {
    char url[256];
    if (!morning_config_get_feed_url(url, sizeof(url))) {
        Serial.println("Feed URL not configured");
        return false;
    }
    
    // Cache-bust: the Gist raw CDN caches for 5 min (max-age=300) per URL, so a
    // fetch soon after Trad's push could get the old feed. GitHub ignores the
    // query string; the CDN keys on it.
    {
        size_t n = strlen(url);
        snprintf(url + n, sizeof(url) - n, "%cmb=%lu", strchr(url, '?') ? '&' : '?',
                 (unsigned long)time(nullptr));
    }
    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    
    int code = http.GET();
    last_feed_code = code;
    if (code != 200) {
        Serial.printf("Feed fetch failed: %d\n", code);
        http.end();
        return false;
    }
    
    String payload = http.getString();
    http.end();
    
    if (payload.length() > 16384) {
        Serial.println("Feed too large");
        return false;
    }
    
    // Parse JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("Feed parse error: %s\n", err.c_str());
        return false;
    }
    
    data->version = doc["version"] | 1;
    strlcpy(data->generated, doc["generated"] | "", sizeof(data->generated));
    
    JsonArray days = doc["days"].as<JsonArray>();
    data->day_count = 0;
    for (JsonObject day : days) {
        if (data->day_count >= 7) break;
        DayData* d = &data->days[data->day_count++];
        
        strlcpy(d->date, day["date"] | "", sizeof(d->date));
        
        JsonArray bdays = day["birthdays"].as<JsonArray>();
        d->birthday_count = 0;
        for (const char* name : bdays) {
            if (d->birthday_count >= MAX_BIRTHDAYS) break;
            strlcpy(d->birthdays[d->birthday_count++], name, MAX_NAME_LEN);
        }
        
        // Parse kids
        for (int k = 0; k < 2; k++) {
            const char* kid_name = (k == 0) ? "arlo" : "willow";
            KidDay* kid = (k == 0) ? &d->arlo : &d->willow;
            JsonObject kid_obj = day["kids"][kid_name];
            
            const char* uniform_str = kid_obj["uniform"] | "";
            if (strcmp(uniform_str, "sport") == 0) kid->uniform = UNIFORM_SPORT;
            else if (strcmp(uniform_str, "formal") == 0) kid->uniform = UNIFORM_FORMAL;
            else if (strcmp(uniform_str, "mufti") == 0) kid->uniform = UNIFORM_MUFTI;
            else kid->uniform = UNIFORM_NONE;
            
            JsonArray acts = kid_obj["activity"].as<JsonArray>();
            kid->activity_count = 0;
            for (const char* act : acts) {
                if (kid->activity_count >= MAX_ACTIVITIES) break;
                Activity a = ACTIVITY_OTHER;
                if (strcmp(act, "football") == 0) a = ACTIVITY_FOOTBALL;
                else if (strcmp(act, "sailing") == 0) a = ACTIVITY_SAILING;
                else if (strcmp(act, "dance") == 0) a = ACTIVITY_DANCE;
                else if (strcmp(act, "choir") == 0) a = ACTIVITY_CHOIR;
                else if (strcmp(act, "swimming") == 0) a = ACTIVITY_SWIMMING;
                else if (strcmp(act, "party") == 0) a = ACTIVITY_PARTY;
                else if (strcmp(act, "trombone") == 0) a = ACTIVITY_TROMBONE;
                else if (strcmp(act, "violin") == 0) a = ACTIVITY_VIOLIN;
                else if (strcmp(act, "piano") == 0) a = ACTIVITY_PIANO;
                else if (strcmp(act, "art") == 0) a = ACTIVITY_ART;
                else if (strcmp(act, "pottery") == 0) a = ACTIVITY_POTTERY;
                else if (strcmp(act, "surfing") == 0) a = ACTIVITY_SURFING;
                else if (strcmp(act, "netball") == 0) a = ACTIVITY_NETBALL;
                kid->activities[kid->activity_count++] = a;
            }
            
            JsonArray tonight = kid_obj["tonight"].as<JsonArray>();
            kid->tonight_count = 0;
            for (const char* item : tonight) {
                if (kid->tonight_count >= MAX_ITEMS) break;
                strlcpy(kid->tonight[kid->tonight_count++], item, MAX_TEXT_LEN);
            }
            
            JsonArray items = kid_obj["items"].as<JsonArray>();
            kid->item_count = 0;
            for (JsonObject item_obj : items) {
                if (kid->item_count >= MAX_ITEMS) break;
                Item* item = &kid->items[kid->item_count++];
                strlcpy(item->text, item_obj["text"] | "", MAX_TEXT_LEN);
                
                const char* type_str = item_obj["type"] | "other";
                if (strcmp(type_str, "event") == 0) item->type = ITEM_EVENT;
                else if (strcmp(type_str, "bring") == 0) item->type = ITEM_BRING;
                else if (strcmp(type_str, "action") == 0) item->type = ITEM_ACTION;
                else if (strcmp(type_str, "uniform") == 0) item->type = ITEM_UNIFORM;
                else if (strcmp(type_str, "activity") == 0) item->type = ITEM_ACTIVITY;
                else item->type = ITEM_OTHER;
                
                strlcpy(item->deadline, item_obj["deadline"] | "", sizeof(item->deadline));
            }
        }
        
        // Parse parents
        JsonArray parent_items = day["parents"]["items"].as<JsonArray>();
        d->parents.item_count = 0;
        for (JsonObject item_obj : parent_items) {
            if (d->parents.item_count >= MAX_ITEMS) break;
            Item* item = &d->parents.items[d->parents.item_count++];
            strlcpy(item->text, item_obj["text"] | "", MAX_TEXT_LEN);
            
            const char* type_str = item_obj["type"] | "other";
            if (strcmp(type_str, "event") == 0) item->type = ITEM_EVENT;
            else if (strcmp(type_str, "bring") == 0) item->type = ITEM_BRING;
            else if (strcmp(type_str, "action") == 0) item->type = ITEM_ACTION;
            else item->type = ITEM_OTHER;
            
            strlcpy(item->deadline, item_obj["deadline"] | "", sizeof(item->deadline));
        }
    }
    
    data->valid = true;
    data->fetched_at = millis();
    return true;
}

static bool fetch_weather_sync(WeatherData* data) {
    float lat, lon;
    morning_config_get_weather_location(&lat, &lon);
    
    char url[256];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
        "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weathercode"
        "&timezone=Australia/Sydney&forecast_days=7",
        lat, lon);
    
    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    
    int code = http.GET();
    if (code != 200) {
        Serial.printf("Weather fetch failed: %d\n", code);
        http.end();
        return false;
    }
    
    String payload = http.getString();
    http.end();
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("Weather parse error: %s\n", err.c_str());
        return false;
    }
    
    JsonArray max_temps = doc["daily"]["temperature_2m_max"].as<JsonArray>();
    JsonArray min_temps = doc["daily"]["temperature_2m_min"].as<JsonArray>();
    JsonArray precip = doc["daily"]["precipitation_probability_max"].as<JsonArray>();
    JsonArray codes = doc["daily"]["weathercode"].as<JsonArray>();
    
    int i = 0;
    for (float temp : max_temps) {
        if (i >= 7) break;
        data->max_temp[i++] = temp;
    }
    i = 0;
    for (float temp : min_temps) {
        if (i >= 7) break;
        data->min_temp[i++] = temp;
    }
    i = 0;
    for (int prob : precip) {
        if (i >= 7) break;
        data->precip_prob[i++] = prob;
    }
    i = 0;
    for (int code : codes) {
        if (i >= 7) break;
        data->weathercode[i++] = code;
    }
    
    data->valid = true;
    data->fetched_at = millis();
    return true;
}

void morning_net_fetch_feed(void (*callback)(const FeedData*, bool)) {
    last_feed_fetch = millis();
    if (callback) feed_callback = callback;   // periodic calls pass nullptr: keep UI callback
    callback = feed_callback;
    
    if (!morning_net_is_connected()) {
        if (callback) callback(&cached_feed, false);
        return;
    }
    
    char prev_generated[sizeof(cached_feed.generated)];
    strlcpy(prev_generated, cached_feed.generated, sizeof(prev_generated));
    bool success = fetch_feed_sync(&cached_feed);
    if (success) {
        last_feed_ok_ms = millis();
        // Flash writes stall the display's bounce-buffer ISR, so only rewrite
        // the LittleFS cache when the feed actually changed.
        if (strcmp(prev_generated, cached_feed.generated) != 0) save_cached_feed();
    }
    Serial.printf("Feed fetch %s (code %d), first day %s, generated %s\n", success ? "OK" : "FAILED",
                  last_feed_code, cached_feed.day_count > 0 ? cached_feed.days[0].date : "-",
                  cached_feed.generated);
    if (callback) callback(&cached_feed, success);
}

void morning_net_fetch_weather(void (*callback)(const WeatherData*, bool)) {
    last_weather_fetch = millis();
    if (callback) weather_callback = callback;
    callback = weather_callback;
    
    if (!morning_net_is_connected()) {
        if (callback) callback(&cached_weather, false);
        return;
    }
    
    bool success = fetch_weather_sync(&cached_weather);
    if (callback) callback(&cached_weather, success);
}

const FeedData* morning_net_get_cached_feed() {
    return &cached_feed;
}

const WeatherData* morning_net_get_cached_weather() {
    return &cached_weather;
}

static bool load_cached_feed() {
    File f = LittleFS.open("/feed_cache.json", "r");
    if (!f) {
        Serial.println("No cached feed found");
        return false;
    }
    
    size_t size = f.size();
    if (size == 0 || size > 20000) {
        f.close();
        return false;
    }
    
    String content = f.readString();
    f.close();
    
    // Parse directly into cached_feed structure (simplified - just validate JSON)
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, content);
    if (err) {
        Serial.printf("Cached feed parse error: %s\n", err.c_str());
        return false;
    }
    
    cached_feed.version = doc["version"] | 1;
    cached_feed.day_count = doc["days"].size();
    cached_feed.valid = (cached_feed.day_count > 0);
    cached_feed.fetched_at = 0;  // Will be old
    
    Serial.printf("Loaded cached feed (%d days)\n", cached_feed.day_count);
    return true;
}

static bool save_cached_feed() {
    if (!cached_feed.valid) return false;
    
    File f = LittleFS.open("/feed_cache.json", "w");
    if (!f) {
        Serial.println("Failed to open feed cache for writing");
        return false;
    }
    
    // Simplified: just save a marker. Full serialization would require
    // re-assembling the JSON from the FeedData structure, which is expensive.
    // For now, the in-memory cache is sufficient.
    f.println("{\"cached\":true}");
    f.close();
    
    Serial.println("Feed cached to LittleFS");
    return true;
}

bool morning_net_in_provision_window() {
    if (millis() - boot_time_ms < PROVISION_WINDOW_MS) return true;
    
    char ssid[64];
    if (!morning_config_get_wifi_ssid(ssid, sizeof(ssid))) return true;
    
    return false;
}

bool morning_net_is_wifi_configured() {
    char ssid[64];
    return morning_config_get_wifi_ssid(ssid, sizeof(ssid));
}

void morning_net_process_serial() {
    static char cmd_buf[256];
    static int cmd_pos = 0;
    
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_pos] = '\0';
            
            if (strncmp(cmd_buf, "SET WIFI_SSID=", 14) == 0) {
                morning_config_set_wifi_ssid(cmd_buf + 14);
                Serial.println("OK: WiFi SSID set");
            } else if (strncmp(cmd_buf, "SET WIFI_PASS=", 14) == 0) {
                morning_config_set_wifi_pass(cmd_buf + 14);
                Serial.println("OK: WiFi password set");
            } else if (strncmp(cmd_buf, "SET FEED_URL=", 13) == 0) {
                morning_config_set_feed_url(cmd_buf + 13);
                Serial.println("OK: Feed URL set");
            } else if (strcmp(cmd_buf, "SHOW") == 0) {
                char ssid[64], url[256];
                bool has_ssid = morning_config_get_wifi_ssid(ssid, sizeof(ssid));
                bool has_url = morning_config_get_feed_url(url, sizeof(url));
                Serial.println("=== Configuration ===");
                Serial.printf("WIFI_SSID=%s\n", has_ssid ? ssid : "(not set)");
                Serial.printf("FEED_URL=%s\n", has_url ? url : "(not set)");
                Serial.println("(password hidden)");
                if (wifi_connected) {
                    Serial.printf("WiFi: Connected (%s)\n", WiFi.localIP().toString().c_str());
                } else {
                    Serial.printf("WiFi: %s\n", wifi_configured ? "Configured, not connected" : "Not configured");
                }
            } else if (strncmp(cmd_buf, "BL ", 3) == 0) {
                int v = atoi(cmd_buf + 3);
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                io_expander_raw_pwm((uint8_t)v);
            } else if (strcmp(cmd_buf, "BL") == 0) {
                Serial.printf("BL readback=%d boot=%d\n", io_expander_read_pwm(), io_expander_boot_pwm());
            } else if (strcmp(cmd_buf, "OTA") == 0 || strcmp(cmd_buf, "ota") == 0) {
                morning_print_ota_info();
            } else if (strcmp(cmd_buf, "STATUS") == 0) {
                extern void touch_hal_debug_status(void);
                touch_hal_debug_status();
            } else if (strcmp(cmd_buf, "REBOOT") == 0) {
                Serial.println("Rebooting...");
                delay(100);
                ESP.restart();
            }
            
            cmd_pos = 0;
        } else if (cmd_pos < 255) {
            cmd_buf[cmd_pos++] = c;
        }
    }
}
#endif  // BOARD_MORNING_BOARD
