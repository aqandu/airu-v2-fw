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
#include "ota_if.h"

/* GPIO */
#define STAT1_LED 21
#define STAT2_LED 19
#define STAT3_LED 18
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<STAT1_LED) | (1ULL<<STAT2_LED) | (1ULL<<STAT3_LED))

static TaskHandle_t task_http_server = NULL;
static TaskHandle_t task_wifi_manager = NULL;
static const char *TAG = "AIRU";
static const char *MQTT_PKT = "airQuality\,ID\=%s\,SensorModel\=H2+S2\ POSIX\=3751032723\,SecActive\=1556892\,Altitude\=1607.30\,Latitude\=40.749818\,Longitude\=-111.803977\,PM1\=%.2f\,PM2.5\=%.2f\,PM10\=%.2f\,Temperature\=%.2f\,Humidity\=%.2f\,CO\=%zu\,NO\=%zu";
static char DEVICE_MAC[12];

/**
 * @brief RTOS task that periodically prints the heap memory available.
 * @note Pure debug information, should not be ever started on production code!
 */
void monitoring_task(void *pvParameter)
{
	for(;;){
		printf("free heap: %d\n",esp_get_free_heap_size());
		vTaskDelay(5000 / portTICK_PERIOD_MS);
	}
}

/**
 * data task
 */
static void data_task(void *pvParameter)
{
	char msg[256];
	pm_data_t pm_data;
	uint32_t ox_val, red_val;
	double temp, hum;

	vTaskDelay(20000 / portTICK_PERIOD_MS);
	for(;;)
	{
		vTaskDelay(5000 / portTICK_PERIOD_MS);

		/* PM Data */
		PMS_Poll(&pm_data);
		ESP_LOGI(TAG, "\n\r\tPM1 = %.2f\n\r\tPM2.5 = %.2f\n\r\tPM10 = %.2f\n\r", pm_data.pm1, pm_data.pm2_5, pm_data.pm10);

		/* Temperature and Humidity */
		HDC1080_Poll(&temp, &hum);
		ESP_LOGI(TAG, "\n\r\tTemperature = %.2f C\n\r\tHumidity =\t%.2f%%\n\r", temp, hum);

		/* Oxidation and Reduction Values */
		MICS4514_Poll(&ox_val, &red_val);
		ESP_LOGI(TAG, "\n\r\tOxidation = %d mV\n\r\tReduction =\t%d mV\n\r", ox_val, red_val);

		if(wifi_manager_connected_to_access_point()) {
			bzero(msg, 256);
			sprintf(msg, MQTT_PKT, DEVICE_MAC, pm_data.pm1, pm_data.pm2_5, pm_data.pm10, temp, hum, ox_val, red_val);
			ESP_LOGI(TAG, "MQTT - Publishing: \n\n\r%s\n", msg);
			mqtt_publish(MQTT_DAT_TPC, msg);
		}
		else {
			printf("WIFI not connected: skipping MQTT Publish\n\r");
		}
	}
}

static void mics_task(void *pvParameters)
{
	uint32_t ox_val = 0;
	uint32_t red_val;
	while(1)
	{
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		MICS4514_Poll(&ox_val, &red_val);
		ESP_LOGI(TAG, "%d, %d", ox_val, red_val);
	}
}


void app_main()
{
	/* disable the default wifi logging */
	esp_log_level_set("wifi", ESP_LOG_INFO);
	//esp_log_level_set(TAG, ESP_LOG_NONE);

	/* initialize flash memory */
	nvs_flash_init();

	/* get the device MAC right away */
	char *tmp = malloc(sizeof(char) * 6);
	esp_efuse_mac_get_default((uint8_t*)tmp);
	sprintf(DEVICE_MAC, "%x%x%x%x%x%x", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
	free(tmp);
	ESP_LOGE(TAG, "Device MAC: %s", DEVICE_MAC);

	/* Initialize the PM Sensor */
	PMS_Initialize();

	/* Initialize the I2C for HDC1080 */
	HDC1080_Initialize();

	/* Initialize the MICS ADC */
	MICS4514_Initialize();

	/* Mics test */
	//xTaskCreate(&mics_task, "mics_task", 2048, NULL, 5, NULL);

	/* start the HTTP Server task */
	xTaskCreate(&http_server, "http_server", 2048, NULL, 5, &task_http_server);

	/* start the wifi manager task */
	xTaskCreate(&wifi_manager, "wifi_manager", 4096, NULL, 4, &task_wifi_manager);

	/* start the OTA task */
	vTaskDelay(5000 / portTICK_PERIOD_MS);
	xTaskCreate(&ota_task, "ota_task", 2048, NULL, 6, NULL);

	/* start the data task */
	xTaskCreate(data_task, "data_task", 2048, NULL, 6, NULL);

	/* Blinky test task */
	//xTaskCreate(blinky_task, "blinky_task", 2048, NULL, 3, NULL);

	/* your code should go here. In debug mode we create a simple task on core 2 that monitors free heap memory */
#if WIFI_MANAGER_DEBUG
	xTaskCreatePinnedToCore(&monitoring_task, "monitoring_task", 2048, NULL, 1, NULL, 1);
#endif
}
