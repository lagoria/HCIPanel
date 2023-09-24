#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "app_nvs.h"

#define TAG                     "app_nvs"


/**
 * save data to nvs
*/
esp_err_t nvs_store_data(const char *namespace, const char *key, 
                        void *data, size_t size)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, key, data, size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) writing data to NVS!", esp_err_to_name(err));
        return err;
    }
    nvs_commit(nvs_handle);     // 更改写入物理存储
    nvs_close(nvs_handle);
    return ESP_OK;
}


/**
 * read data from nvs
*/
esp_err_t nvs_load_data(const char *namespace, const char *key, 
                        void *data, size_t size)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }

    err = nvs_get_blob(nvs_handle, key, data, &size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) loading data to NVS!", esp_err_to_name(err));
        return err;
    }
    nvs_close(nvs_handle);
    return ESP_OK;
}


