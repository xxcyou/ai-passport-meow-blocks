// main/app/merit_logic.c —— 功德计数与连击状态机实现。
#include "app/merit_logic.h"

static uint32_t clamp_count(uint32_t value) {
    return value > MERIT_COUNT_MAX ? MERIT_COUNT_MAX : value;
}

void merit_init(merit_state_t *state, uint32_t total, uint32_t best_combo) {
    if (!state) return;
    state->total = clamp_count(total);
    state->session = 0;
    state->best_combo = best_combo > MERIT_COMBO_MAX ? MERIT_COMBO_MAX : best_combo;
    state->combo = 0;
    state->last_hit_ms = 0;
    state->combo_active = false;
}

uint32_t merit_hit(merit_state_t *state, uint32_t now_ms) {
    if (!state) return 0;

    // 连击判定:窗口内且已有连击则累加,否则从 1 重新开始。
    // 用无符号差值比较,时钟回绕也不会误判(调用方是单调时钟,回绕间隔 49 天)。
    if (state->combo_active && (uint32_t)(now_ms - state->last_hit_ms) < MERIT_COMBO_WINDOW_MS) {
        if (state->combo < MERIT_COMBO_MAX) {
            state->combo = (uint16_t)(state->combo + 1);
        }
    } else {
        state->combo = 1;
    }

    state->combo_active = true;
    state->last_hit_ms = now_ms;
    if (state->combo > state->best_combo) {
        state->best_combo = state->combo;
    }

    state->total = clamp_count(state->total + 1);
    state->session = clamp_count(state->session + 1);
    return state->combo;
}

void merit_tick(merit_state_t *state, uint32_t now_ms) {
    if (!state || !state->combo_active) return;
    if ((uint32_t)(now_ms - state->last_hit_ms) >= MERIT_COMBO_WINDOW_MS) {
        state->combo_active = false;
    }
}

void merit_reset(merit_state_t *state) {
    if (!state) return;
    state->total = 0;
    state->session = 0;
    state->combo = 0;
    state->combo_active = false;
    state->last_hit_ms = 0;
}

bool merit_combo_visible(const merit_state_t *state) {
    return state && state->combo_active && state->combo >= 2;
}
