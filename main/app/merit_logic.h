// main/app/merit_logic.h —— 功德计数与连击状态机(纯逻辑,不依赖 ESP-IDF / LVGL)。
//
// 为什么单独抽出来:计数上限、连击窗口、清零语义都是"用久了才会出错"的地方,
// 放在 UI 文件里只能靠上板试。这里做成纯 C,主机侧直接跑边界用例。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 连击判定窗口:两次敲击间隔小于该值算连击。1500ms 对应"手速稍慢也算连击"的手感。
#define MERIT_COMBO_WINDOW_MS 1500u

// 计数上限。8 位数字在 240px 宽、48px 字号下刚好放得下;到顶后不再增长,
// 避免 32 位回绕把天文数字摔回 0。
#define MERIT_COUNT_MAX 99999999u

// 连击上限,防止 UI 上的 "xNNN" 撑破布局。
#define MERIT_COMBO_MAX 999u

typedef struct {
    uint32_t total;        // 累计功德(含本次开机)
    uint32_t session;      // 本次开机功德
    uint32_t best_combo;   // 历史最高连击
    uint16_t combo;        // 当前连击
    uint32_t last_hit_ms;  // 上一次敲击的时间戳(调用方提供单调时钟)
    bool combo_active;     // combo 是否仍在窗口内
} merit_state_t;

// 用已持久化的数据初始化状态;超出上限的值会被夹到上限。
void merit_init(merit_state_t *state, uint32_t total, uint32_t best_combo);

// 记录一次敲击,返回本次敲击后的连击数(1 表示重新开始)。
uint32_t merit_hit(merit_state_t *state, uint32_t now_ms);

// 推进时间:连击窗口超时则结束连击。可被 UI 定时器高频调用。
void merit_tick(merit_state_t *state, uint32_t now_ms);

// 清零功德与连击,但保留历史最高连击(那是"曾经做到过"的记录)。
void merit_reset(merit_state_t *state);

// 是否还有连击可展示(combo >= 2 且未超时)。
bool merit_combo_visible(const merit_state_t *state);
