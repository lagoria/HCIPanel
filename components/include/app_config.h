#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*----sock_client configure----*/
#define BROADCAST_PORT              8080
#define BIND_UDP_PORT               8888
#define TCP_SERVER_NAME             "SOFTAP_SERVER"      // 目标TCP服务器名

#define LOCAL_DEVICE_MARK           "MASTER"          // 客户端标识符

#define SOCK_RX_BUFFER_LEN          4 * 1024            // 阻塞接收缓存大小
#define APP_TCP_RX_BUF_LEN          24 * 1024           // 数据帧接收缓存大小
#define APP_TCP_TX_BUF_LEN          1024                // 数据帧发送缓存大小
#define IMAGE_BUFFER_SIZE           24 * 1024

/*----wifi_sta configure----*/
#define WIFI_AP_SSID		    "ESP32_SOFTAP"			// WIFI 网络名称
#define WIFI_AP_PAS			    "3524791358"			// WIFI 密码
#define WIFI_MAXIMUM_RETRY      100                     // WiFi连接次数

/*----app_gatts configure-----*/
#define BLE_DEVICE_NAME         "ESP32_MASTER"

/*------camera configure-------*/
#define CAMERA_DEVICE       "ESP32-CAM"
#define CMD_KEY_IMAGE       "image"

#define CMD_VALUE_PICTURE           "picture"
#define CMD_VALUE_VIDEO             "video"

/* public 指令定义  */
#define CMD_KEY_STATUS              "status"
#define CMD_KEY_NAME                "name"
#define CMD_KEY_UDP_REQUEST         "request"
#define CMD_KEY_COMMAND             "command"
#define CMD_KEY_UUID                "id"

#define CMD_VALUE_REGISTER          "register"
#define CMD_VALUE_LIST              "list"
#define CMD_VALUE_UUID              "uuid"
#define CMD_VALUE_SUCCESS           "succeed"


#endif

