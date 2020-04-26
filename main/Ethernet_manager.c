/* Ethernet Manager
 * Written by Henry Gilbert
 * Last updated 26 April 2020
 *
 * This library uses the ENC28J60 library for Ethernet connection.
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "enc28j60.h"
#include "Ethernet_manager.h"
#include "sdkconfig.h"

#include "mdns.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"

#include "esp_ping.h"
#include "ping/ping.h"
#include "esp_event_loop.h"
#include "led_if.h"


#define MS2TICK(ms) (( ms / portTICK_PERIOD_MS ))
#define THIRTY_SECONDS_TIMEOUT (30000 / portTICK_PERIOD_MS)
#define ONE_SECOND_DELAY (1000 / portTICK_PERIOD_MS)
#define RECONNECT_RETRY_PERIOD 30 * ONE_SECOND_DELAY
#define PING_TEST_TIMEOUT_MS 3000


static const char *TAG = "Ethernet";
static TimerHandle_t ;
static EventGroupHandle_t eth_event_group = NULL;
const int CONNECTED_BIT = BIT0;
const int REQUEST_PING_TEST = BIT1;

static TimerHandle_t eth_reconnect_timer;


static void vTimerCallback(TimerHandle_t xTimer);


/*
 * @brief 	Reconnect timer callback. Try to reconnect every x seconds.
 * 			Needed when AP and AirU get power cycled and AirU comes back
 * 			first. It will listen for AP coming back online and connect
 * 			when it does.
 *
 * @param 	xTimer - the timer handle
 *
 * @return 	N/A
 */
static void vTimerCallback(TimerHandle_t xTimer)
{
	ESP_LOGI(TAG, "TIMER: Reconnect timer done. Setting REQUEST_PING_TEST Bit.");
	xTimerStop(xTimer, 0);
	xEventGroupSetBits(eth_event_group, REQUEST_PING_TEST);
}

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        xEventGroupClearBits(eth_event_group, CONNECTED_BIT);
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    xEventGroupSetBits(eth_event_group, CONNECTED_BIT);

}

/** Main task for the ENC28J60 Ethernet*/
void Initialize_eth()
{
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    // Initialize TCP/IP network interface (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());

    eth_event_group = xEventGroupCreate();
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    // Set default handlers to process TCP/IP stuffs
    ESP_ERROR_CHECK(esp_eth_set_default_handlers(eth_netif));
    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    spi_bus_config_t buscfg = {
        .miso_io_num = 12,
        .mosi_io_num = 15,
        .sclk_io_num = 14,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(VSPI_HOST, &buscfg, 1));
    /* ENC28J60 ethernet driver is based on spi driver */
    spi_device_interface_config_t devcfg = {
        .command_bits = 3,
        .address_bits = 5,
        .mode = 0,
        .clock_speed_hz = 5 * 1000 * 1000,
        .spics_io_num = 16,
        .queue_size = 20
    };
    spi_device_handle_t spi_handle = NULL;
    ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &devcfg, &spi_handle));

    eth_enc28j60_config_t enc28j60_config = ETH_ENC28J60_DEFAULT_CONFIG(spi_handle);
    enc28j60_config.int_gpio_num = 25;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.smi_mdc_gpio_num = -1;  // ENC28J60 doesn't have SMI interface
    mac_config.smi_mdio_gpio_num = -1;
    esp_eth_mac_t *mac = esp_eth_mac_new_enc28j60(&enc28j60_config, &mac_config);

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 0; // ENC28J60 doesn't support auto-negotiation
    phy_config.reset_gpio_num = -1; // ENC28J60 doesn't have a pin to reset internal PHY
    esp_eth_phy_t *phy = esp_eth_phy_new_enc28j60(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    /* ENC28J60 doesn't burn any factory MAC address, we need to set it manually.
       02:00:00 is a Locally Administered OUI range so should not be used except when testing on a LAN under your control.
    */
    mac->set_addr(mac, (uint8_t[]) {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56
    });

    /* attach Ethernet driver to TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    /* start Ethernet driver state machine */
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    eth_reconnect_timer = xTimerCreate("eth_reconnect_timer",
    					  RECONNECT_RETRY_PERIOD,
    					  pdFALSE, (void *)NULL,
    					  vTimerCallback);

}

