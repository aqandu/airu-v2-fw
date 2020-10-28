/*
 * mqtt_if.c
 *
 *  Created on: Oct 7, 2018
 *  Author: tombo
 *  Last modified on: Oct 2, 2019
 *  Author: SGale
 */

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <base64url.h>

#include "esp_err.h"
#include "esp_system.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "ota_if.h"
#include "mqtt_if.h"
#include "wifi_manager.h"
#include "hdc1080_if.h"
#include "mics4514_if.h"
#include "time_if.h"
#include "gps_if.h"
#include "pm_if.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "jwt_if.h"


// #define WIFI_CONNECTED_BIT 	BIT0
#define RECONNECT_SECONDS 82800		// Frequency to reconnect to Google IoT (82800 = 23 hours) JWT expires at 24 hours
#define KEEPALIVE_TIME 300			// Frequency pingreq is sent to IoT (300 will send a ping every 150 seconds)

static const char *TAG = "MQTT_DATA";
static TaskHandle_t task_mqtt = NULL;
static EventGroupHandle_t mqtt_event_group;
static char DEVICE_MAC[13];
extern const uint8_t roots_pem_start[] asm("_binary_roots_pem_start");
extern uint8_t rsaprivate_pem_start[] asm("_binary_rsaprivate_pem_start");
static esp_mqtt_client_handle_t client;
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event);
char firmware_version[OTA_FILE_BN_LEN];
char current_state[128];

////Google IoT constants / connection parameters-------------------------------------------
static const char* HOST = "mqtt.googleapis.com";					// This string can also be set in menuconfig (ssl://mqtt.googleapis.com)
static const char* URI = "mqtts://mqtt.googleapis.com:8883";		// URI for IoT
static const int PORT = 8883;
static const char* USER_NAME = "unused"; 							// Unused by Google IoT but supplied to ensure password is read
char* JWT_PASSWORD;

//*******ENSURE THIS IS CORRECT********************************************************************
static const char* PROJECT_ID = "scottgale";
//static const char* PROJECT_ID = "aqandu-184820";
//static const char* PROJECT_ID = "airquality-272118";

//*************************************************************************************************

static char client_ID[MQTT_CLIENTID_LEN] = {0};
static char mqtt_topic[MQTT_TOPIC_LEN] = {0};
static char mqtt_state_topic[MQTT_TOPIC_LEN] = {0};
uint32_t reconnect_time;							// Used in the event handler and while() loop to denote when the JWT expires.
time_t last_publish_time; 						 	// Ensure MQTT is publishing once per hour
bool service_ota = false;							// flag to set when an ota update is requested - set in event handler / handled in MQTT loop
bool client_connected;								// indicates that the MQTT connection is active
bool ota_successful;								// indicates if the last ota update was successful or not


/*
* @brief Waits for wifi to connect to the Internet then creates the MQTT task. Calls a blocking function
* from within wifi_manager.c (wifi_manager_wait_internet_access())
*
* @param N/A
*
* @return N/A
*/
void MQTT_Initialize(void)
{
   // Function (wifi_manager.c) waits for the wifi to connect
	printf("MQTT Blocked: wifi_manager_wait_internet_access_and_AP()\n");
	//wifi_manager_wait_internet_access();
	wifi_manager_wait_internet_access_and_AP();
	printf("PASSED wifi_manager_wait_internet_access_and_AP()\n");
	xTaskCreate(&mqtt_task, "mqtt_task", 16000, NULL, 2, task_mqtt);
}

/*
* @brief Used by the watchdog_if.c to get the last mqtt publish time
*
* @param N/A
*
* @return type time_t; the last time a successful mqtt packet was published
*/
time_t mqtt_last_publish_time(){
	return last_publish_time;
}


