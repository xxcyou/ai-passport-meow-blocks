// main/app/catcube_gen.c —— 无尽模式的确定性关卡生成器。
//
// 设计要点:
//   * 随机性只来自关号:seed = hash(level, attempt),同号必得同图(重置游戏、
//     重刷固件、换机器都一样);
//   * 所有搜索都有节点预算,预算用尽就换下一个确定性 attempt,绝不无限跑;
//   * 只用静态数组,不 malloc —— C3 没有 PSRAM,生成过程不能碰堆。
#include "catcube_gen.h"

#include <string.h>

#define GEN_ATTEMPTS        16
#define GEN_MAX_MIGRATIONS  900
#define GEN_COUNT_BUDGET    2000000u  // 单次枚举的节点预算
#define GEN_TOTAL_BUDGET    12000000u // 单关生成的节点总预算
#define GEN_FRONTIER_MAX    2048

// ---------------------------------------------------------------------------
// 确定性随机数:xorshift32,种子由关号和 attempt 混合
// ---------------------------------------------------------------------------
static uint32_t rng_hash(uint32_t level, uint32_t attempt) {
    uint32_t x = level * 2654435761u ^ (attempt + 1u) * 2246822519u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x ? x : 0x9E3779B9u;
}

static uint32_t rng_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int rng_below(uint32_t *state, int bound) {
    return bound <= 1 ? 0 : (int)(rng_next(state) % (uint32_t)bound);
}

// ---------------------------------------------------------------------------
// 求解器:MRV 选行 + 前向检查(列/色块/八邻域都用位掩码提前删掉)
//
// "按行推进、只剪当前行"的朴素写法在 10x10 以上会炸:每层都要把整行候选试一遍。
// 这里给每行维护"还剩哪些列可行",放一只猫就立刻把同列、相邻行的相邻列、同色块的
// 所有列从其它行删掉,再挑候选最少的行继续 —— 唯一解证明从秒级降到毫秒级。
// ---------------------------------------------------------------------------
typedef struct {
    const catcube_level_def_t *lv;
    int cap;
    int found;
    uint32_t nodes;
    uint32_t budget;
    bool budget_hit;
    uint8_t sol[2][CATCUBE_GEN_MAX_CELLS];
    uint8_t sol_len[2];
} gen_solver_t;

typedef struct {
    uint32_t row_mask[CATCUBE_GEN_MAX_SIDE];
    uint32_t placed_rows;
    uint32_t col_used;
    uint32_t region_used;
    uint8_t chosen[CATCUBE_GEN_MAX_SIDE];
} gen_state_t;

static int bit_count(uint32_t v) {
    int n = 0;
    while (v) {
        v &= v - 1;
        n++;
    }
    return n;
}

static int low_bit(uint32_t v) {
    int i = 0;
    while (!(v & 1u)) {
        v >>= 1;
        i++;
    }
    return i;
}

