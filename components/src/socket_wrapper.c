#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "socket_wrapper.h"
#include "wifi_wrapper.h"

#define MAX_CLIENT_NUM      10

static const char *TAG = "socket_wrapper";

// server variable
static tcp_recv_callback_t tcp_server_recv_callback = NULL;
static udp_recv_callback_t udp_server_recv_callback = NULL;
static tcp_client_info_t *tcp_client_info_head = NULL;

static int const keepAlive = 1;              // 开启keepalive属性
static int const keepIdle = 3;               // 如该连接在3秒内没有任何数据往来,则进行探测 
static int const keepInterval = 1;           // 探测时发包的时间间隔为1 秒
static int const keepCount = 2;              // 探测尝试的次数.如果第1次探测包就收到响应了,则后2次的不再发.

// client variable

/* 创建的客户端信息列表 */
struct socket_client_list_info {
    char server_ip[16];
    uint16_t server_port;
    uint16_t bind_port;
    uint8_t  mark;              // 客户端标识
    struct socket_client_list_info *next;
};

typedef struct socket_client_list_info socket_client_list_t;

static tcp_recv_callback_t tcp_client_recv_callback = NULL;
static udp_recv_callback_t udp_client_recv_callback = NULL;
static socket_connect_callback_t server_conn_callback = NULL;
static socket_client_list_t *client_list_head = NULL;

static EventGroupHandle_t sock_event_group = NULL;


#define TCP_SERVER_RECOVER_BIT  BIT0    // TCP服务器断网重启服务位
#define TCP_CLIENT_RECOVER_BIT  BIT1    // TCP客户端断网重启服务位
#define UDP_SERVER_RECOVER_BIT  BIT2    // UDP服务器断网重启服务位
#define UDP_CLIENT_RECOVER_BIT  BIT3    // UDP客户端断网重启服务位




/**
 * @brief tcp socket receive callback register
 * 
 * @param callback_func 
 */
void tcp_server_register_callback(tcp_recv_callback_t callback_func)
{
    tcp_server_recv_callback = callback_func;
}

/**
 * @brief udp broadcast receive callback register
 * 
 * @param callback_func 
 */
void udp_server_register_callback(udp_recv_callback_t callback_func)
{
    udp_server_recv_callback = callback_func;
}

/**
 * @brief register tcp sock receive data callback
*/
void tcp_client_register_callback(tcp_recv_callback_t callback_func)
{
    tcp_client_recv_callback = callback_func;
}

/**
 * @brief register udp sock receive data callback
*/
void udp_client_register_callback(udp_recv_callback_t callback_func)
{
    udp_client_recv_callback = callback_func;
}

/**
 * @brief register socket server connected callback
 * 
 * @param callback_func 
 */
void socket_connect_register_callback(socket_connect_callback_t callback_func)
{
    server_conn_callback = callback_func;
}

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
int socket_send(const int sock, const uint8_t * data, const size_t len)
{
    if (sock < 0) return sock;
    int to_write = len;
    while (to_write > 0) {
        int written = send(sock, data + (len - to_write), to_write, 0);
        if (written < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG,"Error occurred during sending");
            return -1;
        }
        to_write -= written;
    }
    return len;
}


/**
 * @brief Returns the string representation of client's address (accepted on this server)
 */
static inline char* get_clients_address(struct sockaddr_storage *source_addr)
{
    static char address_str[128];
    char *res = NULL;
    // Convert ip address to string
    if (source_addr->ss_family == PF_INET) {
        res = inet_ntoa_r(((struct sockaddr_in *)source_addr)->sin_addr, address_str, sizeof(address_str) - 1);
    }
    if (!res) {
        address_str[0] = '\0'; // Returns empty string if conversion didn't succeed
    }
    return address_str;
}

static void add_tcp_client_list_node(int socket) 
{  
    tcp_client_info_t *new_client = (tcp_client_info_t *)malloc(sizeof(tcp_client_info_t));  
    if (new_client == NULL) return;
    if (tcp_client_info_head == NULL) {
        tcp_client_info_head = new_client;
    } else {
        tcp_client_info_t *current;
        current = tcp_client_info_head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_client;
    }

    // 设置节点信息
    memset(new_client, 0, sizeof(tcp_client_info_t));
    new_client->socket = socket;
    new_client->next = NULL;
}

