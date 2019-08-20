/*
Copyright (c) 2017 Tony Pottier

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

@file wifi_manager.c
@author Tony Pottier
@brief Defines all functions necessary for esp32 to connect to a wifi/scan wifis

Contains the freeRTOS task and all necessary support

@see https://idyl.io
@see https://github.com/tonyp7/esp32-wifi-manager
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"

#include "json.h"
#include "wifi_manager.h"
#include "http_server_if.h"
#include "led_if.h"
#include "time_if.h"
#include "mqtt_if.h"

#define THIRTY_SECONDS_TIMEOUT (30000 / portTICK_PERIOD_MS)
#define ONE_SECOND_DELAY (1000 / portTICK_PERIOD_MS)

static const char* TAG = "WIFI_MANAGER";
static bool initializeTNTPAndMQTT = false;
//static const char* HDL = "WIFI-HANDLER";

SemaphoreHandle_t wifi_manager_json_mutex = NULL;
uint16_t ap_num = MAX_AP_NUM;
wifi_ap_record_t *accessp_records; //[MAX_AP_NUM];
char *accessp_json = NULL;
char *ip_info_json = NULL;
char *reg_info_json = NULL;
wifi_config_t* wifi_manager_config_sta = NULL;

/**
 * The actual WiFi settings in use
 */
struct wifi_settings_t wifi_settings = {
	.ap_ssid = DEFAULT_AP_SSID,
	.ap_pwd = DEFAULT_AP_PASSWORD,
	.ap_channel = DEFAULT_AP_CHANNEL,
	.ap_ssid_hidden = DEFAULT_AP_SSID_HIDDEN,
	.ap_bandwidth = DEFAULT_AP_BANDWIDTH,
	.sta_only = DEFAULT_STA_ONLY,
	.sta_power_save = DEFAULT_STA_POWER_SAVE,
	.sta_static_ip = 0,
};

/**
 * The user's registration info
 */
struct registration_info_t reg_info = {
		.name = "",
		.email = "",
		.hidden = false,
		.mac = ""
};

const char wifi_manager_nvs_namespace[] = "espwifimgr";

EventGroupHandle_t wifi_manager_event_group;

/* @brief indicate that the ESP32 is currently connected. */
const int WIFI_MANAGER_WIFI_CONNECTED_BIT = BIT0;


const int WIFI_MANAGER_AP_STA_CONNECTED_BIT = BIT1;

/* @brief Set automatically once the SoftAP is started */
const int WIFI_MANAGER_AP_STARTED = BIT2;

/* @brief When set, means a client requested to connect to an access point.*/
const int WIFI_MANAGER_REQUEST_STA_CONNECT_BIT = BIT3;

/* @brief This bit is set automatically as soon as a connection was lost */
const int WIFI_MANAGER_STA_DISCONNECT_BIT = BIT4;

/* @brief When set, means a client requested to scan wireless networks. */
const int WIFI_MANAGER_REQUEST_WIFI_SCAN = BIT5;

/* @brief When set, means a client requested to disconnect from currently connected AP. */
const int WIFI_MANAGER_REQUEST_WIFI_DISCONNECT = BIT6;

/* @brief When set, means a client get kicked off by router, send reconnect signal.
 * Set when receiving SYSTEM_EVENT_STA_DISCONNECTED
 * Clear when receiving IP
 * */
const int WIFI_MANAGER_REQUEST_RECONNECT = BIT7;

EventBits_t wifi_manager_wait_disconnect() {
	return xEventGroupWaitBits(wifi_manager_event_group, WIFI_MANAGER_STA_DISCONNECT_BIT, pdFALSE, pdTRUE, portMAX_DELAY );
}
void wifi_manager_scan_async(){
	xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_WIFI_SCAN);
}

void wifi_manager_disconnect_async(){
	xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_WIFI_DISCONNECT);
}

void wifi_manager_json_status_update(update_reason_code_t statusCode) {
	/* update JSON status */
	if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
		wifi_manager_generate_ip_info_json(statusCode);
		wifi_manager_unlock_json_buffer();
	}
	else{
		/* Even if someone were to furiously refresh a web resource that needs the json mutex,
		 * it seems impossible that this thread cannot obtain the mutex. Abort here is reasonnable.
		 */
		abort();
	}

}
esp_err_t wifi_manager_save_reg_config(){
	nvs_handle handle;
	esp_err_t esp_err;

	esp_err = nvs_open(wifi_manager_nvs_namespace, NVS_READWRITE, &handle);
	if(esp_err != ESP_OK) return esp_err;

	esp_err = nvs_set_blob(handle, "name", reg_info.name, JSON_REG_NAME_SIZE);
	if(esp_err != ESP_OK) return esp_err;

	esp_err = nvs_set_blob(handle, "email", reg_info.email, JSON_REG_EMAIL_SIZE);
	if(esp_err != ESP_OK) return esp_err;

	esp_err = nvs_commit(handle);
	if(esp_err != ESP_OK) return esp_err;

	nvs_close(handle);

	return ESP_OK;
}


