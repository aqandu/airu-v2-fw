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

#include "app_utils.h"
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
#include "http_file_upload.h"

/* GPIO */
#define STAT1_LED 21
#define STAT2_LED 19
#define STAT3_LED 18
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<STAT1_LED) | (1ULL<<STAT2_LED) | (1ULL<<STAT3_LED))

#define ONE_MIN 					60
#define ONE_HR						ONE_MIN * 60
#define ONE_DAY						ONE_HR * 24
#define FILE_UPLOAD_WAIT_TIME_SEC	30 //ONE_HR * 6


static char DEVICE_MAC[13];
static TaskHandle_t task_http_server = NULL;
static TaskHandle_t task_wifi_manager = NULL;
static TaskHandle_t data_task_handle = NULL;
static TaskHandle_t task_ota = NULL;
static TaskHandle_t task_led = NULL;
static TaskHandle_t task_uploadcsv = NULL;
static TaskHandle_t task_offlinetracker = NULL;
static const char *TAG = "AIRU";
static const char *TAG_OFFLINE_TRACKER = "OFFLINE";
static const char *TAG_UPLOAD = "UPLOAD";

const char file_upload_nvs_namespace[] = "fileupload";
const char* earliest_missed_data_ts = "offline";
const char* last_upload_ts = "lastup";

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

void offline_tracker_task(void *pvParameters)
{
	nvs_handle handle;
	esp_err_t esp_err;

	time_t now, nvs_ts;
	struct tm tm;
	char strftime_buf[64];
	uint8_t min, sec, system_time;

	while(1)
	{
		ESP_LOGI(TAG_OFFLINE_TRACKER, "Waiting to connect to internet");
		wifi_manager_wait_internet_access();

		ESP_LOGI(TAG_OFFLINE_TRACKER, "Waiting for time to get set");
		SNTP_time_is_set();

		ESP_LOGI(TAG_OFFLINE_TRACKER, "Waiting for disconnect");
		wifi_manager_wait_disconnect();

		time(&now);
		ESP_LOGI(TAG_OFFLINE_TRACKER, "Device disconnected from WiFi. Logging the current time: %li", now);

		esp_err = nvs_open(file_upload_nvs_namespace, NVS_READWRITE, &handle);
		if(esp_err != ESP_OK) continue;

		// Check if there is already an earlier timestamp set.
		esp_err = nvs_get_i64(handle, earliest_missed_data_ts, &nvs_ts);
		if(esp_err != ESP_OK){
			goto nvsclose;
		}

		ESP_LOGI(TAG_OFFLINE_TRACKER, "Saved timestamp: %li", nvs_ts);

		// If the timestamp is set to 0 (meaning all previous data has been uploaded)
		//	then we can set the timestamp
		if(nvs_ts == 0){
			esp_err = nvs_set_i64(handle, earliest_missed_data_ts, now);
			if(esp_err != ESP_OK){
				goto nvsclose;
			}
		}

		esp_err = nvs_commit(handle);
		if(esp_err != ESP_OK){
			goto nvsclose;
		}

nvsclose:
		nvs_close(handle);
	}
}

esp_err_t timestamp_to_filename(time_t timestamp, char* filename)
{
	struct tm tm;
	time_t now;
	time(&now);
	ESP_LOGI(TAG, "%s %d", __func__, __LINE__);
	if(timestamp < now && timestamp > 0){
		localtime_r(&timestamp, &tm);
		strftime(filename, SD_FILENAME_LENGTH, "%Y-%m-%d.csv", &tm);
		ESP_LOGI(TAG_UPLOAD, "Timestamp: %li, Filename: %s", timestamp, filename);
		return ESP_OK;
	}
	else{
		ESP_LOGE(TAG, "Problem with timestamp: %li", timestamp);
		return ESP_FAIL;
	}
}

int timestamp_round(time_t timestamp, time_t* rounded_timestamp)
{
	/**
	 * If timestamp is from previous day round to midnight of next day.
	 * If timestamp is from today, round to midnight of today.
	 * Do this so we keep upload today's file if we go offline again today.
	 *
	 * return:
	 * 		not today: 0
	 * 		today: 1
	 * 		bad: -1
	 */
	struct tm tm_today;
	struct tm tm;
	time_t now;
	int today;

	// Get today's day
	time(&now);
	localtime_r(&now, &tm_today);
	localtime_r(&timestamp, &tm);

	tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;

	// currently set to the beginning of the day
	*rounded_timestamp = mktime(&tm);

	// if the timestamp isn't from today, add a day
	if(!(tm_today.tm_year == tm.tm_year &&
	     tm_today.tm_mon  == tm.tm_mon &&
	     tm_today.tm_mday == tm.tm_mday)){
		*rounded_timestamp += 86400; // TODO: is it seconds or ms? + 1000 if ms
		return 0;
	}

	return 1;
}

