#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "cJSON.h"

#include "app_gpio.h"
#include "lvgl.h"
#include "lvgl_helpers.h"
#include "lv_port_disp.h"
#include "wifi_wrapper.h"
#include "socket_wrapper.h"
#include "app_config.h"

static const char *TAG = "main";

static int tcp_socket = INVALID_SOCK;
static EventGroupHandle_t app_event_group = NULL;
static QueueHandle_t sock_queue = NULL;
static uint8_t local_device_id = FRAME_INVALID_ID;
static uint8_t camera_id = FRAME_INVALID_ID;
static uint8_t *tx_rx_buffer = NULL;
static uint8_t *image_buffer = NULL;
uint32_t image_size = 0;
#define SERVER_READY_BIT    BIT0        // 服务器状态位
#define CLIENT_READY_BIT    BIT1
#define CAMERA_DATA_BIT     BIT2


int data_frame_send(int sock, uint8_t *frame, frame_type_t type, uint8_t target_id, 
                    uint8_t local_id, uint32_t len, uint8_t *data)
{
    frame[FRAME_HEAD_BIT] = FRAME_HEAD;
    frame[FRAME_TYPE_BIT] = type;
    frame[FRAME_TARGET_BIT] = target_id;
    frame[FRAME_LOCAL_BIT] = local_id;
    *(uint32_t *)&frame[FRAME_LENGTH_BIT] = len;
    memcpy(&frame[FRAME_DATA_BIT], data, len);
    return (socket_send(sock, frame, len + FRAME_HEADER_LEN));
}

static void softap_server_connect_callback(socket_connect_info_t info)
{
    /*  注册身份  */
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, CMD_KEY_COMMAND, CMD_VALUE_REGISTER);
    cJSON_AddStringToObject(json, CMD_KEY_NAME, LOCAL_DEVICE_MARK);
    char *json_str = cJSON_PrintUnformatted(json);
    int len = data_frame_send(info.socket, tx_rx_buffer, FRAME_TYPE_DIRECT, FRAME_SERVER_ID, FRAME_INVALID_ID,
                                strlen(json_str) + 1, (uint8_t *)json_str);
    if (len < 0) {
        ESP_LOGE(TAG, "Error occurred during socket_send");
    }
    ESP_LOGI(TAG, "Written: %.*s", len, json_str);
    cJSON_Delete(json);
    free(json_str);
}

static void softap_server_recv_callback(tcp_socket_info_t info)
{
    tcp_socket_info_t sock_info = info;
    memcpy(tx_rx_buffer, info.data, info.len);
    sock_info.data = tx_rx_buffer;
    // 将数据送入数据队列
    xQueueSend(sock_queue, &sock_info, 10 / portTICK_PERIOD_MS);
}

static void tcp_sock_handle_task(void *arg)
{
    tcp_socket_info_t sock_info;
    while (1) {
        /* wait for queue */
        xQueueReceive(sock_queue, &sock_info, portMAX_DELAY);

        uint8_t *recv_data = NULL;      // 接收的数据指针
        int recv_len = 0;               // 接收数据长度
        /* data frame parse*/
        frame_header_info_t frame = *(frame_header_info_t *)sock_info.data;
        if (frame.head == FRAME_HEAD && frame.length == sock_info.len - FRAME_HEADER_LEN) {
            recv_data = &sock_info.data[FRAME_DATA_BIT];
            recv_len = frame.length;
        }
        if (recv_data == NULL) continue;
        sock_info.data[sock_info.len] = '\0';
        ESP_LOGI(TAG, "[sock]: %d byte received %s", recv_len, (char *)recv_data);
    
        switch (frame.type) {
            case FRAME_TYPE_RESPOND: {
                tcp_socket = sock_info.socket;
                local_device_id = frame.goal;

                cJSON *root = cJSON_Parse((char *)recv_data);  // 解析JSON字符串
                cJSON *status = cJSON_GetObjectItem(root, CMD_KEY_STATUS);
                cJSON *goal_id = cJSON_GetObjectItem(root, CMD_KEY_UUID);
                // 服务器就绪状态判断
                if (status != NULL && status->type == cJSON_String) {
                    if (strcmp(status->valuestring, CMD_VALUE_SUCCESS) == 0) {
                        if (goal_id != NULL && status->type == cJSON_String) {
                            camera_id = (uint8_t )atoi(goal_id->valuestring);
                            xEventGroupSetBits(app_event_group, CLIENT_READY_BIT);
                        }
                        xEventGroupSetBits(app_event_group, SERVER_READY_BIT);
                    } else {
                        xEventGroupClearBits(app_event_group, CLIENT_READY_BIT);
                    }
                }
                cJSON_Delete(root);
                
                break;
            }
            case FRAME_TYPE_TRANSMIT: {
                ESP_LOGI(TAG, "[sock]: %d byte received", sock_info.len);
                
                if (frame.source == camera_id && camera_id != FRAME_INVALID_ID) {
                    memcpy(image_buffer, sock_info.data, sock_info.len);
                    image_size = sock_info.len;
                    xEventGroupSetBits(app_event_group, CAMERA_DATA_BIT);
                }
                break;
            }
            default : break;
        } /* switch (frame.type) */
    } /* while (1) */
}