esp_err_t wifi_manager_save_wifi_settings(){
	nvs_handle handle;
	esp_err_t esp_err;

	ESP_LOGI(TAG, "About to save wifi settings to flash\n");

//	/* Let's set the Soft AP SSID here */
//	uint8_t mac[6];
//	esp_efuse_mac_get_default(mac);
//	sprintf((char*)wifi_settings.ap_ssid, "%s-%02X%02X", DEFAULT_AP_SSID, mac[4], mac[5]);

	esp_err = nvs_open(wifi_manager_nvs_namespace, NVS_READWRITE, &handle);
	if (esp_err != ESP_OK) return esp_err;

	esp_err = nvs_set_blob(handle, "settings", &wifi_settings, sizeof(wifi_settings));
	if (esp_err != ESP_OK) return esp_err;

	esp_err = nvs_commit(handle);
	if (esp_err != ESP_OK) return esp_err;

	nvs_close(handle);

	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: SoftAP_ssid: %s",wifi_settings.ap_ssid);
	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: SoftAP_pwd: %s",wifi_settings.ap_pwd);
	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: SoftAP_channel: %i",wifi_settings.ap_channel);
	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: SoftAP_hidden (1 = yes): %i",wifi_settings.ap_ssid_hidden);
	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: SoftAP_bandwidth (1 = 20MHz, 2 = 40MHz): %i",wifi_settings.ap_bandwidth);
	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_only (0 = APSTA, 1 = STA when connected): %i",wifi_settings.sta_only);
	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_power_save (1 = yes): %i",wifi_settings.sta_power_save);
	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_static_ip (0 = dhcp client, 1 = static ip): %i",wifi_settings.sta_static_ip);
	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_ip_addr: %s", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.ip));
	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_gw_addr: %s", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.gw));
	ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_netmask: %s", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.netmask));

	return ESP_OK;
}

esp_err_t wifi_manager_save_sta_config(){

	nvs_handle handle;
	esp_err_t esp_err;

	ESP_LOGI(TAG, "About to save config to flash\n");

	if(wifi_manager_config_sta){

		esp_err = nvs_open(wifi_manager_nvs_namespace, NVS_READWRITE, &handle);
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_set_blob(handle, "ssid", wifi_manager_config_sta->sta.ssid, 32);
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_set_blob(handle, "password", wifi_manager_config_sta->sta.password, 64);
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_set_blob(handle, "settings", &wifi_settings, sizeof(wifi_settings));
		if (esp_err != ESP_OK) return esp_err;

		esp_err = nvs_commit(handle);
		if (esp_err != ESP_OK) return esp_err;

		nvs_close(handle);

		ESP_LOGI(TAG, "wifi_manager_wrote wifi_sta_config: ssid:%s password:%s",wifi_manager_config_sta->sta.ssid,wifi_manager_config_sta->sta.password);
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: SoftAP_ssid: %s",wifi_settings.ap_ssid);
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: SoftAP_pwd: %s",wifi_settings.ap_pwd);
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: SoftAP_channel: %i",wifi_settings.ap_channel);
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: SoftAP_hidden (1 = yes): %i",wifi_settings.ap_ssid_hidden);
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: SoftAP_bandwidth (1 = 20MHz, 2 = 40MHz): %i",wifi_settings.ap_bandwidth);
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_only (0 = APSTA, 1 = STA when connected): %i",wifi_settings.sta_only);
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_power_save (1 = yes): %i",wifi_settings.sta_power_save);
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_static_ip (0 = dhcp client, 1 = static ip): %i",wifi_settings.sta_static_ip);
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_ip_addr: %s", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.ip));
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_gw_addr: %s", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.gw));
		ESP_LOGI(TAG, "wifi_manager_wrote wifi_settings: sta_netmask: %s\n\r", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.netmask));
	}

	return ESP_OK;
}

bool wifi_manager_fetch_reg_config(){
	nvs_handle handle;
	esp_err_t esp_err;

	reg_info.name[0] = '\0';
	reg_info.email[0] = '\0';

	esp_err = nvs_open(wifi_manager_nvs_namespace, NVS_READONLY, &handle);
	if(esp_err == ESP_OK){
		size_t sz = JSON_REG_EMAIL_SIZE;
		uint8_t *buff = (uint8_t*)malloc(sizeof(uint8_t) * sz);

		esp_err = nvs_get_blob(handle, "name", buff, &sz);
		if(esp_err != ESP_OK) {
			free(buff);
			return false;
		}
		buff[sz] = '\0';
		memcpy(reg_info.name, buff, sz);

		sz = JSON_REG_EMAIL_SIZE;

		esp_err = nvs_get_blob(handle, "email", buff, &sz);
		if(esp_err != ESP_OK) {
			free(buff);
			return false;
		}
		buff[sz] = '\0';
		memcpy(reg_info.email, buff, sz);

		free(buff);
		nvs_close(handle);

		return true;
	}
	else {
		return false;
	}
}

