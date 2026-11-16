// main/app/catcube_logic.c —— 「猫猫方块」找猫规则模型实现。
//
// 纯计算:不碰 LVGL、不碰 NVS、不分配内存,主机侧 tests/test_catcube_logic.c 直接跑。
// 自动排除每次改动后整体重算(O(n^3) 最坏 14^3 = 2744 次比较),比维护增量便宜,
// 也不会出现"撤销一只猫后残留叉"这类状态不一致。
#include "app/catcube_logic.h"

#include <string.h>

static inline int iabs(int v) { return v < 0 ? -v : v; }

static int cells_of(const catcube_state_t *st) {
    return (st && st->level) ? st->level->w * st->level->h : 0;
}

void catcube_state_reset(catcube_state_t *st, const catcube_level_def_t *level) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    st->level = level;
    st->cursor = 0;
    st->auto_exclude = true;
    st->lives = CATCUBE_LIVES;
    if (!level) return;
    const int cells = level->w * level->h;
    for (int i = 0; i < level->k; i++) {
        const int idx = level->answer[i];
        if (idx >= 0 && idx < cells) st->answer_bit[idx] = 1;
    }
}

bool catcube_auto_exclude(const catcube_state_t *st) { return st ? st->auto_exclude : false; }
int catcube_target(const catcube_state_t *st) { return (st && st->level) ? (int)st->level->k : 0; }
int catcube_found(const catcube_state_t *st) { return st ? (int)st->found : 0; }
int catcube_lives(const catcube_state_t *st) { return st ? (int)st->lives : 0; }
bool catcube_failed(const catcube_state_t *st) { return st ? st->failed : true; }

void catcube_mark_failed(catcube_state_t *st) {
    if (st) st->failed = true;
}

uint8_t catcube_cursor(const catcube_state_t *st) { return st ? st->cursor : 0; }

void catcube_cursor_rc(const catcube_state_t *st, uint8_t *row, uint8_t *col) {
    if (!st || !st->level) {
        if (row) *row = 0;
        if (col) *col = 0;
        return;
    }
    if (row) *row = (uint8_t)(st->cursor / st->level->w);
    if (col) *col = (uint8_t)(st->cursor % st->level->w);
}

bool catcube_cursor_set(catcube_state_t *st, uint8_t row, uint8_t col) {
    if (!st || !st->level) return false;
    if (row >= st->level->h || col >= st->level->w) return false;
    st->cursor = (uint8_t)(row * st->level->w + col);
    return true;
}

void catcube_move(catcube_state_t *st, int dr, int dc) {
    if (!st || !st->level) return;
    const int w = st->level->w;
    const int h = st->level->h;
    int row = st->cursor / w;
    int col = st->cursor % w;
    if (dr) row = ((row + dr) % h + h) % h;
    if (dc) col = ((col + dc) % w + w) % w;
    st->cursor = (uint8_t)(row * w + col);
}

uint8_t catcube_region_at(const catcube_state_t *st, uint8_t row, uint8_t col) {
    if (!st || !st->level) return 0;
    if (row >= st->level->h || col >= st->level->w) return 0;
    return st->level->cells[(int)row * st->level->w + col];
}

static bool cats_conflict(const catcube_state_t *st, int a, int b) {
    const int w = st->level->w;
    const int ra = a / w;
    const int ca = a % w;
    const int rb = b / w;
    const int cb = b % w;
    if (ra == rb || ca == cb) return true;
    if (iabs(ra - rb) <= 1 && iabs(ca - cb) <= 1) return true;
    return st->level->cells[a] == st->level->cells[b];
}

static void recompute_auto(catcube_state_t *st) {
    if (!st || !st->level) return;
    const int cells = cells_of(st);
    memset(st->auto_cnt, 0, sizeof(st->auto_cnt));
    if (!st->auto_exclude) return;
    for (int a = 0; a < cells; a++) {
        if (!st->cat[a]) continue;
        for (int b = 0; b < cells; b++) {
            if (b == a || st->cat[b]) continue;
            if (!cats_conflict(st, a, b)) continue;
            if (st->auto_cnt[b] < 0xFF) st->auto_cnt[b]++;
        }
    }
}

void catcube_set_auto_exclude(catcube_state_t *st, bool on) {
    if (!st || !st->level) return;
    st->auto_exclude = on;
    recompute_auto(st);
}

catcube_mark_t catcube_mark_at(const catcube_state_t *st, uint8_t idx) {
    if (!st || !st->level) return CATCUBE_MARK_NONE;
    if (idx >= cells_of(st)) return CATCUBE_MARK_NONE;
    if (st->cat[idx]) return CATCUBE_MARK_CAT;
    if (st->wrong_x[idx]) return CATCUBE_MARK_WRONG;
    if (st->manual_x[idx] || st->auto_cnt[idx] > 0) return CATCUBE_MARK_X;
    return CATCUBE_MARK_NONE;
}

