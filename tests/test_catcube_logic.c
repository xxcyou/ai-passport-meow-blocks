// tests/test_catcube_logic.c —— 「猫猫方块」规则模型与关卡数据的宿主侧校验。
//
// 这里做三件事:
//   1. 规则函数的单元测试(行/列/色块/邻域冲突、提示、通关判定);
//   2. 关卡表自检:尺寸与阶梯表一致、色块四连通、每块恰好一只猫、答案合法;
//   3. 独立求解器:对每一关枚举所有解,断言解数恰好为 1(与生成器实现无关)。
//
// 第 3 项是"唯一解"这条硬性要求的独立证据:生成器用 Python 生成,这里用 C 重算。
#include "app/catcube_levels.h"
#include "app/catcube_logic.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, ...)                       \
    do {                                       \
        if (!(cond)) {                         \
            g_failures++;                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);               \
            printf("\n");                      \
        }                                      \
    } while (0)

// ---------------------------------------------------------------------------
// 合成关卡:4x4 两个色块(色块数 = 猫数 = 2),答案 (0,0) 与 (2,2)。
// 色块 0 是一条"拐角条" {(0,0),(0,1),(0,2),(1,2)},用来构造"同色块但不同行不同列
// 且不邻接"的格子 —— 只有这种格子才能把 REGION 冲突单独测出来。
// ---------------------------------------------------------------------------
static const uint8_t SYN_CELLS[16] = {
    0, 0, 0, 1,
    1, 1, 0, 1,
    1, 1, 1, 1,
    1, 1, 1, 1,
};
static const uint8_t SYN_ANSWER[2] = {0, 10}; // (0,0) 与 (2,2)
static const catcube_level_def_t SYN_LEVEL = {
    .w = 4, .h = 4, .k = 2, .cells = SYN_CELLS, .answer = SYN_ANSWER,
};

