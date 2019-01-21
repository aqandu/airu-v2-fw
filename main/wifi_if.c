/*
 * wifi_if.c
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#include "wifi_if.h"
#include "mqtt_if.h"
#include "time_if.h"
#include "app_utils.h"

#include <string.h>
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "WIFI";


/*
* @brief
*
* @param
*
* @return
*/
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
 switch(event->event_id)
 {
   case SYSTEM_EVENT_STA_START:
     esp_wifi_connect();
     break;

   case SYSTEM_EVENT_STA_GOT_IP:
     ESP_LOGI(TAG, "Local IP: %s",
              ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
     xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);

     sntp_initialize();
     MQTT_Initialize();

     break;

   case SYSTEM_EVENT_AP_STACONNECTED:
		ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d",
			  MAC2STR(event->event_info.sta_connected.mac),
			   event->event_info.sta_connected.aid);
		break;

   case SYSTEM_EVENT_AP_STADISCONNECTED:
   	ESP_LOGI(TAG, "station:"MACSTR"leave, AID=%d",
   			MAC2STR(event->event_info.sta_disconnected.mac),
				event->event_info.sta_disconnected.aid);
   	break;

   case SYSTEM_EVENT_STA_DISCONNECTED:
   	ESP_LOGI(TAG, "wifi sta disconnected");
   	xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

   	/* May need to call:
   	 * esp_wifi_stop();
   	 * wifi_init_sta();
   	 *  */
   	esp_wifi_connect();
   	break;

   case SYSTEM_EVENT_AP_STOP:
		ESP_LOGI(TAG, "AP mode stopped.");
		xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
		wifi_init_sta();
		break;

   default:
       break;

   }//switch

   return ESP_OK;
}


/*
* @brief
*
* @param
*
* @return
*/
void wifi_init_sta(void)
{
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	wifi_config_t wifi_config = {
	   .sta = {
		   .ssid = CONFIG_WIFI_STA_SSID,
		   .password = CONFIG_WIFI_STA_PASSWORD,
	   },
	};

	tcpip_adapter_init();
	wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_LOGI(TAG, "start the WIFI SSID:[%s]", CONFIG_WIFI_STA_SSID);
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(TAG, "Waiting for wifi");
	xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}


/*
* @brief
*
* @param
*
* @return
*/
void wifi_init_softap()
{
   tcpip_adapter_init();
   ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));

   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK(esp_wifi_init(&cfg));
   wifi_config_t wifi_config =
   {
       .ap =
       {
           .ssid = CONFIG_WIFI_AP_SSID,
           .ssid_len = strlen(CONFIG_WIFI_AP_SSID),
           .password = CONFIG_WIFI_AP_PASSWORD,
           .authmode = WIFI_AUTH_WPA_WPA2_PSK
       },
   };

   if (strlen(CONFIG_WIFI_AP_PASSWORD) == 0)
   {
       wifi_config.ap.authmode = WIFI_AUTH_OPEN;
   }

   ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
   ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
   ESP_ERROR_CHECK(esp_wifi_start());

   ESP_LOGI(TAG, "wifi_init_softap finished.SSID:%s password:%s",
            CONFIG_WIFI_AP_SSID, CONFIG_WIFI_AP_PASSWORD);
}
