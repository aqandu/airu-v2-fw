/*
 * time_if.h
 *
 *  Created on: Oct 8, 2018
 *      Author: tombo
 */
#include <time.h>
#include <sys/time.h>

#ifndef MAIN_INCLUDE_TIME_IF_H_
#define MAIN_INCLUDE_TIME_IF_H_


/*
* @brief
*
* @param
*
* @return
*/
time_t time_gmtime(void);


/*
* @brief
*
* @param
*
* @return
*/
void sntp_initialize(void);


/*
* @brief Set the event bit for wifi connection.
* 			This will start the SNTP lib.
*/
void sntp_wifi_connected(void);


#endif /* MAIN_INCLUDE_TIME_IF_H_ */