static void btn_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    // lv_obj_t * btn = lv_event_get_target(e);
    switch (code) {
        case LV_EVENT_CLICKED: {
            ESP_LOGI(TAG, "clicked");
            EventBits_t bits = xEventGroupWaitBits(app_event_group,
                        SERVER_READY_BIT | CLIENT_READY_BIT,
                        pdFALSE, pdFALSE, 10 / portTICK_PERIOD_MS);
            if (bits & CLIENT_READY_BIT) {
                // cJSON *json = cJSON_CreateObject();
                // cJSON_AddStringToObject(json, CMD_KEY_IMAGE, CMD_VALUE_PICTURE);
                // char *json_str = cJSON_PrintUnformatted(json);
                // data_frame_send(tcp_socket, tx_rx_buffer, FRAME_TYPE_TRANSMIT, camera_id, local_device_id,
                //                             strlen(json_str) + 1, (uint8_t *)json_str);
                // cJSON_Delete(json);
                // free(json_str);
                
            } else if (bits & SERVER_READY_BIT) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, CMD_KEY_COMMAND, CMD_VALUE_UUID);
                cJSON_AddStringToObject(json, CMD_KEY_NAME, CAMERA_DEVICE);
                char *json_str = cJSON_PrintUnformatted(json);
                data_frame_send(tcp_socket, tx_rx_buffer, FRAME_TYPE_DIRECT, FRAME_SERVER_ID, local_device_id,
                                            strlen(json_str) + 1, (uint8_t *)json_str);
                cJSON_Delete(json);
                free(json_str);
            }
            break;
        }
        default : break;
    }
}


void screen_manage_task(void *pvParameter)
{
    image_buffer = (uint8_t *)heap_caps_malloc(IMAGE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (image_buffer == NULL) {
        ESP_LOGE(TAG, "rx malloc failed!");
        goto over;
    }

    lv_obj_t * btn = lv_btn_create(lv_scr_act());           /*Add a button the current screen*/
    lv_obj_set_size(btn, 120, 50);                          /*Set its size*/
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 160);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);           /*Assign a callback to the button*/

    lv_obj_t * label = lv_label_create(btn);          /*Add a label to the button*/
    lv_label_set_text(label, "Start!");                     /*Set the labels text*/
    lv_obj_center(label);


    lv_img_dsc_t image = {
        .header.always_zero = 0,
        .header.w = 320,
        .header.h = 240,
        .header.cf = LV_IMG_CF_RAW,
        .data_size = 320 * 240 * 2,
        .data = image_buffer,
    };
    lv_obj_t * picture = lv_img_create(lv_scr_act());
    while (1) {
        xEventGroupWaitBits(app_event_group,
                        CAMERA_DATA_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
        esp_log_buffer_hex(TAG, image_buffer, 10);
        lv_img_set_src(picture, &image);
        lv_obj_center(picture);
        
        xEventGroupClearBits(app_event_group, CAMERA_DATA_BIT);
        vTaskDelay(pdMS_TO_TICKS(100));

    }
over:
    vTaskDelete(NULL);
}





void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    rgb_light_init();
    lv_init();
    lv_port_disp_init();

    wifi_account_config_t wifi_config = {
        .ssid = WIFI_AP_SSID,
        .password = WIFI_AP_PAS,
    };
    wifi_sta_init(wifi_config);

    tx_rx_buffer = (uint8_t *)heap_caps_malloc(SOCK_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (tx_rx_buffer == NULL) {
        ESP_LOGE(TAG, "tx rx malloc failed!");
        return;
    }

    sock_queue = xQueueCreate(1, sizeof(tcp_socket_info_t));
    if (sock_queue == NULL) {
        ESP_LOGE(TAG, "sock queue create failed");
        return;
    }
    app_event_group = xEventGroupCreate();
    if (app_event_group == NULL) {
        ESP_LOGE(TAG, "event group create failed");
        return;
    }
    
    tcp_clinet_config_t ap_client_config = {
        .server_ip = SOFTAP_SERVER_IP,
        .server_port = SOFTAP_TCP_PORT,
    };
    create_tcp_socket_client(&ap_client_config);
    tcp_client_register_callback(softap_server_recv_callback);
    socket_connect_register_callback(softap_server_connect_callback);

    xTaskCreatePinnedToCore(tcp_sock_handle_task, "tcp_sock_handle_task", 8 * 1024, NULL, 15, NULL, APP_CPU_NUM);

    xTaskCreatePinnedToCore(screen_manage_task, "screen_manage_task", 4096, NULL, 4, NULL, APP_CPU_NUM);
}


