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
static int     boot_pwm      = -2;   // CH32 reg 0x05 read at boot (-1 read failed, -2 n/a)
#define CH32_REG_PWM         0x05

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
        // Read-only peek at the backlight PWM register before anything writes
        // it (nothing writes 0x05 at boot). Reported as the power-on default.
        boot_pwm = iox_read(CH32_REG_PWM);
        delay(10);
        Serial.printf("LCD-4 CH32 PWM reg 0x05 at boot (read-only): %d\n", boot_pwm);
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

// Rev4 CH32 backlight PWM (register 0x05) polarity, bench-verified on
// Oliver's board 2026-11-24: INVERTED. 0 = full brightness (also the CH32
// power-on default), 247 = dark, 255 = dark (same as 247). We use 255 for off.
#define CH32_PWM_FULL 0
#define CH32_PWM_OFF  255

void io_expander_set_backlight(bool on) {
    // CH32 (Rev4): drive only the PWM register 0x05. Never touches the EXIO
    // output/mode registers, so it cannot raise the buzzer bit (EXIO6); EXIO2
    // is touch INT on Rev4. iox_write keeps the 10 ms CH32 settle delay.
    // At boot display_hal_begin() calls this with on=true, which writes 0
    // (full, same as the power-on default), so boot never writes anything else.
    if (is_ch32) {
        if (!expander_addr) return;
        uint8_t duty = on ? CH32_PWM_FULL : CH32_PWM_OFF;
        bool ok = iox_write(CH32_REG_PWM, duty);
        Serial.printf("Backlight %s (CH32 0x05=%u, %s)\n", on ? "ON" : "OFF", duty, ok ? "ack" : "NACK");
        return;
    }
    if (on) output_latch |= (1u << IOX_PIN_BACKLIGHT);
    else    output_latch &= ~(1u << IOX_PIN_BACKLIGHT);
    iox_commit_output();
}

void io_expander_set_brightness(uint8_t level) {
    // level 0 = off .. 255 = full. CH32 Rev4 is inverted (see above):
    // duty = 255 - level, so 255 -> 0 (full) and 0 -> 255 (off).
    // TCA9554 (pre-Rev4) has no PWM: on/off at EXIO2.
    if (is_ch32) {
        if (!expander_addr) return;
        iox_write(CH32_REG_PWM, (uint8_t)(CH32_PWM_OFF - level));
    } else {
        io_expander_set_backlight(level > 0);
    }
}

uint8_t io_expander_addr(void) { return expander_addr; }

// ---- Backlight polarity test hooks (serial "BL <n>", HTTP /bl?v=n) ----
// Raw write of n to the CH32 PWM register 0x05 only. Never touches the EXIO
// output/mode registers (buzzer-safe); iox_write adds the 10 ms settle delay.
bool io_expander_raw_pwm(uint8_t value) {
    if (!is_ch32 || !expander_addr) return false;
    bool ok = iox_write(CH32_REG_PWM, value);
    Serial.printf("BL: wrote %u to CH32 reg 0x05 (%s) t=%lums\n", value, ok ? "ack" : "NACK",
                  (unsigned long)millis());
    return ok;
}
int io_expander_read_pwm(void) {
    if (!is_ch32 || !expander_addr) return -2;
    int v = iox_read(CH32_REG_PWM);
    delay(10);
    return v;
}
int io_expander_boot_pwm(void) { return boot_pwm; }