static void delete_tcp_client_list_node(int socket)
{
    if (tcp_client_info_head == NULL) return;
    tcp_client_info_t *current, *prev;
    current = tcp_client_info_head;
    // 判断是否为第一个节点
    if (current != NULL && current->socket == socket) {
        tcp_client_info_head = current->next;
        free(current);
        return;
    }
    while (current->next != NULL) {
        prev = current;
        current = current->next;
        if (current != NULL && current->socket == socket) {
            prev->next = current->next;
            free(current);
            break;
        }
    }
}

static void tcp_recv_task(void *pvParameters)
{
    tcp_socket_info_t sock_info = {0};
    int *pragma = (int *)pvParameters;
    int socket = *pragma;
    ESP_LOGI(TAG, "client_sock = %d", socket);
    {   // 获取客户端IP地址(作用域限定)
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(struct sockaddr);
        int ret = getpeername(socket, &client_addr, &client_addr_len);
        if (ret == 0) {
            struct sockaddr_in *client_addr_in = (struct sockaddr_in *)&client_addr;
            char *client_ip = inet_ntoa(client_addr_in->sin_addr);
            char client_port = ntohs(client_addr_in->sin_port);
            ESP_LOGI(TAG, "Client IP:%s,Port:%d", client_ip, client_port);
            // 保存客户端IP地址,端口号
            for (tcp_client_info_t *list = tcp_client_info_head; list; list = list->next) {
                if (list->socket == socket) {
                    strcpy(list->ip, client_ip);
                    list->port = client_port;
                    // ip地址的机器码作id
                    list->id = (uint8_t ) (client_addr_in->sin_addr.s_addr >> 24);
                    break;
                }
            }
        }
    }

    /* allocation sock date buffer */
    sock_info.data = (uint8_t *)malloc(SOCK_BUFFER_SIZE + 8);
    if (sock_info.data == NULL) {
        ESP_LOGE(TAG, "sock buffer malloc failed");
        goto over;
    }

    while(1) {
        int recv_len = recv(socket, (char *)sock_info.data, SOCK_BUFFER_SIZE, 0);
        if (recv_len < 0) {
            // Error occurred within this client's socket -> close and mark invalid
            ESP_LOGW(TAG, "[sock=%d]: recv() returned %d -> closing the socket", socket, recv_len);
            goto over;
        } else {
            sock_info.len = recv_len;
            sock_info.data[recv_len] = '\0';
            sock_info.socket = socket;
            if (tcp_server_recv_callback != NULL) {
                tcp_server_recv_callback(sock_info);          // 回调函数
            }
        }

    }

over:
    /* colse... */
    delete_tcp_client_list_node(socket);
    close(socket);
    vTaskDelete(NULL);

}


static void tcp_server_task(void *pvParameters)
{
    socket_server_config_t *pragma = (socket_server_config_t *)pvParameters;
    socket_server_config_t server_config = *pragma;

    // 等待wifi连接(AP模式跳过)
    wait_wifi_connect(portMAX_DELAY);

    // Creating a listener socket
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) {
        ESP_LOGI(TAG,"Unable to create socket");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // Binding socket to the given address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(server_config.listen_port);
    int err = bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err != 0) {
        ESP_LOGW(TAG,"Socket unable to bind");
        goto CLEAN_UP;
    }

    // Set queue (backlog) of pending connections to one (can be more)
    err = listen(listen_sock, 5);
    if (err != 0) {
        ESP_LOGE(TAG,"Error occurred during listen");
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "tcp server started. port:%d", server_config.listen_port);

    // Main loop for accepting new connections and serving all connected clients
    while (1) {
        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);

        // Find a free socket
        uint8_t client_count = 0;
        for (tcp_client_info_t *list = tcp_client_info_head; list; list = list->next) {
            client_count++;
        }

        // We accept a new connection only if we have a free socket
        if (client_count < server_config.max_conn_num) {
            // Try to accept a new connections
            int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (sock < 0) {
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
                xEventGroupSetBits(sock_event_group, TCP_SERVER_RECOVER_BIT);
                break;
            } else {
                // We have a new client connected -> print it's address
                ESP_LOGI(TAG, "[sock=%d]: Connection accepted from IP:%s", sock, get_clients_address(&source_addr));

                // Set tcp keepalive option
                setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

                // add client infor list node
                add_tcp_client_list_node(sock);

                char task_name[16];
                sprintf(task_name, "tcp_recv_%d", sock);
                ESP_LOGI(TAG, "task_name = %s\n", task_name);
                if (xTaskCreatePinnedToCore(tcp_recv_task, task_name, 5 * 1024, (void *)&sock, 10, NULL, tskNO_AFFINITY) != pdPASS) {
                    ESP_LOGE(TAG, "tcp_recv_task create failed");
                }
            
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGW(TAG, "too many TCP connect");
        }
    }

