/*
 * mqtt_if.h
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_MQTT_IF_H_
#define MAIN_INCLUDE_MQTT_IF_H_

#define MQTT_PKT_LEN 256
#define DATA_WRITE_PERIOD_SEC	60

#define MQTT_ROOT_TOPIC 		"offline" // or airu
#define MQTT_DATA_PUB_TOPIC 	MQTT_ROOT_TOPIC "/influx"
#define MQTT_SUB_ALL_TOPIC		MQTT_ROOT_TOPIC "/all/v2"
#define MQTT_ACK_TOPIC_TMPLT	MQTT_ROOT_TOPIC "/ack/%s"

#define MQTT_PKT "airQuality\,ID\=%s\,SensorModel\=H2+S2\ SecActive\=%llu\,Altitude\=%.2f\,Latitude\=%.4f\,Longitude\=%.4f\,PM1\=%.2f\,PM2.5\=%.2f\,PM10\=%.2f\,Temperature\=%.2f\,Humidity\=%.2f\,CO\=%zu\,NO\=%zu"

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
void MQTT_Publish_General(const char* topic, const char* msg, int qos);

/*
* @brief
*
* @param
*
* @return
*/
void MQTT_Publish_Data(const char* msg);

/*
* @brief: Prepare data in MQTT format
*
* @param
*
* @return
*/
//void data_task();

#endif /* MAIN_INCLUDE_MQTT_IF_H_ */
