// main/app/catcube_audio.h —— 极简音效:合成短音,不占素材空间。
//
// 播放会阻塞(I2S 写满 DMA 才返回),所以永远不在按键回调或 LVGL 任务里做,
// 统一丢给工作队列。队列满就丢请求 —— 宁可少一声,不能卡住按键。
#pragma once

#include <stdbool.h>

typedef enum {
    CAT_SND_MOVE = 0, // 方向键
    CAT_SND_X,        // 打叉 / 自动排除
    CAT_SND_CAT,      // 确认一只猫
    CAT_SND_SWEEP,    // 确认猫后自动排除的连击
    CAT_SND_ALL,      // 找齐全部猫
    CAT_SND_SLOT,     // 猫头落进进度条
    CAT_SND_WIN,      // 胜利
    CAT_SND_WRONG,    // 猜错扣命
    CAT_SND_COUNT,
} cat_snd_t;

// 初始化 codec/I2S 并启动播放任务。可重复调用;失败返回 false,应用静音降级。
bool cat_audio_init(void);
void cat_audio_deinit(void);

// 非阻塞播放。steps 只对 CAT_SND_SWEEP 有意义(自动排除了几个格子)。
void cat_audio_play(cat_snd_t snd, int steps);

void cat_audio_set_muted(bool muted);
bool cat_audio_muted(void);