/*
* @brief Acquires a JSON WEB TOKEN (JWT) that is used as the password in the client configuration.
* Initializes the client configuration for connection with IoT.
*
* @param N/A
*
* @return configuration structure used to connect to IoT.
*/
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
* @brief Event handler for the MQTT client.
*
* @param event: This captures details about the event that called the handler.
*
* @return N/A
*/
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
	esp_mqtt_client_handle_t this_client = event->client;
	int msg_id = 0;
	char topic[MQTT_TOPIC_LEN] = {0};
	char payload[MQTT_TOPIC_LEN] = {0};
	char mqtt_subscribe_topic[MQTT_TOPIC_LEN] = {0};

	switch (event->event_id) {
	   case MQTT_EVENT_CONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
		   client_connected = true;
		   free(JWT_PASSWORD);										// This frees the memory allocated in jwt_if.c
		   const char mqtt_topic_helper1[] = "/devices/M";			// Helper char[] to create subscription strings for subscriptions
		   const char mqtt_topic_helper2[] = "/config";
		   const char mqtt_topic_helper3[] = "/commands/#";

		   // Subscribing to the configuration topic. This allows communication from IoT to the sensor for ota firmware updates
		   snprintf(mqtt_subscribe_topic, sizeof(mqtt_subscribe_topic), "%s%s%s", mqtt_topic_helper1, DEVICE_MAC, mqtt_topic_helper2);
		   msg_id = esp_mqtt_client_subscribe(this_client, mqtt_subscribe_topic, 1);		// QOS 1 sends every time a device restarts
		   ESP_LOGI(TAG, "Subscribing to %s, msg_id=%d", mqtt_subscribe_topic, msg_id);

		   // Subscribing to the command topic. This allows messages to be sent from the GCP to the device - currently used for the "restart"
		   memset(mqtt_subscribe_topic, 0, MQTT_TOPIC_LEN);

		   snprintf(mqtt_subscribe_topic, sizeof(mqtt_subscribe_topic), "%s%s%s", mqtt_topic_helper1, DEVICE_MAC, mqtt_topic_helper3);
		   msg_id = esp_mqtt_client_subscribe(this_client, mqtt_subscribe_topic, 0);	// QOS 0 - No guarantee (best effort only)
		   ESP_LOGI(TAG, "Subscribing to %s, msg_id=%d", mqtt_subscribe_topic, msg_id);

		   break;

	   case MQTT_EVENT_DISCONNECTED:
		   ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
		   client_connected = false;
		   esp_restart();
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
		   strncpy(topic, event->topic, event->topic_len);
		   strncpy(payload, event->data, event->data_len);
		   ESP_LOGI(TAG, "MQTT_EVENT_DATA, topic: %s, payload: %s", topic, payload);
		   if (strcmp(payload, "restart")==0){
			   esp_restart();
		   }
		   else{
			   const char space[2] = " ";				// Use a space a the delimiter for the strtok
			   char *tok;

			   tok = strtok(payload, space);			// Get the first token

			   if(tok != NULL && strcmp(tok, "ota") == 0) {
				   tok = strtok(NULL, space);
				   if(tok != NULL && strstr(tok, ".bin")){
					   // Handle the ota update in the MQTT loop to avoid conflicts with publishing
					   ESP_LOGI(TAG,"OTA update needs to be performed");
					   ota_set_filename(tok);
					   service_ota = true;
					   //ota_trigger();					// Comment out to avoid doing OTA updates during development
				   }
				   else{
					   ESP_LOGI(TAG,"No binary file");
				   }
			   }
		   }
		   break;
	   case MQTT_EVENT_ERROR:
		   ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
		   esp_restart();
		   break;

	   default:
		   break;
	}
	return ESP_OK;
}