bool wifi_manager_fetch_wifi_sta_config(){

	nvs_handle handle;
	esp_err_t esp_err;
	esp_err = nvs_open(wifi_manager_nvs_namespace, NVS_READONLY, &handle);
	if(esp_err == ESP_OK){
		ESP_LOGI(TAG, "Namespace exists");
		if(wifi_manager_config_sta == NULL){
			wifi_manager_config_sta = (wifi_config_t*)malloc(sizeof(wifi_config_t));
		}
		memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));
		memset(&wifi_settings, 0x00, sizeof(struct wifi_settings_t));

		/* allocate buffer */
		size_t sz = sizeof(wifi_settings);
		uint8_t *buff = (uint8_t*)malloc(sizeof(uint8_t) * sz);
		memset(buff, 0x00, sizeof(sz));

		/* ssid */
		sz = sizeof(wifi_manager_config_sta->sta.ssid);
		esp_err = nvs_get_blob(handle, "ssid", buff, &sz);
		if(esp_err != ESP_OK){
			free(buff);
			return false;
		}
		memcpy(wifi_manager_config_sta->sta.ssid, buff, sz);

		/* password */
		sz = sizeof(wifi_manager_config_sta->sta.password);
		esp_err = nvs_get_blob(handle, "password", buff, &sz);
		if(esp_err != ESP_OK){
			free(buff);
			return false;
		}
		memcpy(wifi_manager_config_sta->sta.password, buff, sz);

		/* settings */
		sz = sizeof(wifi_settings);
		esp_err = nvs_get_blob(handle, "settings", buff, &sz);
		if(esp_err != ESP_OK){
			free(buff);
			return false;
		}
		memcpy(&wifi_settings, buff, sz);

		free(buff);
		nvs_close(handle);

		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_sta_config: ssid:%s password:%s",wifi_manager_config_sta->sta.ssid,wifi_manager_config_sta->sta.password);
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: SoftAP_ssid:%s",wifi_settings.ap_ssid);
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: SoftAP_pwd:%s",wifi_settings.ap_pwd);
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: SoftAP_channel:%i",wifi_settings.ap_channel);
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: SoftAP_hidden (1 = yes):%i",wifi_settings.ap_ssid_hidden);
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: SoftAP_bandwidth (1 = 20MHz, 2 = 40MHz)%i",wifi_settings.ap_bandwidth);
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: sta_only (0 = APSTA, 1 = STA when connected):%i",wifi_settings.sta_only);
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: sta_power_save (1 = yes):%i",wifi_settings.sta_power_save);
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: sta_static_ip (0 = dhcp client, 1 = static ip):%i",wifi_settings.sta_static_ip);
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: sta_static_ip_config: IP: %s , GW: %s , Mask: %s", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.ip), ip4addr_ntoa(&wifi_settings.sta_static_ip_config.gw), ip4addr_ntoa(&wifi_settings.sta_static_ip_config.netmask));
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: sta_ip_addr: %s", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.ip));
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: sta_gw_addr: %s", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.gw));
		ESP_LOGI(TAG, "wifi_manager_fetch_wifi_settings: sta_netmask: %s", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.netmask));

		return wifi_manager_config_sta->sta.ssid[0] != '\0';


	}
	else{
		return false;
	}

}

