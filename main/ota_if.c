/*
 * ota_if.c
 *
 * Notes:
 * 		Change the partition table option in menuconfig to
 * 		factory, two apps.
 *
 * 		Change flash size to 4MB in menuconfig flash options
 *
 *  Created on: Oct 10, 2018
 *      Author: tombo
 */

#include <string.h>
#include "ota_if.h"
#include "app_utils.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "app_utils.h"

#define BUFFSIZE 1024

static EventGroupHandle_t ota_event_group;
static char ota_file_basename[OTA_FILE_BN_LEN] = {0};

static const char *TAG = "OTA";
static char ota_write_data[BUFFSIZE + 1] = { 0 };
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static void _http_cleanup(esp_http_client_handle_t client);
static esp_err_t _ota_commence( void );


static void _http_cleanup(esp_http_client_handle_t client)
{
	esp_http_client_close(client);
    esp_http_client_cleanup(client);
}


void ota_trigger()
{
	xEventGroupSetBits(ota_event_group, OTA_TRIGGER_OTA_BIT);
}

void ota_set_filename(char *fn){
	strcpy(ota_file_basename, fn);
	ESP_LOGI(TAG, "File copy: %s", ota_file_basename);
}


void ota_task(void *pvParameters)
{
	esp_err_t err;
	ota_event_group = xEventGroupCreate();
	bzero(ota_file_basename, OTA_FILE_BN_LEN);
	xEventGroupClearBits(ota_event_group, OTA_TRIGGER_OTA_BIT);
	ESP_LOGI(TAG, "Waiting for MQTT to trigger OTA...");

	for(;;) {

		xEventGroupWaitBits(ota_event_group, OTA_TRIGGER_OTA_BIT, pdTRUE, pdTRUE, portMAX_DELAY );
		ESP_LOGI(TAG, "MQTT triggered OTA...");
		err = _ota_commence();

	}

}


/*
 * Trigger OTA using file defined by 'ota_file_basename'
 */
static esp_err_t _ota_commence()
{
    

    esp_err_t err;
    char fn_path[64 + OTA_FILE_BN_LEN] = { 0 };

    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting OTA...");

    if (strlen(ota_file_basename) <= 0) {
    	ESP_LOGI(TAG, "No filename... Exiting.");
    	return ESP_FAIL;
    }

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    sprintf(fn_path, "http://air.eng.utah.edu:80/files/updates/%s", ota_file_basename);
    ESP_LOGI(TAG, "OTA: %s", fn_path);

    esp_http_client_config_t config = {
        .url = fn_path,
        .cert_pem = (char *)server_cert_pem_start,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP connection");
        return ESP_FAIL;
//        task_fatal_error(TAG);
    }
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return ESP_FAIL;
//        task_fatal_error(TAG);
    }
    esp_http_client_fetch_headers(client);

    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);
    assert(update_partition != NULL);

    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        _http_cleanup(client);
        return ESP_FAIL;
//        task_fatal_error(TAG);
    }
    ESP_LOGI(TAG, "esp_ota_begin succeeded");

    int binary_file_length = 0;
    /*deal with all receive packet*/
    while (1) {
        int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            _http_cleanup(client);
            return ESP_FAIL;
//            task_fatal_error(TAG);
        }
        else if (data_read > 0) {
            err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                _http_cleanup(client);
                return ESP_FAIL;
//                task_fatal_error(TAG);
            }
            binary_file_length += data_read;
            ESP_LOGI(TAG, "Written image length: %d", binary_file_length);
        }
        else if (data_read == 0) {
            ESP_LOGI(TAG, "Connection closed,all data received");
            break;
        }
    }
    ESP_LOGI(TAG, "Total Write binary data length : %d", binary_file_length);

    if (esp_ota_end(update_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed!");
        _http_cleanup(client);
        return ESP_FAIL;
//        task_fatal_error(TAG);
    }

    if (esp_partition_check_identity(esp_ota_get_running_partition(), update_partition) == true) {
        ESP_LOGI(TAG, "The current running firmware is same as the firmware just downloaded");
        ESP_LOGI(TAG, "When a new firmware is available on the server, press the reset button to download it");
        return ESP_FAIL;
//        while(1) {
//            ESP_LOGI(TAG, "Waiting for a new firmware ... %d", ++i);
//            vTaskDelay(2000 / portTICK_PERIOD_MS);
//        }
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        _http_cleanup(client);
        return ESP_FAIL;
//        task_fatal_error(TAG);
    }
    ESP_LOGI(TAG, "Prepare to restart system!");
    esp_restart();
    return ESP_OK;
}