/*
* @brief Controls all MQTT connection, data transmission, state transmission
*
* @param
*
* @return If running properly this task will run indefinitely.
*/
void mqtt_task(void* pvParameters){
	ESP_LOGI(TAG, "Starting mqtt_task ...");

	mqtt_event_group = xEventGroupCreate();
	xEventGroupClearBits(mqtt_event_group, OTA_COMPLETE);

	// Constants that define data change thresholds for publishing a packet
	float pm_delta = 0.25;			// All PM data
	float minor_delta = 1.0;		// Temp, Humidity, NOX
	float co_delta = 30.0;			// CO
	float gps_delta = 0.05;			// GPS

	// Variables that hold sensor data (variables that begin with pub_ store previously published data)
	static pm_data_t pub_pm_dat, pm_dat;
	static double pub_temp, temp, pub_hum, hum;
	static uint16_t pub_co, co, pub_nox, nox;
	static esp_gps_t pub_gps, gps;
	static float version_number = 2.3;				// First digit = PCB hardware, second digit = pm sensor

	static char mqtt_pkt[MQTT_PKT_LEN] = {0};		// Clear packet char[]

	uint8_t tmp[6];									// Save MAC address for use in client_ID and mqtt_topic
	esp_efuse_mac_get_default(tmp);
	sprintf(DEVICE_MAC, "%02X%02X%02X%02X%02X%02X", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);

	// Generate client_ID . . . includes MAC Address as "IoT Device ID"
	const char mqtt_client_helper1[] = "projects/";
	// ***************************************************ADJUST FOR AIRQUALITY PROJECT*******************************************
	const char mqtt_client_helper2[] = "/locations/us-central1/registries/airu-sensor-registry/devices/M";
	//const char mqtt_client_helper2[] = "/locations/us-central1/registries/airquality/devices/M";
	// ***************************************************ADJUST FOR AIRQUALITY PROJECT*******************************************
	snprintf(client_ID, sizeof(client_ID), "%s%s%s%s", mqtt_client_helper1, PROJECT_ID, mqtt_client_helper2, DEVICE_MAC);
	ESP_LOGI(TAG, "Generated client_ID: %s", client_ID);

	// Generate mqtt_topic
	const char mqtt_topic_helper1[] = "/devices/M";
	// ***************************************************ADJUST FOR AIRQUALITY PROJECT*******************************************
	const char mqtt_topic_helper2[] = "/events/airU";
	//const char mqtt_topic_helper2[] = "/events/data";
	// ***************************************************ADJUST FOR AIRQUALITY PROJECT*******************************************
	const char mqtt_topic_helper3[] = "/state";
	snprintf(mqtt_topic, sizeof(mqtt_topic), "%s%s%s", mqtt_topic_helper1, DEVICE_MAC, mqtt_topic_helper2);
	ESP_LOGI(TAG, "Generated mqtt_topic: %s", mqtt_topic);

	// Generate mqtt_state_topic
	snprintf(mqtt_state_topic, sizeof(mqtt_state_topic), "%s%s%s", mqtt_topic_helper1, DEVICE_MAC, mqtt_topic_helper3);

	MQTT_Connect();

	time_t current_time = time(NULL);
	reconnect_time = (uint32_t)current_time + RECONNECT_SECONDS;	// Must be less than expiration set when creating the JWT in getMQTT_Config (jwt_if.c)
	last_publish_time = time(NULL);

	// Initial pull from sensors to populate the PUBLISH data variables
	PMS_Poll(&pub_pm_dat);
	HDC1080_Poll(&pub_temp, &pub_hum);
	MICS4514_Poll(&pub_co, &pub_nox);
	GPS_Poll(&pub_gps);
	int publish_flag = 1; 							// set to 1 by time (PUBLISH_SECONDS) OR change in data defined by delta variable above
	int loop_counter = 12;							// Initialize to 12 to trigger publication on 1st iteration
	vTaskDelay(30000 / portTICK_PERIOD_MS);			// wifi configuration time

	while(wifi_manager_connected_to_access_point() && client_connected) {
		if (service_ota) {												// perform ota update if required
			ota_trigger();
			xEventGroupWaitBits(mqtt_event_group, OTA_COMPLETE, pdTRUE, pdTRUE, portMAX_DELAY);	// block until OTA update is complete
			service_ota = false;

		}
		current_time = time(NULL);					// Get the current time for the packet

		printf("current_time: %d, ", (uint32_t)current_time);
		printf("reconnect_time: %d, ", reconnect_time);
		printf("last_publish_time: %d\n", (uint32_t)last_publish_time);

		if (current_time > reconnect_time){							// Check to see if it's time to reconnect
			esp_mqtt_client_destroy(client);						// Stop the mqtt client and free all the memory
			vTaskDelay(20000 / portTICK_PERIOD_MS);					// Allow time for disconnect to propagate through system (MQTT)
			MQTT_Connect();
		}
		else {														// Get and send data packet
			PMS_Poll(&pm_dat);
			HDC1080_Poll(&temp, &hum);
			MICS4514_Poll(&co, &nox);
			GPS_Poll(&gps);

			// Check to see if new data is different from last published data
			if(fabs(pm_dat.pm1-pub_pm_dat.pm1) >= pm_delta)
				publish_flag = 1;
			else if (fabs(pm_dat.pm2_5-pub_pm_dat.pm2_5) >= pm_delta)
				publish_flag = 2;
			else if (fabs(pm_dat.pm10-pub_pm_dat.pm10) >= pm_delta)
				publish_flag = 3;
			else if (fabs(temp-pub_temp) >= minor_delta)
				publish_flag = 4;
			else if (fabs(hum-pub_hum) >= minor_delta)
				publish_flag = 5;
			else if (fabs(nox-pub_nox) >= minor_delta)
				publish_flag = 6;
			else if (fabs(co-pub_co >= co_delta))
				publish_flag = 7;
			else if (fabs(gps.lat-pub_gps.lat) >= gps_delta)
				publish_flag = 8;
			else if (fabs(gps.lon-pub_gps.lon) >= gps_delta)
				publish_flag = 9;
			else if (loop_counter >= 12){
				publish_flag = 10;
			}

			printf("publish_flag: %d, ", publish_flag);
			printf("loop_counter: %d\n", loop_counter);

			if (publish_flag > 0) {
				publish_flag = 0;							// Reset publish_flag
				loop_counter = 0;							// Reset loop_counter
				memset(mqtt_pkt, 0, MQTT_PKT_LEN);			// Clear contents of packet
				sprintf(mqtt_pkt, "{\"DEVICE_ID\": \"M%s\", \"TIMESTAMP\": %ld, \"PM1\": %.2f, \"PM25\": %.2f, \"PM10\": %.2f, " \
				"\"TEMP\": %.2f, \"HUM\": %.2f, \"CO\": %d, \"NOX\": %d, \"LAT\": %.4f, \"LON\": %.4f, \"ALT\": %.1f, \"VER\": %.1f}", \
				DEVICE_MAC, current_time, pm_dat.pm1, pm_dat.pm2_5, pm_dat.pm10, temp, hum, co, nox, gps.lat, gps.lon, gps.alt, version_number);

				MQTT_Publish(mqtt_topic, mqtt_pkt);
				pub_pm_dat = pm_dat;
				pub_temp = temp;
				pub_hum = hum;
				pub_co = co;
				pub_gps = gps;
			}
			// Publish state consists of current firmware version saved in NVS
			// It is important that state gets published every 5 minutes as a backup to keep the connection with Google alive.
			get_firmware_version();
			snprintf(current_state, sizeof(current_state), "%s, Loop:%d, Time:%d, Reconnect:%d, Last Publish:%d, WiFi:%d, OTA:%d", \
					firmware_version, loop_counter, (uint32_t)current_time, reconnect_time, (uint32_t)last_publish_time, (int)wifi_manager_wait_internet_access(), ota_successful);
			MQTT_Publish(mqtt_state_topic, current_state);
			loop_counter++;
		}
		vTaskDelay(300000 / portTICK_PERIOD_MS);	// Time in milliseconds. 300000 = 5 minutes
	} // End while(1)
	esp_restart();									// Restart if exit the while loop
}

