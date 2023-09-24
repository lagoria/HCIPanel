file(GLOB_RECURSE srcs src/*.c)

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "include"
                    REQUIRES driver
                    REQUIRES esp_timer 
                    REQUIRES esp_wifi
                    REQUIRES bt
                    REQUIRES nvs_flash
                    REQUIRES json
                    REQUIRES lvgl
                    REQUIRES lvgl_esp32_drivers
                    )
