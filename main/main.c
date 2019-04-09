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
static const char *MQTT_PKT = "airQuality\,ID\=%s\,SensorModel\=H2+S2\ SecActive\=%llu\,Altitude\=%.2f\,Latitude\=%.4f\,Longitude\=%.4f\,PM1\=%.2f\,PM2.5\=%.2f\,PM10\=%.2f\,Temperature\=%.2f\,Humidity\=%.2f\,CO\=%zu\,NO\=%zu";

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

//	vTaskDelay(5000 / portTICK_PERIOD_MS);
	for(;;){
		vTaskDelay(60000 / portTICK_PERIOD_MS);
		ESP_LOGI(TAG, "Data Task...");

		PMS_Poll(&pm_dat);
		HDC1080_Poll(&temp, &hum);
		MICS4514_Poll(&co, &nox);
		GPS_Poll(&gps);

		uptime = esp_timer_get_time() / 1000000;

		//
		// Send data over MQTT
		//

		// Prepare the packet
		/* "airQuality\,ID\=%s\,SensorModel\=H2+S2\ SecActive\=%lu\,Altitude\=%.2f\,Latitude\=%.4f\,Longitude\=%.4f\,
		 * PM1\=%.2f\,PM2.5\=%.2f\,PM10\=%.2f\,Temperature\=%.2f\,Humidity\=%.2f\,CO\=%zu\,NO\=%zu";
		 */
		bzero(mqtt_pkt, MQTT_PKT_LEN);
		sprintf(mqtt_pkt, MQTT_PKT, DEVICE_MAC,		/* ID */
									uptime, 		/* secActive */
									gps.alt,		/* Altitude */
									gps.lat, 		/* Latitude */
									gps.lon, 		/* Longitude */
									pm_dat.pm1,		/* PM1 */
									pm_dat.pm2_5,	/* PM2.5 */
									pm_dat.pm10, 	/* PM10 */
									temp,			/* Temperature */
									hum,			/* Humidity */
									co,				/* CO */
									nox				/* NOx */);
		MQTT_Publish(MQTT_DAT_TPC, mqtt_pkt);
		ESP_LOGI(TAG, "\nMQTT Publish Topic: %s\n", MQTT_DAT_TPC);
		ESP_LOGI(TAG, "Packet: %s\n", mqtt_pkt);
		ESP_LOGI(TAG, "\n\rPM:\t%.2f\n\rT/H:\t%.2f/%.2f\n\rCO/NOx:\t%d/%d\n\n\r", pm_dat.pm2_5, temp, hum, co, nox);
		ESP_LOGI(TAG, "GPS Datetime: %02d/%02d/%d %02d:%02d:%02d\n", gps.month, gps.day, gps.year, gps.hour, gps.min, gps.sec);
		ESP_LOGI(TAG, "GPS: %.4f, %.4f\n", gps.lat, gps.lon);
		ESP_LOGI(TAG, "Uptime: %llu\n", uptime);

		//
		// Save data to the SD card
		//
		bzero(sd_pkt, MQTT_PKT_LEN);
		sprintf(sd_pkt, "%02d:%02d:%02d,%s,%llu,%.2f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d\n",
									gps.hour, gps.min, gps.sec,	/* time */
									DEVICE_MAC,		/* ID */
									uptime, 		/* secActive */
									gps.alt,		/* Altitude */
									gps.lat, 		/* Latitude */
									gps.lon, 		/* Longitude */
									pm_dat.pm1,		/* PM1 */
									pm_dat.pm2_5,	/* PM2.5 */
									pm_dat.pm10, 	/* PM10 */
									temp,			/* Temperature */
									hum,			/* Humidity */
									co,				/* CO */
									nox,			/* NO */
									);
		sd_write_data(sd_pkt, gps.year, gps.month, gps.day);

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
	MICS4514_Initialize();

	/* Initialize the SD Card Driver */
	sd_init();

	/* start the HTTP Server task */
	xTaskCreate(&http_server, "http_server", 4096, NULL, 5, &task_http_server);

	/* start the wifi manager task */
	xTaskCreate(&wifi_manager, "wifi_manager", 6000, NULL, 4, &task_wifi_manager);

	/* start the led task */
	xTaskCreate(&led_task, "led_task", 2048, NULL, 3, &task_led);

	/* start the data task */
	xTaskCreate(&data_task, "data_task", 4096, NULL, 2, &task_data);

	/* start the ota task */
	xTaskCreate(&ota_task, "ota_task", 4096, NULL, 1, &task_ota);

	/* In debug mode we create a simple task on core 2 that monitors free heap memory */
#if WIFI_MANAGER_DEBUG
	xTaskCreatePinnedToCore(&monitoring_task, "monitoring_task", 2048, NULL, 1, NULL, 1);
#endif
}
