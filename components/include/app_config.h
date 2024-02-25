#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define APP_OTA_URL         "http://192.168.4.254:8070/hci_panel.bin"

/*----sock_client configure----*/


#define LOCAL_DEVICE_MARK           "HCIPanel"          // 客户端标识符

#define IMAGE_BUFFER_SIZE           24 * 1024

/*----wifi_sta configure----*/
#define SOFTAP_SERVER_IP        "192.168.4.1"
#define WIFI_AP_SSID		    "ESP-SOFTAP"			// WIFI 网络名称
#define WIFI_AP_PAS			    "3325035137"			// WIFI 密码

#define SOFTAP_UDP_PORT              8080
#define SOFTAP_TCP_PORT              8888
#define TCP_SERVER_NAME             "SOFTAP_SERVER"      // 目标TCP服务器名

/*----app_gatts configure-----*/
#define BLE_DEVICE_NAME         "HCIPanel"

/*------camera configure-------*/
#define CAMERA_DEVICE       "ESP32-CAM"
#define CMD_KEY_IMAGE       "image"

#define CMD_VALUE_PICTURE           "picture"
#define CMD_VALUE_VIDEO             "video"

/* public 指令定义  */
// 服务器回应状态
#define CMD_KEY_STATUS              "status"
#define CMD_VALUE_SUCCESS           "succeed"

#define CMD_KEY_NAME                "name"
#define CMD_KEY_UUID                "id"
#define CMD_KEY_IP                  "ip"
#define CMD_KEY_PORT                "port"

// 请求设备服务，获取服务器地址
#define CMD_KEY_REQUEST             "request"
#define CMD_VALUE_SERVICE           "service"

// 服务器操作命令
#define CMD_KEY_COMMAND             "command"
#define CMD_VALUE_REGISTER          "register"
#define CMD_VALUE_LIST              "list"
#define CMD_VALUE_UUID              "uuid"


typedef struct {
    uint8_t head;
    uint8_t type;
    uint8_t goal;
    uint8_t source;
    uint32_t length;
} frame_header_info_t;

/* date frame type decline */
typedef enum {
    FRAME_TYPE_INVALID = 0,     // 无效类型
    FRAME_TYPE_DIRECT,          // 直达,客户端发给服务器
    FRAME_TYPE_TRANSMIT,        // 转发,客户端发给客户端
    FRAME_TYPE_RESPOND,         // 回应,来自服务器的回应
    FRAME_TYPE_NOTIFY,          // 通知,来自服务器的通知

    FRAME_TYPE_MAX = 0xFF,      // 类型无效
} frame_type_t;

/** socket date frame format*/

/* --------------------------------
1 byte          FRAME_HEADER        帧头
1 byte          frame_type_t        帧类型
1 byte          uint8_t             目标设备ID
1 byte          uint8_t             发送方设备ID
4 byte          uint32_t            数据长度
......                              数据
-------------------------------- */

#define FRAME_SERVER_ID         0x10        // 服务器固定ID
#define FRAME_INVALID_ID        0xF0        // 无效ID
#define FRAME_HEAD              0xAA        // 帧头标志(1010 1010)

#define FRAME_HEADER_LEN            8
#define FRAME_HEAD_BIT              0
#define FRAME_TYPE_BIT              1
#define FRAME_TARGET_BIT            2
#define FRAME_LOCAL_BIT             3
#define FRAME_LENGTH_BIT            4
#define FRAME_DATA_BIT              8

#endif

