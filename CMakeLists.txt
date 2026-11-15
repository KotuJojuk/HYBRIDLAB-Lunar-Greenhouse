idf_component_register(
    SRCS
        "main.c"
        "hidrolab_control.c"
        "hidrolab_buzzer.c"
        "hidrolab_scheduler.c"
        "hidrolab_settings.c"
        "hidrolab_wifi.c"
        "hidrolab_time.c"
        "hidrolab_font_tr.c"
        "hidrolab_assets.c"
        "hidrolab_brand_asset.c"
        "hidrolab_ui.c"
        "hidrolab_simulation.c"
        "hidrolab_simulation_ui.c"
        "hidrolab_simulation_runtime.c"
        "hidrolab_simulation_store.c"
        "hidrolab_nasa_data.c"
        "hidrolab_science_questions.c"

        "waveshare_rgb_lcd_port.c"
        "lvgl_port.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer nvs_flash esp_wifi esp_event esp_netif lwip spiffs
)

spiffs_create_partition_image(spiffs spiffs FLASH_IN_PROJECT)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
