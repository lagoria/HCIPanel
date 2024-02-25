#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/event_groups.h"

#include "wifi_wrapper.h"
#include "http_ota_wrapper.h"


static const char *TAG = "app_http_ota";
static EventGroupHandle_t ota_event_group;
static esp_https_ota_handle_t https_ota_handle = NULL;
static char ota_upgrade_url[256] = {0};
static int interval_ms = 0;
static ota_service_mode_t mode;

#define OTA_ENABLE_BIT  BIT0
#define OTA_BEGIN_BIT   BIT1
#define OTA_SMAE_BIT    BIT2
#define OTA_FINISH_BIT  BIT3
#define OTA_FAIL_BIT    BIT4

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

    /* compare app version */
    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
        ESP_LOGW(TAG, "App version is the same as a new.");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t http_ota_process(esp_https_ota_config_t *ota_config)
{
    https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        xEventGroupClearBits(ota_event_group, OTA_BEGIN_BIT);
        return err;
    }
    xEventGroupSetBits(ota_event_group, OTA_BEGIN_BIT);

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        return err;
    }
    
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        xEventGroupSetBits(ota_event_group, OTA_SMAE_BIT);
        return ESP_OK;
    }

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
    }

    if (err != ESP_OK) {
        return err;
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(TAG, "Complete data was not received.");
        return ESP_FAIL;
    } else {
        err = esp_https_ota_finish(https_ota_handle);
        return err;
    }

}


void http_ota_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting OTA service");

    esp_http_client_config_t config = {
        .url = ota_upgrade_url,
        .event_handler = _http_event_handler,
        .timeout_ms = 3000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    while (1)
    {
        if (mode == OTA_AUTOMATIC) {
            wait_wifi_connect(portMAX_DELAY);
        } else {
            xEventGroupWaitBits(ota_event_group,
                        OTA_ENABLE_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

        }
        /* clear all event bits */
        xEventGroupClearBits(ota_event_group, OTA_ENABLE_BIT | OTA_BEGIN_BIT |
                                            OTA_SMAE_BIT | OTA_FAIL_BIT | OTA_FINISH_BIT);


        esp_err_t err = http_ota_process(&ota_config);
        EventBits_t bits = xEventGroupWaitBits(ota_event_group,
                        OTA_BEGIN_BIT | OTA_SMAE_BIT,
                        pdFALSE, pdFALSE, 0);
        
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA upgrade failed.");
            ESP_LOGD(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", err);
            /* Clean-up HTTPS OTA Firmware upgrade and close HTTPS connection */
            if (bits & OTA_BEGIN_BIT) {
                esp_https_ota_abort(https_ota_handle);
            }
            xEventGroupSetBits(ota_event_group, OTA_FAIL_BIT);
        } else {
            if (bits & OTA_SMAE_BIT) {
                ESP_LOGI(TAG, "Cancel app update.");
                esp_https_ota_abort(https_ota_handle);
            } else {
                ESP_LOGI(TAG, "OTA upgrade successful.");
                xEventGroupSetBits(ota_event_group, OTA_FINISH_BIT);
                if (mode == OTA_AUTOMATIC) {
                    ESP_LOGI(TAG, "Rebooting...");
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    esp_restart();
                }
            }
        }

        if (mode == OTA_AUTOMATIC) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

}



ota_result_t http_ota_wait_result()
{
    ota_result_t status;
    EventBits_t bits = xEventGroupWaitBits(ota_event_group,
                        OTA_FINISH_BIT | OTA_SMAE_BIT | OTA_FAIL_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & OTA_SMAE_BIT) {
        status = OTA_SAME;
    } else if (bits & OTA_FINISH_BIT){
        status = OTA_FINISH;
    } else {
        status = OTA_FAIL;
    }

    return status;
}

void http_ota_service_config(ota_service_config_t *config)
{
    strcpy(ota_upgrade_url, config->url);
    interval_ms = config->interval;
    mode = config->mode;
}


void http_ota_service_start()
{
    /* Ensure to disable any WiFi power save mode, this allows best throughput
     * and hence timings for overall OTA operation.
     */
    esp_wifi_set_ps(WIFI_PS_NONE);

    ota_event_group = xEventGroupCreate();

    xTaskCreate(&http_ota_task, "http_ota_task", 8192, NULL, 5, NULL);
}
