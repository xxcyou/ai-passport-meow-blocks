// main/app/app_fonts.c —— 字体链装配与逐码点覆盖自检。
#include "app/app_fonts.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "app_fonts";

LV_FONT_DECLARE(app_cjk_16);
LV_FONT_DECLARE(app_cjk_24);

// 可写副本:只在 init 里赋值一次,之后只读使用。
static lv_font_t s_body;
static lv_font_t s_title;
static bool s_ready;

void app_fonts_init(void) {
    if (s_ready) return;

    // 浅拷贝生成字体(静态位图字体,数据在 Flash,副本只多一个描述符)。
    s_body = app_cjk_16;
    s_body.fallback = &lv_font_montserrat_16;

    s_title = app_cjk_24;
    s_title.fallback = &lv_font_montserrat_24;

    s_ready = true;
}

const lv_font_t *app_font_body(void) { return s_ready ? &s_body : &lv_font_montserrat_16; }
const lv_font_t *app_font_title(void) { return s_ready ? &s_title : &lv_font_montserrat_24; }
const lv_font_t *app_font_number(void) { return &lv_font_montserrat_48; }

bool app_font_has_glyph(const lv_font_t *font, uint32_t codepoint) {
    if (!font) return false;
    lv_font_glyph_dsc_t glyph = {0};
    // lv_font_get_glyph_dsc 会沿 fallback 链查找;占位符不算覆盖。
    return lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) && !glyph.is_placeholder;
}

// 最小 UTF-8 解码:返回下一个码点并推进 *cursor;非法序列按 U+FFFD 处理并前进 1 字节。
static uint32_t utf8_next(const char **cursor) {
    const unsigned char *p = (const unsigned char *)*cursor;
    if (*p == 0) return 0;

    uint32_t cp;
    size_t extra;
    if (p[0] < 0x80) {
        cp = p[0];
        extra = 0;
    } else if ((p[0] & 0xE0) == 0xC0) {
        cp = p[0] & 0x1F;
        extra = 1;
    } else if ((p[0] & 0xF0) == 0xE0) {
        cp = p[0] & 0x0F;
        extra = 2;
    } else if ((p[0] & 0xF8) == 0xF0) {
        cp = p[0] & 0x07;
        extra = 3;
    } else {
        *cursor += 1;
        return 0xFFFD;
    }

    for (size_t i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *cursor += 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *cursor += extra + 1;
    return cp;
}

size_t app_fonts_check_coverage(const char *const *strings, size_t count) {
    size_t missing = 0;
    for (size_t i = 0; i < count; i++) {
        const char *cursor = strings[i];
        if (!cursor) continue;
        while (*cursor) {
            uint32_t cp = utf8_next(&cursor);
            if (cp == 0 || cp == '\n' || cp == '\r' || cp == '\t') continue;
            if (!app_font_has_glyph(app_font_body(), cp) ||
                !app_font_has_glyph(app_font_title(), cp)) {
                ESP_LOGW(TAG, "缺字 U+%04lX in \"%s\"", (unsigned long)cp, strings[i]);
                missing++;
            }
        }
    }
    if (missing == 0) {
        ESP_LOGI(TAG, "字体覆盖自检通过:%u 条文案", (unsigned)count);
    } else {
        ESP_LOGE(TAG, "字体覆盖自检失败:缺 %u 个字形", (unsigned)missing);
    }
    return missing;
}
