// main/app/app_strings.h —— 应用界面全部中文文案的唯一出处。
//
// 为什么集中放一处:中文字形是"字体集成"问题,不是翻译问题。文案散落在各个
// .c 文件里时,加一个字很容易忘记重新生成字库,最后上板才看到方框。
// tools/gen_app_fonts.py 直接扫描本文件的字符串字面量生成子集字库,
// tests/test_app_font_coverage.py 再回读生成结果做逐码点校验。
//
// 约定:ASCII 由 LVGL 内置 Montserrat 经 fallback 提供,本文件的 CJK 字符
// 由 assets/fonts/app_cjk_*.c 提供。不要使用 Droid Sans Fallback 没有的符号
// (例如 U+00B7 ·、U+2014 —、U+2026 …),它们会变成缺字方框。
#pragma once

// 标题与计数
#define APP_TITLE "功德木鱼"
#define APP_UNIT "功德"
#define APP_SESSION_LABEL "本次"
#define APP_TOTAL_LABEL "累计"
#define APP_BEST_COMBO_LABEL "最高连击"
#define APP_COMBO_PREFIX "连击"

// 操作提示(分两行,避免为分隔符引入字库里没有的码点)
#define APP_HINT_KNOCK "OK 敲击"
#define APP_HINT_RESET "长按归零"
#define APP_HINT_STATS "长按 UP 看功德簿"
#define APP_HINT_BACK "长按 OK 返回"

// 功德簿(统计页)
#define APP_STATS_TITLE "功德簿"
#define APP_SOUND_LABEL "音色"
#define APP_MUTE_LABEL "声音"
#define APP_ON "开"
#define APP_OFF "关"
#define APP_MUTED "静音"

// 音色名
#define APP_SOUND_WOOD "木鱼"
#define APP_SOUND_BELL "铜铃"
#define APP_SOUND_DRUM "木鼓"

// 归零确认
#define APP_RESET_TITLE "归零功德？"
#define APP_RESET_CONFIRM "OK 确认"
#define APP_RESET_CANCEL "UP 取消"

// 电量不可用时的占位符(ASCII,来自 Montserrat)
#define APP_BATTERY_UNKNOWN "--"
