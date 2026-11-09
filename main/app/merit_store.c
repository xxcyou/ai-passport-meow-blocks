// main/app/merit_store.c —— 功德数据的 NVS 持久化实现。
#include "app/merit_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wf_store";

#define WF_NVS_NAMESPACE "wf_merit"
#define WF_NVS_VERSION   1

esp_err_t wf_store_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 分区被裁剪或升级:擦除后重建。只有这里会清掉旧记录,并明确记日志。
        ESP_LOGW(TAG, "NVS 需要擦除重建: %s", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) return err;
        err = nvs_flash_init();
    }
    return err;
}

static bool read_u32(nvs_handle_t handle, const char *key, uint32_t *out) {
    uint32_t value = 0;
    if (nvs_get_u32(handle, key, &value) != ESP_OK) return false;
    *out = value;
    return true;
}

bool wf_store_load(wf_saved_t *out) {
    if (!out) return false;

    nvs_handle_t handle;
    if (nvs_open(WF_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;

    wf_saved_t value = {0};
    bool ok = read_u32(handle, "total", &value.total);
    read_u32(handle, "best", &value.best_combo);

    uint32_t preset = 0;
    if (read_u32(handle, "preset", &preset)) value.preset = (uint8_t)preset;
    uint32_t muted = 0;
    if (read_u32(handle, "muted", &muted)) value.muted = muted != 0;

    nvs_close(handle);
    if (!ok) return false;
    *out = value;
    return true;
}

esp_err_t wf_store_save(const wf_saved_t *in) {
    if (!in) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WF_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u32(handle, "ver", WF_NVS_VERSION);
    if (err == ESP_OK) err = nvs_set_u32(handle, "total", in->total);
    if (err == ESP_OK) err = nvs_set_u32(handle, "best", in->best_combo);
    if (err == ESP_OK) err = nvs_set_u32(handle, "preset", in->preset);
    if (err == ESP_OK) err = nvs_set_u32(handle, "muted", in->muted ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "保存失败: %s", esp_err_to_name(err));
    }
    return err;
}
