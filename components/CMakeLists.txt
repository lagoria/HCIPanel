file(GLOB_RECURSE srcs src/*.c)

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "include"
                    REQUIRES driver
                        esp_timer 
                        esp_wifi
                        bt
                        nvs_flash
                        json
                        lvgl
                        lvgl_esp32_drivers
                        esp_https_ota
                        app_update
                        )