CLEAN_UP:
    close(listen_sock);
    ESP_LOGE(TAG, "delete tcp_server_task");
    vTaskDelete(NULL);
}


static void udp_server_task(void *pvParameters)
{
    socket_server_config_t *pragma = (socket_server_config_t *)pvParameters;
    socket_server_config_t server_config = *pragma;

    int udp_sock = INVALID_SOCK;
    udp_socket_info_t sock_info = {0};
    uint8_t recv_buf[256];
    
    sock_info.data = recv_buf;

    // 等待wifi连接(AP模式跳过)
    wait_wifi_connect(portMAX_DELAY);

    // 创建socket
    if ((udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)) < 0) {
        ESP_LOGE(TAG, "Error creating socket");
        goto error;
    }

    // 设置socket为广播
    int broadcast_enable = 1;
    if (setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        ESP_LOGE(TAG, "Error setting broadcast");
        goto error;
    }

    // 绑定本地端口号
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY); //接受任何ip地址的数据
    dest_addr.sin_port = htons(server_config.listen_port); //绑定本地端口
    if (bind(udp_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        ESP_LOGE(TAG, "Error binding socket");
        goto error;
    }

    ESP_LOGI(TAG, "udp server started. port:%d", server_config.listen_port);

    // 接收广播消息并解析(堵塞)
    while (1) {
        struct sockaddr_in source_addr; 
        memset(&source_addr, 0, sizeof(source_addr));
        socklen_t socklen = sizeof(source_addr);
        int recv_len = recvfrom(udp_sock, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&source_addr, (socklen_t *)&socklen);
        if (recv_len < 0) {
            ESP_LOGE(TAG, "Error receiving data");
            xEventGroupSetBits(sock_event_group, UDP_SERVER_RECOVER_BIT);
            break;
        } else {
            sock_info.data[recv_len] = '\0';
            sock_info.socket = udp_sock;
            sock_info.len = recv_len;
            sock_info.source_addr = &source_addr;
            if (udp_server_recv_callback != NULL) {
                udp_server_recv_callback(sock_info);          // 回调函数
            }
        }
    }
error:
    if (udp_sock != INVALID_SOCK) {
        close(udp_sock);
    }
    ESP_LOGE(TAG, "delete udp_server_task");
    vTaskDelete(NULL);
}

tcp_client_info_t* get_clients_info_list()
{
    return tcp_client_info_head;
}


int create_tcp_socket_server(socket_server_config_t *config)
{
    if (sock_event_group == NULL) {
        sock_event_group = xEventGroupCreate();
    }

    static socket_server_config_t static_config = {0};
    if (config != NULL) {
        static_config = *config;
    }
    if (xTaskCreate(tcp_server_task, "tcp_server_task", 6 * 1024, (void *)&static_config, 10, NULL) != pdPASS) {
        return -1;
    }

    return 0;
}

int create_udp_socket_server(socket_server_config_t *config)
{
    if (sock_event_group == NULL) {
        sock_event_group = xEventGroupCreate();
    }
    static socket_server_config_t static_config = {0};
    if (config != NULL) {
        static_config = *config;
    }
    if (xTaskCreate(udp_server_task, "udp_server_task", 4 * 1024, (void *)&static_config, 12, NULL) != pdPASS) {
        return -1;
    }
    return 0;
}




