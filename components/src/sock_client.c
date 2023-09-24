#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "cJSON.h"

#include "sock_client.h"
#include "wifi_sta.h"
#include "app_config.h"


#define BROADCAST_INTERVAL_MS   5000

static const char *TAG = "sock_client";

static char tcp_server_ip[16] = {0};
static int tcp_server_port = 0;
static sock_recv_callback_t sock_recv_callback = NULL;
static EventGroupHandle_t sock_event_group = NULL;
static int tcp_socket = INVALID_SOCK;

#define OBTAIN_SERVER_IP_BIT  BIT0      // 获取到TCP服务器IP地址位
#define SERVER_CONNECTED_BIT  BIT1      // TCP服务器连接成功位
#define SERVICE_RESTART_BIT   BIT2      // 断网重启服务位

/**
 * @brief register tcp sock receive data callback
*/
void sock_register_callback(sock_recv_callback_t callback_func)
{
    sock_recv_callback = callback_func;
}

/**
 * @brief Tries to receive data from specified sockets in a non-blocking way,
 *        i.e. returns immediately if no data.
 *
 * @param[in] sock Socket for reception
 * @param[out] data Data pointer to write the received data
 * @param[in] max_len Maximum size of the allocated space for receiving data
 * @return
 *          >0 : Size of received data
 *          =0 : No data available
 *          -1 : Error occurred during socket read operation
 *          -2 : Socket is not connected, to distinguish between an actual socket error and active disconnection
 */
int try_receive(const int sock, char * data, size_t max_len)
{
    int len = recv(sock, data, max_len, 0);
    if (len < 0) {
        if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;   // Not an error
        }
        if (errno == ENOTCONN) {
            ESP_LOGW(TAG, "[sock=%d]: Connection closed", sock);
            return -2;  // Socket has been disconnected
        }
        ESP_LOGE(TAG,"Error occurred during receiving");
        return -1;
    }

    return len;
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
int socket_send(const int sock, const char * data, const size_t len)
{
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


int data_frame_send(int sock, uint8_t *frame, frame_type_t type, uint8_t target_id, 
                    uint8_t local_id, uint32_t len, char *data)
{
    frame[FRAME_HEAD_BIT] = FRAME_HEAD;
    frame[FRAME_TYPE_BIT] = type;
    frame[FRAME_TARGET_BIT] = target_id;
    frame[FRAME_LOCAL_BIT] = local_id;
    *(uint32_t *)&frame[FRAME_LENGTH_BIT] = len;
    memcpy(&frame[FRAME_DATA_BIT], data, len);
    return (socket_send(sock, (char *)frame, len + FRAME_HEADER_LEN));
}

static void tcp_client_task(void *pvParameters)
{
    client_sock_data_t sock_info = {0};
    struct sockaddr_in server_addr;

    if(wait_wifi_connect() == false) {
        ESP_LOGE(TAG,"Delete tcp_client_task");
        goto error;
    }

    xEventGroupWaitBits(sock_event_group,
                    OBTAIN_SERVER_IP_BIT,
                    pdFALSE, pdFALSE, portMAX_DELAY);

    // 设置服务器地址和端口号
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(tcp_server_port);
    server_addr.sin_addr.s_addr = inet_addr(tcp_server_ip);

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
    sock_info.data = (uint8_t *)heap_caps_malloc(SOCK_RX_BUFFER_LEN, MALLOC_CAP_SPIRAM);
    if (sock_info.data == NULL) {
        ESP_LOGE(TAG, "malloc failed!");
        goto error;
    }
    {
        /*  注册身份  */
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, CMD_KEY_COMMAND, CMD_VALUE_REGISTER);
        cJSON_AddStringToObject(json, CMD_KEY_NAME, LOCAL_DEVICE_MARK);
        char *json_str = cJSON_PrintUnformatted(json);
        int len = data_frame_send(tcp_socket, sock_info.data, FRAME_TYPE_DIRECT, FRAME_SERVER_ID, FRAME_INVALID_ID,
                                    strlen(json_str) + 1, json_str);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during socket_send");
            goto error;
        }
        ESP_LOGI(TAG, "Written: %.*s", len, json_str);
        cJSON_Delete(json);
        free(json_str);
        xEventGroupSetBits(sock_event_group, SERVER_CONNECTED_BIT);
        ESP_LOGI(TAG, "tcp server connect success");
    }
    while(1) {
        // Keep receiving until we have a reply
        int len = try_receive(tcp_socket, (char *)sock_info.data, SOCK_RX_BUFFER_LEN);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during try_receive");
            goto error;
        }

        if(len > 0) {
            sock_info.sock = tcp_socket;
            sock_info.len = len;
            if (sock_recv_callback != NULL) {
                sock_recv_callback(sock_info);
            }
        }

    }