catcube_mark_t catcube_mark_rc(const catcube_state_t *st, uint8_t row, uint8_t col) {
    if (!st || !st->level) return CATCUBE_MARK_NONE;
    if (row >= st->level->h || col >= st->level->w) return CATCUBE_MARK_NONE;
    return catcube_mark_at(st, (uint8_t)((int)row * st->level->w + col));
}

bool catcube_mark_is_auto(const catcube_state_t *st, uint8_t idx) {
    if (!st || !st->level || idx >= cells_of(st)) return false;
    if (st->cat[idx] || st->manual_x[idx]) return false;
    return st->auto_cnt[idx] > 0;
}

bool catcube_is_cat(const catcube_state_t *st, uint8_t idx) {
    if (!st || !st->level || idx >= cells_of(st)) return false;
    return st->cat[idx] != 0;
}

bool catcube_is_wrong(const catcube_state_t *st, uint8_t idx) {
    if (!st || !st->level || idx >= cells_of(st)) return false;
    return st->wrong_x[idx] != 0;
}

bool catcube_is_answer(const catcube_state_t *st, uint8_t idx) {
    if (!st || !st->level || idx >= cells_of(st)) return false;
    return st->answer_bit[idx] != 0;
}

static void clear_cat(catcube_state_t *st, uint8_t idx) {
    if (!st->cat[idx]) return;
    st->cat[idx] = 0;
    if (st->found > 0) st->found--;
    recompute_auto(st);
}

static void set_cat(catcube_state_t *st, uint8_t idx) {
    if (st->cat[idx]) return;
    st->cat[idx] = 1;
    st->manual_x[idx] = 0;
    st->found++;
    recompute_auto(st);
}

void catcube_toggle_x(catcube_state_t *st) {
    if (!st || !st->level) return;
    const uint8_t idx = st->cursor;
    if (st->wrong_x[idx]) return; // 红叉永久,不响应
    if (st->cat[idx]) {
        clear_cat(st, idx);
        return;
    }
    st->manual_x[idx] = st->manual_x[idx] ? 0 : 1;
}

catcube_act_t catcube_toggle_cat(catcube_state_t *st) {
    if (!st || !st->level) return CATCUBE_ACT_BLOCKED;
    const uint8_t idx = st->cursor;

    if (st->cat[idx]) { // 取消已确认的猫
        clear_cat(st, idx);
        return CATCUBE_ACT_REMOVED;
    }
    if (st->wrong_x[idx]) return CATCUBE_ACT_BLOCKED; // 猜错的格子不能再确认
    if (st->failed) return CATCUBE_ACT_BLOCKED;
    // 规则上已经不可能的格子不许再确认:否则同一色块/同一行能塞进第二只猫。
    if (st->auto_cnt[idx] > 0) return CATCUBE_ACT_BLOCKED;
    if ((int)st->found >= (int)st->level->k) return CATCUBE_ACT_LIMIT;

    if (!st->answer_bit[idx]) { // 猜错:红叉(不可取消)+ 扣命
        st->wrong_x[idx] = 1;
        if (st->lives > 0) st->lives--;
        if (st->lives == 0) st->failed = true;
        return CATCUBE_ACT_WRONG;
    }
    set_cat(st, idx);
    return CATCUBE_ACT_PLACED;
}

static uint32_t pair_conflict(const catcube_state_t *st, int a, int b) {
    const int w = st->level->w;
    const int ra = a / w;
    const int ca = a % w;
    const int rb = b / w;
    const int cb = b % w;
    uint32_t bits = CATCUBE_CONFLICT_NONE;
    if (ra == rb) bits |= CATCUBE_CONFLICT_ROW;
    if (ca == cb) bits |= CATCUBE_CONFLICT_COL;
    if (catcube_region_at(st, (uint8_t)ra, (uint8_t)ca) ==
        catcube_region_at(st, (uint8_t)rb, (uint8_t)cb)) {
        bits |= CATCUBE_CONFLICT_REGION;
    }
    if (iabs(ra - rb) <= 1 && iabs(ca - cb) <= 1) bits |= CATCUBE_CONFLICT_ADJACENT;
    return bits;
}

uint32_t catcube_cell_conflict(const catcube_state_t *st, uint8_t idx) {
    if (!st || !st->level) return CATCUBE_CONFLICT_NONE;
    if (!st->cat[idx]) return CATCUBE_CONFLICT_NONE;
    const int cells = cells_of(st);
    uint32_t bits = CATCUBE_CONFLICT_NONE;
    for (int other = 0; other < cells; other++) {
        if (other == (int)idx || !st->cat[other]) continue;
        bits |= pair_conflict(st, (int)idx, other);
    }
    return bits;
}

