/**
 * @file ili9488.c
 */

/*********************
 *      INCLUDES
 *********************/
#include "ili9488.h"
#include "disp_spi.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*********************
 *      DEFINES
 *********************/
 #define TAG "ILI9488"

/**********************
 *      TYPEDEFS
 **********************/

/*The LCD needs a bunch of command/argument values to be initialized. They are stored in this struct. */
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} lcd_init_cmd_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void ili9488_set_orientation(uint8_t orientation);

static void ili9488_send_cmd(uint8_t cmd);
static void ili9488_send_data(void * data, uint16_t length);
static void ili9488_send_color(void * data, uint16_t length);

void ili9488_full_clear(uint16_t color);
/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
// From github.com/jeremyjh/ESP32_TFT_library
// From github.com/mvturnho/ILI9488-lvgl-ESP32-WROVER-B
void ili9488_init(void)
{
	lcd_init_cmd_t ili_init_cmds[]={
		{ILI9488_CMD_SLEEP_OUT, {0x00}, 0x80},
		{ILI9488_CMD_POSITIVE_GAMMA_CORRECTION, {0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F}, 15},
		{ILI9488_CMD_NEGATIVE_GAMMA_CORRECTION, {0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F}, 15},
		{ILI9488_CMD_POWER_CONTROL_1, {0x17, 0x15}, 2},
		{ILI9488_CMD_POWER_CONTROL_2, {0x41}, 1},
		{ILI9488_CMD_VCOM_CONTROL_1, {0x00, 0x12, 0x80}, 3},
		{ILI9488_CMD_MEMORY_ACCESS_CONTROL, {(0x20 | 0x08)}, 1},
		{ILI9488_CMD_COLMOD_PIXEL_FORMAT_SET, {0x66}, 1},
		{ILI9488_CMD_INTERFACE_MODE_CONTROL, {0x00}, 1},
		{ILI9488_CMD_FRAME_RATE_CONTROL_NORMAL, {0xA0}, 1},
		{ILI9488_CMD_DISPLAY_INVERSION_CONTROL, {0x02}, 1},
		{ILI9488_CMD_DISPLAY_FUNCTION_CONTROL, {0x02, 0x02}, 2},
		{ILI9488_CMD_SET_IMAGE_FUNCTION, {0x00}, 1},
		{ILI9488_CMD_WRITE_CTRL_DISPLAY, {0x28}, 1},
		{ILI9488_CMD_WRITE_DISPLAY_BRIGHTNESS, {0x7F}, 1},
		{ILI9488_CMD_ADJUST_CONTROL_3, {0xA9, 0x51, 0x2C, 0x02}, 4},
		{ILI9488_CMD_DISPLAY_ON, {0x00}, 0x80},
		{0, {0}, 0xff},
	};

	//Initialize non-SPI GPIOs
    esp_rom_gpio_pad_select_gpio(ILI9488_DC);
	gpio_set_direction(ILI9488_DC, GPIO_MODE_OUTPUT);

#if ILI9488_USE_RST
    esp_rom_gpio_pad_select_gpio(ILI9488_RST);
	gpio_set_direction(ILI9488_RST, GPIO_MODE_OUTPUT);

	//Reset the display
	gpio_set_level(ILI9488_RST, 0);
	vTaskDelay(100 / portTICK_PERIOD_MS);
	gpio_set_level(ILI9488_RST, 1);
	vTaskDelay(100 / portTICK_PERIOD_MS);
#endif

	ESP_LOGI(TAG, "ILI9488 initialization.");

	// Exit sleep
	ili9488_send_cmd(0x01);	/* Software reset */
	vTaskDelay(100 / portTICK_PERIOD_MS);

	//Send all the commands
	uint16_t cmd = 0;
	while (ili_init_cmds[cmd].databytes!=0xff) {
		ili9488_send_cmd(ili_init_cmds[cmd].cmd);
		ili9488_send_data(ili_init_cmds[cmd].data, ili_init_cmds[cmd].databytes&0x1F);
		if (ili_init_cmds[cmd].databytes & 0x80) {
			vTaskDelay(100 / portTICK_PERIOD_MS);
		}
		cmd++;
	}

    ili9488_set_orientation(CONFIG_LV_DISPLAY_ORIENTATION);

}

#define MAX_PACKAGE_SIZE	25600

