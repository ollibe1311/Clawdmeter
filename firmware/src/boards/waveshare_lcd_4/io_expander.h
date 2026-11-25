#pragma once

#include <stdint.h>

// TCA9554 / CH32V003-as-expander on the LCD-4. Must run before
// display_hal_init() so the panel power rails and backlight are up.

void io_expander_init(void);
void io_expander_set_backlight(bool on);
void io_expander_set_brightness(uint8_t level);  // 0-255, CH32 PWM on Rev4
bool io_expander_raw_pwm(uint8_t value);   // test hook: raw write to CH32 reg 0x05
int  io_expander_read_pwm(void);           // read CH32 reg 0x05 (-1 fail, -2 n/a)
int  io_expander_boot_pwm(void);           // value read at boot before any write
uint8_t io_expander_addr(void);  // 0 if probe failed
