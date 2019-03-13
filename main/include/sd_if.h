/*
 * sd_if.h
 *
 *  Created on: Feb 24, 2019
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_SD_IF_H_
#define MAIN_INCLUDE_SD_IF_H_

#include "esp_err.h"

esp_err_t sd_init(void);
esp_err_t sd_deinit(void);
esp_err_t sd_write_data(char* pkt);


#endif /* MAIN_INCLUDE_SD_IF_H_ */
