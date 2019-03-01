/*
 * mqtt_if.c
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#include <string.h>
//#include <time.h>			//Scott added
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
extern const uint8_t roots_pem_start[] asm("_binary_roots_pem_start");
extern const uint8_t rsaprivate_pem_start[] asm("_binary_rsaprivate_pem_start");


static bool client_connected;
static esp_mqtt_client_handle_t client;
static EventGroupHandle_t mqtt_event_group;

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event);

////Constants Scott Added-------------------------------------------
//static const char* address = "ssl://mqtt.googleapis.com:8883";
//static const char* clientid = "projects/scottgale/locations/us-central1/registries/airu-sensor-registry/devices/airu_sensor}";
//static const char* deviceid = "airu_sensor";			//must be unique for each device
//static const char* projectid = "scottgale";
//static const char* registryid = "airu-sensor-registry";
//static const char* topic = "projects/scottgale/topics/telemetry-topic"; //devices/{your-device-id}/events (c example)
//static const char* region = "us-central1";


//
static const int EXP_TIME = 600; //300 = 5 minutes, 600 = 10 minutes, 82800 = 23 hours
//
//static const int kQos = 1;
//static const unsigned long kTimeout = 10000L;
//static const char* kUsername = "unused";
//
//static const unsigned long kInitialConnectIntervalMillis = 500L;
//static const unsigned long kMaxConnectIntervalMillis = 6000L;
//static const unsigned long kMaxConnectRetryTimeElapsedMillis = 900000L;
//static const float kIntervalMultiplier = 1.5f;
//
////End Constants Scott Added---------------------------------------------
//
//
///*
//* @brief This function get the time for the Google IoT connection/authentication
//* and is used ICW the JWT library
//*
//* @param iat and exp are char*'s to be populated with time.
//*  iat: issued at time
//*  exp: expiration time - time which connection with Google will terminate
//*  time_size: denotes the length (number of characters in the time) - **not sure if
//*  this is needed . . .  may change to sprintf and remove the int time_size**
//*
//* @return
//
//static void getIatExp (char* iat, char* exp, int time_size){
//	time_t now_seconds = time(NULL);
//	snprintf(iat, time_size, "%lu", now_seconds);
//	snprintf(exp, time_size, "%lu", now_seconds + EXP_TIME);	//Set expiration time
//}
//
///*
//* @brief Calculates a JSON Web Token (JWT) given the path to a private key and
//* Google Cloud project ID. Returns the JWT as a string that the caller must
//* free.
//*
//* @param
//*
//* @return JWT - used to create a connection with G-IoT
//*/
//static char* CreateJwt() {
//  char iat_time[sizeof(time_t) * 3 + 2];
//  char exp_time[sizeof(time_t) * 3 + 2];
//  uint8_t* key = NULL; 						// Stores the Base64 encoded certificate
//  size_t key_len = 0;
//  jwt_t *jwt = NULL;							// Need libJWT
//  int ret = 0;
//  char *out = NULL;
//
//  // Read private key from file
//  FILE *fp = fopen(keypath, "r");
//  if (fp == (void*) NULL) {
//    printf("Could not open file: %s\n", keypath);
//    return "";
//  }
//  fseek(fp, 0L, SEEK_END);
//  key_len = ftell(fp);
//  fseek(fp, 0L, SEEK_SET);
//  key = malloc(sizeof(uint8_t) * (key_len + 1)); // certificate length + \0
//
//  fread(key, 1, key_len, fp);
//  key[key_len] = '\0';
//  fclose(fp);
//
//  // Get JWT parts
//  getIatExp(iat_time, exp_time, sizeof(iat_time));
//
//  jwt_new(&jwt);
//
//  // Write JWT
//  ret = jwt_add_grant(jwt, "iat", iat_time);
//  if (ret) {
//    printf("Error setting issue timestamp: %d\n", ret);
//  }
//  ret = jwt_add_grant(jwt, "exp", exp_time);
//  if (ret) {
//    printf("Error setting expiration: %d\n", ret);
//  }
//  ret = jwt_add_grant(jwt, "aud", projectid);
//  if (ret) {
//    printf("Error adding audience: %d\n", ret);
//  }
//  ret = jwt_set_alg(jwt, jwt_str_alg("RS256"), key, key_len);
//  if (ret) {
//    printf("Error during set alg: %d\n", ret);
//  }
//  out = jwt_encode_str(jwt);
//  if(!out) {
//      perror("Error during token creation:");
//  }
//
//  jwt_free(jwt);
//  free(key);
//  return out;
//}
//
///**
// * Publish a given message, passed in as payload, to Cloud IoT Core using the
// * values passed to the sample, stored in the global opts structure. Returns
// * the result code from the MQTT client.
// */
//// [START iot_mqtt_publish]
//int Publish(char* payload, int payload_size) {
//  int rc = -1;
//  MQTTClient client = {0};
//  MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
//  MQTTClient_message pubmsg = MQTTClient_message_initializer;
//  MQTTClient_deliveryToken token = {0};
//
//  MQTTClient_create(&client, address, clientid,
//      MQTTCLIENT_PERSISTENCE_NONE, NULL);
//  conn_opts.keepAliveInterval = 60;
//  conn_opts.cleansession = 1;
//  conn_opts.username = kUsername;
//  conn_opts.password = CreateJwt();
//  MQTTClient_SSLOptions sslopts = MQTTClient_SSLOptions_initializer;
//
//  sslopts.trustStore = rootpath;
//  sslopts.privateKey = keypath;
//  conn_opts.ssl = &sslopts;
//
//  unsigned long retry_interval_ms = kInitialConnectIntervalMillis;
//  unsigned long total_retry_time_ms = 0;
//  while ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
//    if (rc == 3) {  // connection refused: server unavailable
//      usleep(retry_interval_ms / 1000);
//      total_retry_time_ms += retry_interval_ms;
//      if (total_retry_time_ms >= kMaxConnectRetryTimeElapsedMillis) {
//        printf("Failed to connect, maximum retry time exceeded.");
//        exit(EXIT_FAILURE);
//      }
//      retry_interval_ms *= kIntervalMultiplier;
//      if (retry_interval_ms > kMaxConnectIntervalMillis) {
//        retry_interval_ms = kMaxConnectIntervalMillis;
//      }
//    } else {
//      printf("Failed to connect, return code %d\n", rc);
//      exit(EXIT_FAILURE);
//    }
//  }
//
//  pubmsg.payload = payload;
//  pubmsg.payloadlen = payload_size;
//  pubmsg.qos = kQos;
//  pubmsg.retained = 0;
//  MQTTClient_publishMessage(client, topic, &pubmsg, &token);
//  printf("Waiting for up to %lu seconds for publication of %s\n"
//          "on topic %s for client with ClientID: %s\n",
//          (kTimeout/1000), payload, topic, clientid);
//  rc = MQTTClient_waitForCompletion(client, token, kTimeout);
//  printf("Message with delivery token %d delivered\n", token);
//  MQTTClient_disconnect(client, 10000);
//  MQTTClient_destroy(&client);
//
//  return rc;
//}

//---------------------------Original Code----------------------------------------------------------
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

   ESP_LOGI(TAG, "Initializing client ...");

   ESP_LOGI(TAG, "rsaprivate_pem file read test: %c", (char)rsaprivate_pem_start[0]);
   printf("roots_pem file read test: %c\n", (char)roots_pem_start[0]);

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

