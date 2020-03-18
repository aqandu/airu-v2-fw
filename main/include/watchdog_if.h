/*
 * watchdog_if.h
 *
 *  Created on: Dec 11, 2019
 *      Author: sgale
 */

#ifndef MAIN_INCLUDE_WATCHDOG_IF_H_
#define MAIN_INCLUDE_WATCHDOG_IF_H_

// #define ONE_HOUR 3600
#define FIFTY_FIVE_MINUTES 3300		// 55 minutes


void watchdog_task(void *pvParameters);



#endif /* MAIN_INCLUDE_WATCHDOG_IF_H_ */