/* --------------------------socket client wrapper---------------------*/

static void tcp_client_task(void *pvParameters)
{
    uint8_t *pragma = (uint8_t *)pvParameters;
    uint8_t instance_mark = *pragma;
    int tcp_socket = INVALID_SOCK;

    socket_client_list_t *instance;
    for (instance = client_list_head; instance; instance = instance->next) {
        if (instance->mark == instance_mark) {
            break;
        }
    }
    if (instance == NULL) goto error;

    /* 将IPv4地址从点分十进制转化为网络字节序 */
    struct in_addr ip_addr;
    inet_pton(AF_INET, instance->server_ip, &ip_addr);

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = ip_addr.s_addr,
        .sin_port = htons(instance->server_port),
    };

    // 等待wifi连接(AP模式跳过)
    wait_wifi_connect(portMAX_DELAY);

    // 创建TCP socket
    tcp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (tcp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create TCP socket");
        goto error;
    }

    if (connect(tcp_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
        if (errno == EINPROGRESS) {
            ESP_LOGD(TAG, "connection in progress");
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(tcp_socket, &fdset);

            // Connection in progress -> have to wait until the connecting socket is marked as writable, i.e. connection completes
            esp_err_t res = select(tcp_socket+1, NULL, &fdset, NULL, NULL);
            if (res < 0) {
                ESP_LOGI(TAG,"Error during connection: select for socket to be writable");
                goto error;
            } else if (res == 0) {
                ESP_LOGI(TAG,"Connection timeout: select for socket to be writable");
                goto error;
            } else {
                int sockerr;
                socklen_t len = (socklen_t)sizeof(int);

                if (getsockopt(tcp_socket, SOL_SOCKET, SO_ERROR, (void*)(&sockerr), &len) < 0) {
                    ESP_LOGI(TAG,"Error when getting socket error using getsockopt()");
                    goto error;
                }
                if (sockerr) {
                    ESP_LOGI(TAG,"Connection error");
                    goto error;
                }
            }
        } else {
            ESP_LOGI(TAG,"Socket is unable to connect");
            goto error;
        }
    }

    tcp_socket_info_t sock_info = {0};
    sock_info.mark = instance_mark;           // 多客户端标识
    sock_info.socket = tcp_socket;
    /* allocation sock date buffer */
    sock_info.data = (uint8_t *)malloc(SOCK_BUFFER_SIZE + 8);
    if (sock_info.data == NULL) {
        ESP_LOGE(TAG, "sock buffer malloc failed");
        goto error;
    }

    ESP_LOGI(TAG, "[-%d-]tcp client started!", instance_mark);
    if (server_conn_callback != NULL) {
        socket_connect_info_t conn_info;
        conn_info.socket = tcp_socket;
        conn_info.target_addr = NULL;
        conn_info.mark = sock_info.mark;
        server_conn_callback(conn_info);
    }

    while(1) {
        // Keep receiving until we have a reply
        int len = recv(tcp_socket, sock_info.data, SOCK_BUFFER_SIZE, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during recv");
            xEventGroupSetBits(sock_event_group, TCP_CLIENT_RECOVER_BIT);
            break;
        }

        if(len > 0) {
            sock_info.data[len] = '\0';
            sock_info.len = len;
            if (tcp_client_recv_callback != NULL) {
                tcp_client_recv_callback(sock_info);
            }
        }
    }

error:
    if (tcp_socket > 0) {
        close(tcp_socket);
    }
    ESP_LOGE(TAG, "delete tcp_client_task");
    vTaskDelete(NULL);
}



