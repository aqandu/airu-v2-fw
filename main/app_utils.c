/*
 * app_utils.c
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "app_utils.h"

const char* TAG = "APP";
char DEVICE_MAC[13] = { 0 };

/*
* @brief	Delete the caller task and loop ad-infinitum
*
* @param	TAG: tag from caller
*
* @return 	N/A
*/
void __attribute__((noreturn)) task_fatal_error(const char *TAG)
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    (void)vTaskDelete(NULL);

    while (1) {
        ;
    }
}

void APP_Setmac()
{
	uint8_t tmp[6];
	esp_efuse_mac_get_default(tmp);
	sprintf(DEVICE_MAC, "%02X%02X%02X%02X%02X%02X", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
}

/*
* @brief
*
* @param
*
* @return
*/
void APP_Initialize(void)
{
    uint8_t sha_256[SHA256_HASH_LEN] = { 0 };
	esp_partition_t partition;

	APP_Setmac();

	ESP_LOGI(TAG, "Startup..");
    ESP_LOGI(TAG, "Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    esp_app_desc_t *app_desc = esp_ota_get_app_description();
    ESP_LOGI(TAG, "APP Version: %s", app_desc->version);

//    esp_log_level_set("*", ESP_LOG_INFO);
//    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
//    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
//    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
//    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
//    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

//	// get sha256 digest for the partition table
//	partition.address   = ESP_PARTITION_TABLE_OFFSET;
//	partition.size      = ESP_PARTITION_TABLE_MAX_LEN;
//	partition.type      = ESP_PARTITION_TYPE_DATA;
//	esp_partition_get_sha256(&partition, sha_256);
//	print_sha256(sha_256, "SHA-256 for the partition table: ");
//
//	// get sha256 digest for bootloader
//	partition.address   = ESP_BOOTLOADER_OFFSET;
//	partition.size      = ESP_PARTITION_TABLE_OFFSET;
//	partition.type      = ESP_PARTITION_TYPE_APP;
//	esp_partition_get_sha256(&partition, sha_256);
//	print_sha256(sha_256, "SHA-256 for bootloader: ");
//
//	// get sha256 digest for running partition
//	esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
//	print_sha256(sha_256, "SHA-256 for current firmware: ");

	// Initialize NVS.
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		// OTA app partition table has a smaller NVS partition size than the non-OTA
		// partition table. This size mismatch may cause NVS initialization to fail.
		// If this happens, we erase NVS partition and initialize NVS again.
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK( err );
}


/*
* @brief	Print the SHA-256 Digest
*
* @param	image_hash: SHA-256 hash of the image
* @param	label: 		image label
*
* @return 	N/A
*/
void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[SHA256_HASH_LEN * 2 + 1];
    hash_print[SHA256_HASH_LEN * 2] = 0;
    for (int i = 0; i < SHA256_HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s: %s", label, hash_print);
}
