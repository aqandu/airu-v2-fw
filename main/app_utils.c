/*
 * app_utils.c
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#include "app_utils.h"
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

const char* TAG = "APP";


/*
* @brief
*
* @param
*
* @return
*/
void app_initialize(void)
{
    uint8_t mac[6];

	ESP_LOGI(TAG, "Startup..");
    ESP_LOGI(TAG, "Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
    sprintf(DEVICE_MAC, "%02X:%02X:%02X:%02X:%02X:%02X",
    		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device MAC: %s", DEVICE_MAC);

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    nvs_flash_init();
}