void wifi_manager_clear_reg_info_json(){
	strcpy(reg_info_json, "{}\n");
}
void wifi_manager_generate_reg_info_json(){

	memset(reg_info_json, 0x00, JSON_REG_INFO_SIZE);

    /* ssid needs to be json escaped. To save on heap memory it's directly printed at the correct address */

	// Name (First and Last)
    strcat(reg_info_json, "{\"name\":");
    json_print_string( (unsigned char*)reg_info.name,  (unsigned char*)(reg_info_json+strlen(reg_info_json)) );

    // Email
    strcat(reg_info_json, ",\"email\":");
    json_print_string( (unsigned char*)reg_info.email,  (unsigned char*)(reg_info_json+strlen(reg_info_json)) );

    // Visibility
    size_t len = strlen(reg_info_json);
    len += sprintf(reg_info_json + len, ",\"mapVisibility\":%d", !reg_info.hidden);

    // MAC Address
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
	len += sprintf(reg_info_json + len, ",\"macAddress\":\"%02X:%02X:%02X:%02X:%02X:%02X\"}", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void wifi_manager_clear_ip_info_json(){
	strcpy(ip_info_json, "{}\n");
}
void wifi_manager_generate_ip_info_json(update_reason_code_t update_reason_code){

	wifi_config_t *config = wifi_manager_get_wifi_sta_config();
	if(config){

		const char ip_info_json_format[] = ",\"ip\":\"%s\",\"netmask\":\"%s\",\"gw\":\"%s\",\"urc\":%d}\n";

		memset(ip_info_json, 0x00, JSON_IP_INFO_SIZE);

		/* to avoid declaring a new buffer we copy the data directly into the buffer at its correct address */
		strcpy(ip_info_json, "{\"ssid\":");
		json_print_string(config->sta.ssid,  (unsigned char*)(ip_info_json+strlen(ip_info_json)) );

		if(update_reason_code == UPDATE_CONNECTION_OK){
			/* rest of the information is copied after the ssid */
			tcpip_adapter_ip_info_t ip_info;
			ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
			char ip[IP4ADDR_STRLEN_MAX]; /* note: IP4ADDR_STRLEN_MAX is defined in lwip */
			char gw[IP4ADDR_STRLEN_MAX];
			char netmask[IP4ADDR_STRLEN_MAX];
			strcpy(ip, ip4addr_ntoa(&ip_info.ip));
			strcpy(netmask, ip4addr_ntoa(&ip_info.netmask));
			strcpy(gw, ip4addr_ntoa(&ip_info.gw));

			snprintf( (ip_info_json + strlen(ip_info_json)), JSON_IP_INFO_SIZE, ip_info_json_format,
					ip,
					netmask,
					gw,
					(int)update_reason_code);
		}
		else{
			/* notify in the json output the reason code why this was updated without a connection */
			snprintf( (ip_info_json + strlen(ip_info_json)), JSON_IP_INFO_SIZE, ip_info_json_format,
								"0",
								"0",
								"0",
								(int)update_reason_code);
		}
	}
	else{
		wifi_manager_clear_ip_info_json();
	}


}


void wifi_manager_clear_access_points_json(){
	strcpy(accessp_json, "[]\n");
}
void wifi_manager_generate_acess_points_json(){

	strcpy(accessp_json, "[");


	const char oneap_str[] = ",\"chan\":%d,\"rssi\":%d,\"auth\":%d}%c\n";

	/* stack buffer to hold on to one AP until it's copied over to accessp_json */
	char one_ap[JSON_ONE_APP_SIZE];
	for(int i=0; i<ap_num;i++){

		wifi_ap_record_t ap = accessp_records[i];

		/* ssid needs to be json escaped. To save on heap memory it's directly printed at the correct address */
		strcat(accessp_json, "{\"ssid\":");
		json_print_string( (unsigned char*)ap.ssid,  (unsigned char*)(accessp_json+strlen(accessp_json)) );

		/* print the rest of the json for this access point: no more string to escape */
		snprintf(one_ap, (size_t)JSON_ONE_APP_SIZE, oneap_str,
				ap.primary,
				ap.rssi,
				ap.authmode,
				i==ap_num-1?']':',');

		/* add it to the list */
		strcat(accessp_json, one_ap);
	}

}


bool wifi_manager_lock_json_buffer(TickType_t xTicksToWait){
	if(wifi_manager_json_mutex){
		if( xSemaphoreTake( wifi_manager_json_mutex, xTicksToWait ) == pdTRUE ) {
			return true;
		}
		else{
			return false;
		}
	}
	else{
		return false;
	}

}
void wifi_manager_unlock_json_buffer(){
	xSemaphoreGive( wifi_manager_json_mutex );
}

char* wifi_manager_get_ap_list_json(){
	return accessp_json;
}


esp_err_t wifi_manager_event_handler(void *ctx, system_event_t *event)
{
	ESP_LOGI(TAG, "Event Handler Event ID: %d", event->event_id);
    switch(event->event_id) {

    case SYSTEM_EVENT_AP_START:
    	xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_AP_STARTED);
		break;

    case SYSTEM_EVENT_AP_STACONNECTED:
    	xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_AP_STA_CONNECTED_BIT);
		break;

    case SYSTEM_EVENT_AP_STADISCONNECTED:
    	xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_AP_STA_CONNECTED_BIT);
		break;

    case SYSTEM_EVENT_STA_START:
        break;

	case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_WIFI_CONNECTED_BIT);
		xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RECONNECT);
		LED_SetEventBit(LED_EVENT_WIFI_CONNECTED_BIT);

        break;

	case SYSTEM_EVENT_STA_DISCONNECTED:
    	ESP_LOGW(TAG, "disconnect reason [%d]", event->event_info.disconnected.reason);
    	if ((event->event_info.disconnected.reason != WIFI_REASON_ASSOC_LEAVE)  /*Get kicked off by router*/
    			& (event->event_info.disconnected.reason != WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) /*Authenticate failed*/
				& (event->event_info.disconnected.reason != WIFI_REASON_AUTH_FAIL) /*Authenticate failed*/
//				& (event->event_info.disconnected.reason != WIFI_REASON_NO_AP_FOUND) /* No AP found */
				)
    	{
    		xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RECONNECT);
    	}
    	xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_STA_DISCONNECT_BIT);
		xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_WIFI_CONNECTED_BIT);
		LED_SetEventBit(LED_EVENT_WIFI_DISCONNECTED_BIT);
        break;

	default:
        break;
    }
	return ESP_OK;
}

