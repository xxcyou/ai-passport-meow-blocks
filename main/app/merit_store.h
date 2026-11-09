// main/app/merit_store.h —— 功德数据的 NVS 持久化。
//
// 只存"值得跨重启保留"的四项:累计功德、最高连击、音色、静音。
// 本次开机的计数属于会话状态,不落盘。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t total;
    uint32_t best_combo;
    uint8_t preset;
    bool muted;
} wf_saved_t;

// 初始化 NVS 分区。已初始化或首次擦除都会正确处理。
esp_err_t wf_store_init(void);

// 读取已保存状态;没有记录或读取失败时返回 false 并保持 out 不变。
bool wf_store_load(wf_saved_t *out);

// 写入状态。写失败(例如 Flash 满)返回错误,调用方应保留 dirty 标记重试。
esp_err_t wf_store_save(const wf_saved_t *in);
