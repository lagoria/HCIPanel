#ifndef WIFI_WRAPPER_H
#define WIFI_WRAPPER_H

#include "esp_wifi.h"
#include "esp_netif.h"
#include "netdb.h"

#define CONFIG_WIFI_CHANNEL     7           // WiFi广播信道
#define WIFI_SOFTAP_MAXCON      8           // 最多连接数量（最多10个）


typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
} wifi_account_config_t;


struct wifi_sta_info_wrapper {  
    uint8_t mac[6];  
    uint8_t aid;
    uint8_t ip[4];  
    struct wifi_sta_info_wrapper *next;  
};  
typedef struct wifi_sta_info_wrapper sta_info_wrapper_t;


/*--------------function declarations-----------*/

/**
 * @brief 初始化软AP,创建WiFi
 * 
 * @param account wifi账户配置
 */
void wifi_softap_init(wifi_account_config_t account);

/**
 * @brief Get the local ip addr object
 * 
 * @return char* 
 */
char* get_local_ip_addr(void);

/**
 * @brief Get the wifi sta list info object
 * 
 * @return sta_info_wrapper_t* 
 */
sta_info_wrapper_t * get_wifi_sta_list_info(void);

/*--------------station------------------*/

// 初始化wifi station模式
void wifi_sta_init(wifi_account_config_t account);
// 等待wifi连接
bool wait_wifi_connect(uint32_t wait_time);
// wifi连接重置配置
void wifi_sta_connect_reset(wifi_account_config_t account);

#endif