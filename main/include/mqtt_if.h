/*
 * mqtt_if.h
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_MQTT_IF_H_
#define MAIN_INCLUDE_MQTT_IF_H_

#define MQTT_PKT_LEN 256
#define MQTT_TOPIC_PREFIX "airu"
#define MQTT_DBG_TPC "v2/dbg"
#define MQTT_DAT_TPC ""MQTT_TOPIC_PREFIX"/influx"
#define MQTT_CLIENT_SUBCRIBE_TPC ""MQTT_TOPIC_PREFIX"/all/v2"


/*
* @brief
*
* @param
*
* @return
*/
void MQTT_Initialize(void);

void MQTT_Connect(void);

void mqtt_task(void*);
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
void MQTT_Publish(const char* topic, const char* msg, int qos);

/*
* @brief: Prepare data in MQTT format
*
* @param
*
* @return
*/
void data_task();

#endif /* MAIN_INCLUDE_MQTT_IF_H_ */
