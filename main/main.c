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
@brief Entry point for the ESP32 application.

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
//#include "esp_wifi.h"
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
//#include "mdns.h"
//#include "lwip/api.h"
//#include "lwip/err.h"
//#include "lwip/netdb.h"
//
//#include "http_server_if.h"
//#include "wifi_manager.h"

#include "pm_if.h"
#include "hdc1080_if.h"
#include "mics4514_if.h"
#include "gps_if.h"
#include "led_if.h"
#include "sd_if.h"

/* GPIO */
//#define STAT1_LED 21
//#define STAT2_LED 19
//#define STAT3_LED 18
//#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<STAT1_LED) | (1ULL<<STAT2_LED) | (1ULL<<STAT3_LED))

static char DEVICE_MAC[13];
static TaskHandle_t task_led = NULL;
static const char *TAG = "AIRU";

/**
 * @brief RTOS task that periodically prints the heap memory available.
 * @note Pure debug information, should not be ever started on production code!
 */
//void monitoring_task(void *pvParameter)
//{
//	while(1){
//		printf("free heap: %d\n",esp_get_free_heap_size());
//		vTaskDelay(5000 / portTICK_PERIOD_MS);
//	}
//}

/*
 * Data gather task
 */
void happy_little_task(void *pvParameters)
{
	pm_data_t pm_dat;
	esp_gps_t gps_dat;
	double temp, hum;
	uint64_t uptime;
	char sd_pkt[250];

	time_t now;
	struct tm tm;
	struct timeval tv;
	tv.tv_sec = 1555082450;
	tv.tv_usec = 0;
	char strftime_buf[64];
	GPS_SetSystemTimeFromGPS();




	while(1)
	{
		PMS_Poll(&pm_dat);
		HDC1080_Poll(&temp, &hum);
		GPS_Poll(&gps_dat);

		uptime = esp_timer_get_time() / 1000000;

		time(&now);
		localtime_r(&now, &tm);
	    strftime(strftime_buf, sizeof(strftime_buf), "%c", &tm);
	    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);

		sprintf(sd_pkt, DATA_FORMAT,
				tm.tm_hour, tm.tm_min, tm.tm_sec,	/* time */
				DEVICE_MAC,			/* ID */
				uptime, 			/* secActive */
				gps_dat.alt,		/* Altitude */
				gps_dat.lat, 		/* Latitude */
				gps_dat.lon, 		/* Longitude */
				pm_dat.pm1,			/* PM1 */
				pm_dat.pm2_5,		/* PM2.5 */
				pm_dat.pm10, 		/* PM10 */
				temp,				/* Temperature */
				hum);				/* Humidity */

		SD_LogData(sd_pkt, &tm);
		vTaskDelay(5000 / portTICK_PERIOD_MS);

	}
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
	GPS_Initialize();

	/* Initialize the PM Driver */
	PMS_Initialize();

	/* Initialize the HDC1080 Driver */
	HDC1080_Initialize();

	/* Initialize the MICS Driver */
//	MICS4514_Initialize();
	MICS4514_GPIO_Init();
	MICS4514_Disable();

	/* Initialize the SD Card Driver */
	SD_Initialize();

	/* start the led task */
	xTaskCreate(&led_task, "led_task", 2048, NULL, 3, &task_led);
//
//	/* start the data task */
	xTaskCreate(&happy_little_task, "data_task", 4096, NULL, 2, NULL);

//
//	/* In debug mode we create a simple task on core 2 that monitors free heap memory */
//#if WIFI_MANAGER_DEBUG
//	xTaskCreatePinnedToCore(&monitoring_task, "monitoring_task", 2048, NULL, 1, NULL, 1);
//#endif
}
