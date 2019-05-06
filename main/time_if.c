/*
 * time_if.c
 *
 *  Created on: Oct 8, 2018
 *      Author: tombo
 */

#include "time_if.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/apps/sntp.h"

#define WIFI_CONNECTED_BIT 	BIT0

static const unsigned long MS_BETWEEN_NTP_UPDATE = 600000;
static const unsigned long SEC_JAN1_2018 = 1514764800;
static const char *TAG = "TIME";
static time_t utc_time = 0;
static clock_t ms_active = 0;

static EventGroupHandle_t ntp_event_group;

static time_t _sntp_obtain_time(void);

/*
* @brief	Try 10 times to get the current time from the NTP server
*
* @param 	N/A
*
* @return	Seconds since the UNIX Epoch (January 1, 1970 00:00:00)
*/
static time_t _sntp_obtain_time(void)
{
    // wait for time to be set
    time_t now = 0;
    int retry = 0;
    const int retry_count = 10;

    while(now < (time_t) SEC_JAN1_2018 && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... [%d / %d]", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
    }
    return now;
}


/*
* @brief	Get the current GMT time since UNIX Epoch, in seconds.
* 			If more than MS_BETWEEN_NTP_UPDATE have passed, make a
* 			call to the NTP server to get current UTC.
*
* @param	N/A
*
* @return	Seconds since the UNIX Epoch (January 1, 1970 00:00:00)
*/
time_t time_gmtime(void){
	ESP_LOGI(TAG, "time_gmtime()");
	clock_t current_ms = clock();

	// must update using SNTP if over 10 minutes
	if ((current_ms - ms_active > MS_BETWEEN_NTP_UPDATE) || utc_time < SEC_JAN1_2018){
		ESP_LOGI(TAG, "Calling NTP server to retreive timestamp");
		utc_time = _sntp_obtain_time();
		ms_active = clock();	// only update after getting ntp time so we can track it

		// Error will give us time stamps around 1970, so we'll know
		if (utc_time < 0)
			return ms_active;
		else
			return utc_time;
	}
	else{
		return utc_time + ((current_ms - ms_active) / 1000);
	}
}


void sntp_wifi_connected()
{
	xEventGroupSetBits(ntp_event_group, WIFI_CONNECTED_BIT);
}

/*
* @brief	Initialize the parameters for the NTP server and update the system time.
*
* @param	N/A
*
* @return	N/A
*/
void sntp_initialize(void)
{
	time_t now;
	struct tm timeinfo;
    char strftime_buf[64];

    ntp_event_group = xEventGroupCreate();
    xEventGroupClearBits(ntp_event_group, WIFI_CONNECTED_BIT);

    /* Waiting for WiFi to connect */
//    xEventGroupWaitBits(ntp_event_group, WIFI_CONNECTED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
	ESP_LOGI(TAG, "Initializing SNTP");

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "north-america.pool.ntp.org");
    sntp_setservername(2, "us.pool.ntp.org");
    sntp_init();

    // Set timezone to Eastern Standard Time and print local time
    setenv("TZ", "Etc/GMT", 1);
    tzset();

    time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        now = _sntp_obtain_time();
    }

    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "NTP Time Set! The current GMT date/time is: %s", strftime_buf);
}



