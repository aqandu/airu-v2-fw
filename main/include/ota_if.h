/*
 * ota_if.h
 *
 *  Created on: Oct 10, 2018
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_OTA_IF_H_
#define MAIN_INCLUDE_OTA_IF_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define OTA_TRIGGER_OTA_BIT BIT0
#define OTA_FILE_BN_LEN		64


void ota_task(void *pvParameters);
void ota_trigger( void );
void ota_set_filename(char *fn);


#endif /* MAIN_INCLUDE_OTA_IF_H_ */
