// main/app/knock_synth.c —— 敲击音合成实现。
//
// 模型:两个指数衰减的正弦模态(木块/金属的共振峰)+ 一段更短的噪声瞬态(槌头接触)。
// 这个组合足以让人听出"木头"、"金属"和"皮/木鼓"的区别,而且全部参数都是常数,
// 渲染结果可复现 —— 主机测试可以断言长度、峰值和衰减趋势。
#include "app/knock_synth.h"

#include <math.h>

#define KNOCK_PI 3.14159265358979323846
#define KNOCK_TWO_PI (2.0 * KNOCK_PI)

// 连击音高台阶:每多一下升 6Hz,封顶 +36Hz。再高就不像木鱼了。
#define KNOCK_PITCH_STEP_HZ 6
#define KNOCK_PITCH_MAX_HZ 36

// 两个模态的配比:主模态占主导,次模态给"木头"的硬度。
#define KNOCK_MODE2_GAIN 0.55

// 噪声瞬态的时间常数 = noise_ms 的 1/3,让噪声在前 1/3 段就基本消失。
#define KNOCK_NOISE_TAU_DIV 3.0

knock_spec_t knock_spec_for(uint8_t preset, uint32_t combo) {
    knock_spec_t spec = {
        .sample_rate = 16000,
        .duration_ms = 150,
        .mode1_hz = 420,
        .mode2_hz = 780,
        .mode1_decay_ms = 38,
        .mode2_decay_ms = 18,
        .noise_ms = 8,
        .noise_level = 30,
        .amplitude = 78,
        .pitch_shift_hz = 0,
    };

    switch (preset) {
    case KNOCK_PRESET_BELL:
        spec.duration_ms = 520;
        spec.mode1_hz = 1180;
        spec.mode2_hz = 2350;
        spec.mode1_decay_ms = 220;
        spec.mode2_decay_ms = 90;
        spec.noise_ms = 4;
        spec.noise_level = 8;
        spec.amplitude = 62;
        break;
    case KNOCK_PRESET_DRUM:
        spec.duration_ms = 260;
        spec.mode1_hz = 180;
        spec.mode2_hz = 300;
        spec.mode1_decay_ms = 70;
        spec.mode2_decay_ms = 35;
        spec.noise_ms = 14;
        spec.noise_level = 55;
        spec.amplitude = 85;
        break;
    case KNOCK_PRESET_WOOD:
    default:
        break;
    }

    // combo 从 1 开始;第一下不加偏移,之后逐级上扬。
    if (combo > 1) {
        uint32_t step = (combo - 1) * KNOCK_PITCH_STEP_HZ;
        if (step > KNOCK_PITCH_MAX_HZ) step = KNOCK_PITCH_MAX_HZ;
        spec.pitch_shift_hz = (int16_t)step;
    }
    return spec;
}

size_t knock_sample_count(const knock_spec_t *spec) {
    if (!spec || spec->sample_rate == 0 || spec->duration_ms == 0) return 0;
    return (size_t)((uint32_t)spec->sample_rate * spec->duration_ms / 1000u);
}

// 轻量 xorshift:噪声必须可复现,rand() 的实现随平台变化,不能用。
static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state ? *state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

size_t knock_render(int16_t *out, size_t capacity, const knock_spec_t *spec) {
    if (!out || !spec) return 0;
    const size_t count = knock_sample_count(spec);
    if (count == 0 || capacity < count) return 0;

    const double fs = (double)spec->sample_rate;
    const double f1 = (double)spec->mode1_hz + (double)spec->pitch_shift_hz;
    const double f2 = (double)spec->mode2_hz + (double)spec->pitch_shift_hz;
    const double tau1 = (double)spec->mode1_decay_ms / 1000.0;
    const double tau2 = (double)spec->mode2_decay_ms / 1000.0;
    const double tau_noise = ((double)spec->noise_ms / 1000.0) / KNOCK_NOISE_TAU_DIV;
    const double noise_end = (double)spec->noise_ms / 1000.0;
    const double noise_mix = (double)spec->noise_level / 100.0;
    const double peak = 32767.0 * ((double)spec->amplitude / 100.0);

    // 种子只由参数决定:同参数同结果,测试可复现。
    uint32_t rng = 0x1234567u ^ ((uint32_t)spec->mode1_hz << 16) ^
                   (uint32_t)spec->mode2_hz ^ ((uint32_t)spec->pitch_shift_hz << 8);

    for (size_t i = 0; i < count; i++) {
        const double t = (double)i / fs;
        const double env1 = exp(-t / tau1);
        const double env2 = exp(-t / tau2);
        double body = sin(KNOCK_TWO_PI * f1 * t) * env1 +
                      KNOCK_MODE2_GAIN * sin(KNOCK_TWO_PI * f2 * t) * env2;

        double noise = 0.0;
        if (t < noise_end && tau_noise > 0.0) {
            const double unit = ((double)(xorshift32(&rng) >> 8) / 16777216.0) * 2.0 - 1.0;
            noise = unit * exp(-t / tau_noise);
        }

        double sample = (body + noise_mix * noise) * peak;
        // 相加后可能超过满量程,硬限幅比软削波更不容易在喇叭上破音。
        if (sample > 32767.0) sample = 32767.0;
        if (sample < -32768.0) sample = -32768.0;
        out[i] = (int16_t)(sample >= 0.0 ? sample + 0.5 : sample - 0.5);
    }
    return count;
}
