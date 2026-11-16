// main/app/catcube_ui.h —— 「猫猫方块」界面层:关卡列表、自绘棋盘、通关演出、结算。
//
// 调用约定:所有函数都必须在【持 bsp_lvgl_lock()】的上下文里调用 —— 界面层自己
// 不加锁,只保证"一次调用内不跨任务"。
//
// 页面:
//   关卡列表:只显示已通关的关 + 当前这一关(未通过的关不出现),UP=向右 DOWN=向下;
//   游戏页  :顶部生命/计时/找到数,中间自绘棋盘(像素猫、像素叉、光标),底部提示;
//   浮层    :菜单 / 玩法 / 通关演出+结算 / 失败。
#pragma once

#include "bsp_button.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 进度钩子。关卡号一律 1 起。
typedef struct {
    bool (*is_cleared)(int level);
    int (*highest_unlocked)(void); // 当前可玩的关号(= 已通关数 + 1)
    void (*mark_cleared)(int level);
    // 音量/静音/重置进度:由应用层落 NVS,界面层只负责交互
    int (*volume_get)(void);
    void (*volume_set)(int percent);
    bool (*muted_get)(void);
    void (*muted_set)(bool muted);
    void (*reset_progress)(void);
} catcube_ui_hooks_t;

void catcube_ui_init(const catcube_ui_hooks_t *hooks);
void catcube_ui_show_levels(void);
void catcube_ui_show_play(int level);
void catcube_ui_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);
void catcube_ui_tick(void);

// 计时:由输入任务每秒推一次。limit_ms = 0 表示本关不限时。
void catcube_ui_set_clock(uint32_t elapsed_ms, uint32_t limit_ms);

// 当前所在关卡号(1 起);不在游戏页返回 0。
int catcube_ui_current_level(void);

// 每次进入一关都会 +1,输入任务据此重置本关计时。
uint32_t catcube_ui_level_serial(void);

// 计时是否在走(新手教学弹窗打开时暂停,避免读规则被判超时/扣星)。
bool catcube_ui_clock_running(void);

// 通关演出是否在跑(在跑时输入任务要用更短的轮询间隔驱动动画)。
bool catcube_ui_anim_active(void);
void catcube_ui_tick_anim(uint32_t dt_ms);

void catcube_ui_deinit(void);

#ifdef __cplusplus
}
#endif