static void solve_rec(gen_solver_t *s, gen_state_t *st, int placed) {
    if (s->found >= s->cap || s->budget_hit) return;
    if (++s->nodes > s->budget) {
        s->budget_hit = true;
        return;
    }
    const catcube_level_def_t *lv = s->lv;
    if (placed == lv->k) {
        int n = 0;
        for (int r = 0; r < lv->h; r++) {
            if (!(st->placed_rows & (1u << r))) continue;
            s->sol[s->found][n++] = (uint8_t)(r * lv->w + st->chosen[r]);
        }
        s->sol_len[s->found] = (uint8_t)n;
        s->found++;
        return;
    }

    // MRV:挑候选最少的未放行;有 0 候选直接回溯。
    int best = -1;
    int best_cnt = 1000;
    for (int r = 0; r < lv->h; r++) {
        if (st->placed_rows & (1u << r)) continue;
        const int cnt = bit_count(st->row_mask[r]);
        if (cnt == 0) return;
        if (cnt < best_cnt) {
            best_cnt = cnt;
            best = r;
            if (cnt == 1) break;
        }
    }
    if (best < 0) return;

    uint32_t mask = st->row_mask[best];
    while (mask) {
        const int c = low_bit(mask);
        mask &= mask - 1;

        gen_state_t ns = *st;
        ns.placed_rows |= 1u << best;
        ns.col_used |= 1u << c;
        const uint8_t region = lv->cells[best * lv->w + c];
        ns.region_used |= 1u << region;
        ns.chosen[best] = (uint8_t)c;
        ns.row_mask[best] = 0;

        const uint32_t col_bit = 1u << c;
        const uint32_t adj_bits = col_bit | (c > 0 ? (1u << (c - 1)) : 0u) |
                                  (c + 1 < lv->w ? (1u << (c + 1)) : 0u);
        for (int r = 0; r < lv->h; r++) {
            if (ns.placed_rows & (1u << r)) continue;
            uint32_t m = ns.row_mask[r] & ~col_bit; // 同列不可能
            if (r + 1 == best || r == best + 1) m &= ~adj_bits; // 八邻域
            for (int cc = 0; cc < lv->w; cc++) { // 同色块
                if (lv->cells[r * lv->w + cc] == region) m &= ~(1u << cc);
            }
            ns.row_mask[r] = m;
        }

        solve_rec(s, &ns, placed + 1);
        if (s->found >= s->cap || s->budget_hit) return;
    }
}

static void solve_all(gen_solver_t *s) {
    gen_state_t st;
    memset(&st, 0, sizeof(st));
    const uint32_t all = (s->lv->w >= 32) ? 0xFFFFFFFFu : ((1u << s->lv->w) - 1u);
    for (int r = 0; r < s->lv->h; r++) st.row_mask[r] = all;
    solve_rec(s, &st, 0);
}

int catcube_gen_count_solutions(const catcube_level_def_t *lv, int cap, uint32_t node_budget,
                                uint32_t *nodes_used, bool *budget_hit) {
    if (nodes_used) *nodes_used = 0;
    if (budget_hit) *budget_hit = false;
    if (!lv || !lv->cells || cap <= 0) return 0;

    gen_solver_t s;
    memset(&s, 0, sizeof(s));
    s.lv = lv;
    s.cap = cap > 2 ? 2 : cap;
    s.budget = node_budget ? node_budget : GEN_COUNT_BUDGET;
    solve_all(&s);
    if (nodes_used) *nodes_used = s.nodes;
    if (budget_hit) *budget_hit = s.budget_hit;
    return s.found;
}

