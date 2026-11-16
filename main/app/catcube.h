// main/app/catcube.h —— 「猫猫方块」应用入口。
//
// 内部完成:NVS 进度读取、界面创建与首帧刷新、按键事件任务、电量周期刷新。
#pragma once

#include "bsp_button.h"
#include "esp_err.h"

// 在显示、LVGL、字体就绪之后调用一次。
esp_err_t catcube_start(void);

// 按键事件入口:只入队,永不阻塞。可在 esp_timer 任务里安全调用。
esp_err_t catcube_handle_button(bsp_btn_t btn, bsp_btn_ev_t ev);

// 停止应用:删除界面对象、按键队列与输入任务。仅用于有序关闭/自检。
void catcube_stop(void);
