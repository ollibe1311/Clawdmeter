#include "../../hal/touch_hal.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <TouchDrvGT911.hpp>

// GT911 via SensorLib. Address is latched from INT at reset — probe 0x5D
// then fall back to 0x14. Polled (no IRQ): Waveshare's INT pin is GPIO 16
// but the original lcd4 port treated it as unused and polling is reliable.

static TouchDrvGT911 touch;

static bool     touch_ok      = false;
static bool     touch_pressed = false;
static uint16_t touch_x       = 0;
static uint16_t touch_y       = 0;
static uint8_t  touch_addr    = 0;
static uint32_t touch_count   = 0;

void touch_hal_init(void) {
    uint8_t gt_addr = 0x14;
    Wire.beginTransmission(0x5D);
    if (Wire.endTransmission() == 0) gt_addr = 0x5D;

    touch.setPins(-1, -1);
    if (!touch.begin(Wire, gt_addr, IIC_SDA, IIC_SCL)) {
        Serial.printf("Touch GT911 init failed (tried 0x%02X)\n", gt_addr);
        return;
    }
    touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
    touch.setSwapXY(false);
    touch.setMirrorXY(false, false);
    touch_ok = true;
    touch_addr = gt_addr;
    Serial.printf("Touch GT911 init OK (addr 0x%02X) model=%s chipID=0x%lX fw=0x%04X points=%u\n",
                  gt_addr, touch.getModelName(), (unsigned long)touch.getChipID(),
                  touch.getFwVersion(), touch.getSupportTouchPoint());
}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    if (touch_ok) {
        int16_t tx[5], ty[5];
        uint8_t n = touch.getPoint(tx, ty, touch.getSupportTouchPoint());
        if (n > 0) {
            if (!touch_pressed) {
                touch_count++;
                Serial.printf("Touch down x=%d y=%d\n", tx[0], ty[0]);
            }
            touch_pressed = true;
            touch_x = (uint16_t)tx[0];
            touch_y = (uint16_t)ty[0];
        } else {
            if (touch_pressed)
                Serial.printf("Touch up   x=%u y=%u\n", touch_x, touch_y);
            touch_pressed = false;
        }
    }
    *x = touch_x;
    *y = touch_y;
    *pressed = touch_pressed;
}

// Debug: serial STATUS command. Re-reads the GT911 ID live so it works even
// though USB-CDC drops the early boot lines.
void touch_hal_debug_status(void) {
    if (!touch_ok) { Serial.println("TOUCH: init failed / not present"); return; }
    Serial.printf("TOUCH: GT911 addr 0x%02X model=%s chipID=0x%lX fw=0x%04X taps=%lu pressed=%d\n",
                  touch_addr, touch.getModelName(), (unsigned long)touch.getChipID(),
                  touch.getFwVersion(), (unsigned long)touch_count, (int)touch_pressed);
}
