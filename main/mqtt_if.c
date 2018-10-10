/*
 * mqtt_if.c
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#include "mqtt_if.h"

#include <string.h>
#include "esp_err.h"
#include "esp_system.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "app_utils.h"

#define MQTT_DBG_TOPIC "/v2/dbg"
#define MQTT_DAT_TOPIC "/v2/dat"

static const char* TAG = "MQTT";

esp_mqtt_client_handle_t client;
extern const uint8_t ca_pem_start[] asm("_binary_ca_pem_start");


/*
* @brief
*
* @param
*
* @return
*/
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
	esp_mqtt_client_handle_t this_client = event->client;
	int msg_id = 0;
	uint8_t tmp[25];

	switch (event->event_id) {
	   case MQTT_EVENT_CONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

		   msg_id = esp_mqtt_client_subscribe(this_client, "/v2/all", 1);
		   ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

		   sprintf((char *) tmp, "/v2/%s", DEVICE_MAC);
		   msg_id = esp_mqtt_client_subscribe(this_client, (const char*) tmp, 1);
		   ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
		   break;

	   case MQTT_EVENT_DISCONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		   esp_mqtt_client_destroy(this_client);
		   mqtt_initialize();
		   break;

	   case MQTT_EVENT_SUBSCRIBED:
		   ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
		   ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
		   break;

	   case MQTT_EVENT_UNSUBSCRIBED:
		   ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
		   break;

	   case MQTT_EVENT_PUBLISHED:
		   ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
		   break;

	   case MQTT_EVENT_DATA:
		   ESP_LOGI(TAG, "MQTT_EVENT_DATA");
		   printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
		   printf("DATA=%.*s\r\n", event->data_len, event->data);
		   break;

	   case MQTT_EVENT_ERROR:
		   ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
		   break;
	}
	return ESP_OK;
}


/*
* @brief
*
* @param
*
* @return
*/
void mqtt_initialize(void)
{
	/*
	 * This exact configuration was what works. Won't work
	 * without the "transport" parameter set.
	 *
	 * Copy and paste ca.pem into project->main
	 *
	 * Define the start and end pointers in .rodata with:
	 * 	"_binary_ca_pem_start" & "_binary_ca_pem_end",
	 * 	^ it's the filename for the CA with '.' replaced with '_'
	 *
	 * Must also define the name in the component.mk file under main
	 * 	so that it gets loaded into the .data section of memory
	 */
   const esp_mqtt_client_config_t mqtt_cfg = {
   	.host = CONFIG_MQTT_HOST,
		.username = CONFIG_MQTT_USERNAME,
		.password = CONFIG_MQTT_PASSWORD,
		.port = 8883,
		.transport = MQTT_TRANSPORT_OVER_SSL,
       .event_handle = mqtt_event_handler,
       .cert_pem = (const char *)ca_pem_start,
   };

   ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
   client = esp_mqtt_client_init(&mqtt_cfg);
   esp_mqtt_client_start(client);
}


/*
* @brief
*
* @param
*
* @return
*/
void mqtt_publish(const char* topic, const char* msg)
{
	int msg_id = esp_mqtt_client_publish(client, MQTT_DBG_TOPIC, msg, 0, 0, 0);
	ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
}

