// main/app/catcube_audio.c —— 合成音效播放任务。
//
// 全部音效都是现场合成的方波/三角波短音,没有一个素材文件:8MB Flash 里省下的
// 空间留给关卡表。采样率 16kHz / 16bit / 单声道,一个 512 采样的块就够用。
#include "app/catcube_audio.h"

#include "bsp_audio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

static const char *TAG = "cat_audio";

#define AUD_HZ       16000
#define AUD_CHUNK    512
#define AUD_QUEUE    32
#define AUD_STACK    4096
#define AUD_PRIO     4
#define AUD_AMP      7000 // 约 -13dBFS,小喇叭够响但不刺耳

typedef struct {
    uint16_t freq;
    uint16_t ms;
} tone_t;

typedef struct {
    uint8_t count;
    tone_t step[6];
} seq_t;

// 音色表:短、干净、彼此可区分。
static const seq_t SND_MOVE = {1, {{1500, 14}}};
static const seq_t SND_X = {1, {{760, 22}}};
static const seq_t SND_CAT = {2, {{980, 32}, {1320, 38}}};
static const seq_t SND_ALL = {3, {{1200, 45}, {1600, 45}, {2000, 80}}};
static const seq_t SND_SLOT = {1, {{1800, 20}}};
static const seq_t SND_WIN = {4, {{880, 70}, {1100, 70}, {1320, 70}, {1760, 150}}};
static const seq_t SND_WRONG = {1, {{240, 120}}};

typedef struct {
    uint8_t kind; // 0 = 普通音效,1 = 自动排除连击
    uint8_t snd;
    uint8_t steps;
} req_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static volatile bool s_ready;
static volatile bool s_muted;

static int16_t s_buf[AUD_CHUNK];

static void write_tone(uint16_t freq, uint16_t ms) {
    if (freq == 0 || ms == 0) return;
    const int total = (int)((uint32_t)AUD_HZ * ms / 1000u);
    const uint32_t step_phase = (uint32_t)((uint64_t)freq * 65536u * 2u / AUD_HZ); // 方波:半周期
    uint32_t phase = 0;
    int done = 0;
    while (done < total) {
        const int n = (total - done) < AUD_CHUNK ? (total - done) : AUD_CHUNK;
        for (int i = 0; i < n; i++) {
            const int env = (i < 64) ? (i * AUD_AMP / 64) : AUD_AMP; // 起音斜坡,去咔哒声
            s_buf[i] = (phase & 0x10000u) ? (int16_t)env : (int16_t)-env;
            phase += step_phase;
        }
        bsp_audio_write(s_buf, (size_t)n * sizeof(int16_t));
        done += n;
    }
}

static void play_seq(const seq_t *seq) {
    for (int i = 0; i < seq->count; i++) write_tone(seq->step[i].freq, seq->step[i].ms);
}

// 自动排除的"连击":一格里一个短音,音高逐步升高,时长随数量变化。
static void play_sweep(int steps) {
    if (steps < 1) steps = 1;
    if (steps > 20) steps = 20;
    for (int i = 0; i < steps; i++) {
        write_tone((uint16_t)(900 + i * 70), 18);
    }
}

static void audio_task(void *arg) {
    (void)arg;
    for (;;) {
        req_t req;
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE) continue;
        if (s_muted) continue;
        if (req.kind == 1) {
            play_sweep(req.steps);
            continue;
        }
        switch (req.snd) {
            case CAT_SND_MOVE: play_seq(&SND_MOVE); break;
            case CAT_SND_X: play_seq(&SND_X); break;
            case CAT_SND_CAT: play_seq(&SND_CAT); break;
            case CAT_SND_ALL: play_seq(&SND_ALL); break;
            case CAT_SND_SLOT: play_seq(&SND_SLOT); break;
            case CAT_SND_WIN: play_seq(&SND_WIN); break;
            case CAT_SND_WRONG: play_seq(&SND_WRONG); break;
            default: break;
        }
    }
}

bool cat_audio_init(void) {
    if (s_ready) return true;
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "codec 初始化失败,静音运行");
        return false;
    }
    if (bsp_audio_set_format(AUD_HZ, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "I2S 格式设置失败,静音运行");
        return false;
    }
    bsp_audio_set_volume(55);

    s_queue = xQueueCreate(AUD_QUEUE, sizeof(req_t));
    if (!s_queue) return false;
    if (xTaskCreate(audio_task, "cat_audio", AUD_STACK, NULL, AUD_PRIO, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return false;
    }
    s_ready = true;
    ESP_LOGI(TAG, "音效就绪(%d Hz 合成)", AUD_HZ);
    return true;
}

void cat_audio_deinit(void) {
    if (!s_ready) return;
    s_ready = false;
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
}

void cat_audio_play(cat_snd_t snd, int steps) {
    if (!s_ready || !s_queue) return;
    req_t req = {.kind = 0, .snd = (uint8_t)snd, .steps = 0};
    if (snd == CAT_SND_SWEEP) {
        req.kind = 1;
        req.steps = (uint8_t)(steps < 0 ? 1 : (steps > 20 ? 20 : steps));
    }
    (void)xQueueSend(s_queue, &req, 0);
}

void cat_audio_set_muted(bool muted) { s_muted = muted; }
bool cat_audio_muted(void) { return s_muted; }
