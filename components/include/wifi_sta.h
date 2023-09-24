#ifndef WIFI_STA_H
#define WIFI_STA_H

#include "esp_wifi.h"
#include "esp_netif.h"


/*-------------------------------------*/

// 初始化wifi station模式
void wifi_init_sta(void);
// 获取本机IP地址
esp_ip4_addr_t get_local_ip_addr();
// 等待wifi连接
bool wait_wifi_connect();

#endif