wifi_config_t* wifi_manager_get_wifi_sta_config(){
	return wifi_manager_config_sta;
}

void wifi_manager_connect_async(){
	/* in order to avoid a false positive on the front end app we need to quickly flush the ip json
	 * There'se a risk the front end sees an IP or a password error when in fact
	 * it's a remnant from a previous connection
	 */
	ESP_LOGI(TAG, "connect_async: waiting for json buffer lock");
	if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
		wifi_manager_clear_ip_info_json();
		wifi_manager_unlock_json_buffer();
	}
	ESP_LOGI(TAG, "connect_async: cleared json info");
	xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);
}

char* wifi_manager_get_ip_info_json(){
	return ip_info_json;
}

char* wifi_manager_get_reg_info_json(){
	return reg_info_json;
}

void wifi_manager_destroy(){

	/* heap buffers */
	free(accessp_records);
	accessp_records = NULL;
	free(accessp_json);
	accessp_json = NULL;
	free(ip_info_json);
	ip_info_json = NULL;
	free(reg_info_json);
	reg_info_json = NULL;
	if(wifi_manager_config_sta){
		free(wifi_manager_config_sta);
		wifi_manager_config_sta = NULL;
	}

	/* RTOS objects */
	vSemaphoreDelete(wifi_manager_json_mutex);
	wifi_manager_json_mutex = NULL;
	vEventGroupDelete(wifi_manager_event_group);

	vTaskDelete(NULL);
}

void wifi_manager_filter_unique( wifi_ap_record_t * aplist, uint16_t * aps) {
	int total_unique;
	wifi_ap_record_t * first_free;
	total_unique=*aps;

	first_free=NULL;

	for(int i=0; i<*aps-1;i++) {
		wifi_ap_record_t * ap = &aplist[i];

		/* skip the previously removed APs */
		if (ap->ssid[0] == 0) continue;

		/* remove the identical SSID+authmodes */
		for(int j=i+1; j<*aps;j++) {
			wifi_ap_record_t * ap1 = &aplist[j];
			if ( (strcmp((const char *)ap->ssid, (const char *)ap1->ssid)==0) &&
			     (ap->authmode == ap1->authmode) ) { /* same SSID, different auth mode is skipped */
				/* save the rssi for the display */
				if ((ap1->rssi) > (ap->rssi)) ap->rssi=ap1->rssi;
				/* clearing the record */
				memset(ap1,0, sizeof(wifi_ap_record_t));
			}
		}
	}
	/* reorder the list so APs follow each other in the list */
	for(int i=0; i<*aps;i++) {
		wifi_ap_record_t * ap = &aplist[i];
		/* skipping all that has no name */
		if (ap->ssid[0] == 0) {
			/* mark the first free slot */
			if (first_free==NULL) first_free=ap;
			total_unique--;
			continue;
		}
		if (first_free!=NULL) {
			memcpy(first_free, ap, sizeof(wifi_ap_record_t));
			memset(ap,0, sizeof(wifi_ap_record_t));
			/* find the next free slot */
			for(int j=0; j<*aps;j++) {
				if (aplist[j].ssid[0]==0) {
					first_free=&aplist[j];
					break;
				}
			}
		}
	}
	/* update the length of the list */
	*aps = total_unique;
}

