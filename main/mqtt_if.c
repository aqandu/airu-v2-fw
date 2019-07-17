
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
#define THIRTY_SECONDS_DELAY THIRTY_SECONDS_COUNT*ONE_SECOND_DELAY

extern const uint8_t ca_pem_start[] asm("_binary_ca_pem_start");
extern int WIFI_MANAGER_STA_DISCONNECT_BIT;

static const char* TAG = "MQTT";
static char DEVICE_MAC[13];
static volatile bool client_connected;
static esp_mqtt_client_handle_t client = NULL;
static TaskHandle_t task_mqtt = NULL;
static const char *MQTT_PKT = "airQuality\,ID\=%s\,SensorModel\=H2+S2\ SecActive\=%llu\,Altitude\=%.2f\,Latitude\=%.4f\,Longitude\=%.4f\,PM1\=%.2f\,PM2.5\=%.2f\,PM10\=%.2f\,Temperature\=%.2f\,Humidity\=%.2f\,CO\=%zu\,NO\=%zu";

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
	char pld[MQTT_BUFFER_SIZE_BYTE] = {0};

	ESP_LOGI(TAG, "EVENT ID: %d", event->event_id);

	switch (event->event_id) {
	   case MQTT_EVENT_CONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
		   client_connected = true;
		   msg_id = esp_mqtt_client_subscribe(this_client, "airu/all/v2", 2);
		   ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

		   sprintf(tmp, "airu/%s", DEVICE_MAC);
		   ESP_LOGI(TAG, "Subscribing to: %s", tmp);
		   msg_id = esp_mqtt_client_subscribe(this_client, (const char*) tmp, 2);
		   ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

		   sprintf(tmp, "airu/ack/v2/%s", DEVICE_MAC);
		   time_t now;
		   time(&now);
		   sprintf(tmp2, "up %lu", now);
		   MQTT_Publish((const char*) tmp, tmp2, 2);
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
			   sprintf(tmp, "airu/ack/v2/%s", DEVICE_MAC);
			   MQTT_Publish(tmp, "pong", 2);
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

	MQTT_Connect();
	EventBits_t uxBits;
	while(1) {
		uxBits = wifi_manager_wait_disconnect();
		if (uxBits & WIFI_MANAGER_STA_DISCONNECT_BIT) {
			break;
		}
	}
	printf("\nDeleting mqtt_task\n");
	MQTT_wifi_disconnected();			// Stop the mqtt client and free all the memory
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
	uint64_t hr, rm;
	uint8_t min, sec, system_time;
	const char* SD_PKT = "%s,%s,%lu,%.2f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d\n";
	char sd_pkt[250] = {0};

	app_getmac(DEVICE_MAC);

	while (1) {

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

		time_t now;
		struct tm tm;
		char strftime_buf[64];
		time(&now);
		localtime_r(&now, &tm);
		strftime(strftime_buf, sizeof(strftime_buf), "%c", &tm);
		ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);

		if (gps.year <= 18 || gps.year >= 80){
			hr = uptime / 3600;
			rm = uptime % 3600;
			min = rm / 60;
			sec = rm % 60;

			sprintf(strftime_buf, "%llu:%02d:%02d", hr, min, sec);
			system_time = 1;	// Using system time
		}
		else {
			sprintf(strftime_buf, "%02d:%02d:%02d", gps.hour, gps.min, gps.sec);
			system_time = 0;	// Using GPS time
		}

		sprintf(sd_pkt, SD_PKT,     strftime_buf,
		                            DEVICE_MAC,
		                            uptime,
		                            gps.alt,
		                            gps.lat,
		                            gps.lon,
		                            pm_dat.pm1,
		                            pm_dat.pm2_5,
		                            pm_dat.pm10,
		                            temp,
		                            hum,
		                            co,
		                            nox,
									system_time);

		ESP_LOGI(TAG, "%s", mqtt_pkt);
		MQTT_Publish(MQTT_DAT_TPC, mqtt_pkt, 2);
		sd_write_data(sd_pkt, gps.year, gps.month, gps.day);
		periodic_timer_callback(NULL);
		vTaskDelay(ONE_SECOND_DELAY*5);
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
void MQTT_Publish(const char* topic, const char* msg, int qos)
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