static void test_rule_functions(void) {
    catcube_state_t st;
    catcube_state_reset(&st, &SYN_LEVEL);
    CHECK(catcube_found(&st) == 0, "reset 后已确认猫数应为 0");
    CHECK(catcube_lives(&st) == CATCUBE_LIVES, "reset 后应满血");
    CHECK(catcube_cursor(&st) == 0, "reset 后光标应在左上角");
    CHECK(catcube_mark_at(&st, 0) == CATCUBE_MARK_NONE, "reset 后格子应为未标记");

    // 光标移动 + 绕回
    catcube_move(&st, 1, 0);
    CHECK(catcube_cursor(&st) == 4, "下移一格应为 (1,0)");
    catcube_move(&st, -1, 0);
    CHECK(catcube_cursor(&st) == 0, "上移一格应回到 (0,0)");
    catcube_cursor_set(&st, 0, 3);
    catcube_move(&st, 0, 1);
    CHECK(catcube_cursor(&st) == 0, "最右列再右移应绕回最左列");
    catcube_cursor_set(&st, 3, 0);
    catcube_move(&st, 1, 0);
    CHECK(catcube_cursor(&st) == 0, "最下行再下移应绕回第一行");
    CHECK(!catcube_cursor_set(&st, 4, 0), "越界行应拒绝");

    // 打叉 / 取消
    catcube_state_reset(&st, &SYN_LEVEL);
    catcube_toggle_x(&st);
    CHECK(catcube_mark_at(&st, 0) == CATCUBE_MARK_X, "单击应打叉");
    catcube_toggle_x(&st);
    CHECK(catcube_mark_at(&st, 0) == CATCUBE_MARK_NONE, "再单击应取消叉");

    // 确认猫(现在要求点必须是真猫)
    catcube_state_reset(&st, &SYN_LEVEL);
    CHECK(catcube_toggle_cat(&st) == CATCUBE_ACT_PLACED, "在真猫格确认应成功");
    CHECK(catcube_found(&st) == 1, "确认后已找到数应为 1");
    CHECK(catcube_toggle_cat(&st) == CATCUBE_ACT_REMOVED, "再确认一次应取消");
    CHECK(catcube_found(&st) == 0, "取消后已找到数应回到 0");

    // 猜错:不是猫 -> 扣命 + 打叉记住,不会计入找到数
    catcube_state_reset(&st, &SYN_LEVEL);
    catcube_cursor_set(&st, 3, 3);
    CHECK(catcube_toggle_cat(&st) == CATCUBE_ACT_WRONG, "确认非猫格应判为猜错");
    CHECK(catcube_found(&st) == 0, "猜错不能计入找到数");
    CHECK(catcube_lives(&st) == CATCUBE_LIVES - 1, "猜错应扣一条命");
    CHECK(catcube_mark_rc(&st, 3, 3) == CATCUBE_MARK_WRONG, "猜错的格子应是红叉");
    CHECK(catcube_is_wrong(&st, 15), "猜错标记应可查询");
    // 红叉不可取消、不可再确认
    catcube_toggle_x(&st);
    CHECK(catcube_mark_rc(&st, 3, 3) == CATCUBE_MARK_WRONG, "红叉不能被单击取消");
    CHECK(catcube_toggle_cat(&st) == CATCUBE_ACT_BLOCKED, "红叉格不能再确认");
    CHECK(!catcube_failed(&st), "还有命就不算失败");
    // 再猜错一次 -> 失败
    catcube_cursor_set(&st, 3, 2);
    CHECK(catcube_toggle_cat(&st) == CATCUBE_ACT_WRONG, "第二次猜错也应判错");
    CHECK(catcube_lives(&st) == 0, "两条命应扣完");
    CHECK(catcube_failed(&st), "命耗尽应判负");

    // 规则拦截:同一色块/同一行/同一列的格子不能再确认成猫
    catcube_state_reset(&st, &SYN_LEVEL);
    catcube_cursor_set(&st, 0, 0);
    CHECK(catcube_toggle_cat(&st) == CATCUBE_ACT_PLACED, "先确认一只");
    catcube_cursor_set(&st, 0, 1); // 同一行 + 同一色块
    CHECK(catcube_toggle_cat(&st) == CATCUBE_ACT_BLOCKED, "同行同色块应被拦下");
    CHECK(catcube_found(&st) == 1, "被拦下不应改变找到数");
    CHECK(catcube_lives(&st) == CATCUBE_LIVES, "被拦下不应扣命");
    catcube_cursor_set(&st, 1, 2); // 同一色块 0,不同行不同列不邻接
    CHECK(catcube_toggle_cat(&st) == CATCUBE_ACT_BLOCKED, "同色块应被拦下");

    // 找到数永远不超过目标数
    catcube_state_reset(&st, &SYN_LEVEL);
    catcube_cursor_set(&st, 0, 0);
    catcube_toggle_cat(&st);
    catcube_cursor_set(&st, 2, 2);
    catcube_toggle_cat(&st);
    CHECK(catcube_found(&st) == 2, "两只真猫都应确认");
    CHECK(catcube_is_solved(&st), "答案全中应通关");
    CHECK(catcube_found(&st) <= catcube_target(&st), "找到数不应超过目标数");

    // 打叉不影响通关
    catcube_state_reset(&st, &SYN_LEVEL);
    catcube_cursor_set(&st, 1, 1);
    catcube_toggle_x(&st);
    catcube_cursor_set(&st, 0, 0);
    catcube_toggle_cat(&st);
    catcube_cursor_set(&st, 2, 2);
    catcube_toggle_cat(&st);
    CHECK(catcube_is_solved(&st), "叉只是笔记,不应阻止通关");

    // 提示:补一只真猫
    catcube_state_reset(&st, &SYN_LEVEL);
    CHECK(catcube_hint(&st), "应能给出一只提示");
    CHECK(catcube_found(&st) == 1, "提示应确认一只猫");
    CHECK(catcube_hint(&st), "应能再给一只");
    CHECK(catcube_is_solved(&st), "提示补满应通关");

    // 真实关卡上的整块排除:确认一只猫后,它同色块的其他格子必须全部变成叉
    {
        const catcube_level_def_t *lv = &CATCUBE_LEVELS[0];
        catcube_state_reset(&st, lv);
        const int cells = lv->w * lv->h;
        const uint8_t cat_idx = lv->answer[0];
        const uint8_t region = lv->cells[cat_idx];
        catcube_cursor_set(&st, (uint8_t)(cat_idx / lv->w), (uint8_t)(cat_idx % lv->w));
        CHECK(catcube_toggle_cat(&st) == CATCUBE_ACT_PLACED, "L1 第一只猫应能确认");
        int in_region = 0;
        int excluded = 0;
        for (int i = 0; i < cells; i++) {
            if (lv->cells[i] != region) continue;
            in_region++;
            const catcube_mark_t m = catcube_mark_at(&st, (uint8_t)i);
            if (m == CATCUBE_MARK_X || m == CATCUBE_MARK_CAT) excluded++;
        }
        CHECK(in_region > 1, "该色块应有多格");
        CHECK(excluded == in_region, "同色块必须被整体排除(%d/%d)", excluded, in_region);
    }

    // 星级:满血快速 = 3 星;掉一条命 = 少半星;拖太久再扣
    catcube_state_reset(&st, &SYN_LEVEL);
    CHECK(catcube_stars_half(&st, 5000) == 6, "满血快速应 3 星");
    st.lives = 1;
    CHECK(catcube_stars_half(&st, 5000) == 5, "掉一条命应扣半星");
    st.lives = CATCUBE_LIVES;
    CHECK(catcube_stars_half(&st, 1000000) == 4, "超时太久应扣一整星");
    CHECK(catcube_stars_half(&st, 1000000) >= 1, "星级不应低于半星");

    // 限时:150 关起才有,越往后越紧
    CHECK(catcube_time_limit_ms(149) == 0, "150 关之前不限时");
    CHECK(catcube_time_limit_ms(150) == 400000, "150 关应为 400s");
    CHECK(catcube_time_limit_ms(200) == 300000, "200 关应为 300s");
    CHECK(catcube_time_limit_ms(300) == 200000, "300 关应为 200s");

    CHECK(catcube_answer_valid(&SYN_LEVEL), "合成关卡答案应合法");
}