esp_err_t find_next_file_from_ts(time_t* timestamp, char* filename)
{
	struct tm tm;
	struct stat st;
	time_t today;
	time(&today);
	localtime_r(&today, &tm);
	tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	today = mktime(&tm);
	FILE *fp;

	ESP_LOGI(TAG_UPLOAD, "%s %d timestamp: %li, today: %li", __func__, __LINE__, *timestamp, today);

	// go through the days until we hit today
	while(*timestamp < today)
	{
		if(timestamp_to_filename(*timestamp, filename) != ESP_OK){
			filename = NULL;
			return ESP_FAIL;
		}

		// Get the file statistics
		if(stat(filename, &st) == ESP_OK){
			return ESP_OK;
		}

		*timestamp += 86400; // TODO: or + 1000?
	}
	filename = NULL;
	return ESP_FAIL;
}

void upload_task(void *pvParameters)
{
	// TODO: If no valid files on SD card pertaining to timestamp stored in NVS, set NVS timestamp to 0
	const unsigned int more_files_wait_time_sec = 5;
	esp_err_t esp_err;
	nvs_handle handle;
	time_t now, nvs_offline_ts, nvs_lastup_ts;
	uint32_t wait_time_sec = more_files_wait_time_sec;
	bool more_files_to_send = false;
	char filename[SD_FILENAME_LENGTH];

	// Check last upload, wait that amount of time before executing

	vTaskDelay(wait_time_sec * ONE_SECOND_DELAY);

	while(1){
		// if not online, wait until we are
		wifi_manager_wait_internet_access();

		ESP_LOGI(TAG_UPLOAD, "%s online", __func__);

		ESP_LOGI(TAG_UPLOAD, "Waiting for time to get set");
		SNTP_time_is_set();

		// Get the current timestamp
		time(&now);
		ESP_LOGI(TAG_UPLOAD, "timestamp: %li", now);

		// Get the earliest offline timestamp
		esp_err = nvs_open(file_upload_nvs_namespace, NVS_READWRITE, &handle);
		if(esp_err != ESP_OK) {
			ESP_LOGE(TAG_UPLOAD, "%s %d error: %d", __func__, __LINE__, esp_err);
			more_files_to_send = true;
			goto wait_time_calc;
		}

		esp_err = nvs_get_i64(handle, earliest_missed_data_ts, &nvs_offline_ts);
		if(esp_err != ESP_OK){
			ESP_LOGE(TAG_UPLOAD, "%s %d error: %d", __func__, __LINE__, esp_err);
			goto nvsclose;
		}

		ESP_LOGI(TAG_UPLOAD, "Earliest saved timestamp: %li", nvs_offline_ts);

		// Get the filename of the earliest timestamp

		// no missed data, check again in FILE_UPLOAD_WAIT_TIME_SEC
		if(nvs_offline_ts == 0){
			more_files_to_send = false;
			goto nvsclose;
		}

		// Look for the next available file from that timestamp
		esp_err = find_next_file_from_ts(&nvs_offline_ts, filename);
		if(esp_err == ESP_OK){
			esp_err = http_upload_file_from_sd(filename);
		}

		// No file found, remove this timestamp
		if(esp_err == NO_SD_FILE_FOUND){
			nvs_offline_ts = 0;
			if(nvs_set_i64(handle, earliest_missed_data_ts, nvs_offline_ts)) goto nvsclose;
			if(nvs_commit(handle)) goto nvsclose;
		}

		// File was uploaded. Set earliest missed timestamp to the end of that day
		else if(esp_err == ESP_OK){
			esp_err = timestamp_round(nvs_offline_ts, &nvs_offline_ts);
			if(nvs_set_i64(handle, earliest_missed_data_ts, now)) goto nvsclose;
			if(nvs_commit(handle)) goto nvsclose;
		}

nvsclose:
		nvs_close(handle);

wait_time_calc:
		// if more files to send, only wait a few seconds, otherwise, wait the designated time
		wait_time_sec = (more_files_to_send) ? more_files_wait_time_sec : FILE_UPLOAD_WAIT_TIME_SEC;

		ESP_LOGI(TAG_UPLOAD, "Wait time: %d", wait_time_sec);

		/* Wait */
		vTaskDelay(wait_time_sec * ONE_SECOND_DELAY);
	}
}

/*
 * Data gather task
 */
