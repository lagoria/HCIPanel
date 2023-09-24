#ifndef SOCK_CLIENT_H
#define SOCK_CLIENT_H

#include "sys/socket.h"
#include "netdb.h"
#include "errno.h"

#include "app_config.h"

#define INVALID_SOCK            (-1)

#ifndef SOCK_RX_BUFFER_LEN
#define SOCK_RX_BUFFER_LEN      256
#endif

typedef struct {
    int sock;
    uint8_t *data;
    int len;
} client_sock_data_t;

typedef struct {
    uint8_t head;
    uint8_t type;
    uint8_t goal;
    uint8_t source;
    uint32_t length;
} frame_header_info_t;

typedef struct {
    frame_header_info_t frame;
    int                 sock;
    uint8_t             *data;
    int                 len;
} sock_package_info_t;

/* date frame type decline */
typedef enum {
    FRAME_TYPE_NULL = 0,        // 类型为空
    FRAME_TYPE_DIRECT,          // 直达,客户端发给服务器
    FRAME_TYPE_TRANSMIT,        // 转发,客户端发给客户端
    FRAME_TYPE_RESPOND,         // 回应,来自服务器的回应
    FRAME_TYPE_NOTIFY,          // 通知,来自服务器的通知

    FRAME_TYPE_MAX = 0xFF,      // 类型无效
} frame_type_t;


typedef void (*sock_recv_callback_t)(client_sock_data_t);

/*--------------------------------------------*/

// 套接字发送数据
int socket_send(const int sock, const char * data, const size_t len);

// 注册tcp套接字接收函数
void sock_register_callback(sock_recv_callback_t callback_func);
// 创建UDP广播，TCP客户端任务
void create_client_sock_task();
// 注册客户端身份信息
void register_client_info(int sock, uint8_t *tx_buffer);

int data_frame_send(int sock, uint8_t *frame, frame_type_t type, uint8_t target_id, 
                    uint8_t local_id, uint32_t len, char *data);



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