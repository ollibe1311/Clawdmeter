# Morning Board

Morning Board is a kitchen family schedule display built on the Clawdmeter codebase, targeting the Waveshare ESP32-S3-Touch-LCD-4 hardware.

## Features

- **WiFi Connectivity**: Connects to 2.4GHz WiFi and fetches schedule data
- **NTP Time Sync**: Australia/Sydney timezone with automatic DST
- **Schedule Feed**: JSON feed with 7-day family schedule (kids + parents)
- **Weather Integration**: Open-Meteo API for Sydney weather forecasts
- **Dress Advice**: Automatic clothing recommendations based on temperature and rain
- **Web Settings**: Local web page (`morningboard.local`) for configuration
- **Screen Schedule**: Auto on/off times (morning 6-9am, afternoon 3-8:30pm)
- **Clawd Animations**: Reuses the Clawd pixel art and animation engine
- **Touch Navigation**: Swipe between kid pages and parent page
- **Serial Provisioning**: USB serial setup for WiFi and feed URL

## Architecture

```
firmware/src/
  morning_main.cpp      - Main application loop
  morning_ui.{h,cpp}    - UI screens (Clawd, kids, parents)
  morning_net.{h,cpp}   - WiFi, HTTP, NTP, mDNS, web server
  morning_config.{h,cpp}- NVS preferences storage
  morning_data.h        - Data structures for feed/weather
  morning_fonts.h       - Font declarations (TODO: replace with open fonts)
```

The Morning Board build (`-e morning_board`) reuses:
- `boards/waveshare_lcd_4/` hardware drivers (display, touch, IO expander)
- `splash.{h,cpp}` Clawd animation engine
- LVGL UI framework

It excludes Clawdmeter-specific modules:
- BLE transport (`ble.cpp`)
- Chime audio (`chime.cpp`, `es8311.c`)
- Usage rate tracking (`usage_rate.cpp`)
- Brightness cycling (`brightness.cpp`)

## Display Layout

### Clawd Screen (Wake Screen)
Full-screen Clawd animation. Tap to enter schedule pages.

Special behaviors:
- **Friday**: More frequent dancing animation
- **Birthdays**: Dancing animation
- **Activities**: Activity-matched clips (football→soccer, sailing→sailing, etc.)

### Kid Pages
- **Top strip**: Day name, date, time, weather icon + max temp
- **Name**: Large, in kid's accent color (Arlo = green, Willow = hot pink)
- **Items list**: Icons by type, sorted (event, bring, action, uniform, activity, other)
- **Action items**: Warning mark + short deadline (⚠ Fri)
- **Bottom line**: Dress advice or uniform
- **Tonight section**: Shows from 3pm onwards with evening prep tasks

Empty day: "Nothing on today 🎉"

### Parents Page
Same layout with orange accent, no dress line.

### Navigation
- **Vertical swipe**: Switch kid1 ↔ kid2 ↔ parents
- **Horizontal swipe**: Change date (today + 6 days)
- **Tap top strip**: Return to Clawd
- **2 min idle**: Auto-return to Clawd

Which kid appears first alternates daily (based on days since 2026-01-01).

## Configuration

### WiFi Credentials (NVS)
- `wifi_ssid`
- `wifi_pass` (never displayed)

### Feed URL (NVS)
- `feed_url` - HTTPS endpoint returning JSON schedule

### Dress Rules (NVS, editable via web)
- `dress_shorts` - Temperature (°C) for shorts/skort (default: 22.0)
- `dress_pants` - Temperature (°C) for long sleeves (default: 16.0)
- `dress_rain` - Precipitation probability (%) for rain jacket (default: 40)

### Screen Schedule (NVS, editable via web)
- `sched_msh`, `sched_msm` - Morning start (default: 06:00)
- `sched_meh`, `sched_mem` - Morning end (default: 09:00)
- `sched_ash`, `sched_asm` - Afternoon start (default: 15:00)
- `sched_aeh`, `sched_aem` - Afternoon end (default: 20:30)

### Weather Location (NVS)
- `weather_lat` - Latitude (default: -33.87)
- `weather_lon` - Longitude (default: 151.21)

Currently requires code change to update (web UI pending).

## Data Sources

### Feed (JSON, < 16KB)
Fetched every 15 minutes from `FEED_URL`. Cached to LittleFS. If fetch fails or offline, uses cached data. Shows "last updated HH:MM" if cache is > 2 hours old.

Sample feed: `firmware/morning_board_sample_feed.json`

### Weather (JSON)
Fetched every hour from Open-Meteo API (no key required):
```
https://api.open-meteo.com/v1/forecast
  ?latitude=-33.87&longitude=151.21
  &daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max
  &timezone=Australia/Sydney&forecast_days=7
```

## Build and Flash

See [`FLASHING.md`](../FLASHING.md) for complete instructions.

Quick start:
```bash
cd firmware
pio run -e morning_board -t upload --upload-port /dev/ttyACM0
```

## Provisioning

After first flash, provision via serial (115200 baud) within 5 seconds:
```
SET WIFI_SSID=YourNetwork
SET WIFI_PASS=YourPassword
SET FEED_URL=https://example.com/feed.json
SHOW
REBOOT
```

Or use the `tools/provision.py` script:
```bash
python3 tools/provision.py --env tools/provision.env
```

## TODO / Known Limitations

1. **Fonts**: Currently using Anthropic's proprietary Styrene fonts as placeholders. Must replace with Atkinson Hyperlegible or Andika before distribution. See `morning_fonts.h`.

2. **Simulator**: Desktop simulator (`-e sim`) support not yet implemented. Would require WiFi/HTTP stubs and Morning Board UI integration.

3. **Weather Location**: Not yet exposed in web settings UI. Requires code change + reflash to update.

4. **LittleFS Feed Cache**: Save/load partially implemented. Currently relies on NVS and in-memory cache.

5. **Touch Gestures**: Swipe left/right/up/down for navigation not yet wired. Currently only tap to toggle is implemented.

6. **Date Selection**: Horizontal swipe to change date (today + 6 days) not yet implemented.

7. **Page Dots**: Visual indicator for current day (1 of 7) not rendered.

8. **Hardware Verification**: Built but not tested on actual LCD-4 hardware. Display, touch, WiFi, HTTPS all confirmed working on the same board with Clawdmeter, but Morning Board code is untested.

9. **Empty Icons**: Item type icons not yet rendered (event, bring, action, etc.). Currently text-only.

10. **Tonight Section**: Logic for showing tonight items from 3pm is implemented but UI rendering needs completion.

## License

Morning Board is derived from Clawdmeter and provided for personal use. Replace proprietary fonts before distribution. Clawd artwork © Anthropic (personal use only).
