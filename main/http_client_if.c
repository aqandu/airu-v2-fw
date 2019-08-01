#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "tcpip_adapter.h"
//#include "protocol_examples_common.h"
//#include "esp_tls.h"
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
//#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_http_client.h"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_SEND_BUFFER 512
#define SERVER_IP	"192.168.0.40"
#define WEB_SERVER 	"http://" SERVER_IP
#define WEB_URL		"http://" SERVER_IP "/uploadcsv"

static const char *TAG = "HTTP_CLIENT";

static esp_err_t ftp_send_cmd(int sock, const char* cmd);
static esp_err_t rcv(int sock, char *rx_buffer);
void efail(int sock);

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Write out data
                // printf("%.*s", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
//            int mbedtls_err = 0;
//            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
//            if (err != 0) {
//                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
//                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
//            }
            break;
    }
    return ESP_OK;
}

static void http_post_multipart()
{
    esp_err_t err;

    ESP_LOGI(TAG, "%s", WEB_URL);

	esp_http_client_config_t config = {
        .url = "http://httpbin.org/get", /* WEB_URL */
        .event_handler = _http_event_handler,
		.method = HTTP_METHOD_POST,
		.buffer_size_tx = MAX_HTTP_SEND_BUFFER,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // POST
    const char *post_data = "field1=value1&field2=value2";
//    esp_http_client_set_url(client, "http://httpbin.org/post");
//    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

esp_err_t ftp_put()
{
	/* Connect command socket to server (Port 21) */
    const char* HOST_IP_ADDR = "155.98.12.99";
    uint16_t PORT = 21;
	char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;
	struct sockaddr_in dest_addr;
	dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons(PORT);
	addr_family = AF_INET;
	ip_protocol = IPPROTO_IP;
	inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

	int sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
	if (sock < 0) {
		ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "Socket created, connecting to %s:%d", HOST_IP_ADDR, PORT);

	int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
	if (err != 0) {
		ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
		return ESP_FAIL;
	}
	ESP_LOGI(TAG, "Successfully connected");

	/* Open file from SD card */

	/* Send "USER <user>" to server */
	ftp_send_cmd(sock, "USER becnel");
	if(rcv(sock, rx_buffer) < 0){
		ESP_LOGE(TAG, "USER CMD");
	}

	/* Send "PASS <password>" to server */
	ftp_send_cmd(sock, "PASS nitroT1");
	if(rcv(sock, rx_buffer) < 0){
		ESP_LOGE(TAG, "PASS CMD");
	}

	close(sock);
	return ESP_OK;
	/* Send "SYST" to server */
	/* Send "Type I" to server */
	/* Send "PASV" to server */
	/* Connect data socket to server (port given by server) */
	/* Send "STOR <filename>" to server */
	/* Write <=64 bytes from file to server until EOF */
	/* Terminate data socket */
	/* Send "QUIT" to server */
	/* Terminate command socket */
}

static esp_err_t ftp_send_cmd(int sock, const char* cmd)
{
	esp_err_t err = send(sock, cmd, strlen(cmd), 0);
	if (err < 0) {
		ESP_LOGE(TAG, "Error occurred in [%s] during sending: errno %d", __func__, errno);
	}
	return err;
}

static esp_err_t rcv(int sock, char *rx_buffer)
{
	uint8_t respCode, thisByte;
	esp_err_t err;

	int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, MSG_PEEK);
	respCode = rx_buffer[0]; /* I think? */
	ESP_LOGI(TAG, "rcv peek: %s", rx_buffer);

	while(len > 0){
		len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
		ESP_LOGI(TAG, "rcv recv: %s", rx_buffer);
	}

	if(respCode >= '4'){
		efail(sock);
		return ESP_FAIL;
	}

	return 0;

}

void efail(int sock)
{
	esp_err_t err;
	uint8_t thisByte = 0;
	const char* payload = "QUIT";
	err = send(sock, payload, strlen(payload), 0);
	if (err < 0) {
		ESP_LOGE(TAG, "Error occurred in [%s] during sending: errno %d", __func__, errno);
		return;
	}
	int len = 1;
	do{
		len = recv(sock, &thisByte, sizeof(char), 0);
		ESP_LOGI(TAG, "[%s] - '%c'", __func__, thisByte);
	}while(len>0);
	close(sock);
	ESP_LOGI(TAG, "[%s] - Socket closed", __func__);
}
