#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"

// OTA stuff
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "app_utils.h"
#include "wifi_if.h"
#include "mqtt_if.h"
#include "time_if.h"

#define EXAMPLE_SERVER_URL "air.eng.utah.edu/files/updates/mqtt_ssl.bin"
#define BUFFSIZE 1024
#define HASH_LEN 32 /* SHA-256 digest length */

static const char* TAG = "APP";
static const char* SNTP_TASK_TAG = "SNTP";

//static const char *TAG = "native_ota_example";
/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[BUFFSIZE + 1] = { 0 };
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");


/*
* @brief	Get and print the current GM Time
*
* @param	pvParameters: Unused
*
* @return	N/A
*/
static void sntp_task(void *pvParameters)
{
	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];

	while(1)
	{
		vTaskDelay(1000 / portTICK_PERIOD_MS);

		now = time_gmtime();
	    gmtime_r(&now, &timeinfo);
	    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	    ESP_LOGI(SNTP_TASK_TAG, "\t%s", strftime_buf);
	}
}


/*
* @brief	WiFi, MQTT, SNTP
* 			Still need:
* 				- OTA
* 				- Bluetooth
* 				- MQTT arguments
* 				- Sensors (PM, T&H, MICS)
* 				- SD Card
*
* @param	N/A
*
* @return	N/A
*/
void app_main()
{
	ESP_LOGI(TAG, "Starting application");

	app_initialize();
    wifi_init_sta();

    xTaskCreate(sntp_task, "sntp_task", 4 * 1024, NULL, 5, NULL);
}