/* UDP客户端任务 */
void udp_client_task(void *pvParameters)
{
    uint8_t *pragma = (uint8_t *)pvParameters;
    uint8_t instance_mark = *pragma;
    int udp_socket = INVALID_SOCK;

    socket_client_list_t *instance;
    for (instance = client_list_head; instance; instance = instance->next) {
        if (instance->mark == instance_mark) {
            break;
        }
    }
    if (instance == NULL) goto error;

    // 等待wifi连接(AP模式跳过)
    wait_wifi_connect(portMAX_DELAY);

    // Create a socket for UDP broadcast
    udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", udp_socket);
        goto error;
    }

    // 设置套接字选项以启用地址重用
    int reuseEnable = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &reuseEnable, sizeof(reuseEnable));

    // Enable broadcasting
    int broadcast_enable = 1;
    if (setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        ESP_LOGE(TAG, "Failed to enable broadcasting");
        goto error;
    }

    // Set up the local IP address and port number
    struct sockaddr_in local_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),       // 接受任何ip地址
        .sin_port = htons(instance->bind_port),        // 绑定本地端口
    };

    if (bind(udp_socket, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "Error binding socket");
        goto error;
    }

    /* 将IPv4地址从点分十进制转化为网络字节序 */
    struct in_addr ip_addr;
    inet_pton(AF_INET, instance->server_ip, &ip_addr);

    /* 目标地址设置 */
    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = ip_addr.s_addr,
        .sin_port = htons(instance->server_port),
    };
    socklen_t socklen = sizeof(dest_addr);

    udp_socket_info_t sock_info = {0};
    sock_info.mark = instance_mark;           // 多客户端标识
    sock_info.socket = udp_socket;
    sock_info.source_addr = &dest_addr;
    /* allocation sock date buffer */
    sock_info.data = (uint8_t *)malloc(SOCK_BUFFER_SIZE + 8);
    if (sock_info.data == NULL) {
        ESP_LOGE(TAG, "sock buffer malloc failed");
        goto error;
    }


    ESP_LOGI(TAG, "[-%d-]udp client started!", instance_mark);
    if (server_conn_callback != NULL) {
        socket_connect_info_t conn_info;
        conn_info.socket = udp_socket;
        conn_info.target_addr = &dest_addr;
        conn_info.mark = sock_info.mark;
        server_conn_callback(conn_info);
    }

    while (1) {
        int recv_len = recvfrom(udp_socket, sock_info.data, SOCK_BUFFER_SIZE, 0,
                             (struct sockaddr *)&dest_addr, (socklen_t *)&socklen);
        if (recv_len < 0) {
            // Error occurred within this client's socket -> close and mark invalid
            ESP_LOGW(TAG, "[sock=%d]: recv() returned %d -> closing the socket", udp_socket, recv_len);
            xEventGroupSetBits(sock_event_group, UDP_CLIENT_RECOVER_BIT);
            break;
        } else if(recv_len > 0) {
            sock_info.data[recv_len] = '\0';
            sock_info.len = recv_len;
            if (udp_client_recv_callback != NULL) {
                udp_client_recv_callback(sock_info);          // 回调函数
            }
        }
    }

error:
    if (udp_socket > 0) {
        close(udp_socket);
    }
    ESP_LOGE(TAG, "delete udp_client_task");
    vTaskDelete(NULL);
}

/**
 * @brief 添加一个客户端创建实例链表节点
 * 
 * @param mark 客户端实例标识
 */
static socket_client_list_t * add_client_instance_list_node(uint8_t mark)
{
    socket_client_list_t *new_client = (socket_client_list_t *)malloc(sizeof(socket_client_list_t));  
    if (new_client == NULL) return NULL;
    if (client_list_head == NULL) {
        client_list_head = new_client;
    } else {
        socket_client_list_t *current;
        current = client_list_head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_client;
    }

    // 设置节点信息
    memset(new_client, 0, sizeof(socket_client_list_t));
    new_client->mark = mark;
    new_client->next = NULL;
    return new_client;
}

