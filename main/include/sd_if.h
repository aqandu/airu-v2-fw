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

esp_err_t SD_Initialize(void);
esp_err_t sd_deinit(void);
esp_err_t sd_write_data(char* pkt, uint8_t year, uint8_t month, uint8_t day);
vprintf_like_t esp_sd_log_write(const char* format, va_list ap);
void periodic_timer_callback(void* arg);
FILE *getLogFileInstance();
void releaseLogFileInstance();
FILE* sd_fopen(const char* filename);

#endif /* MAIN_INCLUDE_SD_IF_H_ */
