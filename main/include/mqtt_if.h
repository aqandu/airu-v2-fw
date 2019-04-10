/*
 * mqtt_if.h
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_MQTT_IF_H_
#define MAIN_INCLUDE_MQTT_IF_H_

#define MQTT_PKT_LEN 256
#define MQTT_TOPIC_LEN 50
#define MQTT_CLIENTID_LEN 100
#define MQTT_DBG_TPC "v2/dbg"
#define MQTT_DAT_TPC "airu/offline"

/*
* @brief
*
* @param
*
* @return
*/
void MQTT_Initialize(void);

void MQTT_Reinit(void);

/*
* @brief
*
* @param
*
* @return
*/
void MQTT_wifi_connected(void);

/*
* @brief
*
* @param
*
* @return
*/
void MQTT_wifi_disconnected(void);

/*
* @brief
*
* @param
*
* @return
*/
void MQTT_Publish(const char* topic, const char* msg);

#endif /* MAIN_INCLUDE_MQTT_IF_H_ */
