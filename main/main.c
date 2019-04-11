/*
Copyright (c) 2018 University of Utah

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

@file main.c
@author Thomas Becnel
@author Trenton Taylor
@brief Entry point for the ESP32 application.
@thanks Special thanks to Tony Pottier for the esp32-wifi-manager repo
	@see https://idyl.io
	@see https://github.com/tonyp7/esp32-wifi-manager

Notes:
	- GPS: 	keep rolling average of GPS alt, lat, lon. Set up GPS UART handler
			similar to PM UART, where every sample that comes in gets parsed.
			Accumulate the last X measurements, and when we publish over MQTT
			take an average and send it. We need the GPS location to be the same
			within 4 decimal points.
*/


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_adc_cal.h"
#include "esp_spi_flash.h"
#include "esp_event_loop.h"

#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "soc/rtc.h"

#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "mdns.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"

#include "http_server_if.h"
#include "wifi_manager.h"
#include "pm_if.h"
#include "mqtt_if.h"
#include "hdc1080_if.h"
#include "mics4514_if.h"
#include "time_if.h"
#include "gps_if.h"
#include "ota_if.h"
#include "led_if.h"
#include "sd_if.h"

/* GPIO */
#define STAT1_LED 21
#define STAT2_LED 19
#define STAT3_LED 18
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<STAT1_LED) | (1ULL<<STAT2_LED) | (1ULL<<STAT3_LED))

static char DEVICE_MAC[13];
static TaskHandle_t task_http_server = NULL;
static TaskHandle_t task_wifi_manager = NULL;
static TaskHandle_t task_data = NULL;
static TaskHandle_t task_ota = NULL;
static TaskHandle_t task_led = NULL;
static const char *TAG = "AIRU";

/**
 * @brief RTOS task that periodically prints the heap memory available.
 * @note Pure debug information, should not be ever started on production code!
 */
void monitoring_task(void *pvParameter)
{
	while(1){
		printf("free heap: %d\n",esp_get_free_heap_size());
		vTaskDelay(5000 / portTICK_PERIOD_MS);
	}
}

/*
 * Data gather task
 */
void data_task(void *pvParameters)
{
	pm_data_t pm_dat;
	double temp, hum;
	uint16_t co, nox;
	esp_gps_t gps;
	char mqtt_pkt[MQTT_PKT_LEN];
	char sd_pkt[MQTT_PKT_LEN];
	uint64_t uptime = 0;

}

void app_main()
{

	/* initialize flash memory */
	nvs_flash_init();

	uint8_t tmp[6];
	esp_efuse_mac_get_default(tmp);
	sprintf(DEVICE_MAC, "%02X%02X%02X%02X%02X%02X", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);

	printf("\nMAC Address: %s\n\n", DEVICE_MAC);

	/* Initialize the LED Driver */
	LED_Initialize();

	/* Initialize the GPS Driver */
//	GPS_Initialize();

	/* Initialize the PM Driver */
//	PMS_Initialize();

	/* Initialize the HDC1080 Driver */
//	HDC1080_Initialize();

	/* Initialize the MICS Driver */
//	MICS4514_Initialize();
	MICS4514_GPIO_Init();

	/* Initialize the SD Card Driver */
//	sd_init();

	/* start the HTTP Server task */
//	xTaskCreate(&http_server, "http_server", 4096, NULL, 5, &task_http_server);

	/* start the wifi manager task */
//	xTaskCreate(&wifi_manager, "wifi_manager", 6000, NULL, 4, &task_wifi_manager);

	/* start the led task */
//	xTaskCreate(&led_task, "led_task", 2048, NULL, 3, &task_led);
//
//	/* start the data task */
//	xTaskCreate(&data_task, "data_task", 4096, NULL, 2, &task_data);
//
//	/* start the ota task */
////	xTaskCreate(&ota_task, "ota_task", 4096, NULL, 1, &task_ota);
//
//	/* In debug mode we create a simple task on core 2 that monitors free heap memory */
//#if WIFI_MANAGER_DEBUG
//	xTaskCreatePinnedToCore(&monitoring_task, "monitoring_task", 2048, NULL, 1, NULL, 1);
//#endif
	static RTC_DATA_ATTR struct timeval sleep_enter_time;
	struct timeval now;
	gettimeofday(&now, NULL);
	int sleep_time_ms = (now.tv_sec - sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - sleep_enter_time.tv_usec) / 1000;
	switch (esp_sleep_get_wakeup_cause()) {
		case ESP_SLEEP_WAKEUP_TIMER: {
			ESP_LOGI(TAG, "Wake up from timer. Time spent in deep sleep: %dms", sleep_time_ms);
			break;
		}
		case ESP_SLEEP_WAKEUP_UNDEFINED:
		default:
			ESP_LOGI(TAG, "Not a deep sleep reset");

	}

	const int wakeup_time_sec = 5;
	ESP_LOGI(TAG, "Enabling timer wakeup %ds", wakeup_time_sec);
	esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);
	esp_deep_sleep_start();
}
