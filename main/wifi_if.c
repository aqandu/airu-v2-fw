/*
 * ble_if.h
 *
 *  Created on: Feb 14, 2019
 *      Author: tombo
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "http_server_if.h"
#include "led_if.h"
#include "time_if.h"
#include "mqtt_if.h"
#include "ble_if.h"
#include "wifi_if.h"

static const char *TAG = "wifi station";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t sta_wifi_event_group;

EventGroupHandle_t ble_event_group;
EventBits_t uxBits;

/* The event group allows multiple bits for each event, but we only care about one event 
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

static int s_retry_num = 0;

char ssid[48];
char password[48];



/*
 * @brief   
 *
 * @param   
 *
 * @return  
 */
void ble_wifi_cred_recv_set_event_ready(){
    xEventGroupSetBits(ble_event_group, (1 << 0));
}


/*
* @brief
*
* @param
*
* @return
*/
esp_err_t wifi_sta_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(sta_wifi_event_group, WIFI_CONNECTED_BIT);

        /* update the LED */
        LED_SetWifiConn(LED_WIFI_CONNECTED);

        /* Start SNTP */
        sntp_initialize();

        /* Start MQTT */
        MQTT_Initialize();

        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        {
            if (s_retry_num < 5) {
                esp_wifi_connect();
                xEventGroupClearBits(sta_wifi_event_group, WIFI_CONNECTED_BIT);
                s_retry_num++;
                ESP_LOGI(TAG,"retry to connect to the AP");
            }
            ESP_LOGI(TAG,"connect to the AP fail\n");
            break;
        }
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

/* NEED TO CREATE A NEW TASK HERE TO WAIT FOR SSID AND PASSWORD FROM BLE SO THAT MAIN CAN CONTINUTE TO INITIALIZE OTHER THINGS */

void wifi_sta_Initialize()
{
    ESP_LOGI(TAG, "Starting WiFi...");
    sta_wifi_event_group = xEventGroupCreate();
    ble_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_sta_event_handler, NULL) );

    ESP_LOGI(TAG, "Waiting of wifi ssid and password...");
    uxBits = xEventGroupWaitBits(ble_event_group, (1 << 0), pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Got wifi ssid and password\n");

    // Put the ssid and password into strings of the right length
    unsigned int ssid_len = strlen(ssid);
    unsigned int pass_len = strlen(password);
    char set_ssid[ssid_len];
    char set_password[pass_len];

    memcpy(set_ssid, ssid, ssid_len-1);
    set_ssid[ssid_len-1] = '\0';
    memcpy(set_password, password, pass_len-1);
    set_password[pass_len-1] = '\0';

    // Set the wifi credientials using the recieved ssid and password
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = { };
    strcpy((char*)wifi_config.sta.ssid, (const char*)set_ssid);
    strcpy((char*)wifi_config.sta.password, (const char*)set_password);

    // Start the wifi in sta mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
             set_ssid, set_password);

    // Start http server when AirU is connected
    //uxBits = xEventGroupWaitBits( , (1 << 0), pdFALSE, pdTRUE, portMAX_DELAY);
    //ESP_LOGI(TAG, "softAP started, starting http_server\n");
    //http_server_set_event_start();

}