void data_task()
{
	pm_data_t pm_dat;
	double temp, hum;
	uint16_t co, nox;
	esp_gps_t gps;
	char *pkt;
	uint64_t uptime = 0;
	uint64_t hr, rm;
	time_t now;
	struct tm tm;
	char strftime_buf[64];
	uint8_t min, sec, system_time;
	const char* SD_PKT = "%s,%s,%lu,%.2f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d\n";

	app_getmac(DEVICE_MAC);

	while (1) {

		PMS_Poll(&pm_dat);
		HDC1080_Poll(&temp, &hum);
		MICS4514_Poll(&co, &nox);
		GPS_Poll(&gps);

		uptime = esp_timer_get_time() / 1000000;

		pkt = malloc(MQTT_PKT_LEN);

		//
		// Send data over MQTT
		//

		sprintf(pkt, MQTT_PKT, DEVICE_MAC,		/* ID */
							   uptime, 			/* secActive */
							   gps.alt,			/* Altitude */
							   gps.lat, 		/* Latitude */
							   gps.lon, 		/* Longitude */
							   pm_dat.pm1,		/* PM1 */
							   pm_dat.pm2_5,	/* PM2.5 */
							   pm_dat.pm10, 	/* PM10 */
							   temp,			/* Temperature */
							   hum,				/* Humidity */
							   co,				/* CO */
							   nox);			/* NOx */

		ESP_LOGI(TAG, "MQTT PACKET:\n\r%s", pkt);
		MQTT_Publish_Data(pkt);

		time(&now);
		localtime_r(&now, &tm);
		strftime(strftime_buf, sizeof(strftime_buf), "%c", &tm);
		ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);

		if (gps.year <= 18 || gps.year >= 80){
			hr = uptime / 3600;
			rm = uptime % 3600;
			min = rm / 60;
			sec = rm % 60;

			sprintf(strftime_buf, "%llu:%02d:%02d", hr, min, sec);
			system_time = 1;	// Using system time
		}
		else {
			sprintf(strftime_buf, "%02d:%02d:%02d", gps.hour, gps.min, gps.sec);
			system_time = 0;	// Using GPS time
		}

		sprintf(pkt, SD_PKT, strftime_buf,
							 DEVICE_MAC,
							 MQTT_DATA_PUB_TOPIC,
							 uptime,
							 gps.alt,
							 gps.lat,
							 gps.lon,
							 pm_dat.pm1,
							 pm_dat.pm2_5,
							 pm_dat.pm10,
							 temp,
							 hum,
							 co,
							 nox);

		ESP_LOGI(TAG, "SD PACKET:\n\r%s", pkt);
		sd_write_data(pkt, gps.year, gps.month, gps.day);
		periodic_timer_callback(NULL);

		free(pkt);

		vTaskDelay(ONE_SECOND_DELAY * DATA_WRITE_PERIOD_SEC);
	}
}

void app_main()
{
	/* initialize flash memory */
	nvs_flash_init();

//	uint8_t tmp[6];
//	esp_efuse_mac_get_default(tmp);
//	sprintf(DEVICE_MAC, "%02X%02X%02X%02X%02X%02X", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
	app_getmac(DEVICE_MAC);

	printf("\nMAC Address: %s\n\n", DEVICE_MAC);
    ESP_LOGI(TAG, "Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

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

	/* start the HTTP Server task */
	xTaskCreate(&http_server, "http_server", 4096, NULL, 5, &task_http_server);

	/* start the wifi manager task */
	xTaskCreate(&wifi_manager, "wifi_manager", 6000, NULL, 4, &task_wifi_manager);

	/* start the led task */
	xTaskCreate(&led_task, "led_task", 2048, NULL, 3, &task_led);

	/* start the ota task */
//	xTaskCreate(&ota_task, "ota_task", 4096, NULL, 1, &task_ota);

	/* start the data gather task */
	xTaskCreate(&data_task, "Data_task", 4096, NULL, 1, &data_task_handle);

	/* Initialize SNTP */
	SNTP_Initialize();

	/* Initialize MQTT */
	MQTT_Initialize();

	/* start the csv upload task */
	xTaskCreate(&upload_task, "Upload", 2048, NULL, 1, &task_uploadcsv);

	/* start the wifi offline tracker */
	xTaskCreate(&offline_tracker_task, "offline", 2048, NULL, 1, &task_offlinetracker);

	/* In debug mode we create a simple task on core 2 that monitors free heap memory */
#if WIFI_MANAGER_DEBUG
	xTaskCreatePinnedToCore(&monitoring_task, "monitoring_task", 2048, NULL, 1, NULL, 1);
#endif
}