EventBits_t Ethernet_manager_wait_internet_access()
{
	while(eth_event_group == NULL){
		ESP_LOGI(TAG, "waiting for ETH event group creation");
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}

	return xEventGroupWaitBits(eth_event_group, CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY );
}

EventBits_t eth_manager_wait_internet_access() {
	return xEventGroupWaitBits(eth_event_group, CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY );
}

bool eth_manager_connected_to_access_point(){
	return (xEventGroupGetBits(eth_event_group) & CONNECTED_BIT);
}

int eth_manager_check_connection()
{
	EventBits_t uxBits;
	if(eth_manager_connected_to_access_point()){

		// Issue ping test
		eth_manager_ping_test();

		// Wait up to PING_TEST_TIMEOUT_MS for ping test callback to set CONNECTED_BIT
		int ret = (CONNECTED_BIT & \
				xEventGroupWaitBits(eth_event_group, CONNECTED_BIT, pdFALSE, pdTRUE, MS2TICK(PING_TEST_TIMEOUT_MS)));

		// Ping test failed. Start the reconnect timer
		if (!ret){
			LED_SetEventBit(LED_EVENT_WIFI_DISCONNECTED_BIT);

			// Ping test failed. Start the reconnect timer
			if(!xTimerIsTimerActive(eth_reconnect_timer)) {
				ESP_LOGI(TAG, "Starting timer");
				xTimerStart(eth_reconnect_timer, 0);
			}
		}

		// ping test was successful.
		else{
			ESP_LOGI(TAG, "%s, WE HAVE INTERNET! Stop the reconnect timer.", __func__);

		}
		return ret;
	}
	else{
		return -2;
	}
}

static void eth_manager_ping_test(){
	uint32_t ping_timeout = PING_TEST_TIMEOUT_MS; 	// ms till we consider it timed out
	uint32_t ping_count = 1;
	struct in_addr ip;
	inet_aton("8.8.8.8", &ip);		// Google's DNS

	ESP_LOGI("PING", "Issuing Ping test. IP binary: 0x%08x", ip.s_addr);

	xEventGroupClearBits(eth_event_group, CONNECTED_BIT);
	LED_SetEventBit(LED_EVENT_WIFI_DISCONNECTED_BIT);
	esp_ping_set_target(PING_TARGET_IP_ADDRESS_COUNT, &ping_count, sizeof(uint32_t));
	esp_ping_set_target(PING_TARGET_RCV_TIMEO, &ping_timeout, sizeof(uint32_t));
	esp_ping_set_target(PING_TARGET_IP_ADDRESS, &ip.s_addr, sizeof(uint32_t));
	esp_ping_set_target(PING_TARGET_RES_FN, &pingResults, sizeof(pingResults));
	ping_init();

}


esp_err_t pingResults(ping_target_id_t msgType, esp_ping_found * pf){
//	ESP_LOGI("PING", "\n\r\tAvgTime:\t%.1fmS \n\r\tSent:\t\t%d \n\r\tRec:\t\t%d \n\r\tErr Cnt:\t%d  \n\r\tErr:\t\t%d \n\r\tmin(mS):\t%d \n\r\tmax(mS):\t%d \n\r\tResp(mS):\t%d \n\r\tTimeouts:\t%d \n\r\tTotal Time:\t%d\n", (float)pf->total_time/pf->recv_count, pf->send_count, pf->recv_count, pf->err_count, pf->ping_err, pf->min_time, pf->max_time,pf->resp_time, pf->timeout_count, pf->total_time );
	if (pf->recv_count > 0){
		ESP_LOGI("PING", "PING TEST SUCCESS");
		xEventGroupSetBits(eth_event_group, CONNECTED_BIT);
		LED_SetEventBit(LED_EVENT_WIFI_CONNECTED_BIT);
		xTimerStop(eth_reconnect_timer, 0);
	}
	else{
		ESP_LOGE("PING", "Couldn't ping 8.8.8.8. Internet is down!");
	}

	return ESP_OK;
}

