/*
 * mqtt_if.c
 *
 *  Created on: Oct 7, 2018
 *  Author: tombo
 *  Modified on: Apr 17, 2019
 *  Author: SGale
 */

#include <string.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_system.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "ota_if.h"
#include "mqtt_if.h"
//#include "wifi_manager.h"
#include "hdc1080_if.h"
#include "mics4514_if.h"
#include "time_if.h"
#include "gps_if.h"
#include "pm_if.h"

#include <time.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <base64url.h>
#include "jwt_if.h"

#define WIFI_CONNECTED_BIT 	BIT0
#define RECONNECT_SECONDS 82800					// Change to 23 (82800) hours once reconnect algorithm is working
#define KEEPALIVE_TIME 240						// Setting that controls how often a pingreq is sent to IoT (240 will send a ping every 120 seconds)

static const char *TAG = "MQTT_DATA";
static TaskHandle_t task_mqtt = NULL;
static char DEVICE_MAC[13];
//extern const uint8_t ca_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t roots_pem_start[] asm("_binary_roots_pem_start");
extern const uint8_t rsaprivate_pem_start[] asm("_binary_rsaprivate_pem_start");

static bool client_connected;
static esp_mqtt_client_handle_t client;
static EventGroupHandle_t mqtt_event_group;
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event);

////Google IoT constants / connection parameters-------------------------------------------
static const char* HOST = "mqtt.googleapis.com";							// This string can also be set in menuconfig (ssl://mqtt.googleapis.com)
static const char* URI = "https://cloudiotdevice.googleapis.com";			// URI for IoT - I don't think this is used
static const char* PROJECT_ID = "scottgale";
static const int PORT = 8883;
static const char* USER_NAME = "unused"; 									// Unused by Google IoT but supplied to ensure password is read
char* JWT_PASSWORD;

static char client_ID[MQTT_CLIENTID_LEN] = {0};
static char mqtt_topic[MQTT_TOPIC_LEN] = {0};

