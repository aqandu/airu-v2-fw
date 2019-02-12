/*
 * led_if.h
 *
 *  Created on: Jan 18, 2019
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_LED_IF_H_
#define MAIN_INCLUDE_LED_IF_H_

typedef enum {
	LED_WIFI_DISCONNECTED = 0,
	LED_WIFI_CONNECTED	  = 1
}led_wifi_conn_t;

void LED_Initialize( void );
void LED_SetWifiConn(led_wifi_conn_t bit);
void led_task(void* pvParameters);


#endif /* MAIN_INCLUDE_LED_IF_H_ */