// ---------------------------------------------------------------------------
// 关卡表:尺寸/数量必须与阶梯表一致
// ---------------------------------------------------------------------------
typedef struct {
    int w;
    int h;
    int k;
} ladder_expect_t;

static const ladder_expect_t LADDER[] = {
    {4, 4, 3}, /* L1 */
    {4, 4, 3}, /* L2 */
    {4, 4, 3}, /* L3 */
    {4, 4, 4}, /* L4 */
    {4, 4, 4}, /* L5 */
    {4, 4, 4}, /* L6 */
    {5, 5, 4}, /* L7 */
    {5, 5, 4}, /* L8 */
    {5, 5, 5}, /* L9 */
    {5, 5, 5}, /* L10 */
    {5, 5, 5}, /* L11 */
    {5, 5, 5}, /* L12 */
    {6, 6, 6}, /* L13 */
    {6, 6, 6}, /* L14 */
    {6, 6, 6}, /* L15 */
    {6, 6, 6}, /* L16 */
    {6, 6, 6}, /* L17 */
    {7, 7, 7}, /* L18 */
    {7, 7, 7}, /* L19 */
    {7, 7, 7}, /* L20 */
    {8, 8, 8}, /* L21 */
    {9, 9, 9}, /* L22 */
    {8, 8, 8}, /* L23 */
    {9, 9, 9}, /* L24 */
    {8, 8, 8}, /* L25 */
    {9, 9, 9}, /* L26 */
    {8, 8, 8}, /* L27 */
    {9, 9, 9}, /* L28 */
    {8, 8, 8}, /* L29 */
    {9, 9, 9}, /* L30 */
    {9, 9, 9}, /* L31 */
    {10, 10, 10}, /* L32 */
    {9, 9, 9}, /* L33 */
    {10, 10, 10}, /* L34 */
    {9, 9, 9}, /* L35 */
    {10, 10, 10}, /* L36 */
    {9, 9, 9}, /* L37 */
    {10, 10, 10}, /* L38 */
    {9, 9, 9}, /* L39 */
    {10, 10, 10}, /* L40 */
    {10, 10, 10}, /* L41 */
    {11, 11, 11}, /* L42 */
    {10, 10, 10}, /* L43 */
    {11, 11, 11}, /* L44 */
    {10, 10, 10}, /* L45 */
    {11, 11, 11}, /* L46 */
    {10, 10, 10}, /* L47 */
    {11, 11, 11}, /* L48 */
    {10, 10, 10}, /* L49 */
    {11, 11, 11}, /* L50 */
    {11, 11, 11}, /* L51 */
    {12, 12, 12}, /* L52 */
    {11, 11, 11}, /* L53 */
    {12, 12, 12}, /* L54 */
    {11, 11, 11}, /* L55 */
    {12, 12, 12}, /* L56 */
    {11, 11, 11}, /* L57 */
    {12, 12, 12}, /* L58 */
    {11, 11, 11}, /* L59 */
    {12, 12, 12}, /* L60 */
    {12, 12, 12}, /* L61 */
    {13, 13, 13}, /* L62 */
    {12, 12, 12}, /* L63 */
    {13, 13, 13}, /* L64 */
    {12, 12, 12}, /* L65 */
    {13, 13, 13}, /* L66 */
    {12, 12, 12}, /* L67 */
    {13, 13, 13}, /* L68 */
    {12, 12, 12}, /* L69 */
    {13, 13, 13}, /* L70 */
    {13, 13, 13}, /* L71 */
    {14, 14, 14}, /* L72 */
    {13, 13, 13}, /* L73 */
    {14, 14, 14}, /* L74 */
    {13, 13, 13}, /* L75 */
    {14, 14, 14}, /* L76 */
    {13, 13, 13}, /* L77 */
    {14, 14, 14}, /* L78 */
    {13, 13, 13}, /* L79 */
    {14, 14, 14}, /* L80 */
};