bool catcube_has_conflict(const catcube_state_t *st) {
    if (!st || !st->level) return false;
    const int cells = cells_of(st);
    for (int i = 0; i < cells; i++) {
        if (catcube_cell_conflict(st, (uint8_t)i) != CATCUBE_CONFLICT_NONE) return true;
    }
    return false;
}

bool catcube_is_solved(const catcube_state_t *st) {
    if (!st || !st->level) return false;
    const int cells = cells_of(st);
    for (int i = 0; i < cells; i++) {
        if ((st->cat[i] != 0) != (st->answer_bit[i] != 0)) return false;
    }
    return true;
}

bool catcube_hint(catcube_state_t *st) {
    if (!st || !st->level || st->failed) return false;
    for (int i = 0; i < st->level->k; i++) {
        const uint8_t idx = st->level->answer[i];
        if (st->cat[idx]) continue;
        st->cursor = idx;
        set_cat(st, idx);
        return true;
    }
    return false;
}

int catcube_stars_half(const catcube_state_t *st, uint32_t elapsed_ms) {
    if (!st || !st->level) return 0;
    int half = 6; // 满星 3 颗
    // 每条命 1 个半星(2 条命全丢 = 掉一整颗星)
    half -= (CATCUBE_LIVES - (int)st->lives);
    // 用时:每只猫的基准 5s + 起步 4s;超过 1.5 倍扣半星,超过 2 倍再扣半星
    const uint32_t par = 4000u + (uint32_t)st->level->k * 5000u;
    if (elapsed_ms > par * 2u) {
        half -= 2;
    } else if (elapsed_ms > par + par / 2u) {
        half -= 1;
    }
    if (half < 1) half = 1;
    if (half > 6) half = 6;
    return half;
}

uint32_t catcube_time_limit_ms(int level) {
    if (level < 150) return 0;
    if (level < 200) return 400000u;
    if (level < 250) return 300000u;
    if (level < 300) return 240000u;
    return 200000u;
}

bool catcube_region_has_cat(const catcube_state_t *st, uint8_t region) {
    if (!st || !st->level) return false;
    const int cells = cells_of(st);
    for (int i = 0; i < cells; i++) {
        if (st->level->cells[i] != region) continue;
        if (st->cat[i]) return true;
    }
    return false;
}

bool catcube_region_exhausted(const catcube_state_t *st, uint8_t region) {
    if (!st || !st->level) return false;
    if (catcube_region_has_cat(st, region)) return false;
    const int cells = cells_of(st);
    bool any = false;
    for (int i = 0; i < cells; i++) {
        if (st->level->cells[i] != region) continue;
        any = true;
        if (catcube_mark_at(st, (uint8_t)i) == CATCUBE_MARK_NONE) return false;
    }
    return any;
}

bool catcube_answer_valid(const catcube_level_def_t *level) {
    if (!level || !level->cells || !level->answer) return false;
    if (level->w == 0 || level->h == 0 || level->w > CATCUBE_MAX_SIDE ||
        level->h > CATCUBE_MAX_SIDE) {
        return false;
    }
    if (level->k == 0 || level->k > level->w || level->k > level->h) return false;

    uint8_t region_seen[CATCUBE_MAX_SIDE] = {0};
    uint8_t row_used[CATCUBE_MAX_SIDE] = {0};
    uint8_t col_used[CATCUBE_MAX_SIDE] = {0};
    int rows[CATCUBE_MAX_SIDE];
    int cols[CATCUBE_MAX_SIDE];

    for (int i = 0; i < level->k; i++) {
        const int idx = level->answer[i];
        if (idx < 0 || idx >= level->w * level->h) return false;
        const int r = idx / level->w;
        const int c = idx % level->w;
        const uint8_t region = level->cells[idx];
        if (region >= level->k) return false;
        if (region_seen[region]) return false;
        region_seen[region] = 1;
        if (row_used[r] || col_used[c]) return false;
        row_used[r] = 1;
        col_used[c] = 1;
        rows[i] = r;
        cols[i] = c;
    }
    for (int i = 0; i < level->k; i++) {
        for (int j = i + 1; j < level->k; j++) {
            if (iabs(rows[i] - rows[j]) <= 1 && iabs(cols[i] - cols[j]) <= 1) return false;
        }
    }
    for (int i = 0; i < level->w * level->h; i++) {
        if (level->cells[i] >= level->k) return false;
    }
    return true;
}
