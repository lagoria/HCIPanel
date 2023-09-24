#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_system.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "wifi_sta.h"
#include "app_config.h"

#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID		    "ESP32_SOFTAP"			// WIFI 网络名称
#define WIFI_AP_PAS			    "3325035137"			// WIFI 密码
#endif

#ifndef WIFI_MAXIMUM_RETRY
#define WIFI_MAXIMUM_RETRY		5
#endif

static const char *TAG = "app_wifi";

static esp_ip4_addr_t ip_addr;
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;


esp_ip4_addr_t get_local_ip_addr()
{
    esp_ip4_addr_t local_ip_addr = ip_addr;
    return local_ip_addr;
}

bool wait_wifi_connect()
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        return true;
    } else {
        return false;
    }
}


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 WIFI_AP_SSID, WIFI_AP_PAS);
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 WIFI_AP_SSID, WIFI_AP_PAS);
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        ip_addr = event->ip_info.ip;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}



void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

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

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_AP_SSID,
            .password = WIFI_AP_PAS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* The event will not be processed after unregister */
    // ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    // ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    // vEventGroupDelete(s_wifi_event_group);
}


