#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_gpio.h"



static const char *TAG = "app_gpio";


void rgb_light_init()
{
    ESP_LOGI(TAG, "RGB light init");
    gpio_reset_pin(RGB_PIN);
    gpio_set_direction(RGB_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RGB_PIN, 0);
}