#ifdef __cplusplus
extern "C" {
#endif

#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "enc28j60.h"
#include "sdkconfig.h"
#include "tcpip_adapter.h"
#include "tcpip_adapter.h"
#include "esp_event_legacy.h"
#include "freertos/event_groups.h"
#include "esp_ping.h"


/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

/** Main task for the Ethernet_manager*/
void Initialize_eth();

EventBits_t Ethernet_manager_wait_internet_access();

EventBits_t eth_manager_wait_internet_access();

esp_err_t pingResults(ping_target_id_t msgType, esp_ping_found * pf);

bool eth_manager_connected_to_access_point();

static void eth_manager_ping_test();

#ifdef __cplusplus
}
#endif
