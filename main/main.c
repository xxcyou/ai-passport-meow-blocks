// main/main.c —— 功德木鱼应用入口:硬件装配 + 按键接线。
//
// 启动顺序:共享 I2C → 显示 → LVGL → 背光 → 电量计 → 应用(界面/音频/NVS)
// → 按键驱动。按键回调留在本文件:它是"硬件事件 → 应用事件"的接线处,
// 唤醒键守卫也在这里(见 on_key)。
//
// 显示或 LVGL 起不来就没有 UI 可言,直接报错返回;电量计、音频属于可选外设,
// 单项失败只降级,不影响敲木鱼本身。
#include "app/wooden_fish.h"

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "key_guard.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "main";

// 按键唤醒时那颗键还按着,组件会把它判成长按并立刻触发动作。先挡住,
// 直到该键松手;10s 只是兜底,防止卡住的按键让整块板子失去响应。
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)

static key_guard_t s_key_guard;
static volatile bool s_input_ready;

// 按键组件回调运行在共享 esp_timer 任务:只做有界操作,绝不阻塞、不碰 LVGL。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // 先消费守卫再判断就绪:松手事件可能早于 s_input_ready 到达,
    // 若被就绪检查提前 return,守卫会一直武装着吞掉下一次真实按键。
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    if (!s_input_ready) return;
    (void)wooden_fish_handle_button(btn, ev);
}

void app_main(void) {
    ESP_LOGI(TAG, "功德木鱼启动(FoloToy AI Passport)");

    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & ~(1u << ESP_SLEEP_WAKEUP_UNDEFINED)) {
        ESP_LOGI(TAG, "休眠唤醒原因位图: 0x%02x", (unsigned)wake_causes);
    }
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    // 90% 而不是 100%:小屏近距离看,满背光偏刺眼,也省一点电。
    bsp_display_backlight(90);

    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用,界面显示 %s", "--");
    }

    esp_err_t err = wooden_fish_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "应用启动失败: %s", esp_err_to_name(err));
        return;
    }

    err = bsp_button_init(on_key, NULL);
    if (err != ESP_OK) {
        // 没有按键就没有交互,但屏幕与计数仍然在:明确记日志,不假装成功。
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(err));
        return;
    }

    if (s_key_guard.armed) {
        // 唤醒键若在按键驱动启动前就松手了,组件不会产生 release 事件,
        // 守卫会一直拦到兜底。初始化完成后按真实电压同步一次。
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键在按键驱动启动前已松手,提前撤防");
        }
    }

    s_input_ready = true;
    ESP_LOGI(TAG, "就绪");
}
