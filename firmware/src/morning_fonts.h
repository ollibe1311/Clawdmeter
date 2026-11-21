#pragma once
#include <lvgl.h>

/*
 * Placeholder font declarations for Morning Board.
 * 
 * TODO: Generate proper Atkinson Hyperlegible or Andika fonts with lv_font_conv.
 * These fonts should include:
 * - Digits 0-9
 * - Basic Latin (A-Z, a-z, punctuation)
 * - Warning triangle: ⚠ (U+26A0)
 * - Party popper: 🎉 (U+1F389) or substitute icon
 * 
 * Required sizes: 48px (names), 28px (items), 20px (details)
 * 
 * Generation command (example):
 * lv_font_conv --font AtkinsonHyperlegible-Regular.ttf --size 48 \
 *   --range 0x20-0x7E,0x26A0 --format lvgl --bpp 4 \
 *   --no-compress -o morning_font_48.c
 * 
 * For now, we alias to existing Styrene fonts from Clawdmeter.
 * These fonts are Anthropic's proprietary Styrene fonts and must be
 * replaced with open fonts for distribution.
 */

// Temporary aliases to existing fonts
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_20);

#define morning_font_48 font_styrene_48
#define morning_font_28 font_styrene_28
#define morning_font_20 font_styrene_20
