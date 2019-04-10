/*
 * mqtt_if.c
 *
 *  Created on: Oct 7, 2018
 *  Author: tombo
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

static const char *TAG = "MQTT_DATA";
static TaskHandle_t task_mqtt = NULL;
static char DEVICE_MAC[13];
extern const uint8_t ca_pem_start[] asm("_binary_ca_pem_start");
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
static const char* TOPIC = "/devices/M3C71BF153718/events/airU";
static char* CLIENT_ID= "projects/scottgale/locations/us-central1/registries/airu-sensor-registry/devices/M3C71BF14B324";

static char client_ID[MQTT_CLIENTID_LEN] = {0};
static char mqtt_topic[MQTT_TOPIC_LEN] = {0};


uint8_t rsa[] = "-----BEGIN RSA PRIVATE KEY-----\n"\
		"MIIEowIBAAKCAQEA96GB5a9It42MCsrhx3haflIShFebMAzxxainHqfE0C/qYIDs\n"\
		"Swi1+I5MflTJ5drgD7AH/EgMOMVAIZy972l2Mb5uJIojFy4xznVhTDw0J3BVnca+\n"\
		"tnKHpz3LCSmrSa6OKqZd9z6rspaGo5bE2HHOGA5RPclw6xsoQ2m8hlcHOw7c0gMn\n"\
		"9qLjoistiPFtVvTl+d7T7iI9XkI9bvQrCHZHbLaomR5TOLmqYOK3qK8Kk9CaD3I2\n"\
		"fY5hzD8LTnn2OnKvPLNwIfiE937AbHkoRYMY6eLfOF3MGZJ1p8NcK/o5vY0kes0S\n"\
		"w4xTWTNwxcfnt7u7AaQOzno+q9gvsnP5qlUahwIDAQABAoIBAEUgoflzaDJNYlW0\n"\
		"8zhS4bg3wxGMvza3tlp+TUDihq+zYJNWCiCcKuhbGQF/O+ldo4TdmC0WE8tZTSDU\n"\
		"97S41RTn2yl6InebHq5K2EGG4OxNkKj9zUlzSWknd+Fz72wfPXKshLi7lwTAvo82\n"\
		"THc7tdPDU2yTKmGHcEL5ZnZ+HveeDvRMAptAsERQKVXlUXeBp79yc2GDQaxodSkX\n"\
		"nqV2qUCKNs25B69z9OX2T3zmpgFCK7RJXUPKallL1XCKWiwKC2sNrW1mt617//Ca\n"\
		"byLgkHhc7ARbjoGhH4z07rxcklsy1GuCw0qWcZK2MWrpF+pOl87HzhJRwjLfCqx8\n"\
		"/78nhSECgYEA/itauFKFSnQemkHG0aFdzLYA/BN3HV5+V0rbN4SAlq7UiCQfcA90\n"\
		"UpRcGI/4yATLBBMSk1sam2gpPPfKADEYjutc0jYc33SoQORBHHIvZcVqwdV1k7v6\n"\
		"/MB7NYexBQZ6oIcGgXHKHLWWMbXpAPI/HE/sr8ptUxbcG7GTb3BA8NcCgYEA+WoY\n"\
		"4mSjxNoHoHHLK+/ByZaP0jrDQpgeNxULT9NPIw+Uz61KmOFTDRP4fUXSuQ1DZJzN\n"\
		"GI+PL2I4uNOI7MggzhDtftsia+ZGoZxAY70N8LnoZwbsfWr88jwWBMnvwNfa5iZN\n"\
		"KwbCrEZz7GsjoxF2Cqh8hOSRnyFGKbLw9t2U/dECgYA/mH10jUFIpdFaa4bhwOyF\n"\
		"YizQ5dXyBUi7csFzHLZH/aqz/cXX9iX226RHiQ6IjZp2hIcrU6pOpDtdQ+rJLX+l\n"\
		"kwKAnoWO69OFmRcplPCDGGhj45MtyeU9BLRPaopCZaKdM+vOy7f0gwL3oTqRwAtG\n"\
		"fEEOoynDln6wdzgatA2rtQKBgGxOeU3eXAuAjm1K3OpQa/uJKR0mrWH+wqgyuD3K\n"\
		"ygO0oW9plgo7VqBIOtDTgEUhkFFhkeKHfKsb4PvJyBzibvRs/2Tl7dWjIqrNOlzV\n"\
		"XPdbE6OhqxJvYjYih4E+26EHWyQ0H7B+eAztbyuL/uayD2tjbOcchmvuvBQhg2gA\n"\
		"ItHxAoGBAN+sXx6KlVfxR3kDBnZuNTIppmFLudOJn44C6y9udZ7iwYv7n0azDhsT\n"\
		"Gk+83eV8hAQtlWRK/ojUe2XDlOiyH8ZIt6WQAa4WNv16brq4CN6OeqYt1Y7enqlY\n"\
		"pztSqxIX6eYphlp3JAJDc36yVf1AYfcBHurO4j24iqiSiv8D/dqX\n"\
		"-----END RSA PRIVATE KEY-----";


static esp_mqtt_client_config_t getMQTT_Config(){

	char* JWT_PASSWORD = createGCPJWT(PROJECT_ID, rsa, sizeof(rsa)+1);
	printf("%s\n", JWT_PASSWORD);

	esp_mqtt_client_config_t mqtt_cfg = {
		.client_id = client_ID,
		.host = HOST,
		.uri = URI,
		.username = USER_NAME,										// Not used by Google IoT -
		.password = JWT_PASSWORD,									// JWT
		.port = PORT,												// can be set static in make menuconfig
		.transport = MQTT_TRANSPORT_OVER_SSL,						// This setting is what worked
		.event_handle = mqtt_event_handler,
		.cert_pem = (const char *)roots_pem_start					// roots_pem_start
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

	switch (event->event_id) {
	   case MQTT_EVENT_CONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
		   client_connected = true;
		   //msg_id = esp_mqtt_client_subscribe(this_client, "v2/all", 2);							// Add function call to subscribe OR subscribe
		   //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);							// in the connection section

		   sprintf(tmp, "v2/%s", DEVICE_MAC);
		   ESP_LOGI(TAG, "No subscriptions at this time - MTF");
		   //msg_id = esp_mqtt_client_subscribe(this_client, (const char*) tmp, 2);
		   //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
		   break;

	   case MQTT_EVENT_DISCONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		   esp_mqtt_client_destroy(this_client);
		   client_connected = false;
		   MQTT_Reinit();
		   break;

	   case MQTT_EVENT_SUBSCRIBED:
		   ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
		   ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
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
	static char mqtt_client_helper[] = "projects/scottgale/locations/us-central1/registries/airu-sensor-registry/devices/M";
	snprintf(client_ID, sizeof(client_ID), "%s%s", mqtt_client_helper, DEVICE_MAC);
	ESP_LOGI(TAG, "Generated client_ID: %s", client_ID);

	// Generate mqtt_topic
	static char mqtt_topic_helper1[] = "/devices/M";
	static char mqtt_topic_helper2[] = "/events/airU";
	snprintf(mqtt_topic, sizeof(mqtt_topic), "%s%s%s", mqtt_topic_helper1, DEVICE_MAC, mqtt_topic_helper2);
	ESP_LOGI(TAG, "Generated mqtt_topic: %s", mqtt_topic);

	// Connect to Google IoT
	esp_mqtt_client_config_t mqtt_cfg = getMQTT_Config();
	client = esp_mqtt_client_init(&mqtt_cfg);
	ESP_LOGI(TAG, "Connecting to Google IoT MQTT broker ...");
	esp_mqtt_client_start(client);

	client_connected = false;

	// Variables to hold sensor data
	pm_data_t pm_dat;
	double temp, hum;
	uint16_t co, nox;
	esp_gps_t gps;

	char mqtt_pkt[MQTT_PKT_LEN] = {0};			// Empty packet char[]

	uint64_t uptime = 0;
	time_t dtg;

	while(1){
		vTaskDelay(30000 / portTICK_PERIOD_MS);

		PMS_Poll(&pm_dat);
		HDC1080_Poll(&temp, &hum);
		MICS4514_Poll(&co, &nox);
		GPS_Poll(&gps);

		uptime = esp_timer_get_time() / 1000000;
		dtg = time(NULL);							// Save current timestamp to include in packet

		// Prepare the packet
		/* "airQuality\,ID\=%s\,SensorModel\=H2+S2\ SecActive\=%lu\,Altitude\=%.2f\,Latitude\=%.4f\,Longitude\=%.4f\,
		 * PM1\=%.2f\,PM2.5\=%.2f\,PM10\=%.2f\,Temperature\=%.2f\,Humidity\=%.2f\,CO\=%zu\,NO\=%zu";
		 */
		memset(mqtt_pkt, 0, MQTT_PKT_LEN);
		sprintf(mqtt_pkt, "{\"DEVICE_ID\": \"M%s\", \"PM1\": %.2f, \"PM25\": %.2f, \"PM10\": %.2f, \"TIMESTAMP\": %ld}", DEVICE_MAC, pm_dat.pm1, pm_dat.pm2_5, pm_dat.pm10, dtg);

		MQTT_Publish(mqtt_topic, mqtt_pkt);

				/*sprintf(mqtt_pkt, MQTT_PKT, DEVICE_MAC,		 ID
													uptime, 		 secActive
													gps.alt,		 Altitude
													gps.lat, 		 Latitude
													gps.lon, 		 Longitude
													pm_dat.pm1,		 PM1
													pm_dat.pm2_5,	 PM2.5
													pm_dat.pm10, 	 PM10
													temp,			 Temperature
													hum,			 Humidity
													co,				 CO
													nox				 NOx );*/
		//		printf("\n\rPM:\t%.2f\n\rT/H:\t%.2f/%.2f\n\rCO/NOx:\t%d/%d\n\n\r", pm_dat.pm2_5, temp, hum, co, nox);
		//		printf("Date: %02d/%02d/%d %02d:%02d:%02d\n", gps.month, gps.day, gps.year, gps.hour, gps.min, gps.sec);
		//		printf("GPS: %.4f, %.4f\n", gps.lat, gps.lon);
		//
	}
}



void MQTT_Initialize(void)
{
   mqtt_event_group = xEventGroupCreate();
   xEventGroupClearBits(mqtt_event_group , WIFI_CONNECTED_BIT);

   /* Waiting for WiFi to connect */
   //   xEventGroupWaitBits(mqtt_event_group, WIFI_CONNECTED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

   xTaskCreate(&mqtt_task, "jwt", 16000, NULL, 1, task_mqtt);
}

void MQTT_Reinit()
{
	ESP_LOGI(TAG, "Reinitializing client...");
	uint8_t tmp[6];
	esp_efuse_mac_get_default(tmp);
	sprintf(DEVICE_MAC, "%02X%02X%02X%02X%02X%02X", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
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
		ESP_LOGI(TAG, "Sent packet: %s\nTopic: %s\nmsg_id=%d", msg, topic, msg_id);
	}
	else {
		ESP_LOGI(TAG, "MQTT Client is not connected");
	}
}


