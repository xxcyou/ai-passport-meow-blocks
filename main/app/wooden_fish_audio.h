// main/app/wooden_fish_audio.h —— 敲击音的播放任务。
//
// 播放会阻塞(I2S 写满 DMA 才返回),所以永远不在按键回调或 LVGL 任务里做,
// 而是丢给这个工作队列。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// 初始化 codec/I2S 并启动播放任务。可重复调用。
esp_err_t wf_audio_start(void);

// 停止任务并释放队列。停止后仍可再次 start。
esp_err_t wf_audio_stop(void);

// 请求播放一次敲击音。非阻塞;队列满时丢弃本次请求(宁可少一声,不要卡住按键)。
void wf_audio_knock(uint8_t preset, uint32_t combo);

// 静音开关。静音时仍然消费请求,只是不写 I2S。
void wf_audio_set_muted(bool muted);
