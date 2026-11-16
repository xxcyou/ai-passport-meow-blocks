// main/app/catcube.c —— 「喵喵方块」应用编排:进度存档、输入任务、界面生命周期。
//
// 线程模型(与功德木鱼一致):
//   按键组件回调(esp_timer 任务) → 只做 xQueueSend,立即返回
//   input 任务                    → 取 LVGL 锁 → 交给界面层 → 解锁;必要时写 NVS
//   LVGL 任务                     → 唯一的 lv_timer_handler 调用者(自绘回调在这里跑)
//
// 进度存在 NVS 的一个 blob 里:每关一个字节,0=未解锁 1=已解锁 2=已通关。
// 80 关 = 80 字节,一次写完,不需要每关一个键。
#include "app/catcube.h"

#include "app/catcube_audio.h"
#include "app/catcube_logic.h"

#include "app/app_fonts.h"
#include "app/catcube_levels.h"
#include "app/catcube_logic.h"
#include "app/catcube_ui.h"

#include "bsp_audio.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "catcube";

#define CAT_NVS_NAMESPACE "catcube"
#define CAT_NVS_KEY       "progress"
#define CAT_NVS_AUDIO     "audio"

#define CAT_QUEUE_DEPTH   8
#define CAT_TASK_STACK    4096
#define CAT_TASK_PRIO     5
#define CAT_IDLE_POLL_MS  200
#define CAT_BATTERY_US    (5LL * 1000 * 1000)

// 方向键长按 = 快速移动:按下先走一格,超过 HOLD_DELAY 之后每 HOLD_PERIOD 再走一格。
// 按键组件只给 PRESS/RELEASE,不给自动重复,所以重复由输入任务自己发。
#define CAT_HOLD_DELAY_US  (300LL * 1000)
#define CAT_HOLD_PERIOD_US (90LL * 1000)

// 长按之后 400ms 内的单击一律忽略:按键组件在长按释放时会补一个 click,
// 不挡掉的话"长按 OK 打开菜单"会立刻被当成"菜单第一项"。
#define CAT_CLICK_GUARD_MS 400

typedef enum {
    CAT_EV_KEY = 0,
} cat_msg_kind_t;

typedef struct {
    cat_msg_kind_t kind;
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
} cat_msg_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static volatile bool s_ready;

static nvs_handle_t s_nvs;
static bool s_nvs_ok;

// 音效设置:音量 0..100 + 静音开关,单独一个键,和进度互不影响。

static int64_t s_long_guard_us;

// ---------------------------------------------------------------------------
// 进度存档(无尽模式:只存"已通关到第几关")
// ---------------------------------------------------------------------------
// 关卡没有上限,不能像固定关卡表那样每关存一个字节。这里存一个连续计数:
// 已通关 1..cleared,可玩 1..cleared+1。顺序解锁,所以一个 uint32 就够。
#define CAT_PROGRESS_MAGIC   0x42554343u /* "CCUB" */
#define CAT_PROGRESS_VERSION 2u
#define CAT_PROGRESS_LEGACY  80 // 旧固件:每关一字节

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t cleared;
} cat_progress_t;

static nvs_handle_t s_nvs;
static bool s_nvs_ok;

// 音效设置:音量 0..100 + 静音开关,单独一个键,和进度互不影响。
typedef struct {
    uint8_t volume;
    uint8_t muted;
} cat_audio_cfg_t;
static cat_audio_cfg_t s_audio_cfg = {.volume = 55, .muted = 0};
static cat_progress_t s_progress;

static void progress_save(void);

static void progress_load(void) {
    s_progress.magic = CAT_PROGRESS_MAGIC;
    s_progress.version = CAT_PROGRESS_VERSION;
    s_progress.reserved = 0;
    s_progress.cleared = 0;

    if (!s_nvs_ok) return;
    size_t len = 0;
    esp_err_t err = nvs_get_blob(s_nvs, CAT_NVS_KEY, NULL, &len);
    if (err != ESP_OK || len == 0) {
        ESP_LOGI(TAG, "没有进度存档,从第 1 关开始");
        return;
    }

    if (len == sizeof(cat_progress_t)) {
        cat_progress_t rec;
        if (nvs_get_blob(s_nvs, CAT_NVS_KEY, &rec, &len) == ESP_OK &&
            rec.magic == CAT_PROGRESS_MAGIC && rec.version == CAT_PROGRESS_VERSION) {
            s_progress.cleared = rec.cleared;
            ESP_LOGI(TAG, "读取进度:已通关 %u 关", (unsigned)s_progress.cleared);
            return;
        }
    } else if (len == CAT_PROGRESS_LEGACY) {
        // 旧存档迁移:数一下从第 1 关起连续通关了多少关(2 = 已通关)。
        uint8_t legacy[CAT_PROGRESS_LEGACY];
        if (nvs_get_blob(s_nvs, CAT_NVS_KEY, legacy, &len) == ESP_OK) {
            uint32_t cleared = 0;
            for (int i = 0; i < CAT_PROGRESS_LEGACY; i++) {
                if (legacy[i] == 2) cleared = (uint32_t)i + 1;
            }
            s_progress.cleared = cleared;
            ESP_LOGI(TAG, "旧存档迁移:已通关 %u 关", (unsigned)cleared);
            progress_save();
            return;
        }
    }
    ESP_LOGW(TAG, "进度存档无法识别,从第 1 关开始");
}

