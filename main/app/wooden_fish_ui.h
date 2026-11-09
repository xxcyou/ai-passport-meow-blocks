// main/app/wooden_fish_ui.h —— 功德木鱼的界面层(只做 LVGL 绘制与动画)。
//
// 界面约定:调用者必须持有 bsp_lvgl_lock();本层不碰按键队列、NVS 和音频。
#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

// 一次刷新需要的全部显示数据。
typedef struct {
    uint32_t total;
    uint32_t session;
    uint32_t best_combo;
    uint8_t preset;   // 0=木鱼 1=铜铃 2=木鼓
    bool muted;
    int battery_soc;  // 0..100;负值表示读不到
} wf_view_t;

// 创建两个页面与动画定时器。重复调用是幂等的。
void wf_ui_init(void);

// 释放全部对象与定时器。之后再调用 wf_ui_init 可重建。
void wf_ui_destroy(void);

// 页面切换。
void wf_ui_show_knock(void);
void wf_ui_show_stats(void);

// 归零确认浮层。
void wf_ui_set_confirm(bool visible);

// 用最新状态刷新当前可见控件(计数、统计行、电量、静音标记)。
void wf_ui_refresh(const wf_view_t *view);

// 敲击反馈动画:木鱼弹一下、光晕扩散、+1 上浮、数字跳动。
void wf_ui_play_hit(uint32_t combo);
