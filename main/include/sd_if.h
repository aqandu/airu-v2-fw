/*
 * sd_if.h
 *
 *  Created on: Feb 24, 2019
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_SD_IF_H_
#define MAIN_INCLUDE_SD_IF_H_

#include "esp_err.h"
#include "esp_log.h"

#define SD_FILENAME_LENGTH 25
#define SD_HDR "time,ID,topic,SecActive,Altitude,Latitude,Longitude,PM1,PM2.5,PM10,Temperature,Humidity,CO,NO\n"
#define SD_PKT "%s,%s,%llu,%.2f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d\n"
#define SD_PKT_LEN 512


esp_err_t SD_Initialize(void);
esp_err_t sd_deinit(void);
esp_err_t sd_write_data(char* pkt);
vprintf_like_t esp_sd_log_write(const char* format, va_list ap);
void periodic_timer_callback(void* arg);
FILE *getLogFileInstance();
void releaseLogFileInstance();
FILE* sd_fopen(const char* filename);

#endif /* MAIN_INCLUDE_SD_IF_H_ */