error:
    if (tcp_socket != INVALID_SOCK) {
        close(tcp_socket);
    }
    xEventGroupSetBits(sock_event_group, SERVICE_RESTART_BIT);
    vTaskDelete(NULL);

}


//udp广播接收任务
static void udp_recv_task(void *pvParameters)
{
    int *pragma = (int *)pvParameters;
    int broadcast_socket = *pragma;
    char recv_buf[128];

    while(1) {
        EventBits_t bits = xEventGroupWaitBits(sock_event_group,
                        SERVER_CONNECTED_BIT,
                        pdFALSE, pdFALSE, 10 / portTICK_PERIOD_MS);
        if (bits & SERVER_CONNECTED_BIT) {
            break;
        }
        memset(recv_buf, 0, sizeof(recv_buf));
        int recv_len = try_receive(broadcast_socket, recv_buf, sizeof(recv_buf));
        if (recv_len < 0) {
            // Error occurred within this client's socket -> close and mark invalid
            ESP_LOGW(TAG, "[sock=%d]: try_receive() returned %d -> closing the socket", broadcast_socket, recv_len);
            break;
        } else if(recv_len > 0) {
            ESP_LOGI(TAG, "udp Received: %.*s", recv_len, recv_buf);
            cJSON *root = cJSON_Parse(recv_buf);
            cJSON *device = cJSON_GetObjectItem(root, CMD_KEY_NAME);
            cJSON *device_ip = cJSON_GetObjectItem(root, "ip");
            cJSON *device_port = cJSON_GetObjectItem(root, "port");
            if(device != NULL && device_ip != NULL && device_port != NULL) {
                if(strcmp(device->valuestring, TCP_SERVER_NAME) == 0) {
                    strcpy(tcp_server_ip, device_ip->valuestring);
                    tcp_server_port = atoi(device_port->valuestring);
                    ESP_LOGI(TAG, "success to get server information");
                    xEventGroupSetBits(sock_event_group, OBTAIN_SERVER_IP_BIT);
                }
            }
            cJSON_Delete(root);
        }

    }
    /* colse... */
    ESP_LOGW(TAG, "Delete udp receive task");
    vTaskDelete(NULL);

}


