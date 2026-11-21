#!/usr/bin/env python3
"""
Patch lv_font_conv output for LVGL 9 compatibility.
Removes LVGL 8 version guards and adds required LVGL 9 fields.
"""
import sys
import re

def patch_font(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    # Remove LVGL 8 version check wrappers around font_dsc
    content = re.sub(
        r'#if LV_VERSION_CHECK\(8, 0, 0\)\n/\*Store all the custom data of the font\*/\nstatic  lv_font_fmt_txt_glyph_cache_t cache;\nstatic const lv_font_fmt_txt_dsc_t font_dsc = \{\n#else\nstatic lv_font_fmt_txt_dsc_t font_dsc = \{\n#endif',
        'static const lv_font_fmt_txt_dsc_t font_dsc = {',
        content
    )
    
    # Remove .cache field
    content = re.sub(
        r',\n#if LV_VERSION_CHECK\(8, 0, 0\)\n    \.cache = &cache\n#endif',
        '',
        content
    )
    
    # Remove LVGL 8 version check around font declaration
    content = re.sub(
        r'#if LV_VERSION_CHECK\(8, 0, 0\)\nconst lv_font_t (morning_font_\d+) = \{\n#else\nlv_font_t \1 = \{\n#endif',
        r'const lv_font_t \1 = {',
        content
    )
    
    # Add LVGL 9 fields before dsc
    content = re.sub(
        r'(    \.subpx = LV_FONT_SUBPX_NONE,)\n(#if LV_VERSION_CHECK\(7, 4, 0\) \|\| LVGL_VERSION_MAJOR >= 8\n    \.underline_position = [^,]+,\n    \.underline_thickness = [^,]+,\n#endif\n    \.dsc = &font_dsc)',
        r'\1\n    .release_glyph = NULL,\n    .kerning = 0,\n    .static_bitmap = 0,\n    .underline_position = -4,\n    .underline_thickness = 3,\n    .dsc = &font_dsc,\n    .fallback = NULL,\n    .user_data = NULL',
        content
    )
    
    # Remove old underline version check
    content = re.sub(
        r'#if LV_VERSION_CHECK\(7, 4, 0\) \|\| LVGL_VERSION_MAJOR >= 8\n    \.underline_position = [^,]+,\n    \.underline_thickness = [^,]+,\n#endif',
        '',
        content
    )
    
    # Remove trailing comma before closing brace
    content = re.sub(r',(\s*)\.user_data = NULL', r'\1.user_data = NULL', content)
    
    with open(filepath, 'w') as f:
        f.write(content)
    
    print(f"Patched {filepath}")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: patch_lvgl9_fonts.py <font_file.c> [...]")
        sys.exit(1)
    
    for filepath in sys.argv[1:]:
        patch_font(filepath)
