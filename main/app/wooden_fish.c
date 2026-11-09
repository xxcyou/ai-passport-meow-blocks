// main/app/wooden_fish.c —— 功德木鱼:状态、按键语义、页面切换与持久化编排。
//
// 线程模型:
//   按键组件回调(esp_timer 任务) → 只做 xQueueSend,立即返回
//   input 任务                    → 处理事件、访问 LVGL(持 bsp_lvgl_lock)、读电量、写 NVS
//   audio 任务                    → 唯一的 I2S 写入者
//   LVGL 任务                     → 唯一的 lv_timer_handler 调用者(动画定时器在这里跑)
//
// 这样"按键回调不许阻塞""LVGL 非线程安全""慢操作不占 UI"三条约束同时成立。
#include "app/wooden_fish.h"

#include "app/app_fonts.h"
#include "app/app_strings.h"
#include "app/knock_synth.h"
#include "app/merit_logic.h"
#include "app/merit_store.h"
#include "app/wooden_fish_audio.h"
#include "app/wooden_fish_ui.h"

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "wooden_fish";

#define WF_INPUT_QUEUE_DEPTH 8
#define WF_INPUT_TASK_STACK  4096
#define WF_INPUT_TASK_PRIO   5

// 长按之后 400ms 内的单击一律忽略:按键组件在长按释放时可能补一个 click,
// 不挡掉的话"长按 OK 打开确认框"会立刻被当成"确认归零"。
#define WF_CLICK_GUARD_MS 400

// 每 1s 一次的后台节拍:连击超时、电量刷新、延迟落盘。
#define WF_TICK_MS 1000
#define WF_BATTERY_EVERY_TICKS 5
#define WF_SAVE_INTERVAL_MS 5000

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} wf_input_t;

typedef enum {
    WF_PAGE_KNOCK = 0,
    WF_PAGE_STATS,
    WF_PAGE_CONFIRM,
} wf_page_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static volatile bool s_ready;

static merit_state_t s_merit;
static uint8_t s_preset;
static bool s_muted;
static int s_battery_soc = -1;

static wf_page_t s_page;
static uint32_t s_swallow_click_until_ms;
static volatile bool s_dirty;
static uint32_t s_last_save_ms;

// 开机自检用的文案表:与 app_strings.h 保持同源。
static const char *const COVERAGE_TEXT[] = {
    APP_TITLE, APP_UNIT, APP_SESSION_LABEL, APP_TOTAL_LABEL, APP_BEST_COMBO_LABEL,
    APP_COMBO_PREFIX, APP_HINT_KNOCK, APP_HINT_RESET, APP_HINT_STATS, APP_HINT_BACK,
    APP_STATS_TITLE, APP_SOUND_LABEL, APP_MUTE_LABEL, APP_ON, APP_OFF, APP_MUTED,
    APP_SOUND_WOOD, APP_SOUND_BELL, APP_SOUND_DRUM, APP_RESET_TITLE,
    APP_RESET_CONFIRM, APP_RESET_CANCEL,
};

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static wf_view_t current_view(void) {
    wf_view_t view = {
        .total = s_merit.total,
        .session = s_merit.session,
        .best_combo = s_merit.best_combo,
        .preset = s_preset,
        .muted = s_muted,
        .battery_soc = s_battery_soc,
    };
    return view;
}

// 持锁刷新界面。所有 UI 访问都从这里或同类包装进入。
static void refresh_locked(void) {
    if (!bsp_lvgl_lock(200)) return;
    wf_view_t view = current_view();
    wf_ui_refresh(&view);
    bsp_lvgl_unlock();
}

static void persist_now(void) {
    const wf_saved_t saved = {
        .total = s_merit.total,
        .best_combo = s_merit.best_combo,
        .preset = s_preset,
        .muted = s_muted,
    };
    if (wf_store_save(&saved) == ESP_OK) {
        s_dirty = false;
        s_last_save_ms = now_ms();
    }
}

