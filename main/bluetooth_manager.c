/*
 * bluetooth_manager.c
 *
 *  Created on: Jul 31, 2019
 *      Author: Quang Nguyen
 */

/****************************************************************************
* This is a demo for bluetooth config wifi connection to ap. You can config ESP32 to connect a softap
* or config ESP32 as a softap to be connected by other device. APP can be downloaded from github
* android source code: https://github.com/EspressifApp/EspBlufi
* iOS source code: https://github.com/EspressifApp/EspBlufiForiOS
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "lwip/netdb.h"

#include "blufi_example.h"
#include "time_if.h"
#include "mqtt_if.h"
#include "led_if.h"
#include "wifi_manager.h"

extern wifi_config_t* wifi_manager_config_sta;
const int DISCONNECT_BIT = BIT4;

void esp_ble_wifi_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

#define BLUFI_DEVICE_NAME            "BLUFI_DEVICE"
uint8_t ble_gatt_service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
};

//static uint8_t test_manufacturer[TEST_MANUFACTURER_DATA_LEN] =  {0x12, 0x23, 0x45, 0x56};
esp_ble_adv_data_t esp_ble_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 16,
    .p_service_uuid = ble_gatt_service_uuid128,
    .flag = 0x6,
};

esp_ble_adv_params_t example_adv_params = {
    .adv_int_min        = 0x100,
    .adv_int_max        = 0x100,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

#define WIFI_LIST_NUM   10
#define TAG   "BLUETOOTH_TASK"

wifi_config_t sta_config;
//wifi_config_t ap_config;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

/* store the station info for sending back to phone */
bool gl_sta_connected = false;
uint8_t gl_sta_bssid[6];
uint8_t gl_sta_ssid[32];
int gl_sta_ssid_len;

/* connect infor*/
uint8_t server_if;
uint16_t conn_id;
static esp_err_t example_net_event_handler(void *ctx, system_event_t *event)
{
    wifi_mode_t mode;
    ESP_LOGI(TAG, "example_net_event_handler ENTERRED with event [%d]", event->event_id);
    switch (event->event_id) {
    case SYSTEM_EVENT_STA_START:
        break;
    case SYSTEM_EVENT_STA_GOT_IP: {
        esp_blufi_extra_info_t info;

        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        esp_wifi_get_mode(&mode);

        memset(&info, 0, sizeof(esp_blufi_extra_info_t));
        memcpy(info.sta_bssid, gl_sta_bssid, 6);
        info.sta_bssid_set = true;
        info.sta_ssid = gl_sta_ssid;
        info.sta_ssid_len = gl_sta_ssid_len;

        esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
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
		LED_SetEventBit(LED_EVENT_WIFI_CONNECTED_BIT);

        break;
    }
    case SYSTEM_EVENT_STA_CONNECTED:
        gl_sta_connected = true;
        memcpy(gl_sta_bssid, event->event_info.connected.bssid, 6);
        memcpy(gl_sta_ssid, event->event_info.connected.ssid, event->event_info.connected.ssid_len);
        gl_sta_ssid_len = event->event_info.connected.ssid_len;
		xEventGroupClearBits(wifi_event_group, DISCONNECT_BIT);

        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        gl_sta_connected = false;
        memset(gl_sta_ssid, 0, 32);
        memset(gl_sta_bssid, 0, 6);
        gl_sta_ssid_len = 0;
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    	xEventGroupSetBits(wifi_event_group, DISCONNECT_BIT);

		LED_SetEventBit(LED_EVENT_WIFI_DISCONNECTED_BIT);

        break;
    case SYSTEM_EVENT_AP_START:
        esp_wifi_get_mode(&mode);

        /* TODO: get config or information of softap, then set to report extra_info */
        if (gl_sta_connected) {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, NULL);
        } else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
        }
        break;
    case SYSTEM_EVENT_SCAN_DONE: {
        uint16_t apCount = 0;
        esp_wifi_scan_get_ap_num(&apCount);
        if (apCount == 0) {
            ESP_LOGI(TAG,"Nothing AP found");
            break;
        }
        wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
        if (!ap_list) {
            ESP_LOGE(TAG,"malloc error, ap_list is NULL");
            break;
        }
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, ap_list));
        esp_blufi_ap_record_t * blufi_ap_list = (esp_blufi_ap_record_t *)malloc(apCount * sizeof(esp_blufi_ap_record_t));
        if (!blufi_ap_list) {
            if (ap_list) {
                free(ap_list);
            }
            ESP_LOGE(TAG,"malloc error, blufi_ap_list is NULL");
            break;
        }
        for (int i = 0; i < apCount; ++i)
        {
            blufi_ap_list[i].rssi = ap_list[i].rssi;
            memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
        }
        esp_blufi_send_wifi_list(apCount, blufi_ap_list);
        esp_wifi_scan_stop();
        free(ap_list);
        free(blufi_ap_list);
        break;
    }
    default:
        break;
    }
    return ESP_OK;
}

