# Morning Board - Flashing and Setup Instructions

Morning Board is a kitchen family schedule display built on the Waveshare ESP32-S3-Touch-LCD-4 hardware (480×480 ST7701 RGB panel + GT911 touch).

## Hardware Requirements

- Waveshare ESP32-S3-Touch-LCD-4 (Rev 4.0)
- USB-C cable for flashing and power
- 2.4GHz WiFi network

## Build and Flash

### Prerequisites

Install PlatformIO:
```bash
# macOS
brew install platformio

# Linux (via pip)
pip3 install platformio

# Or install PlatformIO IDE (VS Code extension)
```

### Build the Firmware

```bash
cd firmware
pio run -e morning_board
```

The compiled firmware will be in `.pio/build/morning_board/firmware.bin`.

### Flash to Device

1. Connect the board via USB-C
2. The ESP32-S3's native USB-JTAG requires no special button presses for flashing

```bash
# macOS
pio run -e morning_board -t upload --upload-port /dev/cu.usbmodem101

# Linux
pio run -e morning_board -t upload --upload-port /dev/ttyACM0

# Windows
pio run -e morning_board -t upload --upload-port COM3
```

**Note:** The port name varies by OS and may increment (e.g., `/dev/ttyACM1`, `COM4`). Use `pio device list` to find the correct port.

## Initial Provisioning

After flashing, the board boots into **provisioning mode** for the first 5 seconds (or until WiFi is configured).

### Option 1: Serial Provisioning (Recommended)

1. Create a `provision.env` file with your credentials:

```bash
WIFI_SSID=YourWiFiName
WIFI_PASS=YourWiFiPassword
FEED_URL=https://example.com/path/to/feed.json
```

2. Run the provisioning tool:

```bash
cd tools
python3 provision.py --port /dev/ttyACM0 --env provision.env
```

The tool will:
- Send your WiFi credentials and feed URL to the board
- Verify the configuration
- Optionally reboot the board

### Option 2: Manual Serial Provisioning

1. Connect to the serial console at 115200 baud:

```bash
# macOS/Linux
screen /dev/ttyACM0 115200

# Or use PlatformIO's monitor
pio device monitor -b 115200
```

2. Within the first 5 seconds after boot, send these commands:

```
SET WIFI_SSID=YourWiFiName
SET WIFI_PASS=YourWiFiPassword
SET FEED_URL=https://example.com/feed.json
SHOW
REBOOT
```

Each command should return `OK:` confirmation.

**Security Note:** Your WiFi password is never displayed by the `SHOW` command.

## Web Settings Page

Once connected to WiFi, the board starts a web server for adjusting display settings:

- **URL:** `http://morningboard.local` (via mDNS)
- **Fallback:** Use the IP address shown in the serial console (e.g., `http://192.168.1.50`)

Settings you can adjust:
- **Dress Rules:** Temperature thresholds for clothing recommendations
- **Screen Schedule:** Auto on/off times (morning and afternoon windows)

These settings are saved to NVS and persist across reboots.

## Feed Format

The Morning Board fetches a JSON feed from the configured `FEED_URL` every 15 minutes. A sample feed is included at `firmware/morning_board_sample_feed.json`.

### Feed Schema

```json
{
  "version": 1,
  "generated": "ISO8601+11:00",
  "days": [
    {
      "date": "YYYY-MM-DD",
      "birthdays": ["Name"],
      "kids": {
        "arlo": {
          "uniform": null|"sport"|"formal"|"mufti",
          "activity": ["football", "sailing", ...],
          "tonight": ["Pack footy boots"],
          "items": [
            {
              "text": "Library bag",
              "type": "bring|event|action|uniform|activity|other",
              "deadline": "YYYY-MM-DD"
            }
          ]
        },
        "willow": { ... }
      },
      "parents": {
        "items": [ ... ]
      }
    }
  ]
}
```

The feed must:
- Be under 16KB
- Cover 7 days starting from today
- Use Australia/Sydney timezone

