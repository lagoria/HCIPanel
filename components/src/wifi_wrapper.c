#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_system.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "wifi_wrapper.h"

static const char *TAG = "wifi_wrapper";


static sta_info_wrapper_t *sta_list = NULL;      // 链表指针
static char local_ip_str[INET_ADDRSTRLEN] = {0};
static wifi_mode_t wifi_mode = WIFI_MODE_NULL;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_START_BIT          BIT0
#define WIFI_CONNECTED_BIT      BIT1
#define WIFI_DISCONNECTED_BIT   BIT2
#define WIFI_RESET_BIT          BIT3



static void add_wifi_sta_list_node(uint8_t *mac, uint8_t aid) 
{  
    sta_info_wrapper_t *new_sta = (sta_info_wrapper_t*)malloc(sizeof(sta_info_wrapper_t));  
    if (new_sta == NULL) return;
    if (sta_list == NULL) {
        sta_list = new_sta;
    } else {
        sta_info_wrapper_t *current;
        current = sta_list;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_sta;
    }

    // 复制节点信息
    new_sta->aid = aid;
    memcpy(new_sta->mac, mac, 6);
    new_sta->next = NULL;
}

static void delete_wifi_sta_list_node(uint8_t aid)
{
    if (sta_list == NULL) return;
    sta_info_wrapper_t *current, *prev;
    current = sta_list;
    // 判断是否为第一个节点
    if (current != NULL && current->aid == aid) {
        sta_list = current->next;
        free(current);
        return;
    }
    while (current->next != NULL) {
        prev = current;
        current = current->next;
        if (current != NULL && current->aid == aid) {
            prev->next = current->next;
            free(current);
            break;
        }
    }
}

char* get_local_ip_addr(void)
{
    return local_ip_str;
}

sta_info_wrapper_t * get_wifi_sta_list_info(void)
{
    return sta_list;
}

bool wait_wifi_connect(uint32_t wait_time)
{
    if (wifi_mode == WIFI_MODE_AP) return true;

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                        WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, wait_time);
    if (bits & WIFI_CONNECTED_BIT) {
        return true;
    } else {
        return false;
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    /* ---------------WiFi AP event----------------*/
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "softap start");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);

        add_wifi_sta_list_node(event->mac, event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);

        delete_wifi_sta_list_node(event->aid);
    }

    /* ---------------WiFi Station event----------------*/

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* staion start event */
        esp_wifi_connect();
        xEventGroupSetBits(wifi_event_group, WIFI_START_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        /* wifi connected event */
        ESP_LOGI(TAG, "wifi connected.");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* wifi disconnected event */
        xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                        WIFI_RESET_BIT,
                        pdFALSE, pdFALSE, 0);
        if (bits & WIFI_RESET_BIT) {
            /* wait wifi reset */
            ESP_LOGI(TAG, "wifi disconnected.");
        } else {
            /* reconnection */
            esp_wifi_connect();
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
        /* wifi stop event */
        ESP_LOGD(TAG, "wifi stoped.");
        xEventGroupClearBits(wifi_event_group, WIFI_START_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /* wifi connection successful */
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        sprintf(local_ip_str, IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "got ip: %s" , local_ip_str);

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupClearBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
    }
}


void wifi_sta_connect_reset(wifi_account_config_t account)
{
    
    wifi_config_t wifi_config;
    esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

    memcpy(wifi_config.sta.ssid, account.ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, account.password, sizeof(wifi_config.sta.password));

    xEventGroupSetBits(wifi_event_group, WIFI_RESET_BIT);
    esp_wifi_disconnect();
    /* wait for wifi disconnected */
    xEventGroupWaitBits(wifi_event_group,
                        WIFI_DISCONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    xEventGroupClearBits(wifi_event_group, WIFI_RESET_BIT);
    esp_wifi_connect();
}

void wifi_sta_init(wifi_account_config_t account)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.sta.ssid, account.ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, account.password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    wifi_mode = WIFI_MODE_STA;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_sta_init finished.");

}


void wifi_softap_init(wifi_account_config_t account)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .channel = CONFIG_WIFI_CHANNEL,
            .max_connection = WIFI_SOFTAP_MAXCON,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = false,
            .beacon_interval = 100,
        },
    };
    memcpy(wifi_config.ap.ssid, account.ssid, sizeof(wifi_config.ap.ssid));
    memcpy(wifi_config.ap.password, account.password, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);

    if (strlen((char *)wifi_config.ap.password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    wifi_mode = WIFI_MODE_AP;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_softap_init finished. SSID:%s password:%s channel:%d",
             account.ssid, account.password, CONFIG_WIFI_CHANNEL);

    /* save local ipv4 address string */
    esp_netif_ip_info_t current_ip_info;
    esp_netif_get_ip_info(netif, &current_ip_info);
    inet_ntoa_r(current_ip_info.ip.addr, local_ip_str, sizeof(local_ip_str));
    ESP_LOGI(TAG, "Local IP address: %s", local_ip_str);
}