void wifi_manager( void * pvParameters ){

	esp_err_t err;
	ESP_LOGI(TAG, "wifi_manager task Started");
	/* memory allocation of objects used by the task */
	wifi_manager_json_mutex = xSemaphoreCreateMutex();
	accessp_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * MAX_AP_NUM);

	accessp_json = (char*)malloc(MAX_AP_NUM * JSON_ONE_APP_SIZE + 4); /* 4 bytes for json encapsulation of "[\n" and "]\0" */

	wifi_manager_clear_access_points_json();
		ip_info_json = (char*)malloc(sizeof(char) * JSON_IP_INFO_SIZE);
		reg_info_json = (char*)malloc(sizeof(char) * JSON_REG_INFO_SIZE);
	wifi_manager_clear_ip_info_json();
	wifi_manager_clear_reg_info_json();
		wifi_manager_config_sta = (wifi_config_t*)malloc(sizeof(wifi_config_t));

	memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));
	memset(&wifi_settings.sta_static_ip_config, 0x00, sizeof(tcpip_adapter_ip_info_t));
	IP4_ADDR(&wifi_settings.sta_static_ip_config.ip, 192, 168, 0, 10);
	IP4_ADDR(&wifi_settings.sta_static_ip_config.gw, 192, 168, 0, 1);
	IP4_ADDR(&wifi_settings.sta_static_ip_config.netmask, 255, 255, 255, 0);

	/* Save the current WiFi Settings */
