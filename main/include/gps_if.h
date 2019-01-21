/*
 * gps_if.h
 *
 *  Created on: Jan 3, 2019
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_GPS_IF_H_
#define MAIN_INCLUDE_GPS_IF_H_

typedef struct {
	float lat;
	float lon;
	float alt;
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t min;
	uint8_t sec;
} esp_gps_t;

esp_err_t GPS_Initialize(void);
void GPS_Poll(esp_gps_t* gps);


#endif /* MAIN_INCLUDE_GPS_IF_H_ */
