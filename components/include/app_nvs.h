#ifndef APP_NVS_H
#define APP_NVS_H

#include "nvs_flash.h"
#include "nvs.h"




/*--------------------------------------------------------------*/


// 通用函数，根据数据对象进行NVS存储操作
esp_err_t nvs_store_data(const char *namespace, const char *key, 
                        void *data, size_t size);
// 通用函数，根据数据对象进行NVS读取操作
esp_err_t nvs_load_data(const char *namespace, const char *key, 
                        void *data, size_t size);



#endif