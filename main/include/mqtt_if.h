/*
 * mqtt_if.h
 *
 *  Created on: Oct 7, 2018
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_MQTT_IF_H_
#define MAIN_INCLUDE_MQTT_IF_H_

/*
* @brief
*
* @param
*
* @return
*/
void mqtt_initialize(void);


/*
* @brief
*
* @param
*
* @return
*/
void mqtt_publish(const char* topic, const char* msg);

#endif /* MAIN_INCLUDE_MQTT_IF_H_ */
