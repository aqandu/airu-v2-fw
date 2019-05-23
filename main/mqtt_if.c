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

#include "http_server_if.h"
#include "wifi_manager.h"
#include "pm_if.h"
#include "hdc1080_if.h"
#include "mics4514_if.h"
#include "time_if.h"
#include "gps_if.h"
#include "led_if.h"
#include "sd_if.h"


#define WIFI_CONNECTED_BIT 		BIT0
#define ONE_SECOND_DELAY (1000 / portTICK_PERIOD_MS)
#define THIRTY_SECONDS_COUNT 30

extern const uint8_t ca_pem_start[] asm("_binary_ca_pem_start");
extern bool MQTT_Wifi_Connection;

static const char* TAG = "MQTT";
static char DEVICE_MAC[13];
static volatile bool client_connected;
static esp_mqtt_client_handle_t client = NULL;
static EventGroupHandle_t mqtt_event_group;
static TaskHandle_t task_mqtt = NULL;
static const char *MQTT_PKT = "airQuality\,ID\=%s\,SensorModel\=H2+S2\ SecActive\=%llu\,Altitude\=%.2f\,Latitude\=%.4f\,Longitude\=%.4f\,PM1\=%.2f\,PM2.5\=%.2f\,PM10\=%.2f\,Temperature\=%.2f\,Humidity\=%.2f\,CO\=%zu\,NO\=%zu";

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event);
void data_task();
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
		   client_connected = false;
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

void mqtt_task(void* pvParameters){
	uint8_t thirtySecCounter = THIRTY_SECONDS_COUNT;
	ESP_LOGI(TAG, "Starting mqtt_task ...");

	MQTT_Connect();
	vTaskDelay(ONE_SECOND_DELAY);
	while(MQTT_Wifi_Connection) {
		if (thirtySecCounter >= THIRTY_SECONDS_COUNT) {	//	30 seconds count
			thirtySecCounter = 0;
			data_task();
		}
		// Only 1s delay to check for wifi connection status every one second
		thirtySecCounter++;
		vTaskDelay(ONE_SECOND_DELAY);
	}
	printf("\nDeleting mqtt_task\n");
	MQTT_wifi_disconnected();								// Stop the mqtt client and free all the memory
	vEventGroupDelete(mqtt_event_group);					// Delete the event group handler
	vTaskDelete(NULL);
}

/*
 * Data gather task
 */
void data_task()
{
	pm_data_t pm_dat;
	double temp, hum;
	uint16_t co, nox;
	esp_gps_t gps;
	char mqtt_pkt[MQTT_PKT_LEN];
	uint64_t uptime = 0;

	PMS_Poll(&pm_dat);
	HDC1080_Poll(&temp, &hum);
	MICS4514_Poll(&co, &nox);
	GPS_Poll(&gps);

	uptime = esp_timer_get_time() / 1000000;

	//
	// Send data over MQTT
	//
	bzero(mqtt_pkt, MQTT_PKT_LEN);
	sprintf(mqtt_pkt, MQTT_PKT, DEVICE_MAC,		/* ID */
								uptime, 		/* secActive */
								gps.alt,		/* Altitude */
								gps.lat, 		/* Latitude */
								gps.lon, 		/* Longitude */
								pm_dat.pm1,		/* PM1 */
								pm_dat.pm2_5,	/* PM2.5 */
								pm_dat.pm10, 	/* PM10 */
								temp,			/* Temperature */
								hum,			/* Humidity */
								co,				/* CO */
								nox				/* NOx */);
	ESP_LOGI(TAG, "%s", mqtt_pkt);
	MQTT_Publish(MQTT_DAT_TPC, mqtt_pkt);
	periodic_timer_callback(NULL);
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
	mqtt_event_group = xEventGroupCreate();
	xEventGroupClearBits(mqtt_event_group , WIFI_CONNECTED_BIT);
	uint8_t tmp[6];
	esp_efuse_mac_get_default(tmp);
	sprintf(DEVICE_MAC, "%02X%02X%02X%02X%02X%02X", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
	xTaskCreate(&mqtt_task, "task_mqtt", 4096, NULL, 1, task_mqtt);
}

void MQTT_Connect()
{
	ESP_LOGI(TAG, "%s enter", __func__);

	esp_mqtt_client_config_t mqtt_cfg = getMQTT_Config();
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
	ESP_LOGI(TAG, "free heap: %d\n",esp_get_free_heap_size());
	printf("Going to free MQTT memory...\n");
	esp_err_t ret = esp_mqtt_client_destroy(client);
	printf("Memory freed... %d\n", ret);
	ESP_LOGI(TAG, "free heap: %d\n",esp_get_free_heap_size());
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
	ESP_LOGI(TAG, "%s ENTERRED client_connected %d", __func__, client_connected);
	if(client_connected) {
		msg_id = esp_mqtt_client_publish(client, topic, msg, strlen(msg), 0, 0);
		ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
	}
	else {
		ESP_LOGW(TAG, "Client is not connected");
	}
}

