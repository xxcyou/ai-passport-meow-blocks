// main/app/catcube_logic.h —— 「猫猫方块」找猫规则模型(纯 C,不依赖 LVGL/ESP-IDF)。
//
// 规则(生成与游玩共用同一份定义):
//   1. 任意一行、任意一列最多 1 只猫;
//   2. 任意一块四连通同色色块最多 1 只猫(色块数 = 目标猫数,故每块恰好 1 只);
//   3. 任意两只猫八邻域互不接触(上下左右 + 四个斜角);
//   4. 每关唯一解。
//
// 玩法:打叉排除(笔记)、长按确认猫。确认猫会按规则自动排除同行/同列/四周/同色块。
// 关键约束(这版修的两个 bug):
//   * 已经按规则不可能成立的格子(被别的猫排除)不能再被确认成猫 —— 否则同一色块
//     能塞进两只猫,规则形同虚设;
//   * 确认的格子不是真猫 = 猜错,扣一条命;命没了这局重来。
// 所以"找到数"永远不可能超过目标数。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app/catcube_levels.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CATCUBE_MAX_SIDE  14
#define CATCUBE_MAX_CELLS (CATCUBE_MAX_SIDE * CATCUBE_MAX_SIDE)

#define CATCUBE_LIVES 2 // 两滴血:猜错两次这局重来

typedef enum {
    CATCUBE_MARK_NONE = 0,
    CATCUBE_MARK_X,     // 排除(手打或自动),可取消
    CATCUBE_MARK_CAT,   // 已确认是猫
    CATCUBE_MARK_WRONG, // 猜错留下的红叉:不可取消、不可再确认
} catcube_mark_t;

// 长按 OK 的结果。UI 据此决定播什么音、闪什么颜色。
typedef enum {
    CATCUBE_ACT_PLACED = 0, // 确认成功
    CATCUBE_ACT_REMOVED,    // 取消了一只已确认的猫
    CATCUBE_ACT_BLOCKED,    // 该格按规则已不可能,拒绝确认
    CATCUBE_ACT_WRONG,      // 猜错,扣命
    CATCUBE_ACT_LIMIT,      // 已经找齐,不能再加
} catcube_act_t;

typedef enum {
    CATCUBE_CONFLICT_NONE = 0,
    CATCUBE_CONFLICT_REGION = 1u << 0,
    CATCUBE_CONFLICT_ROW = 1u << 1,
    CATCUBE_CONFLICT_COL = 1u << 2,
    CATCUBE_CONFLICT_ADJACENT = 1u << 3,
} catcube_conflict_t;

typedef struct {
    const catcube_level_def_t *level;
    uint8_t cat[CATCUBE_MAX_CELLS];
    uint8_t manual_x[CATCUBE_MAX_CELLS];
    uint8_t wrong_x[CATCUBE_MAX_CELLS]; // 猜错标记(红叉,永久)
    uint8_t auto_cnt[CATCUBE_MAX_CELLS];
    uint8_t answer_bit[CATCUBE_MAX_CELLS]; // 标准答案表(1 = 这格是猫)
    uint8_t found;
    uint8_t lives;
    uint8_t cursor;
    bool auto_exclude;
    bool failed; // 命耗尽或超时:这局结束,只能重来
} catcube_state_t;

void catcube_state_reset(catcube_state_t *st, const catcube_level_def_t *level);

void catcube_set_auto_exclude(catcube_state_t *st, bool on);
bool catcube_auto_exclude(const catcube_state_t *st);

void catcube_move(catcube_state_t *st, int dr, int dc);
uint8_t catcube_cursor(const catcube_state_t *st);
void catcube_cursor_rc(const catcube_state_t *st, uint8_t *row, uint8_t *col);
bool catcube_cursor_set(catcube_state_t *st, uint8_t row, uint8_t col);

// 单击 OK:手动打叉 / 取消。已确认成猫的格子会先取消猫。
void catcube_toggle_x(catcube_state_t *st);

// 长按 OK:确认猫 / 取消,返回动作类型。
catcube_act_t catcube_toggle_cat(catcube_state_t *st);

catcube_mark_t catcube_mark_at(const catcube_state_t *st, uint8_t idx);
catcube_mark_t catcube_mark_rc(const catcube_state_t *st, uint8_t row, uint8_t col);
bool catcube_mark_is_auto(const catcube_state_t *st, uint8_t idx);
bool catcube_is_cat(const catcube_state_t *st, uint8_t idx);
bool catcube_is_answer(const catcube_state_t *st, uint8_t idx);
bool catcube_is_wrong(const catcube_state_t *st, uint8_t idx);

uint8_t catcube_region_at(const catcube_state_t *st, uint8_t row, uint8_t col);

uint32_t catcube_cell_conflict(const catcube_state_t *st, uint8_t idx);
bool catcube_has_conflict(const catcube_state_t *st);

int catcube_target(const catcube_state_t *st);
int catcube_found(const catcube_state_t *st);
int catcube_lives(const catcube_state_t *st);
bool catcube_failed(const catcube_state_t *st);
void catcube_mark_failed(catcube_state_t *st); // 超时等外部原因判负

bool catcube_is_solved(const catcube_state_t *st);

bool catcube_hint(catcube_state_t *st);

// 星级:返回"半星"数量 0..6(6 = 三星)。按剩余生命 + 用时 + 每只猫的平均速度算。
int catcube_stars_half(const catcube_state_t *st, uint32_t elapsed_ms);

// 限时:150 关起才有,越往后越紧(400s → 200s)。0 = 不限时。
uint32_t catcube_time_limit_ms(int level);

bool catcube_answer_valid(const catcube_level_def_t *level);

#ifdef __cplusplus
}
#endif
