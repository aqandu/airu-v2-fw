/*
 * led_if.h
 *
 *  Created on: Jan 18, 2019
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_LED_IF_H_
#define MAIN_INCLUDE_LED_IF_H_

#include "driver/ledc.h"

#define STAT1_LED 21	/* RED 	 */
#define STAT2_LED 19	/* GREEN */
#define STAT3_LED 18	/* BLUE  */

#define STAT1_CH	LEDC_CHANNEL_0
#define STAT2_CH	LEDC_CHANNEL_1
#define STAT3_CH	LEDC_CHANNEL_2

typedef enum {
	LED_EVENT_WIFI_DISCONNECTED_BIT = BIT0,
	LED_EVENT_WIFI_CONNECTED_BIT = BIT1,
	LED_EVENT_GPS_RTC_NOT_SET_BIT = BIT2,
	LED_EVENT_GPS_RTC_SET_BIT = BIT3,
	LED_EVENT_ALL_BITS = (BIT0 | BIT1 | BIT2 | BIT3)
}led_events_t;


void LED_Initialize( void );
void LED_SetEventBit(led_events_t bit);
void led_task(void* pvParameters);
void LED_Set(int ch, uint8_t level);


#endif /* MAIN_INCLUDE_LED_IF_H_ */