/* UDP广播任务 */
void udp_broadcast_task(void *arg)
{  
    if(wait_wifi_connect() == false) {
        ESP_LOGW(TAG, "wifi not connect delete udp_broadcast_task");
        goto error;
    }
    // 将IPv4地址转换为字符串格式的IP地址
    esp_ip4_addr_t ip_addr = get_local_ip_addr();
    char ip_addr_str[INET_ADDRSTRLEN];
    inet_ntoa_r(ip_addr.addr, ip_addr_str, sizeof(ip_addr_str));

    // Set up the local IP address and port number
    struct sockaddr_in broadcast_addr = {
        .sin_addr.s_addr = ip_addr.addr | htonl(0xFF), //广播地址，其类型为uint32_t
        .sin_family = AF_INET,
        .sin_port = htons(BROADCAST_PORT), //目标服务器端口
    };
    // 将广播地址转换为字符串
    ESP_LOGI(TAG, "broadcast_addr IP: %s", inet_ntoa(broadcast_addr.sin_addr.s_addr));
    

    // Create a socket for UDP broadcast
    int broadcast_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (broadcast_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", broadcast_socket);
        goto error;
    }


    // 设置套接字选项以启用地址重用
    int reuseEnable = 1;
    setsockopt(broadcast_socket, SOL_SOCKET, SO_REUSEADDR, &reuseEnable, sizeof(reuseEnable));

    // Enable broadcasting
    int broadcast_enable = 1;
    if (setsockopt(broadcast_socket, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        ESP_LOGE(TAG, "Failed to enable broadcasting");
        close(broadcast_socket);
        goto error;
    }

    // 绑定本地端口号
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 接受任何ip地址
    dest_addr.sin_port = htons(BIND_UDP_PORT);      // 绑定本地端口
    if (bind(broadcast_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        ESP_LOGE(TAG, "Error binding socket");
        goto error;
    }
    // 创建udp接收任务
    xTaskCreate(udp_recv_task, "udp_recv_task", 4 * 1024, (void *)&broadcast_socket, 15, NULL);

    // Send the broadcast message every BROADCAST_INTERVAL_MS milliseconds
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(sock_event_group,
                        SERVER_CONNECTED_BIT,
                        pdFALSE, pdFALSE, 10 / portTICK_PERIOD_MS);
        if (bits & SERVER_CONNECTED_BIT) {
            ESP_LOGW(TAG, "delete udp_Broadcast_task");
            goto error;
        }
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, CMD_KEY_UDP_REQUEST, TCP_SERVER_NAME);

        char *json_str = cJSON_PrintUnformatted(json);

        ESP_LOGI(TAG, "Broadcasting message: %s", json_str);
        int ret = sendto(broadcast_socket, json_str, strlen(json_str), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to send broadcast message: %d", ret);
        }
        free(json_str);
        cJSON_Delete(json);
        vTaskDelay(BROADCAST_INTERVAL_MS / portTICK_PERIOD_MS);
    }

error:
    close(broadcast_socket);
    vTaskDelete(NULL);
}


/**
 * WiFi连接断开后，重启UDP,TCP任务
*/
static void service_restart_task(void *arg)
{
    if(wait_wifi_connect() == false) {
        ESP_LOGW(TAG, "wifi not connect delete service_restart_task");
        goto over;
    }
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(sock_event_group,
                        SERVICE_RESTART_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
        if (bits & SERVICE_RESTART_BIT) {
            ESP_LOGW(TAG, "wait restart broadcast and tcp client task");
            if(wait_wifi_connect() == true) {
                // 连接成功重启套接字任务
                xTaskCreatePinnedToCore(udp_broadcast_task, "udp_broadcast_task", 4 * 1024, NULL, 5, NULL, 0);
                xTaskCreatePinnedToCore(tcp_client_task, "tcp_client_task", 5 * 1024, NULL, 10, NULL, 0);
                xEventGroupClearBits(sock_event_group, SERVICE_RESTART_BIT | OBTAIN_SERVER_IP_BIT | SERVER_CONNECTED_BIT);
            } else {
                ESP_LOGW(TAG, "wifi not connect delete service_restart_task");
                goto over;
            }
        }
    }    
over:
    vTaskDelete(NULL);
}

void create_client_sock_task()
{
    sock_event_group = xEventGroupCreate();
    if (sock_event_group == NULL) {
        ESP_LOGE(TAG, "sock_event_group create failed");
        return;
    }
    xTaskCreatePinnedToCore(udp_broadcast_task, "udp_broadcast_task", 4 * 1024, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(tcp_client_task, "tcp_client_task", 5 * 1024, NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(service_restart_task, "service_restart_task", 3 * 1024, NULL, 20, NULL, 1);
}
