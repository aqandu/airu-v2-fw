/*
 * mqtt_if.h
 *
 *  Created on: Oct 7, 2018
 *  Author: tombo
 *  Modified on: Apr 18, 2019
 *  Author: SGale
 */

#ifndef MAIN_INCLUDE_MQTT_IF_H_
#define MAIN_INCLUDE_MQTT_IF_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define MQTT_PKT_LEN 256
#define MQTT_TOPIC_LEN 50
#define MQTT_CLIENTID_LEN 100
#define MQTT_DBG_TPC "v2/dbg"
#define MQTT_DAT_TPC "airu/offline"
#define OTA_COMPLETE BIT0



void MQTT_Initialize(void);
time_t mqtt_last_publish_time();
void MQTT_Connect(void);
void get_firmware_version(void);
void mqtt_task(void* pvParameters);
void MQTT_Publish(const char* topic, const char* msg);
void ota_complete(bool ota_status);

#endif /* MAIN_INCLUDE_MQTT_IF_H_ */
