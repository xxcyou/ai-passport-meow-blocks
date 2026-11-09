// main/app/app_fonts.h —— 应用字体:生成的 CJK 子集 + Montserrat 回退。
//
// 生成的 app_cjk_16/24 只含中文字形(见 tools/gen_app_fonts.py)。ASCII、数字和
// LV_SYMBOL_* 由 LVGL 内置 Montserrat 通过 fallback 提供,因此这里持有一份
// 可写的字体描述符副本 —— 直接改 const 的 Flash 字体对象会踩到只读内存。
#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 在创建任何控件之前调用一次(需在 LVGL 初始化之后、持锁状态下)。
void app_fonts_init(void);

// 正文 16px(标签、提示),标题 24px(页面标题、统计行)。
const lv_font_t *app_font_body(void);
const lv_font_t *app_font_title(void);

// 大号数字:直接用内置 Montserrat 48,数字与 ASCII 覆盖由 LVGL 保证。
const lv_font_t *app_font_number(void);

// 逐码点检查字体链覆盖,返回缺失码点数;缺失项以 U+XXXX 记入日志。
// 用于开机自检:让"中文变方框"在串口日志里立刻可见,而不是等人盯屏幕。
size_t app_fonts_check_coverage(const char *const *strings, size_t count);

// 单码点覆盖判断(含 fallback 链)。is_placeholder 不算覆盖。
bool app_font_has_glyph(const lv_font_t *font, uint32_t codepoint);
