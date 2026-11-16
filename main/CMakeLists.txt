idf_component_register(
    SRCS "main.c"
         "demo_navigation.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "demo_display.c"
         "demo_button.c"
         "demo_audio.c"
         "demo_battery.c"
         "demo_radio.c"
         "demo_wifi.c"
         "demo_ble.c"
         "demo_low_power.c"
         "app/app_fonts.c"
         "app/catcube_logic.c"
         "app/catcube_levels.c"
         "app/catcube_levels_data.c"
         "app/catcube_levels_extra.c"
         "app/catcube_audio.c"
         "app/catcube_ui.c"
         "app/catcube.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_http_server esp_netif esp_timer esp_wifi nvs_flash
)

# 生成的 CJK 子集字库。由 tools/gen_app_fonts.py 生成,不要手改。
# 用 target_sources 追加,避免和上面的大列表混在一起。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_cjk_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/app_cjk_24.c"
)
