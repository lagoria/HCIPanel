#ifndef SOCKET_WRAPPER_H
#define SOCKET_WRAPPER_H

#include "sys/socket.h"
#include "netdb.h"
#include "errno.h"
#include "esp_netif.h"


#ifndef DEFAULT_SOCK_BUF_SIZE
#define DEFAULT_SOCK_BUF_SIZE        1024         // 套接字数据缓存大小(参考MTU大小)
#endif

#define INVALID_SOCK            (-1)

typedef enum {
    WAY_TCP,
    WAY_UDP,
} socket_way_t;

/* ----------------type define----------------*/

/* tcp socket 接收回调参数类型 */
typedef struct {
    int                 socket;
    uint8_t             *data;
    int                 len;
    uint8_t             mark;                   // 实例标识
} tcp_socket_info_t;

/* tcp 接收回调函数类型 */
typedef void (*tcp_recv_callback_t)(tcp_socket_info_t);

typedef struct {
    int                 socket;
    uint8_t             *data;
    int                 len;
    struct sockaddr_in  *source_addr;
    uint8_t             mark;                   // 实例标识
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
    uint8_t  maxcon_num;
    socket_way_t way;
    uint8_t mark;                   // 实例标识
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
    char server_ip[16];         // 目标服务器IP
    uint16_t server_port;       // 目标服务器端口
    uint16_t bind_port;         // UDP客户端配置绑定端口
    socket_way_t way;           // 通信方式 TCP or UDP
    uint8_t  mark;              // 客户端标识
} socket_clinet_config_t;


/*-----------------------function define-------------------------------*/

/**
 * @brief Sends the specified data to the socket. This function blocks until all bytes got sent.
 *
 * @param[in] sock Socket to write data
 * @param[in] data Data to be written
 * @param[in] len Length of the data
 * @return
 *          >0 : Size the written data
 *          -1 : Error occurred during socket write operation
 */
int socket_send(const int sock, const uint8_t * data, const size_t len);

/* -------- server function define -------*/

// tcp套接字注册接收回调函数
void tcp_server_register_callback(tcp_recv_callback_t callback_func);

// udp广播套接字接收回调函数注册
void udp_server_register_callback(udp_recv_callback_t callback_func);

// 获取客户端信息
tcp_client_info_t* get_clients_info_list();

/**
 * @brief Create a socket wrapper server object
 * 
 * @param config 
 * @return int 
 */
int create_socket_wrapper_server(socket_server_config_t *config);

/* -------- client function define -------*/


// 注册tcp客户端套接字接收函数
void tcp_client_register_callback(tcp_recv_callback_t callback_func);
// 注册udp客户端套接字接收函数
void udp_client_register_callback(udp_recv_callback_t callback_func);
// 注册服务器连接成功回调函数
void socket_connect_register_callback(socket_connect_callback_t callback_func);

/**
 * @brief Create a socket wrapper client object
 * 
 * @param config 客户端配置
 * @return int   0：成功，-1失败
 */
int create_socket_wrapper_client(socket_clinet_config_t *config);


/**
 * @brief Create a socket client recover service object
 * 
 * @return int 
 */
int create_socket_client_recover_service(void);

#endif