// Flush function based on mvturnho repo
void ili9488_flush(lv_disp_drv_t * drv, const lv_area_t * area, lv_color_t * color_map)
{
    uint32_t size = lv_area_get_width(area) * lv_area_get_height(area) * 3;
	
    lv_color16_t *buffer_16bit = (lv_color16_t *) color_map;
    uint8_t *mybuf = NULL;
    do {
        mybuf = (uint8_t *) heap_caps_malloc(size * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
        if (mybuf == NULL)  ESP_LOGW(TAG, "Could not allocate enough DMA memory!");
    } while (mybuf == NULL);

    uint16_t LD = 0;
    uint32_t j = 0;

    for (uint32_t i = 0; i < size / 3; i++) {
        LD = buffer_16bit[i].full;
        mybuf[j] = (uint8_t) (((LD & 0xF800) >> 8) | ((LD & 0x8000) >> 13));
        j++;
        mybuf[j] = (uint8_t) ((LD & 0x07E0) >> 3);
        j++;
        mybuf[j] = (uint8_t) (((LD & 0x001F) << 3) | ((LD & 0x0010) >> 2));
        j++;

		// mybuf[j] = (uint8_t) (LD >> 8) & 0xF8;
        // j++;
        // mybuf[j] = (uint8_t) (LD >> 3) & 0xFC;
        // j++;
        // mybuf[j] = (uint8_t) (LD << 3);
        // j++;
    }

	/* Column addresses  */
	uint8_t xb[] = {
	    (uint8_t) (area->x1 >> 8) & 0xFF,
	    (uint8_t) (area->x1) & 0xFF,
	    (uint8_t) (area->x2 >> 8) & 0xFF,
	    (uint8_t) (area->x2) & 0xFF,
	};

	/* Page addresses  */
	uint8_t yb[] = {
	    (uint8_t) (area->y1 >> 8) & 0xFF,
	    (uint8_t) (area->y1) & 0xFF,
	    (uint8_t) (area->y2 >> 8) & 0xFF,
	    (uint8_t) (area->y2) & 0xFF,
	};

	/*Column addresses*/
	ili9488_send_cmd(ILI9488_CMD_COLUMN_ADDRESS_SET);
	ili9488_send_data(xb, 4);

	/*Page addresses*/
	ili9488_send_cmd(ILI9488_CMD_PAGE_ADDRESS_SET);
	ili9488_send_data(yb, 4);

	/*Memory write*/
	ili9488_send_cmd(ILI9488_CMD_MEMORY_WRITE);

	for (uint32_t i = 0; i < size; i += MAX_PACKAGE_SIZE) {
		if (size - i < MAX_PACKAGE_SIZE) {
			ili9488_send_color((void *) &mybuf[i], size - i);
		} else {
			ili9488_send_color((void *) &mybuf[i], MAX_PACKAGE_SIZE);
		}
	}
	// ili9488_send_color((void *) mybuf, size);
	
	heap_caps_free(mybuf);

}

/**********************
 *   STATIC FUNCTIONS
 **********************/


static void ili9488_send_cmd(uint8_t cmd)
{
    disp_wait_for_pending_transactions();
    gpio_set_level(ILI9488_DC, 0);	 /*Command mode*/
    disp_spi_send_data(&cmd, 1);
}

static void ili9488_send_data(void * data, uint16_t length)
{
    disp_wait_for_pending_transactions();
    gpio_set_level(ILI9488_DC, 1);	 /*Data mode*/
    disp_spi_send_data(data, length);
}

static void ili9488_send_color(void * data, uint16_t length)
{
    disp_wait_for_pending_transactions();
    gpio_set_level(ILI9488_DC, 1);   /*Data mode*/
    disp_spi_send_colors(data, length);
}

static void ili9488_set_orientation(uint8_t orientation)
{
    // ESP_ASSERT(orientation < 4);

    const char *orientation_str[] = {
        "PORTRAIT", "PORTRAIT_INVERTED", "LANDSCAPE", "LANDSCAPE_INVERTED"
    };

    ESP_LOGI(TAG, "Display orientation: %s", orientation_str[orientation]);

#if defined (CONFIG_LV_PREDEFINED_DISPLAY_NONE)
    uint8_t data[] = {0x48, 0x88, 0x28, 0xE8};
#endif

    ESP_LOGI(TAG, "0x36 command value: 0x%02X", data[orientation]);

    ili9488_send_cmd(0x36);
    ili9488_send_data((void *) &data[orientation], 1);
}

void ili9488_set_windows(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
	/* Column addresses  */
	uint8_t xb[] = {
	    (uint8_t) (x_start >> 8) & 0xFF,
	    (uint8_t) (x_start) & 0xFF,
	    (uint8_t) (x_end >> 8) & 0xFF,
	    (uint8_t) (x_end) & 0xFF,
	};

	/* Page addresses  */
	uint8_t yb[] = {
	    (uint8_t) (y_start >> 8) & 0xFF,
	    (uint8_t) (y_start) & 0xFF,
	    (uint8_t) (y_end >> 8) & 0xFF,
	    (uint8_t) (y_end) & 0xFF,
	};

	/*Column addresses*/
	ili9488_send_cmd(ILI9488_CMD_COLUMN_ADDRESS_SET);
	ili9488_send_data(xb, 4);

	/*Page addresses*/
	ili9488_send_cmd(ILI9488_CMD_PAGE_ADDRESS_SET);
	ili9488_send_data(yb, 4);
	/*Memory write*/
	ili9488_send_cmd(ILI9488_CMD_MEMORY_WRITE);
}

void ili9488_full_clear(uint16_t color)
{
	ili9488_set_windows(0, 0, 319, 479);
	uint8_t temp[3] = {0};
	temp[0] = (uint8_t) (color >> 8) & 0xF8;
	temp[1] = (uint8_t) (color >> 3) & 0xFC;
	temp[2] = (uint8_t) (color << 3);
	for (int i = 0; i < 480; i ++) {
		for (int j = 0; j < 320; j ++) {
			ili9488_send_data(temp, 3);
		}
	}
	
}

void ili9488_put_px(uint16_t x, uint16_t y, uint16_t color)
{
	ili9488_set_windows(x, y, x, y);
	uint8_t temp[3] = {0};
	temp[0] = (uint8_t) (color >> 8) & 0xF8;
	temp[1] = (uint8_t) (color >> 3) & 0xFC;
	temp[2] = (uint8_t) (color << 3);

	ili9488_send_data(temp, 3);
	
}