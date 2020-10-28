/*
 * watchdog_if.c
 *
 *  Created on: Dec 11, 2019
 *      Author: sgale
 */

#include <time.h>

#include "watchdog_if.h"
#include "mqtt_if.h"
#include "wifi_manager.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "WATCHDOG_TASK";
static int count = 0;


void watchdog_task(void *pvParameters)
{
	vTaskDelay(600000 / portTICK_PERIOD_MS);				// Pause for 10 minutes to allow WiFi and MQTT time to initialize
	ESP_LOGI(TAG, "Starting watchdog_task . . . ");
	esp_err_t twdt_error =  esp_task_wdt_init(60, true);	// Setup and initialize the Task Watchdog Timer - panic after 60 seconds
	if (twdt_error == ESP_OK){
		ESP_LOGI(TAG, "Subscribed task to Task Watchdog Timer (TWDT)");
		esp_task_wdt_add(NULL);								// Subscribe current running task to the task watchdog timer
	}
	else{
		ESP_LOGI(TAG, "UNABLE to subscribed task to Task Watchdog Timer (TWDT)");
		esp_restart();
	}


	for(;;){
		ESP_LOGI(TAG, "WATCHDOG LOOP");
		count++;
		vTaskDelay(55000 / portTICK_PERIOD_MS);				// Run the task every minute
		esp_task_wdt_reset();

		if (!wifi_manager_connected_to_access_point()){
			printf("WATCHDOG: wifi not connected. RESET CONDITION \n\n\n");
			esp_restart();
		}
		else if ((uint32_t)mqtt_last_publish_time() + FIFTY_FIVE_MINUTES <= time(NULL)) {
			printf("WATCHDOG: Publish time expired. RESET CONDITION \n\n\n");
			esp_restart();
		}
		else if (count >= 785) {
			count = 0;
			esp_restart();					// Brute force restart every 12 hours
		}
		else {
			printf("WATCHDOG: wifi connection and publish_time are good.\n");
		}
	}
}