static const catcube_level_def_t *level_at(int level) {
    if (level >= 1 && level <= CATCUBE_LEVEL_COUNT) return &CATCUBE_LEVELS[level - 1];
    const int idx = level - CATCUBE_LEVEL_COUNT - 1;
    if (idx >= 0 && idx < CATCUBE_EXTRA_COUNT) return &CATCUBE_EXTRA_LEVELS[idx];
    return NULL;
}

static void test_level_table(void) {
    CHECK(CATCUBE_LEVEL_COUNT == 80, "关卡数应为 80,实际 %d", CATCUBE_LEVEL_COUNT);

    for (int i = 0; i < CATCUBE_LEVEL_COUNT && i < 80; i++) {
        const catcube_level_def_t *lv = &CATCUBE_LEVELS[i];
        CHECK(lv->w >= 2 && lv->w <= CATCUBE_MAX_SIDE && lv->h >= 2 && lv->h <= CATCUBE_MAX_SIDE,
              "L%d 尺寸越界", i + 1);
        CHECK(lv->k <= lv->w && lv->k <= lv->h, "L%d 猫数超过行/列数", i + 1);
        CHECK(catcube_answer_valid(lv), "L%d 标准答案不满足规则(行/列/邻域/每块一只)", i + 1);

        // 色块四连通:逐块 BFS,连通分量必须覆盖整块。
        const int cells = lv->w * lv->h;
        uint8_t visited[CATCUBE_MAX_CELLS];
        memset(visited, 0, sizeof(visited));
        int region_cells[CATCUBE_MAX_SIDE] = {0};
        for (int c = 0; c < cells; c++) {
            CHECK(lv->cells[c] < lv->k, "L%d 第 %d 格色块 id 越界", i + 1, c);
            if (lv->cells[c] < lv->k) region_cells[lv->cells[c]]++;
        }
        for (int r = 0; r < (int)lv->k; r++) {
            CHECK(region_cells[r] > 0, "L%d 色块 %d 没有任何格子", i + 1, r);
        }
        for (int start = 0; start < cells; start++) {
            if (visited[start]) continue;
            const uint8_t region = lv->cells[start];
            int stack[CATCUBE_MAX_CELLS];
            int top = 0;
            stack[top++] = start;
            visited[start] = 1;
            int reached = 0;
            while (top > 0) {
                const int cur = stack[--top];
                reached++;
                const int r = cur / lv->w;
                const int c = cur % lv->w;
                const int dr[4] = {-1, 1, 0, 0};
                const int dc[4] = {0, 0, -1, 1};
                for (int d = 0; d < 4; d++) {
                    const int nr = r + dr[d];
                    const int nc = c + dc[d];
                    if (nr < 0 || nc < 0 || nr >= (int)lv->h || nc >= (int)lv->w) continue;
                    const int nidx = nr * lv->w + nc;
                    if (visited[nidx]) continue;
                    if (lv->cells[nidx] != region) continue;
                    visited[nidx] = 1;
                    stack[top++] = nidx;
                }
            }
            CHECK(reached == region_cells[region], "L%d 色块 %d 不连通(触及 %d/%d)", i + 1,
                  (int)region, reached, region_cells[region]);
        }
    }

    // 前 20 关的阶梯(尺寸 + 猫数)必须与设计表逐关一致。
    for (int i = 0; i < 20 && i < CATCUBE_LEVEL_COUNT; i++) {
        const catcube_level_def_t *lv = &CATCUBE_LEVELS[i];
        CHECK(lv->w == LADDER[i].w && lv->h == LADDER[i].h, "L%d 尺寸应为 %dx%d,实际 %dx%d", i + 1,
              LADDER[i].w, LADDER[i].h, (int)lv->w, (int)lv->h);
        CHECK((int)lv->k == LADDER[i].k, "L%d 猫数应为 %d,实际 %d", i + 1, LADDER[i].k,
              (int)lv->k);
    }

    // 81 关起(额外表):正方形、8..14、k=w,答案合法、色块连通、唯一解由后面的求解器验。
    for (int i = 0; i < CATCUBE_EXTRA_COUNT; i++) {
        const catcube_level_def_t *lv = &CATCUBE_EXTRA_LEVELS[i];
        const int level = CATCUBE_LEVEL_COUNT + 1 + i;
        CHECK(lv->w == lv->h, "L%d 应为正方形", level);
        CHECK(lv->w >= 8 && lv->w <= CATCUBE_MAX_SIDE, "L%d 尺寸应在 8..14,实际 %d", level,
              (int)lv->w);
        CHECK((int)lv->k == (int)lv->w, "L%d 猫数应等于边长", level);
        CHECK(catcube_answer_valid(lv), "L%d 标准答案不合法", level);
        const int cells = lv->w * lv->h;
        int region_cnt[CATCUBE_MAX_SIDE] = {0};
        for (int c = 0; c < cells; c++) {
            CHECK(lv->cells[c] < lv->k, "L%d 色块 id 越界", level);
            if (lv->cells[c] < lv->k) region_cnt[lv->cells[c]]++;
        }
        for (int r = 0; r < (int)lv->k; r++) {
            CHECK(region_cnt[r] > 0, "L%d 色块 %d 为空", level, r);
        }
    }

    // 21 关起:每 10 关一个阶梯,棋盘是 8..14 的正方形、k=w,同一阶梯至少混两种尺寸。
    for (int tier = 0; tier < 6; tier++) {
        const int first = 20 + tier * 10;
        int sizes_seen[CATCUBE_MAX_SIDE + 1] = {0};
        int distinct = 0;
        for (int i = first; i < first + 10 && i < CATCUBE_LEVEL_COUNT; i++) {
            const catcube_level_def_t *lv = &CATCUBE_LEVELS[i];
            CHECK(lv->w == lv->h, "L%d 应为正方形,实际 %dx%d", i + 1, (int)lv->w, (int)lv->h);
            CHECK(lv->w >= 8 && lv->w <= CATCUBE_MAX_SIDE, "L%d 尺寸应在 8..14,实际 %d", i + 1,
                  (int)lv->w);
            CHECK((int)lv->k == (int)lv->w, "L%d 猫数应等于边长 %d,实际 %d", i + 1, (int)lv->w,
                  (int)lv->k);
            if (!sizes_seen[lv->w]) {
                sizes_seen[lv->w] = 1;
                distinct++;
            }
        }
        CHECK(distinct >= 2, "阶梯 %d(第 %d-%d 关)只用了 %d 种尺寸,要求混排", tier + 1, first + 1,
              first + 10, distinct);
    }
}

