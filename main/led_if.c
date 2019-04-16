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

#define STAT1_LED 21	/* RED 	 */
#define STAT2_LED 19	/* GREEN */
#define STAT3_LED 18	/* BLUE  */

#define STAT1_CH	LEDC_CHANNEL_0
#define STAT2_CH	LEDC_CHANNEL_1
#define STAT3_CH	LEDC_CHANNEL_2

#define LEDC_RESOLUTION		LEDC_TIMER_8_BIT
#define LEDC_NUM_LEDS     	(3)
#define LEDC_DUTY         	(0x1 << (LEDC_RESOLUTION - 2))		/* 1/4 Max Duty */

static void _push(uint8_t ch, uint32_t duty);

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


static void _push(uint8_t ch, uint32_t duty)
{
	ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, duty);
	ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);
}


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
	xEventGroupClearBits(led_event_group, LED_EVENT_ALL_BITS);
}


void LED_SetEventBit(led_events_t bit)
{
	xEventGroupSetBits(led_event_group, bit);
}


void led_task(void *pvParameters)
{
	int ch;
	esp_err_t err;
	EventBits_t uxBits;
	_push(STAT1_CH, 0);
	_push(STAT2_CH, 0);
	_push(STAT3_CH, 0);

	for(;;) {
		uxBits = xEventGroupWaitBits(led_event_group, LED_EVENT_ALL_BITS, pdTRUE, pdFALSE, portMAX_DELAY);

//		if (uxBits & LED_EVENT_WIFI_DISCONNECTED_BIT) {
//			_push(STAT1_CH, LEDC_DUTY);
//		}
//		if (uxBits & LED_EVENT_WIFI_CONNECTED_BIT) {
//			_push(STAT1_CH, 0);
//		}
		if (uxBits & LED_EVENT_MICS_HEATER_ON_BIT) {
			ESP_LOGI(TAG, "MICS ON BIT");
			_push(STAT2_CH, LEDC_DUTY);
		}
		if (uxBits & LED_EVENT_MICS_HEATER_OFF_BIT) {
			ESP_LOGI(TAG, "MICS OFF BIT");
			_push(STAT2_CH, 0);
		}
		if (uxBits & LED_EVENT_GPS_RTC_NOT_SET_BIT) {
			_push(STAT3_CH, LEDC_DUTY);
		}
		if (uxBits & LED_EVENT_GPS_RTC_SET_BIT) {
			_push(STAT3_CH, 0);
		}
	}
}