//	wifi_manager_save_wifi_settings();
	/* Let's set the Soft AP SSID here */
	uint8_t mac[6];
	esp_efuse_mac_get_default(mac);
	sprintf((char*)wifi_settings.ap_ssid, "%s-%02X%02X", DEFAULT_AP_SSID, mac[4], mac[5]);

	/* initialize the tcp stack */
	tcpip_adapter_init();

    /* event handler and event group for the wifi driver */
	wifi_manager_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_manager_event_handler, NULL));

    /* wifi scanner config */
	wifi_scan_config_t scan_config = {
		.ssid = 0,
		.bssid = 0,
		.channel = 0,
		.show_hidden = true
	};

	/* try to get access to previously saved wifi */
	ESP_LOGI(TAG, "About to fetch wifi sta config");
	if(wifi_manager_fetch_wifi_sta_config()){

		ESP_LOGI(TAG, "saved wifi found on startup\n");

		/* request a connection */
		xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);
	}

	/* start the softAP access point */
	/* stop DHCP server */
	ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));

	/* assign a static IP to the AP network interface */
	tcpip_adapter_ip_info_t info;
	memset(&info, 0x00, sizeof(info));
	IP4_ADDR(&info.ip, 192, 168, 4, 1);
	IP4_ADDR(&info.gw, 192, 168, 4, 1);
	IP4_ADDR(&info.netmask, 255, 255, 255, 0);
	ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info));

	/* start dhcp server */
	ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));

	tcpip_adapter_dhcp_status_t status;
	if(wifi_settings.sta_static_ip) {

		ESP_LOGI(TAG, "assigning static ip to STA interface. IP: %s , GW: %s , Mask: %s\n", ip4addr_ntoa(&wifi_settings.sta_static_ip_config.ip), ip4addr_ntoa(&wifi_settings.sta_static_ip_config.gw), ip4addr_ntoa(&wifi_settings.sta_static_ip_config.netmask));

		/* stop DHCP client*/
		ESP_ERROR_CHECK(tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA));

		/* assign a static IP to the STA network interface */
		ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &wifi_settings.sta_static_ip_config));
	}
	else {
		/* start DHCP client if not started*/

		ESP_LOGI(TAG, "Start DHCP client for STA interface. If not already running\n");

		ESP_ERROR_CHECK(tcpip_adapter_dhcpc_get_status(TCPIP_ADAPTER_IF_STA, &status));
		if (status!=TCPIP_ADAPTER_DHCP_STARTED)
			ESP_ERROR_CHECK(tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA));
	}


	/* init wifi as station + access point */
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
	ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, wifi_settings.ap_bandwidth));
	ESP_ERROR_CHECK(esp_wifi_set_ps(wifi_settings.sta_power_save));

	// configure the softAP and start it */
	wifi_config_t ap_config = {
		.ap = {
			.ssid_len = 0,
			.channel = wifi_settings.ap_channel,
			.authmode = WIFI_AUTH_WPA2_PSK,
			.ssid_hidden = wifi_settings.ap_ssid_hidden,
			.max_connection = AP_MAX_CONNECTIONS,
			.beacon_interval = AP_BEACON_INTERVAL,
		},
	};
	memcpy(ap_config.ap.ssid, wifi_settings.ap_ssid , sizeof(wifi_settings.ap_ssid));
	memcpy(ap_config.ap.password, wifi_settings.ap_pwd, sizeof(wifi_settings.ap_pwd));

	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
	ESP_ERROR_CHECK(esp_wifi_start());


	ESP_LOGI(TAG, "starting softAP with ssid %s\n", ap_config.ap.ssid);
	if(wifi_settings.ap_bandwidth == 1)
		ESP_LOGI(TAG, "starting softAP with 20 MHz bandwidth\n");
	else
		ESP_LOGI(TAG, "starting softAP with 40 MHz bandwidth\n");
	ESP_LOGI(TAG, "starting softAP on channel %i\n", wifi_settings.ap_channel);
	if(wifi_settings.sta_power_save ==1) ESP_LOGI(TAG, "wifi_manager: STA power save enabled\n");

	/* wait for access point to start */
	xEventGroupWaitBits(wifi_manager_event_group, WIFI_MANAGER_AP_STARTED, pdFALSE, pdTRUE, portMAX_DELAY );

	ESP_LOGI(TAG, "softAP started, starting http_server\n");

	http_server_set_event_start();
	ESP_LOGW(TAG, "free heap: %d\n",esp_get_free_heap_size());

	EventBits_t uxBits;
	for(;;){

		/* actions that can trigger: request a connection, a scan, or a disconnection */
		uxBits = xEventGroupWaitBits(wifi_manager_event_group,
				WIFI_MANAGER_REQUEST_STA_CONNECT_BIT |
				WIFI_MANAGER_REQUEST_WIFI_SCAN |
				WIFI_MANAGER_REQUEST_WIFI_DISCONNECT |
				WIFI_MANAGER_REQUEST_RECONNECT,
				pdFALSE, pdFALSE, portMAX_DELAY );

		if(uxBits & WIFI_MANAGER_REQUEST_WIFI_DISCONNECT){
			/* user requested a disconnect, this will in effect disconnect the wifi but also erase NVS memory*/
			ESP_LOGI(TAG, "WIFI_MANAGER_REQUEST_WIFI_DISCONNECT\n");

			/*disconnect only if it was connected to begin with! */
			if( uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT ){
				xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_STA_DISCONNECT_BIT);
				ESP_ERROR_CHECK(esp_wifi_disconnect());

				/* wait until wifi disconnects. From experiments, it seems to take about 150ms to disconnect */
				xEventGroupWaitBits(wifi_manager_event_group, WIFI_MANAGER_STA_DISCONNECT_BIT, pdFALSE, pdTRUE, portMAX_DELAY );
			}
			else{
				ESP_LOGI(TAG, "WiFi was not connected to begin with!");
			}

			LED_SetEventBit(LED_EVENT_WIFI_DISCONNECTED_BIT);

			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_STA_DISCONNECT_BIT);

			/* erase configuration */
			if(wifi_manager_config_sta){
				memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));
			}

			/* save NVS memory */
			wifi_manager_save_sta_config();

			/* update JSON status */
			wifi_manager_json_status_update(UPDATE_USER_DISCONNECT);

			/* finally: release the scan request bit */
			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_WIFI_DISCONNECT);
		}
		if(uxBits & WIFI_MANAGER_REQUEST_STA_CONNECT_BIT){
			//someone requested a connection!
			ESP_LOGI(TAG, "WIFI_MANAGER_REQUEST_STA_CONNECT_BIT");

			/* first thing: if the esp32 is already connected to a access point: disconnect */
			if( (uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT) == (WIFI_MANAGER_WIFI_CONNECTED_BIT) ){
				ESP_LOGI(TAG, "%d", __LINE__);

				xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_STA_DISCONNECT_BIT);
				ESP_LOGI(TAG, "%d", __LINE__);
				ESP_ERROR_CHECK(esp_wifi_disconnect());

				/* wait until wifi disconnects. From experiments, it seems to take about 150ms to disconnect */
				xEventGroupWaitBits(wifi_manager_event_group, WIFI_MANAGER_STA_DISCONNECT_BIT, pdFALSE, pdTRUE, portMAX_DELAY );
			}

			/* set the new config and connect - reset the disconnect bit first as it is later tested */
			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_STA_DISCONNECT_BIT);

			ESP_LOGI(TAG, "esp_wifi_set_config: [%s]", esp_err_to_name(esp_wifi_set_config(WIFI_IF_STA, wifi_manager_get_wifi_sta_config())));
			ESP_LOGI(TAG, "esp_wifi_connect: [%s]", esp_err_to_name(esp_wifi_connect()));
			/* 2 scenarios here: connection is successful and SYSTEM_EVENT_STA_GOT_IP will be posted
			 * or it's a failure and we get a SYSTEM_EVENT_STA_DISCONNECTED with a reason code.
			 * Note that the reason code is not exploited. For all intent and purposes a failure is a failure.
			 */
			ESP_LOGI(TAG, "xEventGroupWaitBits %d", __LINE__);
			uxBits = xEventGroupWaitBits(wifi_manager_event_group,
					WIFI_MANAGER_WIFI_CONNECTED_BIT | WIFI_MANAGER_STA_DISCONNECT_BIT,
					pdFALSE, pdFALSE, portMAX_DELAY );

			if(uxBits & (WIFI_MANAGER_WIFI_CONNECTED_BIT | WIFI_MANAGER_STA_DISCONNECT_BIT)){

				/* only save the config if the connection was successful! */
				if(uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT){

					ESP_LOGI(TAG, "Got IP address, ping google.com for testing");

					struct hostent *hp = gethostbyname("google.com");
				    if (hp == NULL) {
				    	ESP_LOGE(TAG, "gethostbyname() failed\n");
				    } else {
				    	ESP_LOGI(TAG, "We are able to ping %s = ", hp->h_name);
				       unsigned int i=0;
				       while ( hp -> h_addr_list[i] != NULL) {
				    	   ESP_LOGI(TAG, "%s ", inet_ntoa( *( struct in_addr*)( hp -> h_addr_list[i])));
				          i++;
				       }
				       printf("\n");
				    }

					/* generate the connection info with success */
					wifi_manager_json_status_update(UPDATE_CONNECTION_OK);
					/* update the LED */
					LED_SetEventBit(LED_EVENT_WIFI_CONNECTED_BIT);

					/* save wifi config in NVS */
					ESP_LOGI(TAG, "AirU obtained an IP address from AP\n\r");
					wifi_manager_save_sta_config();

					/* Start SNTP */
					err = sntp_initialize();

					/* Start MQTT */
					MQTT_Initialize();

				}
				else{
					ESP_LOGE(TAG, "AirU FAILED to obtained an IP address from AP\n\r");

					/* failed attempt to connect regardles of the reason */
					wifi_manager_json_status_update(UPDATE_FAILED_ATTEMPT);
					/* update the LED */
					LED_SetEventBit(LED_EVENT_WIFI_DISCONNECTED_BIT);

					/* otherwise: reset the config */
					memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));

					/* Shut down the MQTT socket - It'll come back up when we reconnect */
					xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_STA_DISCONNECT_BIT);
				}
			}
			else {
				/* hit portMAX_DELAY limit ? Guess it would never happen
				 * leave the WIFI_MANAGER_REQUEST_STA_CONNECT_BIT = 1. So that we retry the next circle
				 * */
				ESP_LOGI(TAG, "xEventGroupWaitBits[%d] Timeout with Event Handler Event ID: %d", __LINE__, uxBits);
				xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_STA_DISCONNECT_BIT);
			}

			/* finally: release the connection request bit */
			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);
		}
		else if(uxBits & WIFI_MANAGER_REQUEST_WIFI_SCAN){
			ap_num = MAX_AP_NUM;
			ESP_LOGI(TAG, "WIFI_MANAGER_REQUEST_WIFI_SCAN\n");

			if(!(uxBits & WIFI_MANAGER_REQUEST_STA_CONNECT_BIT))
			{
				ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
				ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, accessp_records));

				/* make sure the http server isn't trying to access the list while it gets refreshed */
				if(wifi_manager_lock_json_buffer( ( TickType_t ) 20 )){
					/* Will remove the duplicate SSIDs from the list and update ap_num */
					wifi_manager_filter_unique(accessp_records, &ap_num);
					wifi_manager_generate_acess_points_json();
					wifi_manager_unlock_json_buffer();
				}
				else{
					ESP_LOGW(TAG, "could not get access to json mutex in wifi_scan\n");
				}
			}
			/* STA is actively trying to connect to an AP that isn't present. Terminate this.
			 * Web interface will request another scan in a few seconds. */
			else{
//				xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_WIFI_DISCONNECT);
			}

			/* finally: release the scan request bit */
			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_WIFI_SCAN);
		}
		else {
			ESP_LOGI(TAG, "xEventGroupWaitBits[%d] Timeout with Event Handler Event ID: %d", __LINE__, uxBits);
		}
		if ((uxBits & WIFI_MANAGER_REQUEST_RECONNECT))
		{
			ESP_LOGW(TAG, "WIFI_MANAGER_REQUEST_RECONNECT");
			ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
			ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, accessp_records));
			wifi_manager_fetch_wifi_sta_config();

			for(int i=0; i<ap_num;i++){
				wifi_ap_record_t ap = accessp_records[i];
				ESP_LOGD(TAG, "[%s] ?? [%s]\n", ap.ssid, wifi_manager_config_sta->sta.ssid);
				/* Looking for the most recent network from the available. Skip if theres not */
				if (strcasecmp((char*)ap.ssid,(char*) wifi_manager_config_sta->sta.ssid) == 0) {
					ESP_LOGI(TAG, "Found 1 matched SSID: [%s]", wifi_manager_config_sta->sta.ssid);
					xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RECONNECT);
					break;
				}
			}
			vTaskDelay( 5*ONE_SECOND_DELAY);
		}
	} /* for(;;) */
	vTaskDelay( (TickType_t)10);
} /*void wifi_manager*/


bool wifi_manager_connected_to_access_point()
{
	return (xEventGroupGetBits(wifi_manager_event_group) & WIFI_MANAGER_WIFI_CONNECTED_BIT);
}

