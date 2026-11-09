// main/app/wooden_fish.h —— 功德木鱼应用入口。
#pragma once

// 内部完成:NVS 读取、界面创建与首帧刷新、音频任务、按键事件任务。
#include "bsp_button.h"
#include "esp_err.h"

// 在显示、LVGL 就绪之后调用一次:读存档、建界面、起音频与输入任务。
// 按键回调不在这里注册 —— 由 main.c 统一注册并做唤醒键守卫。
esp_err_t wooden_fish_start(void);

// 按键事件入口:只入队,永不阻塞。可在 esp_timer 任务里安全调用。
esp_err_t wooden_fish_handle_button(bsp_btn_t btn, bsp_btn_ev_t ev);

// 停止应用:释放界面对象、按键队列、音频任务。仅用于有序关闭/自检。
void wooden_fish_stop(void);
