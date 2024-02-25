#ifndef HTTP_OTA_H
#define HTTP_OTA_H

/**
 * 
 * require components: esp_https_ota app_update
 * 
 * enabled option esp_http_client in config
 * 
 */


typedef enum {
    OTA_SAME,       // app version as same as new
    OTA_FINISH,     // OTA upgrade finished
    OTA_FAIL,       // OTA upgrade failed
} ota_result_t;

typedef enum {
    OTA_AUTOMATIC = 1,  // automatic OTA upgrade
    OTA_MANUAL,         // manual OTA upgrade
} ota_service_mode_t;


typedef struct {
    char url[256];              // HTTP固件URL
    ota_service_mode_t mode;    // 服务工作模式
    int interval;               // 自动模式下OTA更新间隔(ms)
} ota_service_config_t;


/*--------------function declarations-----------*/

/**
 * @brief 等待OTA结束，返回状态
 * 
 * @return ota_result_t 
 */
ota_result_t http_ota_wait_result();

/**
 * @brief 配置OTA服务，确定URL
 * 
 * @param config 
 */
void http_ota_service_config(ota_service_config_t *config);

/**
 * @brief OTA初始化，创建任务
 * 
 */
void http_ota_service_start();

#endif