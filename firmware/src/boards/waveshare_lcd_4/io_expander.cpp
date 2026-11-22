#include "io_expander.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>

// Two expanders ship on this kit, with different register maps:
//   TCA9554 (pre-Rev4, usually 0x20): 0x01 = output, 0x03 = config (1 = input)
//   CH32V003 (Rev4.0, 0x24, Waveshare firmware):
//       0x02 = pin mode (1 = output), 0x03 = output value, 0x04 = input,
//       0x05 = backlight PWM
// Rev4 EXIO map: 1 touch RST, 2 touch INT, 3 LCD RST, 4 SD CS, 5 SYS_EN,
// 6 BUZZER ENABLE, 7 RTC INT. The old code treated 0x24 like a TCA and wrote
// 0x02 = 0xFF / 0x03 = 0x3A back-to-back with no settle time. The CH32 is
// known to drop I2C writes that arrive too close together; if the 0x03 write
// is lost the outputs keep their power-on latch (all high) and EXIO6 drives
// the buzzer continuously. Fix: on the CH32 write the safe output value
// FIRST (buzzer bit low), then the mode, then the value again, with settle
// delays, and verify via the input register.

#define TCA_REG_OUTPUT       0x01
#define TCA_REG_CONFIG       0x03
#define TCA_CONFIG           0x3A     // bits 0/2/6/7 outputs
#define CH32_REG_MODE        0x02
#define CH32_REG_OUTPUT      0x03
#define CH32_REG_INPUT       0x04
#define CH32_PIN_BUZZER      6

// Rev4 safe output: touch RST(1), LCD RST(3), SD CS(4), SYS_EN(5) high;
// touch INT(2) low so GT911 stays at 0x5D; buzzer(6) LOW.
#define CH32_SAFE_OUTPUT     0x3A

static uint8_t expander_addr = 0;
static bool    is_ch32       = false;
static uint8_t output_latch  = 0xFF;

static bool iox_probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static bool iox_write(uint8_t reg, uint8_t val) {
    if (!expander_addr) return false;
    Wire.beginTransmission(expander_addr);
    Wire.write(reg);
    Wire.write(val);
    bool ok = Wire.endTransmission() == 0;
    if (is_ch32) delay(10);   // CH32 needs settle time between writes
    return ok;
}

static int iox_read(uint8_t reg) {
    Wire.beginTransmission(expander_addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((int)expander_addr, 1) != 1) return -1;
    return Wire.read();
}

static void iox_commit_output(void) {
    if (is_ch32) iox_write(CH32_REG_OUTPUT, output_latch);
    else         iox_write(TCA_REG_OUTPUT,  output_latch);
}

void io_expander_init(void) {
    if (iox_probe(IO_EXPANDER_ADDR)) {
        expander_addr = IO_EXPANDER_ADDR;
        is_ch32 = true;                 // 0x24 = Rev4 CH32V003
    } else if (iox_probe(IO_EXPANDER_ADDR_ALT)) {
        expander_addr = IO_EXPANDER_ADDR_ALT;
        is_ch32 = false;                // 0x20 = TCA9554
    } else {
        Serial.println("LCD-4 IO expander not found (0x24/0x20)");
        return;
    }

    if (is_ch32) {
        output_latch = CH32_SAFE_OUTPUT;
        int in = -1;
        for (int attempt = 0; attempt < 5; attempt++) {
            iox_write(CH32_REG_OUTPUT, output_latch);   // latch first, buzzer low
            iox_write(CH32_REG_MODE, 0xFF);             // then drive outputs
            iox_write(CH32_REG_OUTPUT, output_latch);   // and re-assert
            in = iox_read(CH32_REG_INPUT);
            if (in >= 0 && !(in & (1u << CH32_PIN_BUZZER))) break;
            delay(20);
        }
        Serial.printf("LCD-4 CH32 expander init (0x%02X) out=0x%02X in=0x%02X buzzer=%s\n",
                      expander_addr, output_latch, in & 0xFF,
                      (in >= 0 && (in & (1u << CH32_PIN_BUZZER))) ? "ON?!" : "off");
    } else {
        output_latch = 0xFF;
        iox_commit_output();
        iox_write(TCA_REG_CONFIG, TCA_CONFIG);
        Serial.printf("LCD-4 TCA9554 expander init OK (addr 0x%02X)\n", expander_addr);
    }
}

void io_expander_set_backlight(bool on) {
    // Rev4 backlight is the CH32's PWM channel (register 0x05), not an EXIO bit.
    // EXIO2 on Rev4 is touch INT, so leave the latch alone on the CH32.
    // For boolean on/off, use full brightness (255) or off (0).
    // Do NOT drive the PWM register from here: its polarity is unverified on
    // Rev4 (see io_expander_set_brightness) and display_hal_begin() calls this
    // at boot, so a wrong guess would black the screen on every boot. The
    // CH32's power-on PWM default already gives a lit panel.
    if (is_ch32) return;
    if (on) output_latch |= (1u << IOX_PIN_BACKLIGHT);
    else    output_latch &= ~(1u << IOX_PIN_BACKLIGHT);
    iox_commit_output();
}

void io_expander_set_brightness(uint8_t level) {
    // CH32V003 Rev4: register 0x05 is backlight PWM (0-255).
    // Reference: Waveshare ESP32-S3-Touch-LCD-4 Rev4.0 schematic and
    // CH32V003 firmware implementation at I2C address 0x24.
    // TCA9554 (pre-Rev4): no PWM, treat as on/off at EXIO2.
    //
    // VERDICT (2026-11-22): register 0x05 is the PWM register on the Rev4 CH32
    // (Waveshare io_extension.h: IO_EXTENSION_PWM_ADDR 0x05; ESPHome
    // waveshare_io_ch32v003). Polarity is NOT agreed: Espressif's
    // ESP32_IO_Expander driver for this exact board (commit 7eea233) says
    // inverted, 0 = max .. 247 = off, while Waveshare's 7B demo treats it as
    // 0-100% * 2.55 with a 97% cap. Writing 0 could mean full brightness and
    // 255 could mean off. Until verified on Oliver's board this is a no-op on
    // the CH32; screen-off uses a black LVGL overlay instead
    // (morning_ui_set_blank). Define LCD4_CH32_PWM_INVERTED_VERIFIED after a
    // bench test to enable the inverted mapping below. Never touches the EXIO
    // output register, so it cannot raise the buzzer bit (EXIO6).
    if (is_ch32) {
#ifdef LCD4_CH32_PWM_INVERTED_VERIFIED
        uint8_t duty = (uint8_t)(247 - ((uint32_t)level * 247 / 255));
        iox_write(0x05, duty);
#else
        (void)level;
#endif
    } else {
        io_expander_set_backlight(level > 0);
    }
}

uint8_t io_expander_addr(void) { return expander_addr; }
