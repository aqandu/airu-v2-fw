/*
 * http_file_upload.h
 *
 *  Created on: Jul 31, 2019
 *      Author: tombo
 */

#ifndef MAIN_HTTP_FILE_UPLOAD_H_
#define MAIN_HTTP_FILE_UPLOAD_H_

#include "esp_err.h"

typedef enum {
	UPLOAD_NO_ERR = 0,
	GENERIC_ESP_FAIL = -1,
	NO_SD_FILE_FOUND = -2,
	ZERO_LENGTH_FILE = -3,
}http_file_upload_errors_t;

void set_chunk_size(uint32_t chunk_size);
int http_upload_file_from_sd(const char* filename);


#endif /* MAIN_HTTP_FILE_UPLOAD_H_ */
