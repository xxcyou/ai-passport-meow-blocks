// tests/test_knock_synth.c —— 敲击音合成的主机侧用例。
//
// 断言的是"能听出区别"的可测代理量:长度、峰值范围、衰减趋势、确定性、
// 连击升调带来的过零率上升。声音本身好不好听仍需上板听。
#include "../main/app/knock_synth.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAP 16000   // 1s @16k,大于任何预设时长

static int16_t g_buf[CAP];

static int peak_abs(const int16_t *buf, size_t count) {
    int peak = 0;
    for (size_t i = 0; i < count; i++) {
        int v = buf[i] < 0 ? -(int)buf[i] : (int)buf[i];
        if (v > peak) peak = v;
    }
    return peak;
}

static double rms(const int16_t *buf, size_t from, size_t to) {
    double sum = 0.0;
    for (size_t i = from; i < to; i++) sum += (double)buf[i] * (double)buf[i];
    return sqrt(sum / (double)(to - from));
}

static size_t zero_crossings(const int16_t *buf, size_t from, size_t to) {
    size_t count = 0;
    for (size_t i = from + 1; i < to; i++) {
        if ((buf[i - 1] < 0) != (buf[i] < 0)) count++;
    }
    return count;
}

static void test_specs(void) {
    for (uint8_t preset = 0; preset < KNOCK_PRESET_COUNT; preset++) {
        knock_spec_t spec = knock_spec_for(preset, 1);
        assert(spec.sample_rate == 16000);
        assert(spec.duration_ms > 0);
        assert(spec.amplitude > 0 && spec.amplitude <= 100);
        assert(spec.noise_level <= 100);
        assert(spec.mode1_hz > 0 && spec.mode2_hz > spec.mode1_hz);
        assert(knock_sample_count(&spec) > 0);
    }

    // 三种音色必须真的不同,否则"切换音色"就是假的。
    knock_spec_t wood = knock_spec_for(KNOCK_PRESET_WOOD, 1);
    knock_spec_t bell = knock_spec_for(KNOCK_PRESET_BELL, 1);
    knock_spec_t drum = knock_spec_for(KNOCK_PRESET_DRUM, 1);
    assert(wood.duration_ms != bell.duration_ms);
    assert(bell.mode1_hz > wood.mode1_hz);
    assert(drum.mode1_hz < wood.mode1_hz);
    assert(drum.noise_level > bell.noise_level);
}

static void test_combo_pitch(void) {
    knock_spec_t first = knock_spec_for(KNOCK_PRESET_WOOD, 1);
    knock_spec_t later = knock_spec_for(KNOCK_PRESET_WOOD, 4);
    assert(first.pitch_shift_hz == 0);
    assert(later.pitch_shift_hz > 0);

    knock_spec_t capped = knock_spec_for(KNOCK_PRESET_WOOD, 999);
    assert(capped.pitch_shift_hz <= 36);
}

static void test_render_shape(void) {
    knock_spec_t spec = knock_spec_for(KNOCK_PRESET_WOOD, 1);
    const size_t count = knock_sample_count(&spec);
    assert(count == 16000u * 150u / 1000u);

    memset(g_buf, 0, sizeof(g_buf));
    assert(knock_render(g_buf, CAP, &spec) == count);

    const int peak = peak_abs(g_buf, count);
    assert(peak > 3000);          // 足够响,不是静音
    assert(peak <= 32767);        // 不越界

    // 衰减:后四分之一段明显比第一段轻。
    const double head = rms(g_buf, 0, count / 4);
    const double tail = rms(g_buf, count * 3 / 4, count);
    assert(head > tail * 4.0);

    // 尾巴收到接近静音,不会有"咔"的截断声。
    assert(abs((int)g_buf[count - 1]) < peak / 8);

    // 缓冲区不够时拒绝渲染,且不写越界。
    assert(knock_render(g_buf, count - 1, &spec) == 0);
}

static void test_deterministic(void) {
    knock_spec_t spec = knock_spec_for(KNOCK_PRESET_DRUM, 3);
    const size_t count = knock_sample_count(&spec);

    static int16_t a[CAP];
    static int16_t b[CAP];
    assert(knock_render(a, CAP, &spec) == count);
    assert(knock_render(b, CAP, &spec) == count);
    assert(memcmp(a, b, count * sizeof(int16_t)) == 0);
}

static void test_pitch_raises_zero_crossings(void) {
    knock_spec_t low = knock_spec_for(KNOCK_PRESET_BELL, 1);
    knock_spec_t high = knock_spec_for(KNOCK_PRESET_BELL, 7);
    const size_t count = knock_sample_count(&low);

    static int16_t a[CAP];
    static int16_t b[CAP];
    assert(knock_render(a, CAP, &low) == count);
    assert(knock_render(b, CAP, &high) == count);

    const size_t low_zc = zero_crossings(a, 0, count / 2);
    const size_t high_zc = zero_crossings(b, 0, count / 2);
    assert(high_zc > low_zc);
}

static void test_invalid_input(void) {
    knock_spec_t bad = {0};
    assert(knock_sample_count(&bad) == 0);
    assert(knock_render(g_buf, CAP, &bad) == 0);
    assert(knock_render(NULL, CAP, &bad) == 0);

    knock_spec_t ok = knock_spec_for(KNOCK_PRESET_WOOD, 1);
    assert(knock_render(NULL, CAP, &ok) == 0);
    assert(knock_sample_count(NULL) == 0);
}

int main(void) {
    test_specs();
    test_combo_pitch();
    test_render_shape();
    test_deterministic();
    test_pitch_raises_zero_crossings();
    test_invalid_input();
    printf("test_knock_synth: PASS\n");
    return 0;
}