static void do_knock(void) {
    const uint32_t t = now_ms();
    const uint32_t combo = merit_hit(&s_merit, t);
    wf_audio_knock(s_preset, combo);
    s_dirty = true;

    if (bsp_lvgl_lock(200)) {
        wf_view_t view = current_view();
        wf_ui_refresh(&view);
        wf_ui_play_hit(combo);
        bsp_lvgl_unlock();
    }
    ESP_LOGD(TAG, "敲击 #%lu 连击 %lu", (unsigned long)s_merit.total, (unsigned long)combo);
}

static void do_reset(void) {
    merit_reset(&s_merit);
    persist_now();   // 归零是用户明确动作,立即落盘,不等延迟保存
    s_page = WF_PAGE_KNOCK;
    if (bsp_lvgl_lock(200)) {
        wf_ui_set_confirm(false);
        wf_ui_show_knock();
        wf_view_t view = current_view();
        wf_ui_refresh(&view);
        bsp_lvgl_unlock();
    }
    ESP_LOGI(TAG, "功德已归零");
}

static void cycle_preset(void) {
    s_preset = (uint8_t)((s_preset + 1) % KNOCK_PRESET_COUNT);
    s_dirty = true;
    refresh_locked();
    wf_audio_knock(s_preset, 1);   // 立刻试听新音色
    ESP_LOGI(TAG, "音色切换为 %u", (unsigned)s_preset);
}

static void toggle_mute(void) {
    s_muted = !s_muted;
    wf_audio_set_muted(s_muted);
    s_dirty = true;
    refresh_locked();
    ESP_LOGI(TAG, "静音 %s", s_muted ? "开" : "关");
}

static void enter_stats(void) {
    s_page = WF_PAGE_STATS;
    if (bsp_lvgl_lock(200)) {
        wf_view_t view = current_view();
        wf_ui_refresh(&view);
        wf_ui_show_stats();
        bsp_lvgl_unlock();
    }
}

static void leave_stats(void) {
    s_page = WF_PAGE_KNOCK;
    if (bsp_lvgl_lock(200)) {
        wf_ui_show_knock();
        wf_view_t view = current_view();
        wf_ui_refresh(&view);
        bsp_lvgl_unlock();
    }
}

static void open_confirm(void) {
    s_page = WF_PAGE_CONFIRM;
    if (bsp_lvgl_lock(200)) {
        wf_ui_set_confirm(true);
        bsp_lvgl_unlock();
    }
}

static void close_confirm(void) {
    s_page = WF_PAGE_KNOCK;
    if (bsp_lvgl_lock(200)) {
        wf_ui_set_confirm(false);
        wf_ui_show_knock();
        bsp_lvgl_unlock();
    }
}

static void handle_click(bsp_btn_t btn) {
    if (now_ms() < s_swallow_click_until_ms) {
        ESP_LOGD(TAG, "忽略长按后的补发单击");
        return;
    }

    switch (s_page) {
    case WF_PAGE_CONFIRM:
        // 确认框里只有 OK 有确定语义;UP/DOWN 都当作取消,避免误清。
        if (btn == BSP_BTN_OK) {
            do_reset();
        } else {
            close_confirm();
        }
        break;
    case WF_PAGE_STATS:
        break;   // 统计页只接受长按返回
    case WF_PAGE_KNOCK:
    default:
        if (btn == BSP_BTN_OK) do_knock();
        else if (btn == BSP_BTN_UP) cycle_preset();
        else if (btn == BSP_BTN_DOWN) toggle_mute();
        break;
    }
}

static void handle_long(bsp_btn_t btn) {
    s_swallow_click_until_ms = now_ms() + WF_CLICK_GUARD_MS;

    switch (s_page) {
    case WF_PAGE_CONFIRM:
        if (btn == BSP_BTN_OK) do_reset();
        else close_confirm();
        break;
    case WF_PAGE_STATS:
        if (btn == BSP_BTN_OK) leave_stats();
        break;
    case WF_PAGE_KNOCK:
    default:
        if (btn == BSP_BTN_OK) open_confirm();
        else if (btn == BSP_BTN_UP) enter_stats();
        break;
    }
}