static esp_mqtt_client_config_t getMQTT_Config(){

	JWT_PASSWORD = createGCPJWT(PROJECT_ID, rsaprivate_pem_start, strlen((char*)rsaprivate_pem_start)+1);

	esp_mqtt_client_config_t mqtt_cfg = {
		.client_id = client_ID,
		.host = HOST,
		.uri = URI,
		.username = USER_NAME,										// Not used by Google IoT -
		.password = JWT_PASSWORD,									// JWT
		.port = PORT,												// can be set static in make menuconfig
		.transport = MQTT_TRANSPORT_OVER_SSL,						// This setting is what worked
		.event_handle = mqtt_event_handler,
		.cert_pem = (const char *)roots_pem_start,					// roots_pem_start
		.keepalive = KEEPALIVE_TIME,
		.disable_auto_reconnect = true
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
	char mqtt_subscribe_topic[MQTT_TOPIC_LEN] = {0};

	switch (event->event_id) {
	   case MQTT_EVENT_CONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
		   client_connected = true;
		   free(JWT_PASSWORD);										// This frees the memory allocated in jwt_if.c
		   // Generate mqtt_topic
		   //memset(mqtt_subscribe_topic, 0, MQTT_TOPIC_LEN);
		   const char mqtt_topic_helper1[] = "/devices/M";
		   const char mqtt_topic_helper2[] = "/config";
		   snprintf(mqtt_subscribe_topic, sizeof(mqtt_subscribe_topic), "%s%s%s", mqtt_topic_helper1, DEVICE_MAC, mqtt_topic_helper2);
		   printf("MQTT subscribe topic %s\n", mqtt_subscribe_topic);
		   msg_id = esp_mqtt_client_subscribe(this_client, mqtt_subscribe_topic, 0);						// Add function call to subscribe OR subscribe
		   ESP_LOGI(TAG, "Subscribing to config, msg_id=%d", msg_id);

		   sprintf(tmp, "v2/M%s", DEVICE_MAC);
		   //msg_id = esp_mqtt_client_subscribe(this_client, (const char*) tmp, 1);
		   //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
		   break;

	   case MQTT_EVENT_DISCONNECTED:
		   //esp_mqtt_client_destroy(this_client);
		   ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		   client_connected = false;
		   ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED #2");
		   break;

	   case MQTT_EVENT_SUBSCRIBED:
		   ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
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

void mqtt_task(void* pvParameters){
	ESP_LOGI(TAG, "Starting mqtt_task ...");

	uint8_t tmp[6];								// Save MAC address for use in client_ID and mqtt_topic
	esp_efuse_mac_get_default(tmp);
	sprintf(DEVICE_MAC, "%02X%02X%02X%02X%02X%02X", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);

	// Generate client_ID . . . includes MAC Address as "IoT Device ID"
	const char mqtt_client_helper[] = "projects/scottgale/locations/us-central1/registries/airu-sensor-registry/devices/M";
	snprintf(client_ID, sizeof(client_ID), "%s%s", mqtt_client_helper, DEVICE_MAC);
	ESP_LOGI(TAG, "Generated client_ID: %s", client_ID);

	// Generate mqtt_topic
	const char mqtt_topic_helper1[] = "/devices/M";
	const char mqtt_topic_helper2[] = "/events/airU";
	snprintf(mqtt_topic, sizeof(mqtt_topic), "%s%s%s", mqtt_topic_helper1, DEVICE_MAC, mqtt_topic_helper2);
	ESP_LOGI(TAG, "Generated mqtt_topic: %s", mqtt_topic);

	static time_t dtg;								// Variables to hold sensor data
	static struct tm *dtg_struct;
	static pm_data_t pm_dat;
	static double temp, hum;
	static uint16_t co, nox;
	static esp_gps_t gps;
	//static uint64_t uptime = 0;						// Currently not using

	static char mqtt_pkt[MQTT_PKT_LEN] = {0};		// Empty packet char[]

	MQTT_Connect();
	vTaskDelay(10000 / portTICK_PERIOD_MS);			// Delay 10 seconds to connect

	time_t current_time = 0;
	time(&current_time);
	uint32_t reconnect_time = (uint32_t)current_time + RECONNECT_SECONDS;	// Must be less than setting in jwt_if.c

	while(1){
		printf("\nClient_connected: %d\n", client_connected);
		time(&current_time);
		printf("\ncurrent_time: %d\t", (uint32_t)current_time);
		printf("reconnect_time: %d\n", reconnect_time);

		//Working new algorithm
		if (current_time > reconnect_time){
			esp_mqtt_client_destroy(client);				// Stop the mqtt client and free all the memory
			MQTT_Connect();									// Reconnect
			reconnect_time = (uint32_t)current_time + RECONNECT_SECONDS;
		}

		/*
		if (!client_connected){								// This algorithm is NOT working . . . currently the board crashes and resets
			esp_mqtt_client_destroy(client);
			vTaskDelay(10000 / portTICK_PERIOD_MS);			// Delay 10 seconds
			MQTT_Connect();
		}
		*/
		else{												// Get and send data packet
			PMS_Poll(&pm_dat);
			HDC1080_Poll(&temp, &hum);
			MICS4514_Poll(&co, &nox);
			GPS_Poll(&gps);

			//uptime = esp_timer_get_time() / 1000000;
			dtg = time(NULL);								// Current UTC timestamp to include in packet

			/*
			printf("TIME: %ld\t", dtg);
			dtg = mktime(localtime(&current_time));			// Current LOCAL timestamp to include in packet
			dtg_struct = localtime(&dtg);
			printf("isdst: %d\n", dtg_struct->tm_isdst);
			*/

			memset(mqtt_pkt, 0, MQTT_PKT_LEN);
			sprintf(mqtt_pkt, "{\"DEVICE_ID\": \"M%s\", \"TIMESTAMP\": %ld, \"PM1\": %.2f, \"PM25\": %.2f, \"PM10\": %.2f, \"TEMP\": %.2f, \"HUM\": %.2f, \"CO\": %d, \"NOX\": %d, \"LAT\": %.4f, \"LON\": %.4f}", \
										DEVICE_MAC, dtg, pm_dat.pm1, pm_dat.pm2_5, pm_dat.pm10, temp, hum, co, nox, gps.lat, gps.lon);

			MQTT_Publish(mqtt_topic, mqtt_pkt);
		}

		vTaskDelay(300000 / portTICK_PERIOD_MS);			// Time in milliseconds - 300000 = 5 minutes, 600000 = 10 minutes
	}
}


void MQTT_Initialize(void)
{
   mqtt_event_group = xEventGroupCreate();
   xEventGroupClearBits(mqtt_event_group , WIFI_CONNECTED_BIT);

   /* Waiting for WiFi to connect */
   //   xEventGroupWaitBits(mqtt_event_group, WIFI_CONNECTED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

   xTaskCreate(&mqtt_task, "task_mqtt", 16000, NULL, 1, task_mqtt);
}


void MQTT_Connect(void)
{
	// Connect to Google IoT
	ESP_LOGI(TAG, "Connecting to Google IoT MQTT broker ...");
	esp_mqtt_client_config_t mqtt_cfg = getMQTT_Config();
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
void MQTT_Publish(const char* topic, const char* msg)
{
	int msg_id = 0;
	if(client_connected) {
		msg_id = esp_mqtt_client_publish(client, topic, msg, strlen(msg), 0, 0);
		ESP_LOGI(TAG, "Sent packet: %s\nTopic: %s\nmsg_id=%d", msg, topic, msg_id);
	}
	if (msg_id == -1 || !client_connected){
		ESP_LOGI(TAG, "In MQTT_Publish - client not connected - trying to reconnect . . . ");
		client_connected = false;
		esp_mqtt_client_destroy(client);

		vTaskDelay(10000 / portTICK_PERIOD_MS);			// Delay 10 seconds

		MQTT_Connect();
	}
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