static void audio_cfg_load(void) {
    if (!s_nvs_ok) return;
    cat_audio_cfg_t cfg;
    size_t len = sizeof(cfg);
    if (nvs_get_blob(s_nvs, CAT_NVS_AUDIO, &cfg, &len) == ESP_OK && len == sizeof(cfg) &&
        cfg.volume <= 100) {
        s_audio_cfg = cfg;
    }
}

static void audio_cfg_save(void) {
    if (!s_nvs_ok) return;
    (void)nvs_set_blob(s_nvs, CAT_NVS_AUDIO, &s_audio_cfg, sizeof(s_audio_cfg));
    (void)nvs_commit(s_nvs);
}

static int hook_volume_get(void) { return (int)s_audio_cfg.volume; }

static void hook_volume_set(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_audio_cfg.volume = (uint8_t)percent;
    bsp_audio_set_volume((uint8_t)percent);
    audio_cfg_save();
}

static bool hook_muted_get(void) { return s_audio_cfg.muted != 0; }

static void hook_muted_set(bool muted) {
    s_audio_cfg.muted = muted ? 1 : 0;
    cat_audio_set_muted(muted);
    audio_cfg_save();
}

static void hook_reset_progress(void) {
    s_progress.cleared = 0;
    progress_save();
    ESP_LOGW(TAG, "进度已重置");
}

static void progress_save(void) {
    if (!s_nvs_ok) return;
    esp_err_t err = nvs_set_blob(s_nvs, CAT_NVS_KEY, &s_progress, sizeof(s_progress));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGW(TAG, "写入进度失败: %s", esp_err_to_name(err));
}

static bool hook_is_cleared(int level) {
    if (level < 1) return false;
    return (uint32_t)level <= s_progress.cleared;
}

static int hook_highest_unlocked(void) { return (int)s_progress.cleared + 1; }

static void hook_mark_cleared(int level) {
    if (level < 1) return;
    if ((uint32_t)level > s_progress.cleared) {
        s_progress.cleared = (uint32_t)level;
        ESP_LOGI(TAG, "通关第 %d 关", level);
        progress_save();
    }
}

static const catcube_ui_hooks_t HOOKS = {
    .is_cleared = hook_is_cleared,
    .highest_unlocked = hook_highest_unlocked,
    .mark_cleared = hook_mark_cleared,
    .volume_get = hook_volume_get,
    .volume_set = hook_volume_set,
    .muted_get = hook_muted_get,
    .muted_set = hook_muted_set,
    .reset_progress = hook_reset_progress,
};

// ---------------------------------------------------------------------------
// 输入任务
// ---------------------------------------------------------------------------
// 当前被按住的键:0 = 没有,1 = 上键(向右),2 = 下键(向下)。
// 只在按键回调里写、输入任务里读,都是整型赋值,不需要额外同步。
static uint32_t s_elapsed_ms; // 当前关卡用时(毫秒),教学弹窗期间冻结
static volatile int s_held_dir;
static int64_t s_held_since_us;
static int64_t s_last_repeat_us;

static void ui_dispatch(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (bsp_lvgl_lock(200)) {
        catcube_ui_handle_key(btn, ev);
        bsp_lvgl_unlock();
    } else {
        ESP_LOGW(TAG, "取 LVGL 锁超时,丢弃一次按键");
    }
}

