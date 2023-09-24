/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "lv_port_disp.h"
#include "lvgl_helpers.h"

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

/*********************
 *      DEFINES
 *********************/
#ifndef MY_DISP_HOR_RES
    #define MY_DISP_HOR_RES    320
#endif

#ifndef MY_DISP_VER_RES
    #define MY_DISP_VER_RES    480
#endif

#define TAG                     "lv_port_disp"
#define LV_TICK_PERIOD_MS       1

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void gui_disp_task(void *pvParameter);
/**********************
 *  STATIC VARIABLES
 **********************/

/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t xGuiSemaphore;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/


void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/
    lv_color_t *buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    assert(buf1 != NULL);

    lv_color_t *buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    assert(buf2 != NULL);

    static lv_disp_draw_buf_t disp_buf;

    uint32_t size_in_px = DISP_BUF_SIZE;

    /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, size_in_px);

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/

    static lv_disp_drv_t disp_drv;                         /*Descriptor of a display driver*/
    lv_disp_drv_init(&disp_drv);                    /*Basic initialization*/

    /*Used to copy the buffer's content to the display*/
    disp_drv.flush_cb = disp_driver_flush;

    disp_drv.draw_buf = &disp_buf;
    disp_drv.hor_res = LV_HOR_RES_MAX;
    disp_drv.ver_res = LV_VER_RES_MAX;
    lv_disp_drv_register(&disp_drv);

    /* Register an input device when enabled on the menuconfig */
#if CONFIG_LV_TOUCH_CONTROLLER != TOUCH_CONTROLLER_NONE
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.read_cb = touch_driver_read;
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&indev_drv);
#endif

    /* If you want to use a task to create the graphic, you NEED to create a Pinned task
     * Otherwise there can be problem such as memory corruption and so on.
     * NOTE: When not using Wi-Fi nor Bluetooth you can pin the gui_disp_task to core 0 */
    xTaskCreatePinnedToCore(gui_disp_task, "gui", 4096 * 1, NULL, 0, NULL, 1);

}

/**********************
 *   STATIC FUNCTIONS
 **********************/
/**
 * lvgl tick 
*/
static void lv_tick_task(void *arg)
{
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}

/**
 * gui task periodic timer task
*/
static void gui_disp_task(void *pvParameter)
{
    (void) pvParameter;
    ESP_LOGI(TAG, "start GUI diplay task");
    xGuiSemaphore = xSemaphoreCreateMutex();

    /* Create and start a periodic timer interrupt to call lv_tick_inc */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    while (1) {
        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
        }
    }

    /* A task should NEVER return */
    vTaskDelete(NULL);
}


/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /*You code here*/
    lvgl_driver_init();
}