static esp_blufi_callbacks_t example_callbacks = {
    .event_cb = esp_ble_wifi_event_callback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};

void esp_bluetooth_send_wifi_list(uint16_t apCount, esp_blufi_ap_record_t *list) {
	ESP_LOGI(TAG, "%s:%d ENTERRED", __func__, __LINE__);
    uint8_t err = esp_blufi_send_wifi_list(apCount, list);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"esp_blufi_send_wifi_list error [%d]", err);
    }
}
void esp_ble_wifi_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    /* actually, should post to blufi_task handle the procedure,
     * now, as a example, we do it more simply */
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        ESP_LOGI(TAG,"BLUFI init finish\n");
    	uint8_t mac[6];
    	uint8_t bluetooth_device[MAX_SSID_SIZE];
    	esp_efuse_mac_get_default(mac);
    	sprintf((char*)bluetooth_device, "%s-%02X%02X", DEFAULT_AP_SSID, mac[4], mac[5]);

        esp_ble_gap_set_device_name((char*)bluetooth_device);
        esp_ble_gap_config_adv_data(&esp_ble_adv_data);
        break;
    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        ESP_LOGI(TAG,"BLUFI deinit finish\n");
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        ESP_LOGI(TAG,"BLUFI ble connect\n");
        server_if = param->connect.server_if;
        conn_id = param->connect.conn_id;
        esp_ble_gap_stop_advertising();
        blufi_security_init();
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        ESP_LOGI(TAG,"BLUFI ble disconnect\n");
        blufi_security_deinit();
        esp_ble_gap_start_advertising(&example_adv_params);
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        ESP_LOGI(TAG,"BLUFI Set WIFI opmode %d\n", param->wifi_mode.op_mode);
        ESP_ERROR_CHECK( esp_wifi_set_mode(param->wifi_mode.op_mode) );
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        ESP_LOGI(TAG,"BLUFI requset wifi connect to AP\n");
        /* there is no wifi callback when the device has already connected to this wifi
        so disconnect wifi before connection.
        */
        wifi_manager_config_sta = &sta_config;
        wifi_manager_connect_async();
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        ESP_LOGI(TAG,"BLUFI request wifi disconnect from AP\n");
        wifi_manager_disconnect_async();
//        esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        ESP_LOGE(TAG,"BLUFI report error, error code %d\n", param->report_error.state);
        esp_blufi_send_error_info(param->report_error.state);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
        ESP_LOGI(TAG,"BLUFI get wifi status from AP\n");

        wifi_mode_t mode;
        esp_blufi_extra_info_t info;

        esp_wifi_get_mode(&mode);

        if (isWifiConnected() ) {
            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            memcpy(info.sta_bssid, gl_sta_bssid, 6);
            info.sta_bssid_set = true;
            info.sta_ssid = gl_sta_ssid;
            info.sta_ssid_len = gl_sta_ssid_len;
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        } else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
        }

        break;
    }
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        ESP_LOGI(TAG,"blufi close a gatt connection");
        esp_blufi_close(server_if, conn_id);
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        /* TODO */
        break;
	case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        ESP_LOGI(TAG,"Recv STA BSSID\n");
        memcpy(sta_config.sta.bssid, param->sta_bssid.bssid, 6);
        sta_config.sta.bssid_set = 1;
        ESP_LOGI(TAG,"Recv STA BSSID %s\n", sta_config.sta.ssid);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_SSID:
        strncpy((char *)sta_config.sta.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        sta_config.sta.ssid[param->sta_ssid.ssid_len] = '\0';
        ESP_LOGI(TAG,"Recv STA SSID %s\n", sta_config.sta.ssid);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        strncpy((char *)sta_config.sta.password, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        sta_config.sta.password[param->sta_passwd.passwd_len] = '\0';
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        break;
//	case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
//        strncpy((char *)ap_config.ap.ssid, (char *)param->softap_ssid.ssid, param->softap_ssid.ssid_len);
//        ap_config.ap.ssid[param->softap_ssid.ssid_len] = '\0';
//        ap_config.ap.ssid_len = param->softap_ssid.ssid_len;
//        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
//        ESP_LOGI(TAG,"Recv SOFTAP SSID %s, ssid len %d\n", ap_config.ap.ssid, ap_config.ap.ssid_len);
//        break;
//	case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
//        strncpy((char *)ap_config.ap.password, (char *)param->softap_passwd.passwd, param->softap_passwd.passwd_len);
//        ap_config.ap.password[param->softap_passwd.passwd_len] = '\0';
//        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
//        ESP_LOGI(TAG,"Recv SOFTAP PASSWORD %s len = %d\n", ap_config.ap.password, param->softap_passwd.passwd_len);
//        break;
//	case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
//        if (param->softap_max_conn_num.max_conn_num > 4) {
//            return;
//        }
//        ap_config.ap.max_connection = param->softap_max_conn_num.max_conn_num;
//        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
//        ESP_LOGI(TAG,"Recv SOFTAP MAX CONN NUM %d\n", ap_config.ap.max_connection);
//        break;
//	case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
//        if (param->softap_auth_mode.auth_mode >= WIFI_AUTH_MAX) {
//            return;
//        }
//        ap_config.ap.authmode = param->softap_auth_mode.auth_mode;
//        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
//        ESP_LOGI(TAG,"Recv SOFTAP AUTH MODE %d\n", ap_config.ap.authmode);
//        break;
//	case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
//        if (param->softap_channel.channel > 13) {
//            return;
//        }
//        ap_config.ap.channel = param->softap_channel.channel;
//        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
//        ESP_LOGI(TAG,"Recv SOFTAP CHANNEL %d\n", ap_config.ap.channel);
//        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST:{
    	ESP_LOGI(TAG,"ESP_BLUFI_EVENT_GET_WIFI_LIST");
//        wifi_scan_config_t scanConf = {
//            .ssid = NULL,
//            .bssid = NULL,
//            .channel = 0,
//            .show_hidden = false
//        };
//        ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));
        wifi_manager_scan_async();
        break;
    }
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        ESP_LOGI(TAG,"Recv Custom Data %d\n", param->custom_data.data_len);
        esp_log_buffer_hex("Custom Data", param->custom_data.data, param->custom_data.data_len);
        break;
	case ESP_BLUFI_EVENT_RECV_USERNAME:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CA_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_SERVER_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
        /* Not handle currently */
        break;;
	case ESP_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
        /* Not handle currently */
        break;
    default:
        break;
    }
}

