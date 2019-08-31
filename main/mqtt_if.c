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

#include "app_utils.h"
#include "http_server_if.h"
#include "http_client_if.h"
#include "wifi_manager.h"
#include "pm_if.h"
#include "hdc1080_if.h"
#include "mics4514_if.h"
#include "time_if.h"
#include "gps_if.h"
#include "led_if.h"
#include "sd_if.h"
#include "wifi_manager.h"

#define WIFI_CONNECTED_BIT 		BIT0
#define THIRTY_SECONDS_COUNT 30
#define THIRTY_SECONDS_DELAY THIRTY_SECONDS_COUNT*ONE_SECOND_DELAY

extern const uint8_t ca_pem_start[] asm("_binary_ca_airu_pem_start");
extern int WIFI_MANAGER_STA_DISCONNECT_BIT;

static const char* TAG = "MQTT";
static char DEVICE_MAC[13];
static volatile bool client_connected;
static esp_mqtt_client_handle_t client = NULL;
static TaskHandle_t task_mqtt = NULL;


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
esp_mqtt_client_config_t getMQTT_Config(){
	esp_mqtt_client_config_t mqtt_cfg = {
			.host = CONFIG_MQTT_HOST,
			.username = CONFIG_MQTT_USERNAME,
			.password = CONFIG_MQTT_PASSWORD,
			.port = MQTT_SSL_DEFAULT_PORT,
			.transport = MQTT_TRANSPORT_OVER_SSL,
			.event_handle = mqtt_event_handler,
			.cert_pem = (const char *)ca_pem_start,
	};

	return mqtt_cfg;
}

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
	char tmp[64] = {0};
	char tmp2[64] = {0};
	char tpc[64] = {0};
	char *json_buf;
	char pld[MQTT_BUFFER_SIZE_BYTE] = {0};

	ESP_LOGI(TAG, "EVENT ID: %d", event->event_id);

	switch (event->event_id) {
	   case MQTT_EVENT_CONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
		   client_connected = true;

		   // Subscribe to "all" topic
		   msg_id = esp_mqtt_client_subscribe(this_client, MQTT_SUB_ALL_TOPIC, 2);
		   ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

		   // Subscribe to "device" topic
		   sprintf(tmp, "%s/%s", CONFIG_MQTT_ROOT_TOPIC, DEVICE_MAC);

		   ESP_LOGI(TAG, "Subscribing to: %s", tmp);
		   msg_id = esp_mqtt_client_subscribe(this_client, (const char*) tmp, 2);
		   ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

		   // Respond to "ack" topic that we're online
		   sprintf(tmp, MQTT_ACK_TOPIC_TMPLT, DEVICE_MAC);

		   time_t now;
		   time(&now);
		   sprintf(tmp2, " - %lu", now);
		   json_buf = malloc(512);
		   esp_err_t err = http_get_isp_info(json_buf, 512 - strlen(tmp2));
		   strcat(json_buf, tmp2);
		   MQTT_Publish_General((const char*) tmp, json_buf, 2);
		   free(json_buf);
		   break;

	   case MQTT_EVENT_DISCONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		   MQTT_wifi_disconnected();
		   client_connected = false;

		   // Set the WIFI_MANAGER_HAVE_INTERNET_BIT: is it MQTT or internet problem?
		   wifi_manager_check_connection_async();
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
		   ESP_LOGI(TAG, "MQTT_EVENT_DATA: %s", pld);

		   const char s[2] = " ";
		   char *tok;

		   /* get the first token */
		   tok = strtok(pld, s);
		   if(tok == NULL)
			   break;

		   if(strcmp(tok, "ota") == 0){
		        tok = strtok(NULL, s);
		        if(tok != NULL && strstr(tok, ".bin")){
		        	ota_set_filename(tok);
		        	ota_trigger();
		        }
		        else{
		        	ESP_LOGI(TAG,"No binary file");
		        }
		   }

		   else if (strcmp(tok, "ping") == 0){

			   sprintf(tmp, "offline/ack/v2/%s", DEVICE_MAC);
			   MQTT_Publish_General(tmp, "pong", 2);

			   ESP_LOGI(TAG, "response: \"pong\" on \"%s\"", tmp);
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

void mqtt_task(void* pvParameters){
	ESP_LOGI(TAG, "Starting mqtt_task ...");

	// Wait for internet
	ESP_LOGI(TAG, "Waiting for internet access..");
	wifi_manager_wait_internet_access();

	ESP_LOGI(TAG, "Got internet access. Connecting...");
	MQTT_Connect();

	while(1) {

		// Wait for a disconnect event
		wifi_manager_wait_disconnect();
		ESP_LOGI(TAG, "WIFI disconnected");

		// Wait for a reconnect event and internet access
		wifi_manager_wait_internet_access();
		ESP_LOGI(TAG, "WIFI reconnected");
		MQTT_Connect();
	}
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
	ESP_LOGI(TAG, "%s Initializing client...", __func__);
	app_getmac(DEVICE_MAC);
	if (task_mqtt != NULL){
		vTaskDelete(task_mqtt);
	}
	xTaskCreate(&mqtt_task, "task_mqtt", 4096, NULL, 1, task_mqtt);
}

void MQTT_Connect()
{
	ESP_LOGI(TAG, "%s enter", __func__);

	esp_mqtt_client_config_t mqtt_cfg = getMQTT_Config();
	client = esp_mqtt_client_init(&mqtt_cfg);
	ESP_LOGI(TAG, "%s esp_mqtt_client_start [%s]", __func__, esp_err_to_name(esp_mqtt_client_start(client)));
	client_connected = false;
}

void MQTT_wifi_disconnected()
{
	ESP_LOGI(TAG, "%s: ENTERRED\n", __func__);
	if (client) {
		ESP_LOGD(TAG, "%s: Going to free some heap: %d\n", __func__, esp_get_free_heap_size());
		ESP_LOGI(TAG, "esp_mqtt_client_destroy [%s]", esp_err_to_name(esp_mqtt_client_destroy(client)));
		client = NULL;
		ESP_LOGI(TAG, "After freeing some heap: %d\n",esp_get_free_heap_size());
	}
}

/*
* @brief
*
* @param
*
* @return
*/
void MQTT_Publish_General(const char* topic, const char* msg, int qos)
{
	int msg_id;
	ESP_LOGI(TAG, "%s ENTERRED client_connected %d", __func__, client_connected);
	if(client_connected) {
		msg_id = esp_mqtt_client_publish(client, topic, msg, 0, qos, 0);
		ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
	}
	else {
		ESP_LOGW(TAG, "Client has not connected yet");
	}
}

/*
* @brief
*
* @param
*
* @return
*/
void MQTT_Publish_Data(const char* msg)
{
	MQTT_Publish_General(MQTT_DATA_PUB_TOPIC, msg, 2);
}

