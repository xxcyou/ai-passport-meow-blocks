// main/app/wooden_fish_audio.c —— 敲击音播放任务。
#include "app/wooden_fish_audio.h"

#include "app/knock_synth.h"
#include "bsp_audio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "wf_audio";

#define WF_AUDIO_RATE      16000
#define WF_AUDIO_QUEUE_LEN 4
#define WF_AUDIO_VOLUME    85
#define WF_AUDIO_STOP_MS   2000

typedef struct {
    uint8_t preset;
    uint32_t combo;
} knock_request_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_stopped;
static volatile bool s_cancel;
static volatile bool s_muted;
static int16_t *s_pcm;
static size_t s_pcm_capacity;

static size_t max_sample_count(void) {
    size_t max = 0;
    for (uint8_t preset = 0; preset < KNOCK_PRESET_COUNT; preset++) {
        knock_spec_t spec = knock_spec_for(preset, 1);
        size_t count = knock_sample_count(&spec);
        if (count > max) max = count;
    }
    return max;
}

static void play_one(const knock_request_t *request) {
    if (!s_pcm || s_muted) return;

    knock_spec_t spec = knock_spec_for(request->preset, request->combo);
    size_t count = knock_render(s_pcm, s_pcm_capacity, &spec);
    if (count == 0) return;

    esp_err_t err = bsp_audio_write(s_pcm, count * sizeof(int16_t));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "播放失败: %s", esp_err_to_name(err));
    }
}

static void audio_task(void *arg) {
    (void)arg;
    knock_request_t request;
    for (;;) {
        if (xQueueReceive(s_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        if (s_cancel) break;
        play_one(&request);
    }
    xSemaphoreGive(s_stopped);
    for (;;) vTaskSuspend(NULL);
}

esp_err_t wf_audio_start(void) {
    if (s_task) return ESP_OK;

    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "codec 初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    err = bsp_audio_set_format(WF_AUDIO_RATE, 16, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "音频格式设置失败: %s", esp_err_to_name(err));
        return err;
    }
    bsp_audio_set_volume(WF_AUDIO_VOLUME);

    s_pcm_capacity = max_sample_count();
    s_pcm = malloc(s_pcm_capacity * sizeof(int16_t));
    if (!s_pcm) {
        ESP_LOGE(TAG, "PCM 缓冲分配失败(%u 字节)", (unsigned)(s_pcm_capacity * 2));
        s_pcm_capacity = 0;
        return ESP_ERR_NO_MEM;
    }

    s_queue = xQueueCreate(WF_AUDIO_QUEUE_LEN, sizeof(knock_request_t));
    s_stopped = xSemaphoreCreateBinary();
    if (!s_queue || !s_stopped) {
        if (s_queue) { vQueueDelete(s_queue); s_queue = NULL; }
        if (s_stopped) { vSemaphoreDelete(s_stopped); s_stopped = NULL; }
        free(s_pcm);
        s_pcm = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_cancel = false;
    if (xTaskCreate(audio_task, "wf_audio", 4096, NULL, 6, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        vSemaphoreDelete(s_stopped);
        s_queue = NULL;
        s_stopped = NULL;
        free(s_pcm);
        s_pcm = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t wf_audio_stop(void) {
    TaskHandle_t task = s_task;
    if (!task) {
        if (s_queue) { vQueueDelete(s_queue); s_queue = NULL; }
        if (s_stopped) { vSemaphoreDelete(s_stopped); s_stopped = NULL; }
        if (s_pcm) { free(s_pcm); s_pcm = NULL; s_pcm_capacity = 0; }
        return ESP_OK;
    }

    s_cancel = true;
    // 唤醒可能正阻塞在队列上的任务。
    knock_request_t wake = {0};
    xQueueSend(s_queue, &wake, 0);
    if (!s_stopped || xSemaphoreTake(s_stopped, pdMS_TO_TICKS(WF_AUDIO_STOP_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "音频任务停止超时");
        return ESP_ERR_TIMEOUT;
    }

    vTaskDelete(task);
    s_task = NULL;
    vQueueDelete(s_queue);
    s_queue = NULL;
    vSemaphoreDelete(s_stopped);
    s_stopped = NULL;
    free(s_pcm);
    s_pcm = NULL;
    s_pcm_capacity = 0;
    return ESP_OK;
}

void wf_audio_knock(uint8_t preset, uint32_t combo) {
    if (!s_queue) return;
    const knock_request_t request = { .preset = preset, .combo = combo };
    // 0 超时:队列满就丢,按键线程绝不等待。
    (void)xQueueSend(s_queue, &request, 0);
}

void wf_audio_set_muted(bool muted) { s_muted = muted; }