static void handle_input(const wf_input_t *input) {
    if (input->event == BSP_BTN_CLICK) handle_click(input->btn);
    else if (input->event == BSP_BTN_LONG) handle_long(input->btn);
    // PRESS/DOUBLE/RELEASE 在本应用里没有语义。
}

static void housekeeping(void) {
    static uint32_t ticks;

    merit_tick(&s_merit, now_ms());

    if (++ticks % WF_BATTERY_EVERY_TICKS == 0) {
        const int soc = bsp_battery_soc();
        if (soc != s_battery_soc) {
            s_battery_soc = soc;
            refresh_locked();
        }
    }

    if (s_dirty && (uint32_t)(now_ms() - s_last_save_ms) >= WF_SAVE_INTERVAL_MS) {
        persist_now();
    }
}

static void input_task(void *arg) {
    (void)arg;
    wf_input_t input;
    for (;;) {
        if (xQueueReceive(s_queue, &input, pdMS_TO_TICKS(WF_TICK_MS)) == pdTRUE) {
            handle_input(&input);
        } else {
            housekeeping();
        }
    }
}

// 按键事件入口:运行在共享 esp_timer 任务,只入队,不做任何耗时操作。
esp_err_t wooden_fish_handle_button(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!s_ready || !s_queue) return ESP_ERR_INVALID_STATE;
    const wf_input_t input = { .btn = btn, .event = ev };
    return xQueueSend(s_queue, &input, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wooden_fish_start(void) {
    if (s_task) return ESP_OK;

    wf_saved_t saved = {0};
    esp_err_t err = wf_store_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 初始化失败(%s),本次运行不保存", esp_err_to_name(err));
        merit_init(&s_merit, 0, 0);
    } else if (wf_store_load(&saved)) {
        s_preset = saved.preset < KNOCK_PRESET_COUNT ? saved.preset : 0;
        s_muted = saved.muted;
        merit_init(&s_merit, saved.total, saved.best_combo);
        ESP_LOGI(TAG, "读回存档:累计 %lu 最高连击 %lu",
                 (unsigned long)s_merit.total, (unsigned long)s_merit.best_combo);
    } else {
        merit_init(&s_merit, 0, 0);
    }

    if (bsp_lvgl_lock(1000)) {
        wf_ui_init();
        const size_t missing = app_fonts_check_coverage(COVERAGE_TEXT,
                                                        sizeof(COVERAGE_TEXT) / sizeof(COVERAGE_TEXT[0]));
        (void)missing;
        wf_view_t view = current_view();
        wf_ui_refresh(&view);
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "无法获取 LVGL 锁,界面未创建");
        return ESP_ERR_TIMEOUT;
    }

    wf_audio_set_muted(s_muted);
    err = wf_audio_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "音频不可用(%s),应用继续静默运行", esp_err_to_name(err));
    }

    s_queue = xQueueCreate(WF_INPUT_QUEUE_DEPTH, sizeof(wf_input_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(input_task, "wf_input", WF_INPUT_TASK_STACK, NULL,
                    WF_INPUT_TASK_PRIO, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    // 按键由 main.c 注册回调;这里只把队列标记为可用。
    s_ready = true;
    s_last_save_ms = now_ms();
    ESP_LOGI(TAG, "功德木鱼就绪:累计 %lu,音色 %u,静音 %d",
             (unsigned long)s_merit.total, (unsigned)s_preset, (int)s_muted);
    return ESP_OK;
}

void wooden_fish_stop(void) {
    s_ready = false;
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    if (s_dirty) persist_now();
    (void)wf_audio_stop();
    if (bsp_lvgl_lock(500)) {
        wf_ui_destroy();
        bsp_lvgl_unlock();
    }
}
