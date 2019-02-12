/*
 * led_if.c
 *
 *  Created on: Jan 18, 2019
 *      Author: tombo
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "led_if.h"

#define STAT1_LED 21
#define STAT2_LED 19
#define STAT3_LED 18

#define STAT1_CH	LEDC_CHANNEL_0
#define STAT2_CH	LEDC_CHANNEL_1
#define STAT3_CH	LEDC_CHANNEL_2

#define LEDC_RESOLUTION		LEDC_TIMER_8_BIT
#define LEDC_NUM_LEDS     	(3)
#define LEDC_DUTY         	(0x1 << (LEDC_RESOLUTION - 2))		/* 1/4 Max Duty */

#define WIFI_CONNECTED_BIT		BIT0
#define WIFI_DISCONNECTED_BIT 	BIT1
#define ALL_BITS				WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT

static const char* TAG = "LED";

static EventGroupHandle_t led_event_group;
static ledc_channel_config_t ledc_channel[LEDC_NUM_LEDS] = {
    {
        .channel    = STAT1_CH,
        .duty       = LEDC_DUTY,
        .gpio_num   = STAT1_LED,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = LEDC_TIMER_0
    },
    {
        .channel    = STAT2_CH,
        .duty       = 0,
        .gpio_num   = STAT2_LED,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = LEDC_TIMER_0
    },
    {
        .channel    = STAT3_CH,
        .duty       = 0,
        .gpio_num   = STAT3_LED,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = LEDC_TIMER_0
    },
};


void LED_Initialize()
{
    int ch;

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 500,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0
    };

    ledc_timer_config(&ledc_timer);

    // Set LED Controller with previously prepared configuration
    for (ch = 0; ch < LEDC_NUM_LEDS; ch++) {
        ledc_channel_config(&ledc_channel[ch]);
    }

	led_event_group = xEventGroupCreate();
	xEventGroupClearBits(led_event_group, ALL_BITS);
}


void LED_SetWifiConn(led_wifi_conn_t bit)
{
	if(bit) {
		xEventGroupSetBits(led_event_group, WIFI_CONNECTED_BIT);
	}
	else{
		xEventGroupSetBits(led_event_group, WIFI_DISCONNECTED_BIT);
	}
}


void led_task(void *pvParameters)
{
	int ch;
	esp_err_t err;
	EventBits_t uxBits;

	for(;;) {
		ESP_LOGI(TAG, "Waiting for something to happen...");
		uxBits = xEventGroupWaitBits(led_event_group, ALL_BITS, pdTRUE, pdFALSE, portMAX_DELAY);

		if (uxBits & WIFI_CONNECTED_BIT) {
			ch = STAT1_CH;
			ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, 0);
			ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);
		}
		if (uxBits & WIFI_DISCONNECTED_BIT) {
			ch = STAT1_CH;
			ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, LEDC_DUTY);
			ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);
		}

	}
}