void example_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&example_adv_params);
        break;
    default:
        break;
    }
}

//void initialise_wifi(void)
//{
//    tcpip_adapter_init();
//    wifi_manager_event_group = xEventGroupCreate();
//    ESP_ERROR_CHECK( esp_event_loop_init(wifi_manager_event_handler, NULL) );
//    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
//    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
//    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
//    ESP_ERROR_CHECK( esp_wifi_start() );
//}

void bluetooth_manager()
{
    esp_err_t ret;
    ESP_LOGI(TAG, "%s:%d ENTERRED!!", __func__, __LINE__);
//    ret = nvs_flash_init();
//    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//        ESP_ERROR_CHECK(nvs_flash_erase());
//        ret = nvs_flash_init();
//    }
//    initialise_wifi();
    bluetooth_initialize();

	while(1) {
		vTaskDelay(5000 / portTICK_PERIOD_MS);
		printf("free heap: %d\n",esp_get_free_heap_size());
	}
//    wifi_loop();
}

void bluetooth_initialize() {
    ESP_LOGI(TAG,"%s esp_bt_controller_mem_release: [%s]", __func__,
    		esp_err_to_name(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_LOGI(TAG,"%s esp_bt_controller_init: [%s]", __func__,
    		esp_err_to_name(esp_bt_controller_init(&bt_cfg)));
    ESP_LOGI(TAG,"%s esp_bt_controller_enable: %s\n", __func__,
    		esp_err_to_name(esp_bt_controller_enable(ESP_BT_MODE_BLE)));

    ESP_LOGI(TAG, "%s esp_bluedroid_init [%s]", __func__,
    		esp_err_to_name(esp_bluedroid_init()));

    ESP_LOGI(TAG, "%s esp_bluedroid_enable [%s]", __func__,
    		esp_err_to_name(esp_bluedroid_enable()));

    ESP_LOGI(TAG,"BD ADDR: "ESP_BD_ADDR_STR"\n", ESP_BD_ADDR_HEX(esp_bt_dev_get_address()));
    ESP_LOGI(TAG,"BLUFI VERSION %04x\n", esp_blufi_get_version());

    ESP_LOGI(TAG, "%s esp_ble_gap_register_callback [%s]", __func__,
    		esp_err_to_name(esp_ble_gap_register_callback(example_gap_event_handler)));

    ESP_LOGI(TAG, "%s esp_blufi_register_callbacks [%s]", __func__,
    		esp_err_to_name(esp_blufi_register_callbacks(&example_callbacks)));

    ESP_LOGI(TAG, "%s: esp_blufi_profile_init [%s]", __func__, esp_err_to_name(esp_blufi_profile_init()));
    ESP_LOGI(TAG, "QQQ: service UUID ");
    for(uint8_t i = 0; i < sizeof(ble_gatt_service_uuid128); i++) {
    	printf("%X ", ble_gatt_service_uuid128[i]);
    }
}

