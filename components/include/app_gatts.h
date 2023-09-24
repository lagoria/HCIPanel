#ifndef APP_GATTS_H
#define APP_GATTS_H

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
// #include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt.h"
#include "esp_gatt_common_api.h"

/* Attributes State Machine */
enum
{
    IDX_SVC,

	TEST_CUSTOM_CHAR,
	TEST_CUSTOM_CHAR_VAL,		//2
	TEST_CUSTOM_CHAR_CFG,		//3

    HRS_IDX_NB,
};


typedef struct {
	esp_gatt_if_t gatts_if;
	uint16_t conn_id;
	uint8_t index;
	uint8_t *data;
	uint32_t len;
} gatts_data_t;

// 定义 GATTS 接收数据的回调函数原型
typedef void (*gatts_receive_callback_t)(gatts_data_t);


/*---------------------------------------------*/

// 初始化蓝牙，开启GATT服务，创建接收任务gatts_custom_task
void ble_gatts_init();

// 找到属性表特征下标
uint8_t find_char_and_desr_index(uint16_t handle);

// 向客户端发送数据
// void gatts_send_indicate(esp_gatt_if_t gatt_if, uint16_t conn_id, uint8_t *send_data, size_t data_len);

// 注册 GATTS 接收数据的回调函数
void register_gatts_receive_callback(gatts_receive_callback_t callback);

#endif