int create_udp_socket_client(udp_clinet_config_t *config)
{
    if (sock_event_group == NULL) {
        sock_event_group = xEventGroupCreate();
    }
    socket_client_list_t *instance = NULL;
    /* 遍历客户端实例 判断是否重新配置 */
    for (socket_client_list_t *list = client_list_head; list; list = list->next) {
        if (list->mark == config->mark) {
            instance = list;
            break;
        }
    }
    if (instance == NULL) {
        /* 创建新的实例 */
        instance = add_client_instance_list_node(config->mark);
    }
    if (instance == NULL) return -1;
    strncpy(instance->server_ip, config->server_ip, 16);
    instance->server_port = config->server_port;
    instance->bind_port = config->bind_port;

    char task_name[16];
    sprintf(task_name, "udp_client_%d", instance->mark);
    ESP_LOGI(TAG, "create task = %s", task_name);

    if (xTaskCreate(udp_client_task, task_name, 4 * 1024, (void *)&instance->mark, 12, NULL) != pdPASS) {
        return -1;
    }

    return 0;
}

int create_tcp_socket_client(tcp_clinet_config_t *config)
{
    if (sock_event_group == NULL) {
        sock_event_group = xEventGroupCreate();
    }

    socket_client_list_t *instance = NULL;
    /* 遍历客户端实例 判断是否重新配置 */
    for (socket_client_list_t *list = client_list_head; list; list = list->next) {
        if (list->mark == config->mark) {
            instance = list;
            break;
        }
    }
    if (instance == NULL) {
        /* 创建新的实例 */
        instance = add_client_instance_list_node(config->mark);
    }
    if (instance == NULL) return -1;
    strncpy(instance->server_ip, config->server_ip, 16);
    instance->server_port = config->server_port;
    instance->bind_port = 0;

    char task_name[16];
    sprintf(task_name, "tcp_client_%d", instance->mark);
    ESP_LOGI(TAG, "create task = %s", task_name);

    if (xTaskCreate(tcp_client_task, task_name, 6 * 1024, (void *)&instance->mark, 10, NULL) != pdPASS) {
        return -1;
    }

    return 0;
}


/**
 * WiFi连接断开后，重启UDP,TCP任务
*/
static void service_restart_task(void *pvParameters)
{
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(sock_event_group,
                        TCP_SERVER_RECOVER_BIT | TCP_CLIENT_RECOVER_BIT |
                        UDP_SERVER_RECOVER_BIT | UDP_CLIENT_RECOVER_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
        wait_wifi_connect(portMAX_DELAY);
        

        if (bits & TCP_SERVER_RECOVER_BIT) {
            ESP_LOGI(TAG, "recovering tcp server");
            xEventGroupClearBits(sock_event_group, TCP_SERVER_RECOVER_BIT);
            create_tcp_socket_server(NULL);
        }
        if (bits & TCP_CLIENT_RECOVER_BIT) {
            ESP_LOGI(TAG, "recovering tcp client");
            for (socket_client_list_t *instance = client_list_head; instance; instance = instance->next) {
                if (instance->bind_port == 0) {
                    char task_name[16];
                    sprintf(task_name, "tcp_client_%d", instance->mark);
                    xTaskCreate(tcp_client_task, task_name, 6 * 1024, (void *)&instance->mark, 10, NULL);
                }
            }
            xEventGroupClearBits(sock_event_group, TCP_CLIENT_RECOVER_BIT);
        }
        if (bits & UDP_SERVER_RECOVER_BIT) {
            ESP_LOGI(TAG, "recovering udp server");
            xEventGroupClearBits(sock_event_group, UDP_SERVER_RECOVER_BIT);
            create_udp_socket_server(NULL);
        }
        if (bits & UDP_CLIENT_RECOVER_BIT) {
            ESP_LOGI(TAG, "recovering udp client");
            for (socket_client_list_t *instance = client_list_head; instance; instance = instance->next) {
                if (instance->bind_port != 0) {
                    char task_name[16];
                    sprintf(task_name, "udp_client_%d", instance->mark);
                    xTaskCreate(udp_client_task, task_name, 4 * 1024, (void *)&instance->mark, 12, NULL);
                }
            }
            
            xEventGroupClearBits(sock_event_group, UDP_CLIENT_RECOVER_BIT);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }    
}

int create_socket_recover_service(void)
{
    if (sock_event_group == NULL) {
        return -1;
    }
    if (xTaskCreate(service_restart_task, "service_restart_task", 2 * 1024, NULL, 10, NULL) != pdPASS) {
        return -1;
    }
    return 0;
}


