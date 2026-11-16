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

// ---------------------------------------------------------------------------
// PMKID 猎人(抓包应用)
// 注意:本文件的字符集会整份进入生成的 CJK 子集字库,新增文案后必须重新
// 运行 tools/gen_app_fonts.py,否则上板就是方框。
// ---------------------------------------------------------------------------
#define PMKID_APP_TITLE      "PMKID 猎人"
#define PMKID_SCAN_TITLE     "选择热点"
#define PMKID_SCANNING       "正在扫描附近热点"
#define PMKID_SCAN_EMPTY     "没有发现热点"
#define PMKID_HIDDEN_AP      "隐藏网络"
#define PMKID_SELECT_HINT    "UP/DOWN 选择  OK 抓包"
#define PMKID_CAPTURE_TITLE  "抓包中"
#define PMKID_ASSOC_LABEL    "关联请求"
#define PMKID_M1_LABEL       "AP 回应"
#define PMKID_TIME_LABEL     "用时"
#define PMKID_WAIT_M1        "请求 PMKID 中"
#define PMKID_GOT_PMKID      "已捕获 PMKID"
#define PMKID_NO_PMKID       "AP 未返回 PMKID"
#define PMKID_INJECT_OK      "注入 可用"
#define PMKID_INJECT_LIMITED "注入受限 改走关联"
#define PMKID_CAPTURE_HINT   "长按 OK 取消"
#define PMKID_RESULT_TITLE   "抓到 PMKID"
#define PMKID_RESULT_EAPOL   "未返回 PMKID"
#define PMKID_RESULT_HINT    "OK 开启热点"
#define PMKID_RESULT_BACK    "长按 OK 重扫"
#define PMKID_FAIL_TITLE     "没有抓到数据"
#define PMKID_FAIL_HINT      "OK 重试  长按 OK 返回"
#define PMKID_PORTAL_TITLE   "热点已开启"
#define PMKID_PORTAL_SSID    "热点"
#define PMKID_PORTAL_PASS    "密码"
#define PMKID_PORTAL_IPHINT  "手机连上后浏览器打开"
#define PMKID_PORTAL_DL      "页面里可下载 PMKID 数据包"
#define PMKID_PORTAL_HINT    "长按 OK 关闭热点"
#define PMKID_NOTE_PMKID     "PMKID 已写入 hc22000 文件"
#define PMKID_NOTE_EAPOL     "AP 的 M1 里没有 PMKID"
#define PMKID_NOTE_WIFI_FAIL "Wi-Fi 启动失败"
#define PMKID_NOTE_TIMEOUT   "没有收到 AP 的 M1 回应"
#define PMKID_NOTE_SNIFFER   "AP 已握手但抓不到 M1"
#define PMKID_NOTE_HIDDEN    "隐藏网络无法主动关联"
#define PMKID_DIAG_FMT       "关联 %lu 次 原因 %u"
#define PMKID_DIAG_ERR_FMT   "关联 %lu 次 错误 %d"

// ---------------------------------------------------------------------------
// 喵喵方块(找猫解谜)
// 同样:新增/改动文案后必须重新运行 tools/gen_app_fonts.py 生成子集字库。
// 约束:只用 Droid Sans Fallback 有的汉字与 ASCII;不要引入 ·—… 这类符号。
// ---------------------------------------------------------------------------
#define CAT_APP_TITLE       "喵喵方块"
#define CAT_SELECT_TITLE    "选择关卡"
#define CAT_PROGRESS_FMT    "已通关 %d 关"
#define CAT_LEVEL_FMT       "第 %d 关"
#define CAT_SIZE_FMT        "%d x %d"
#define CAT_CATS_FMT        "找到 %d/%d"
#define CAT_HINT_MOVE       "下键上下 上键左右"
#define CAT_HINT_MARK       "OK 打叉 双击OK 认猫"
#define CAT_HINT_MENU       "长按OK 打开菜单"
#define CAT_HINT_BACK       "长按OK 返回"
#define CAT_MENU_TITLE      "菜单"
#define CAT_MENU_RESUME     "继续这一关"
#define CAT_MENU_RESTART    "重开本关"
#define CAT_MENU_HINT       "提示一只猫"
#define CAT_MENU_RULES      "玩法说明"
#define CAT_MENU_SETTINGS   "设置"
#define CAT_MENU_BACK       "返回"
#define CAT_MENU_VOLUME     "音量控制"
#define CAT_MENU_MUTE_ON    "静音 开"
#define CAT_MENU_MUTE_OFF   "静音 关"
#define CAT_MENU_STANDBY_OFF "待机 关闭"
#define CAT_MENU_STANDBY_FMT "待机 %d 分钟"
#define CAT_MENU_RESET      "重置进度"
#define CAT_MENU_RESET_ARM  "再按一次确认"
#define CAT_VOL_TITLE       "音量"
#define CAT_VOL_HINT        "上 音量+ 下 音量-"
#define CAT_VOL_BACK        "长按 OK 返回菜单"
#define CAT_MENU_AUTO_ON    "自动排除 开"
#define CAT_MENU_AUTO_OFF   "自动排除 关"
#define CAT_MENU_LIST       "回到关卡列表"
#define CAT_WIN_TITLE       "通关"
#define CAT_WIN_NEXT        "下一关"
#define CAT_WIN_RETRY       "重试"
#define CAT_WIN_LIST        "关卡列表"
#define CAT_FAIL_TITLE      "失败"
#define CAT_FAIL_LIVES      "爱心用完了 重来一次"
#define CAT_FAIL_TIME       "时间到 重来一次"
#define CAT_HINT_PLACED     "提示:已确认一只猫"
#define CAT_TOAST_REGION    "这个颜色还没有找到猫"
#define CAT_HINT_DONE       "已经找齐了"
#define CAT_BATTERY_NA      "--"

// 新手教学(前 5 关,带示意图)
#define CAT_TUT_TITLE       "玩法说明"
#define CAT_TUT_1           "每行每列最多一只猫"
#define CAT_TUT_2           "每块同色区域最多一只猫"
#define CAT_TUT_3           "两只猫不能挨着 斜对角也不行"
#define CAT_TUT_4           "短按打叉 双击认猫 长按菜单"
#define CAT_TUT_START       "OK 开始"
#define CAT_TUT_BACK        "OK 返回"
