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
#include "led_if.h"

#define STAT1_LED 21
#define STAT2_LED 19
#define STAT3_LED 18
#define LEDC_HS_TIMER          LEDC_TIMER_0
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE
#define LEDC_HS_CH0_GPIO       (STAT1_LED)
#define LEDC_HS_CH0_CHANNEL    LEDC_CHANNEL_0
#define LEDC_HS_CH1_GPIO       (STAT2_LED)
#define LEDC_HS_CH1_CHANNEL    LEDC_CHANNEL_1

#define LEDC_LS_TIMER          LEDC_TIMER_1
#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_LS_CH2_GPIO       (STAT3_LED)
#define LEDC_LS_CH2_CHANNEL    LEDC_CHANNEL_2
//#define LEDC_LS_CH3_GPIO       (5)
//#define LEDC_LS_CH3_CHANNEL    LEDC_CHANNEL_3

#define LEDC_TEST_CH_NUM       (3)
#define LEDC_TEST_DUTY         (4000)
#define LEDC_TEST_FADE_TIME    (3000)

#define LEDC_RESOLUTION			LEDC_TIMER_8_BIT
#define LEDC_HPOINT				LEDC_RESOLUTION >> 4

#define WIFI_CONNECTED_BIT		BIT0
#define WIFI_DISCONNECTED_BIT 	BIT1
#define ALL_BITS				WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT

static EventGroupHandle_t led_event_group;
static ledc_timer_config_t ledc_timer = {
	.duty_resolution = LEDC_RESOLUTION, 	// resolution of PWM duty
	.freq_hz = 500,	                      	// frequency of PWM signal
	.speed_mode = LEDC_HS_MODE,           	// timer mode
	.timer_num = LEDC_HS_TIMER            	// timer index
};
static ledc_channel_config_t ledc_channel[LEDC_TEST_CH_NUM] = {
		{
			.channel    = LEDC_HS_CH0_CHANNEL,
			.duty       = 0,
			.gpio_num   = LEDC_HS_CH0_GPIO,
			.speed_mode = LEDC_HS_MODE,
			.timer_sel  = LEDC_HS_TIMER,
			.hpoint     = LEDC_HPOINT
		},
		{
			.channel    = LEDC_HS_CH1_CHANNEL,
			.duty       = 0,
			.gpio_num   = LEDC_HS_CH1_GPIO,
			.speed_mode = LEDC_HS_MODE,
			.timer_sel  = LEDC_HS_TIMER,
			.hpoint     = LEDC_HPOINT
		},
		{
			.channel    = LEDC_LS_CH2_CHANNEL,
			.duty       = 0,
			.gpio_num   = LEDC_LS_CH2_GPIO,
			.speed_mode = LEDC_LS_MODE,
			.timer_sel  = LEDC_LS_TIMER,
			.hpoint     = LEDC_HPOINT
		},
	};

void LED_Initialize()
{
	int ch;

	/*
	 * Prepare and set configuration of timers
	 * that will be used by LED Controller
	 */
//	ledc_timer = {
//		.duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty
//		.freq_hz = 5000,                      // frequency of PWM signal
//		.speed_mode = LEDC_HS_MODE,           // timer mode
//		.timer_num = LEDC_HS_TIMER            // timer index
//	};
	// Set configuration of timer0 for high speed channels
	ledc_timer_config(&ledc_timer);

	// Prepare and set configuration of timer1 for low speed channels
	ledc_timer.speed_mode = LEDC_LS_MODE;
	ledc_timer.timer_num = LEDC_LS_TIMER;
	ledc_timer_config(&ledc_timer);

	/*
	 * Prepare individual configuration
	 * for each channel of LED Controller
	 * by selecting:
	 * - controller's channel number
	 * - output duty cycle, set initially to 0
	 * - GPIO number where LED is connected to
	 * - speed mode, either high or low
	 * - timer servicing selected channel
	 *   Note: if different channels use one timer,
	 *         then frequency and bit_num of these channels
	 *         will be the same
	 */
//	ledc_channel[LEDC_TEST_CH_NUM] = {
//		{
//			.channel    = LEDC_HS_CH0_CHANNEL,
//			.duty       = 0,
//			.gpio_num   = LEDC_HS_CH0_GPIO,
//			.speed_mode = LEDC_HS_MODE,
//			.timer_sel  = LEDC_HS_TIMER
//		},
//		{
//			.channel    = LEDC_HS_CH1_CHANNEL,
//			.duty       = 0,
//			.gpio_num   = LEDC_HS_CH1_GPIO,
//			.speed_mode = LEDC_HS_MODE,
//			.timer_sel  = LEDC_HS_TIMER
//		},
//		{
//			.channel    = LEDC_LS_CH2_CHANNEL,
//			.duty       = 0,
//			.gpio_num   = LEDC_LS_CH2_GPIO,
//			.speed_mode = LEDC_LS_MODE,
//			.timer_sel  = LEDC_LS_TIMER
//		},
//	};

	// Set LED Controller with previously prepared configuration
	for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
		ledc_channel_config(&ledc_channel[ch]);
	}

	// Initialize fade service.
//	ledc_fade_func_install(0);

	led_event_group = xEventGroupCreate();
	xEventGroupClearBits(led_event_group, ALL_BITS);
}


void led_task(void *pvParameters)
{
	int ch;
	esp_err_t err;
	EventBits_t uxBits;

	ch = LEDC_LS_CH2_CHANNEL;
	ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, ledc_channel[ch].hpoint);
	ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);

	for(;;) {
		uxBits = xEventGroupWaitBits(led_event_group, ALL_BITS, pdTRUE, pdFALSE, portMAX_DELAY);

		if (uxBits & WIFI_CONNECTED_BIT) {
			ch = LEDC_HS_CH0_CHANNEL;
			ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, ledc_channel[ch].hpoint);
			ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);
		}
		if (uxBits & WIFI_DISCONNECTED_BIT) {
			ch = LEDC_HS_CH0_CHANNEL;
			ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, 0);
			ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);
		}

	}
}
//void led_task(void* pvParameters)
//{
//	int ch;
//	while (1) {
//		printf("1. LEDC fade up to duty = %d\n", LEDC_TEST_DUTY);
//		for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
//			ledc_set_fade_with_time(ledc_channel[ch].speed_mode,
//					ledc_channel[ch].channel, LEDC_TEST_DUTY, LEDC_TEST_FADE_TIME);
//			ledc_fade_start(ledc_channel[ch].speed_mode,
//					ledc_channel[ch].channel, LEDC_FADE_NO_WAIT);
//		}
//		vTaskDelay(LEDC_TEST_FADE_TIME / portTICK_PERIOD_MS);
//
//		printf("2. LEDC fade down to duty = 0\n");
//		for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
//			ledc_set_fade_with_time(ledc_channel[ch].speed_mode,
//					ledc_channel[ch].channel, 0, LEDC_TEST_FADE_TIME);
//			ledc_fade_start(ledc_channel[ch].speed_mode,
//					ledc_channel[ch].channel, LEDC_FADE_NO_WAIT);
//		}
//		vTaskDelay(LEDC_TEST_FADE_TIME / portTICK_PERIOD_MS);
//
//		printf("3. LEDC set duty = %d without fade\n", LEDC_TEST_DUTY);
//		for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
//			ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, LEDC_TEST_DUTY);
//			ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);
//		}
//		vTaskDelay(1000 / portTICK_PERIOD_MS);
//
//		printf("4. LEDC set duty = 0 without fade\n");
//		for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
//			ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, 0);
//			ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);
//		}
//		vTaskDelay(1000 / portTICK_PERIOD_MS);
//	}
//}