// ---------------------------------------------------------------------------
// 标准答案:随机排列,相邻行 |列差| >= 2
// ---------------------------------------------------------------------------
static bool build_answer(uint32_t *rng, int n, uint8_t *col_of_row) {
    uint8_t perm[CATCUBE_GEN_MAX_SIDE];
    for (int attempt = 0; attempt < 64; attempt++) {
        for (int i = 0; i < n; i++) perm[i] = (uint8_t)i;
        for (int i = n - 1; i > 0; i--) {
            const int j = rng_below(rng, i + 1);
            const uint8_t t = perm[i];
            perm[i] = perm[j];
            perm[j] = t;
        }
        bool ok = true;
        for (int i = 0; i + 1 < n && ok; i++) {
            const int d = (int)perm[i] - (int)perm[i + 1];
            if (d >= -1 && d <= 1) ok = false;
        }
        if (!ok) continue;
        memcpy(col_of_row, perm, (size_t)n);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 色块生长:多源随机 BFS,每块含且仅含自己的种子
// ---------------------------------------------------------------------------
static void push_frontier(uint16_t *fx, uint8_t *fr, int *fn, int cell, uint8_t region) {
    if (*fn >= GEN_FRONTIER_MAX) return;
    fx[*fn] = (uint16_t)cell;
    fr[*fn] = region;
    (*fn)++;
}

static void grow_regions(uint32_t *rng, int n, const uint8_t *col_of_row, uint8_t *cells) {
    memset(cells, 0xFF, (size_t)(n * n));
    static uint16_t fx[GEN_FRONTIER_MAX];
    static uint8_t fr[GEN_FRONTIER_MAX];
    int fn = 0;

    for (int r = 0; r < n; r++) cells[r * n + col_of_row[r]] = (uint8_t)r;
    for (int r = 0; r < n; r++) {
        const int idx = r * n + col_of_row[r];
        const int rr = idx / n;
        const int cc = idx % n;
        if (rr > 0) push_frontier(fx, fr, &fn, idx - n, (uint8_t)r);
        if (rr + 1 < n) push_frontier(fx, fr, &fn, idx + n, (uint8_t)r);
        if (cc > 0) push_frontier(fx, fr, &fn, idx - 1, (uint8_t)r);
        if (cc + 1 < n) push_frontier(fx, fr, &fn, idx + 1, (uint8_t)r);
    }

    while (fn > 0) {
        const int pick = rng_below(rng, fn);
        const int cell = fx[pick];
        const uint8_t region = fr[pick];
        fn--;
        fx[pick] = fx[fn];
        fr[pick] = fr[fn];
        if (cells[cell] != 0xFF) continue;
        cells[cell] = region;
        const int rr = cell / n;
        const int cc = cell % n;
        if (rr > 0) push_frontier(fx, fr, &fn, cell - n, region);
        if (rr + 1 < n) push_frontier(fx, fr, &fn, cell + n, region);
        if (cc > 0) push_frontier(fx, fr, &fn, cell - 1, region);
        if (cc + 1 < n) push_frontier(fx, fr, &fn, cell + 1, region);
    }

    // 兜底:连通网格上不该有未分配格;真出现就并给任一相邻色块,保证覆盖全盘。
    for (int i = 0; i < n * n; i++) {
        if (cells[i] != 0xFF) continue;
        const int rr = i / n;
        const int cc = i % n;
        if (rr > 0 && cells[i - n] != 0xFF) cells[i] = cells[i - n];
        else if (rr + 1 < n && cells[i + n] != 0xFF) cells[i] = cells[i + n];
        else if (cc > 0 && cells[i - 1] != 0xFF) cells[i] = cells[i - 1];
        else if (cc + 1 < n && cells[i + 1] != 0xFF) cells[i] = cells[i + 1];
    }
}

// ---------------------------------------------------------------------------
// 迁移杀解
// ---------------------------------------------------------------------------
static bool region_connected_without(const uint8_t *cells, int n, uint8_t region, int removed) {
    uint8_t seen[CATCUBE_GEN_MAX_CELLS];
    memset(seen, 0, (size_t)(n * n));
    int start = -1;
    for (int i = 0; i < n * n; i++) {
        if (i == removed || cells[i] != region) continue;
        start = i;
        break;
    }
    if (start < 0) return false; // 色块空了
    int stack[CATCUBE_GEN_MAX_CELLS];
    int top = 0;
    stack[top++] = start;
    seen[start] = 1;
    int reached = 1;
    while (top > 0) {
        const int cur = stack[--top];
        const int rr = cur / n;
        const int cc = cur % n;
        const int nb[4] = {rr > 0 ? cur - n : -1, rr + 1 < n ? cur + n : -1,
                           cc > 0 ? cur - 1 : -1, cc + 1 < n ? cur + 1 : -1};
        for (int k = 0; k < 4; k++) {
            const int m = nb[k];
            if (m < 0 || seen[m] || m == removed) continue;
            if (cells[m] != region) continue;
            seen[m] = 1;
            stack[top++] = m;
            reached++;
        }
    }
    int total = 0;
    for (int i = 0; i < n * n; i++) {
        if (i != removed && cells[i] == region) total++;
    }
    return reached == total;
}

// 把解 S 里的某个非答案格迁到"已含 S 另一格"的相邻色块,让 S 立刻失效。
static bool kill_solution(uint8_t *cells, int n, const uint8_t *sol, int sol_len,
                          const uint8_t *answer, int k) {
    for (int i = 0; i < sol_len; i++) {
        const int x = sol[i];
        bool is_answer = false;
        for (int a = 0; a < k; a++) {
            if (answer[a] == x) {
                is_answer = true;
                break;
            }
        }
        if (is_answer) continue;

        const int rr = x / n;
        const int cc = x % n;
        const int nb[4] = {rr > 0 ? x - n : -1, rr + 1 < n ? x + n : -1, cc > 0 ? x - 1 : -1,
                           cc + 1 < n ? x + 1 : -1};
        for (int d = 0; d < 4; d++) {
            const int y = nb[d];
            if (y < 0) continue;
            const uint8_t from = cells[x];
            const uint8_t to = cells[y];
            if (to == from) continue;
            bool other = false;
            for (int j = 0; j < sol_len; j++) {
                if (sol[j] != x && cells[sol[j]] == to) {
                    other = true;
                    break;
                }
            }
            if (!other) continue;
            if (!region_connected_without(cells, n, from, x)) continue;
            cells[x] = to;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
int catcube_gen_size_for(uint32_t level_index) {
    if (level_index == 0) return 8;
    const uint32_t tier = (level_index - 1) / 10; // 10 关一个阶梯
    if (tier < 8) return 8;                       // 前 80 关走烘焙表
    return 8 + (int)((tier - 8) % 7);             // 8..14 循环,14 为硬上限
}

bool catcube_gen_build(uint32_t level_index, catcube_gen_level_t *out) {
    if (level_index == 0) return false;
    return catcube_gen_build_seeded(level_index, catcube_gen_size_for(level_index), out);
}

bool catcube_gen_build_seeded(uint32_t seed, int n, catcube_gen_level_t *out) {
    if (!out) return false;
    if (n < 8 || n > CATCUBE_GEN_MAX_SIDE) return false;

    static catcube_gen_level_t work;
    memset(&work, 0, sizeof(work));
    work.w = (uint8_t)n;
    work.h = (uint8_t)n;
    work.k = (uint8_t)n;

    uint8_t col_of_row[CATCUBE_GEN_MAX_SIDE];
    uint32_t total_nodes = 0;

    for (uint32_t attempt = 0; attempt < GEN_ATTEMPTS; attempt++) {
        uint32_t rng = rng_hash(seed, attempt);
        if (!build_answer(&rng, n, col_of_row)) continue;
        for (int r = 0; r < n; r++) work.answer[r] = (uint8_t)(r * n + col_of_row[r]);
        grow_regions(&rng, n, col_of_row, work.cells);

        const catcube_level_def_t view = {.w = (uint8_t)n,
                                          .h = (uint8_t)n,
                                          .k = (uint8_t)n,
                                          .cells = work.cells,
                                          .answer = work.answer};

        bool ok = false;
        for (int iter = 0; iter < GEN_MAX_MIGRATIONS; iter++) {
            gen_solver_t s;
            memset(&s, 0, sizeof(s));
            s.lv = &view;
            s.cap = 2;
            s.budget = GEN_COUNT_BUDGET;
            solve_all(&s);
            total_nodes += s.nodes;
            if (s.budget_hit) break; // 太难算,换 attempt
            if (s.found == 1) {
                ok = true;
                break;
            }
            if (s.found == 0) break; // 防御:答案本身就是解,不该出现

            // 取一个不是标准答案的解
            bool same = s.sol_len[0] == (uint8_t)n;
            if (same) {
                for (int i = 0; i < n && same; i++) {
                    if (s.sol[0][i] != work.answer[i]) same = false;
                }
            }
            const int pick = same ? 1 : 0;
            if (!kill_solution(work.cells, n, s.sol[pick], s.sol_len[pick], work.answer, n)) break;
            if (total_nodes > GEN_TOTAL_BUDGET) break;
        }
        if (ok) {
            *out = work;
            return true;
        }
        if (total_nodes > GEN_TOTAL_BUDGET) break;
    }
    return false;
}
