#ifndef SOCKET_WRAPPER_H
#define SOCKET_WRAPPER_H

#include "sys/socket.h"
#include "netdb.h"
#include "errno.h"
#include "esp_netif.h"


#ifndef SOCK_BUFFER_SIZE
#define SOCK_BUFFER_SIZE        2 * 1024         // 套接字数据缓存大小(参考MTU大小)
#endif

#define INVALID_SOCK            (-1)



/* ----------------type define----------------*/

/* tcp socket 接收回调参数类型 */
typedef struct {
    int                 socket;
    uint8_t             *data;
    int                 len;
    uint8_t             mark;                   // 客户端实例标识
} tcp_socket_info_t;

/* tcp 接收回调函数类型 */
typedef void (*tcp_recv_callback_t)(tcp_socket_info_t);

typedef struct {
    int                 socket;
    uint8_t             *data;
    int                 len;
    struct sockaddr_in  *source_addr;
    uint8_t             mark;                   // 客户端实例标识
} udp_socket_info_t;

/* udp 接收回调函数类型 */
typedef void (*udp_recv_callback_t)(udp_socket_info_t);


typedef struct {
    int                 socket;
    struct sockaddr_in  *target_addr;       // TCP通信则为NULL
    uint8_t  mark;                          // 客户端实例标识
} socket_connect_info_t;

/* 服务器连接成功回调函数类型 */
typedef void (*socket_connect_callback_t)(socket_connect_info_t);

/* -------- server type define-------*/
typedef struct {
    uint16_t listen_port;
    uint8_t  max_conn_num;
} socket_server_config_t;


struct tcp_client_info {
    /* 自动获取 */
    int socket;                 // 套接字
    uint8_t id;                 // 客户端设备标识码
    char ip[16];                // 客户端ip
    int  port;                  // 客户端端口
    /* 手动获取 */
    char name[32];              // 客户端名
    struct tcp_client_info *next;
};

typedef struct tcp_client_info tcp_client_info_t;


/* -------- client type define-------*/

typedef struct {
    char server_ip[16];
    uint16_t server_port;
    uint8_t  mark;              // 客户端标识
} tcp_clinet_config_t;

typedef struct {
    char server_ip[16];
    uint16_t server_port;
    uint16_t bind_port;
    uint8_t  mark;              // 客户端标识
} udp_clinet_config_t;


/*-----------------------function define-------------------------------*/

// 通过套接字发送数据
int socket_send(const int sock, const uint8_t * data, const size_t len);

/* -------- server function define -------*/

// tcp套接字注册接收回调函数
void tcp_server_register_callback(tcp_recv_callback_t callback_func);

// udp广播套接字接收回调函数注册
void udp_server_register_callback(udp_recv_callback_t callback_func);

// 获取客户端信息
tcp_client_info_t* get_clients_info_list();

// 创建tcp服务器任务
int create_tcp_socket_server(socket_server_config_t *config);

// 创建udp服务器任务
int create_udp_socket_server(socket_server_config_t *config);


/* -------- client function define -------*/


// 注册tcp客户端套接字接收函数
void tcp_client_register_callback(tcp_recv_callback_t callback_func);
// 注册udp客户端套接字接收函数
void udp_client_register_callback(udp_recv_callback_t callback_func);
// 注册服务器连接成功回调函数
void socket_connect_register_callback(socket_connect_callback_t callback_func);

// 创建TCP客户端任务
int create_tcp_socket_client(tcp_clinet_config_t *config);

// 创建UDP客户端任务
int create_udp_socket_client(udp_clinet_config_t *config);


/**
 * @brief Create a socket recover service object
 * 
 * @return int 
 */
int create_socket_recover_service(void);

#endif