static void input_task(void *arg) {
    (void)arg;
    int64_t last_tick_us = esp_timer_get_time();
    int64_t level_start_us = esp_timer_get_time();
    uint32_t last_serial = 0;
    for (;;) {
        const bool held = s_held_dir != 0;
        TickType_t wait = held ? pdMS_TO_TICKS(20) : pdMS_TO_TICKS(CAT_IDLE_POLL_MS);
        // 通关演出期间要 25fps 左右推进动画
        if (bsp_lvgl_lock(0)) {
            const bool anim = catcube_ui_anim_active();
            bsp_lvgl_unlock();
            if (anim) wait = pdMS_TO_TICKS(40);
        }

        cat_msg_t msg;
        if (xQueueReceive(s_queue, &msg, wait) == pdTRUE) {
            if (msg.kind != CAT_EV_KEY) continue;
            const int64_t now = esp_timer_get_time();
            switch (msg.ev) {
                case BSP_BTN_PRESS:
                    if (msg.btn == BSP_BTN_UP || msg.btn == BSP_BTN_DOWN) {
                        s_held_dir = (msg.btn == BSP_BTN_UP) ? 1 : 2;
                        s_held_since_us = now;
                        s_last_repeat_us = now;
                        ui_dispatch(msg.btn, BSP_BTN_PRESS);
                    } else {
                        ui_dispatch(msg.btn, BSP_BTN_PRESS);
                    }
                    break;
                case BSP_BTN_RELEASE:
                    if ((msg.btn == BSP_BTN_UP && s_held_dir == 1) ||
                        (msg.btn == BSP_BTN_DOWN && s_held_dir == 2)) {
                        s_held_dir = 0;
                    }
                    break;
                case BSP_BTN_LONG:
                    if (msg.btn == BSP_BTN_OK) {
                        s_long_guard_us = now + (int64_t)CAT_CLICK_GUARD_MS * 1000;
                        ui_dispatch(msg.btn, BSP_BTN_LONG);
                    }
                    break;
                case BSP_BTN_CLICK:
                    if (now < s_long_guard_us) break;
                    ui_dispatch(msg.btn, BSP_BTN_CLICK);
                    break;
                case BSP_BTN_DOUBLE:
                    if (msg.btn == BSP_BTN_OK) ui_dispatch(msg.btn, BSP_BTN_DOUBLE);
                    break;
                default:
                    break;
            }
        }

        const int64_t now = esp_timer_get_time();
        if (s_held_dir != 0 && now - s_held_since_us >= CAT_HOLD_DELAY_US &&
            now - s_last_repeat_us >= CAT_HOLD_PERIOD_US) {
            s_last_repeat_us = now;
            ui_dispatch(s_held_dir == 1 ? BSP_BTN_UP : BSP_BTN_DOWN, BSP_BTN_PRESS);
        }

        // 通关演出:每帧推进
        if (bsp_lvgl_lock(200)) {
            if (catcube_ui_anim_active()) catcube_ui_tick_anim(40);
            bsp_lvgl_unlock();
        }

        // 换关就把本关计时清零
        uint32_t serial = 0;
        if (bsp_lvgl_lock(0)) {
            serial = catcube_ui_level_serial();
            bsp_lvgl_unlock();
        }
        if (serial != last_serial) {
            last_serial = serial;
            level_start_us = now;
        }

        // 计时:每 200ms 推一次(限时关需要秒级精度)。教学弹窗打开时冻结。
        if (now - last_tick_us >= 200000) {
            last_tick_us = now;
            bool running = true;
            int level = 0;
            if (bsp_lvgl_lock(200)) {
                running = catcube_ui_clock_running();
                level = catcube_ui_current_level();
                bsp_lvgl_unlock();
            }
            if (!running) {
                // 冻结:把起点往后挪,elapsed 保持不变
                level_start_us = now - (int64_t)s_elapsed_ms * 1000;
            } else {
                s_elapsed_ms = (uint32_t)((now - level_start_us) / 1000);
            }
            const uint32_t limit = level > 0 ? catcube_time_limit_ms(level) : 0;
            if (bsp_lvgl_lock(200)) {
                catcube_ui_set_clock(s_elapsed_ms, limit);
                bsp_lvgl_unlock();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
esp_err_t catcube_handle_button(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!s_ready || !s_queue) return ESP_ERR_INVALID_STATE;
    cat_msg_t msg = {.kind = CAT_EV_KEY, .btn = btn, .ev = ev};
    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t catcube_start(void) {
    if (s_ready) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要重建:%s", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err == ESP_OK) {
        err = nvs_open(CAT_NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
        s_nvs_ok = (err == ESP_OK);
    }
    if (!s_nvs_ok) {
        // 存档不可用不影响玩:只是每次开机都从第 1 关开始。
        ESP_LOGW(TAG, "NVS 不可用,进度不会保存:%s", esp_err_to_name(err));
    }
    progress_load();
    audio_cfg_load();

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "取 LVGL 锁失败,无法建立界面");
        return ESP_ERR_TIMEOUT;
    }
    app_fonts_init();
    catcube_ui_init(&HOOKS);
    bsp_lvgl_unlock();

    // codec/I2S 初始化会走 I2C + 建任务,放在 LVGL 锁外面做,别占着锁。
    if (cat_audio_init()) { // 失败就静音,不影响玩
        bsp_audio_set_volume(s_audio_cfg.volume);
        cat_audio_set_muted(s_audio_cfg.muted != 0);
    }

    s_queue = xQueueCreate(CAT_QUEUE_DEPTH, sizeof(cat_msg_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "按键队列创建失败");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(input_task, "catcube_in", CAT_TASK_STACK, NULL, CAT_TASK_PRIO, &s_task) !=
        pdPASS) {
        ESP_LOGE(TAG, "输入任务创建失败");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    ESP_LOGI(TAG, "喵喵方块就绪:已通关 %u 关,当前第 %d 关", (unsigned)s_progress.cleared,
             hook_highest_unlocked());
    return ESP_OK;
}

void catcube_stop(void) {
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
    cat_audio_deinit();
    if (bsp_lvgl_lock(500)) {
        catcube_ui_deinit();
        bsp_lvgl_unlock();
    }
    if (s_nvs_ok) {
        nvs_close(s_nvs);
        s_nvs_ok = false;
    }
}
