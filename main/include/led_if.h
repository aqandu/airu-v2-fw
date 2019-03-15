/*
 * led_if.h
 *
 *  Created on: Jan 18, 2019
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_LED_IF_H_
#define MAIN_INCLUDE_LED_IF_H_

typedef enum {
	LED_EVENT_WIFI_DISCONNECTED_BIT = BIT0,
	LED_EVENT_WIFI_CONNECTED_BIT = BIT1,
	LED_EVENT_GPS_RTC_NOT_SET_BIT = BIT2,
	LED_EVENT_GPS_RTC_SET_BIT = BIT3,
	LED_EVENT_MICS_HEATER_ON_BIT = BIT4,
	LED_EVENT_MICS_HEATER_OFF_BIT = BIT5,
	LED_EVENT_ALL_BITS = (BIT0 | BIT1 | BIT2 | BIT3 | BIT4)
}led_events_t;


void LED_Initialize( void );
void LED_SetEventBit(led_events_t bit);
void led_task(void* pvParameters);


#endif /* MAIN_INCLUDE_LED_IF_H_ */
