/*
 * mqtt_if.c
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#include <string.h>
#include "esp_err.h"
#include "esp_system.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "ota_if.h"
#include "mqtt_if.h"
//#include "wifi_manager.h"

#define WIFI_CONNECTED_BIT 		BIT0

static const char* TAG = "MQTT";
static char DEVICE_MAC[13];
extern const uint8_t ca_pem_start[] asm("_binary_ca_pem_start");

static bool client_connected;
static esp_mqtt_client_handle_t client;
static EventGroupHandle_t mqtt_event_group;

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event);

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
static const esp_mqtt_client_config_t mqtt_cfg = {
	.host = CONFIG_MQTT_HOST,
	.username = CONFIG_MQTT_USERNAME,
	.password = CONFIG_MQTT_PASSWORD,
	.port = MQTT_SSL_DEFAULT_PORT,
	.transport = MQTT_TRANSPORT_OVER_SSL,
	.event_handle = mqtt_event_handler,
	.cert_pem = (const char *)ca_pem_start,
};


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
	char tmp[25] = {0};
	char tpc[25] = {0};
	char pld[MQTT_BUFFER_SIZE_BYTE] = {0};

	ESP_LOGI(TAG, "EVENT ID: %d", event->event_id);

	switch (event->event_id) {
	   case MQTT_EVENT_CONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
		   client_connected = true;
		   msg_id = esp_mqtt_client_subscribe(this_client, "v2/all", 2);
		   ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

		   sprintf(tmp, "v2/%s", DEVICE_MAC);
		   ESP_LOGI(TAG, "Subscribing to: %s", tmp);
		   msg_id = esp_mqtt_client_subscribe(this_client, (const char*) tmp, 2);
		   ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
		   break;

	   case MQTT_EVENT_DISCONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		   esp_mqtt_client_destroy(this_client);
		   client_connected = false;
		   MQTT_Reinit();
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
		   strncpy(tpc, event->topic, event->topic_len);
		   strncpy(pld, event->data, event->data_len);
		   ESP_LOGI(TAG, "MQTT_EVENT_DATA");

		   const char s[2] = " ";
		   char *tok;

		   /* get the first token */
		   tok = strtok(pld, s);

		   if(tok != NULL && strcmp(tok, "ota") == 0){
		        tok = strtok(NULL, s);
		        if(tok != NULL && strstr(tok, ".bin")){
		        	ota_set_filename(tok);
		        	ota_trigger();
		        }
		        else{
		        	ESP_LOGI(TAG,"No binary file");
		        }
		   }

		   break;

	   case MQTT_EVENT_ERROR:
		   ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
		   break;

	   default:
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
void MQTT_Initialize(void)
{
   mqtt_event_group = xEventGroupCreate();
   xEventGroupClearBits(mqtt_event_group , WIFI_CONNECTED_BIT);

   /* Waiting for WiFi to connect */
//   xEventGroupWaitBits(mqtt_event_group, WIFI_CONNECTED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

   ESP_LOGI(TAG, "Initializing client...");

   uint8_t tmp[6];
   esp_efuse_mac_get_default(tmp);
   sprintf(DEVICE_MAC, "%02X%02X%02X%02X%02X%02X", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
   client = esp_mqtt_client_init(&mqtt_cfg);
   esp_mqtt_client_start(client);

   client_connected = false;
}

void MQTT_Reinit()
{
	ESP_LOGI(TAG, "Reinitializing client...");
	uint8_t tmp[6];
	esp_efuse_mac_get_default(tmp);
	sprintf(DEVICE_MAC, "%02X%02X%02X%02X%02X%02X", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
	client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_start(client);

	client_connected = false;
}

/*
* @brief Signals MQTT to initialize.
*
* @param
*
* @return
*/
void MQTT_wifi_connected()
{
	xEventGroupSetBits(mqtt_event_group, WIFI_CONNECTED_BIT);
}

void MQTT_wifi_disconnected()
{
	xEventGroupClearBits(mqtt_event_group, WIFI_CONNECTED_BIT);
	esp_mqtt_client_stop(client);

}

/*
* @brief
*
* @param
*
* @return
*/
void MQTT_Publish(const char* topic, const char* msg)
{
	int msg_id;
	if(client_connected) {
		msg_id = esp_mqtt_client_publish(client, topic, msg, strlen(msg), 0, 0);
		ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
	}
	else {
		ESP_LOGI(TAG, "MQTT Client is not connected");
	}
}

