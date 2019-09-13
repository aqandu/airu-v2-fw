/*
 * mqtt_if.h
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_MQTT_IF_H_
#define MAIN_INCLUDE_MQTT_IF_H_

#define MQTT_PKT_LEN 			256
#define DATA_WRITE_PERIOD_SEC	60

#define MQTT_DATA_PUB_TOPIC 	CONFIG_MQTT_ROOT_TOPIC "/" CONFIG_MQTT_DATA_PUB_TOPIC	/* I don't know how to concatonate these in kconfig file" */
#define MQTT_SUB_ALL_TOPIC		CONFIG_MQTT_ROOT_TOPIC "/" CONFIG_MQTT_SUB_ALL_TOPIC
#define MQTT_ACK_TOPIC_TMPLT	CONFIG_MQTT_ROOT_TOPIC "/ack/%s"

#define MQTT_PKT CONFIG_MQTT_MEASUREMENT_NAME "\,ID\=%s\,SensorModel\=H2+%s\ SecActive\=%llu\,"\
				 "Altitude\=%.2f\,Latitude\=%.4f\,Longitude\=%.4f\,PM1\=%.2f\,"\
				 "PM2.5\=%.2f\,PM10\=%.2f\,Temperature\=%.2f\,Humidity\=%.2f\,CO\=%zu\,NO\=%zu"

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
int MQTT_Publish_General(const char* topic, const char* msg, int qos);

/*
* @brief
*
* @param
*
* @return
*/
int MQTT_Publish_Data(const char* msg);

/*
* @brief: Prepare data in MQTT format
*
* @param
*
* @return
*/
//void data_task();

#endif /* MAIN_INCLUDE_MQTT_IF_H_ */