// ---------------------------------------------------------------------------
// 独立求解器:枚举所有解,解数必须恰好为 1
// ---------------------------------------------------------------------------
#define SOLVER_NODE_BUDGET 60000000LL

static const catcube_level_def_t *g_lv;
static long long g_nodes;
static int g_solutions;
static int g_budget_hit;

static void solve_dfs(int row, uint32_t col_mask, uint32_t region_mask, int prev_col, int placed) {
    if (g_solutions >= 2) return;
    if (++g_nodes > SOLVER_NODE_BUDGET) {
        g_budget_hit = 1;
        return;
    }
    if (placed == g_lv->k) {
        g_solutions++;
        return;
    }
    if (row >= g_lv->h) return;
    const int rows_left = g_lv->h - row;
    if (g_lv->k - placed > rows_left) return; // 剩下的行放不下剩下的猫

    // 本行不放猫
    solve_dfs(row + 1, col_mask, region_mask, -1, placed);

    // 本行放一只
    for (int c = 0; c < g_lv->w; c++) {
        if (col_mask & (1u << c)) continue;
        if (prev_col >= 0 && (c - prev_col <= 1) && (prev_col - c <= 1)) continue; // 八邻域
        const uint8_t region = g_lv->cells[row * g_lv->w + c];
        if (region_mask & (1u << region)) continue;
        solve_dfs(row + 1, col_mask | (1u << c), region_mask | (1u << region), c, placed + 1);
        if (g_solutions >= 2 || g_budget_hit) return;
    }
}

