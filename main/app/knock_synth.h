// main/app/knock_synth.h —— 木鱼/铃/鼓的敲击音合成(纯 DSP,不依赖 ESP-IDF / LVGL)。
//
// 为什么合成而不是塞 WAV:一段 150ms/16kHz 的 PCM 只有 4.8KB,但三种音色加变调
// 就是十几份素材;而合成只要一张参数表,改音色不用重新导资源,也不涉及音频版权。
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    KNOCK_PRESET_WOOD = 0,   // 木鱼:短促、木质感
    KNOCK_PRESET_BELL,       // 铜铃:明亮、余音长
    KNOCK_PRESET_DRUM,       // 木鼓:低沉、带敲击噪声
    KNOCK_PRESET_COUNT
} knock_preset_t;

// 一次敲击的合成参数。时间量单位 ms,频率单位 Hz。
typedef struct {
    uint16_t sample_rate;      // 采样率(与 bsp_audio_set_format 一致)
    uint16_t duration_ms;      // 总时长
    uint16_t mode1_hz;         // 主模态频率
    uint16_t mode2_hz;         // 次模态频率
    uint16_t mode1_decay_ms;   // 主模态指数衰减时间常数
    uint16_t mode2_decay_ms;   // 次模态指数衰减时间常数
    uint16_t noise_ms;         // 敲击噪声瞬态长度
    uint8_t  noise_level;      // 噪声占比 0..100
    uint8_t  amplitude;        // 峰值幅度 0..100(相对 int16 满量程)
    int16_t  pitch_shift_hz;   // 连击带来的音高偏移(>0 更亮)
} knock_spec_t;

// 取某个音色在指定连击数下的参数。连击越高音高略微上扬(最多 +36Hz),
// 让"连击"这件事能听出来,而不是只看屏幕。
knock_spec_t knock_spec_for(uint8_t preset, uint32_t combo);

// 该参数需要多少采样点。
size_t knock_sample_count(const knock_spec_t *spec);

// 渲染到 out(最多 capacity 个采样点),返回实际写入的采样数。
// 参数非法(采样率为 0 等)或 capacity 不足时返回 0,不会写越界。
size_t knock_render(int16_t *out, size_t capacity, const knock_spec_t *spec);
