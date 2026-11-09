// tests/test_merit_logic.c —— 功德计数与连击状态机的主机侧用例。
//
// 覆盖"用久了才会出现"的边界:窗口边界、连击封顶、计数封顶、时钟回绕、
// 清零后保留最高连击。这些都不需要屏幕或硬件。
#include "../main/app/merit_logic.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_init_clamps(void) {
    merit_state_t state;
    merit_init(&state, 500000000u, 5000u);
    assert(state.total == MERIT_COUNT_MAX);   // 超上限被夹住
    assert(state.best_combo == MERIT_COMBO_MAX);
    assert(state.session == 0);
    assert(state.combo == 0);
    assert(!state.combo_active);
}

static void test_first_hit(void) {
    merit_state_t state;
    merit_init(&state, 0, 0);
    assert(merit_hit(&state, 1000) == 1);
    assert(state.total == 1);
    assert(state.session == 1);
    assert(state.combo_active);
    assert(state.best_combo == 1);
}

static void test_combo_window(void) {
    merit_state_t state;
    merit_init(&state, 0, 0);

    assert(merit_hit(&state, 0) == 1);
    assert(merit_hit(&state, MERIT_COMBO_WINDOW_MS - 1) == 2);   // 窗口内
    assert(state.best_combo == 2);

    // 正好等于窗口长度即算超时(边界取开区间)。
    assert(merit_hit(&state, 2 * MERIT_COMBO_WINDOW_MS - 1) == 1);
    assert(state.combo == 1);
    assert(state.total == 3);
}

static void test_tick_expires_combo(void) {
    merit_state_t state;
    merit_init(&state, 0, 0);
    merit_hit(&state, 100);
    merit_hit(&state, 200);

    merit_tick(&state, 200 + MERIT_COMBO_WINDOW_MS - 1);
    assert(state.combo_active);
    merit_tick(&state, 200 + MERIT_COMBO_WINDOW_MS);
    assert(!state.combo_active);
    assert(!merit_combo_visible(&state));

    // 超时后下一击重新从 1 开始。
    assert(merit_hit(&state, 5000) == 1);
}

static void test_combo_cap(void) {
    merit_state_t state;
    merit_init(&state, 0, 0);
    for (uint32_t i = 0; i <= MERIT_COMBO_MAX + 10u; i++) {
        merit_hit(&state, i);   // 每 1ms 一次,始终在窗口内
    }
    assert(state.combo == MERIT_COMBO_MAX);
    assert(state.best_combo == MERIT_COMBO_MAX);
    assert(state.total == MERIT_COMBO_MAX + 11u);
}

static void test_count_saturation(void) {
    merit_state_t state;
    merit_init(&state, MERIT_COUNT_MAX - 1u, 0);
    merit_hit(&state, 0);
    assert(state.total == MERIT_COUNT_MAX);
    merit_hit(&state, 10);
    assert(state.total == MERIT_COUNT_MAX);   // 到顶后不再增长,也不回绕
}

static void test_reset_keeps_best(void) {
    merit_state_t state;
    merit_init(&state, 42, 7);
    merit_hit(&state, 0);
    merit_hit(&state, 1);
    merit_reset(&state);
    assert(state.total == 0);
    assert(state.session == 0);
    assert(state.combo == 0);
    assert(!state.combo_active);
    assert(state.best_combo == 7);            // "曾经做到过"的记录保留
}

static void test_clock_wrap(void) {
    merit_state_t state;
    merit_init(&state, 0, 0);
    const uint32_t near_max = UINT32_MAX - 10u;
    assert(merit_hit(&state, near_max) == 1);
    // 时钟回绕后 5ms:无符号差值仍能正确判断为窗口内。
    assert(merit_hit(&state, 5) == 2);
    merit_tick(&state, 5 + MERIT_COMBO_WINDOW_MS);
    assert(!state.combo_active);
}

static void test_null_safety(void) {
    merit_init(NULL, 0, 0);
    assert(merit_hit(NULL, 0) == 0);
    merit_tick(NULL, 0);
    merit_reset(NULL);
    assert(!merit_combo_visible(NULL));
}

int main(void) {
    test_init_clamps();
    test_first_hit();
    test_combo_window();
    test_tick_expires_combo();
    test_combo_cap();
    test_count_saturation();
    test_reset_keeps_best();
    test_clock_wrap();
    test_null_safety();
    printf("test_merit_logic: PASS\n");
    return 0;
}
