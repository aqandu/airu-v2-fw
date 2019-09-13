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
			within 4 decimal points, so we can use it as a tag in InfluxDB.
*/

// Base necessary
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_adc_cal.h"
#include "esp_spi_flash.h"
#include "esp_ota_ops.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "app_utils.h"
#include "pm_if.h"
#include "led_if.h"
#ifdef CONFIG_USE_SD
#include "sd_if.h"
#endif
#include "hdc1080_if.h"
#include "mics4514_if.h"
#include "gps_if.h"


/* GPIO */
#define STAT1_LED 21
#define STAT2_LED 19
#define STAT3_LED 18
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<STAT1_LED) | (1ULL<<STAT2_LED) | (1ULL<<STAT3_LED))

static TaskHandle_t data_task_handle = NULL;
static TaskHandle_t task_led = NULL;
static const char *TAG = "AIRU";

/*
 * Data gather task
 */
void data_task()
{
	pm_data_t pm_dat;
	double temp, hum;
	int co, nox;
	esp_gps_t gps;
	char *pkt;
	uint64_t uptime = 0;
	uint64_t hr, rm;
	time_t now;
	struct tm tm;
	char strftime_buf[64];
	uint8_t min, sec, system_time;

	// only need to get it once
	esp_app_desc_t *app_desc = esp_ota_get_app_description();

	while (1) {

//		PMS_Poll(&pm_dat);
//		HDC1080_Poll(&temp, &hum);
//		MICS4514_Poll(&nox, &co);
//		GPS_Poll(&gps);
//
//		uptime = esp_timer_get_time() / 1000000;
//
//		pkt = malloc(SD_PKT_LEN);

		/************************************
		 * Save to SD Card
		 *************************************/
		time(&now);
		localtime_r(&now, &tm);
		strftime(strftime_buf, sizeof(strftime_buf), "%c", &tm);
		ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);

//		if (gps.timeinfo.tm_year <= 18 || gps.timeinfo.tm_year>= 80){
//			hr = uptime / 3600;
//			rm = uptime % 3600;
//			min = rm / 60;
//			sec = rm % 60;
//
//			sprintf(strftime_buf, "%llu:%02d:%02d", hr, min, sec);
//			system_time = 1;	// Using system time
//		}
//		else {
//			sprintf(strftime_buf, "%02d:%02d:%02d", gps.timeinfo.tm_hour, gps.timeinfo.tm_min, gps.timeinfo.tm_sec);
//			system_time = 0;	// Using GPS time
//		}
//
//		sprintf(pkt, SD_PKT, strftime_buf,
//							 DEVICE_MAC,
//							 uptime,
//							 gps.alt,
//							 gps.lat,
//							 gps.lon,
//							 pm_dat.pm1,
//							 pm_dat.pm2_5,
//							 pm_dat.pm10,
//							 temp,
//							 hum,
//							 co,
//							 nox);
//
//		sd_write_data(pkt, gps.timeinfo.tm_year, gps.timeinfo.tm_mon, gps.timeinfo.tm_mday);
//
//		free(pkt);

		vTaskDelay(ONE_SECOND_DELAY * /*CONFIG_DATA_WRITE_PERIOD*/ 5);
	}
}

void app_main()
{
	/* initialize flash memory */

	APP_Initialize();
	printf("\nMAC Address: %s\n\n", DEVICE_MAC);

	/* Initialize the LED Driver */
	LED_Initialize();

	/* Initialize the GPS Driver */
	GPS_Initialize();

	/* Initialize the PM Driver */
	PMS_Initialize();

	/* Initialize the HDC1080 Driver */
	HDC1080_Initialize();

	/* Initialize the MICS Driver */
	MICS4514_Initialize();

	/* Initialize the SD Card Driver */
	SD_Initialize();

	/* start the led task */
	xTaskCreate(&led_task, "led_task", 2048, NULL, 3, &task_led);

	/* start the data gather task */
	xTaskCreate(&data_task, "Data_task", 4096, NULL, 1, &data_task_handle);

//	/* In debug mode we create a simple task on core 2 that monitors free heap memory */
//#if WIFI_MANAGER_DEBUG
//	xTaskCreatePinnedToCore(&monitoring_task, "monitoring_task", 2048, NULL, 1, NULL, 1);
//#endif
}