/*
* @brief Reads the current firmware version from non volatile storage (nvs) and save the value in "firmware_version"
*
* @param None
*
* @return None
*/
void get_firmware_version(void)
{
	printf("Opening NVS...");
	esp_err_t err;
	nvs_handle nvs_handle;
	err = nvs_open("storage", NVS_READONLY, &nvs_handle);
	if (err != ESP_OK) {
	    printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	}
	else {
		size_t firmware_length = (size_t)64;
		printf("Reading firmware_version: ");
		err = nvs_get_str(nvs_handle, "firmware", firmware_version, &firmware_length);	//14 bytes is the length
		switch (err) {
			case ESP_OK:
				printf("%s is current.\n", firmware_version);
				break;
			case ESP_ERR_NVS_NOT_FOUND:
				printf("Value not initialized yet!\n");
				strcpy (firmware_version, "Unknown");
				break;
			default :
				printf("Error (%s) reading!\n", esp_err_to_name(err));
		}
	}
	nvs_close(nvs_handle);
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
* @brief Published an MQTT packets. If msg_id != 0 (error in publishing packet) then client_connected = false (something is wrong)
*
* @param topic = MQTT topic and msg = MQTT msg to be published.
*
* @return N/A
*/
void MQTT_Publish(const char* topic, const char* msg)
{
	int msg_id = esp_mqtt_client_publish(client, topic, msg, strlen(msg), 0, 0);
	ESP_LOGI(TAG, "Sent packet: %s\nTopic: %s\tmsg_id=%d", msg, topic, msg_id);
	if (msg_id == 0){
		last_publish_time = time(NULL);
	}
	else{
		ESP_LOGI(TAG, "MQTT_Publish: msg_id != 0, setting client_connected = false");
		client_connected = false;
		esp_restart();
	}
}

/*
* @brief Reset the OTA_COMPLETE bit and set the ota_successful flag to indicate if last ota was successful or not
*
* @param N/A
*
* @return N/A
*/
void ota_complete(bool otaStatus) {
	ota_successful = otaStatus;
	xEventGroupSetBits(mqtt_event_group, OTA_COMPLETE);
}