static void test_unique_solution(void) {
    const int total = CATCUBE_LEVEL_COUNT + CATCUBE_EXTRA_COUNT;
    for (int i = 0; i < total; i++) {
        g_lv = level_at(i + 1);
        if (!g_lv) continue;
        g_nodes = 0;
        g_solutions = 0;
        g_budget_hit = 0;
        solve_dfs(0, 0, 0, -1, 0);
        CHECK(!g_budget_hit, "L%d 求解超出节点预算(%lld),无法确认唯一解", i + 1, g_nodes);
        CHECK(g_solutions == 1, "L%d 解数应为 1,实际 %d(0=无解)", i + 1, g_solutions);
        if (i < 3 || g_nodes > 3000000) {
            printf("  L%-3d %2dx%-2d k=%-2d nodes=%lld solutions=%d\n", i + 1, (int)g_lv->w,
                   (int)g_lv->h, (int)g_lv->k, g_nodes, g_solutions);
        }
    }
}

int main(void) {
    test_rule_functions();
    test_level_table();
    test_unique_solution();
    if (g_failures) {
        printf("catcube logic tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("catcube logic tests: PASS (%d levels, unique solutions verified)\n",
           CATCUBE_LEVEL_COUNT + CATCUBE_EXTRA_COUNT);
    return 0;
}