## Weather Data

Weather is automatically fetched from [Open-Meteo](https://open-meteo.com) (no API key required) for Sydney by default (lat -33.87, lon 151.21).

The location is configurable via NVS (currently requires code modification - web UI support pending).

## Operation

### Screen Schedule

The screen automatically turns on during configured hours:
- **Morning:** 06:00 - 09:00
- **Afternoon:** 15:00 - 20:30

Tap the screen to wake it anytime. It returns to sleep after 2 minutes of idle (unless in a scheduled window, where it returns to the Clawd screen instead).

### Navigation

1. **Clawd Screen (wake screen):**
   - Shows animated Clawd artwork
   - Tap anywhere (or press BOOT button) to open the schedule pages

2. **Schedule Pages:**
   - **Kid 1 Page** (Arlo or Willow - alternates daily)
   - **Kid 2 Page** (the other kid)
   - **Parents Page**
   - Swipe UP/DOWN to switch pages
   - Swipe LEFT/RIGHT to change the date (today + 6 days)
   - Tap the top strip to return to Clawd
   - Auto-returns to Clawd after 2 minutes idle

### "Today" Logic

- Before 15:00 → shows today's date
- From 15:00 onward → shows tomorrow's date
- When tomorrow is shown, a "Tonight" section appears on kid pages with evening prep tasks

## Resetting Configuration

To clear all saved configuration (WiFi, feed URL, settings):

1. Connect via serial console
2. Send the following commands:

```
SET WIFI_SSID=
SET WIFI_PASS=
SET FEED_URL=
REBOOT
```

Or reflash the firmware with `pio run -e morning_board -t upload`.

## Troubleshooting

### WiFi Won't Connect

1. Verify credentials with `SHOW` command over serial
2. Ensure the network is 2.4GHz (ESP32 doesn't support 5GHz)
3. Check serial console for connection errors
4. Try moving closer to the router during initial setup

### Feed Not Updating

1. Check serial console for fetch errors
2. Verify the feed URL is accessible (test in a browser)
3. Ensure the feed is valid JSON under 16KB
4. Check the board's IP has internet access (firewall/proxy issues)

### Screen Stays Off

1. Check the screen schedule in web settings
2. Tap the screen or press the BOOT button to wake
3. Verify display brightness isn't set to 0

### Display Shows "Last Updated X Hours Ago"

The cached feed is older than 2 hours. This is normal if:
- The board was offline for extended time
- The feed URL is temporarily unreachable

The display will continue using cached data until a fresh fetch succeeds.

## Development Notes

### Fonts

The current build uses **placeholder fonts** (Anthropic's proprietary Styrene from the Clawdmeter project). For distribution, replace these with **Atkinson Hyperlegible** or **Andika**:

1. Download the open font (OFL licensed)
2. Generate LVGL fonts with `lv_font_conv`:

```bash
lv_font_conv --font AtkinsonHyperlegible-Regular.ttf --size 48 \
  --range 0x20-0x7E,0x26A0,0x1F389 --format lvgl --bpp 4 \
  --no-compress -o firmware/src/morning_font_48.c
```

3. Update `firmware/src/morning_fonts.h` with the new declarations

### Simulator Support

The desktop simulator (`-e sim`) can potentially be extended to run Morning Board UI for testing. This requires:
- Stubbing the WiFi/HTTP calls with scenario playback
- Adapting the touch/button input
- Rendering the Morning Board screens instead of Clawdmeter's usage UI

This enhancement is pending.

## Credits

- Hardware: [Waveshare ESP32-S3-Touch-LCD-4](https://www.waveshare.com/esp32-s3-touch-lcd-4.htm)
- Board drivers: Adapted from the Clawdmeter project
- Clawd artwork: © Anthropic (personal use only, not redistributable)
- Weather data: [Open-Meteo.com](https://open-meteo.com)

## License

Morning Board firmware is provided as-is for personal use. Replace proprietary fonts before distribution.
