/*
 * app_utils.h
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#ifndef MAIN_APP_UTILS_H_
#define MAIN_APP_UTILS_H_

#include <stdint.h>
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define SHA256_HASH_LEN 32	 		/* SHA-256 digest length */
#define WIFI_CONNECTED_BIT BIT0		/* IP address obtained */
#define OTA_REQUEST_BIT BIT1		/* OTA request recieved over MQTT */

EventGroupHandle_t wifi_event_group;

/*
* @brief	Delete the caller task and loop ad-infinitum
*
* @param	TAG: tag from caller
*
* @return 	N/A
*/
void __attribute__((noreturn)) task_fatal_error(const char *TAG);


/*
* @brief
*
* @param
*
* @return
*/
void app_initialize(void);


/*
* @brief	Print the SHA-256 Digest
*
* @param	image_hash: SHA-256 hash of the image
* @param	label: 		image label
*
* @return 	N/A
*/
void print_sha256(const uint8_t *image_hash, const char *label);


#endif /* MAIN_APP_UTILS_H_ */
