/*
 * http_file_upload.h
 *
 *  Created on: Jul 31, 2019
 *      Author: tombo
 */

#ifndef MAIN_HTTP_FILE_UPLOAD_H_
#define MAIN_HTTP_FILE_UPLOAD_H_

#include "esp_err.h"

void set_chunk_size(uint32_t chunk_size);
esp_err_t http_upload_file_from_sd(const char* filename);


#endif /* MAIN_HTTP_FILE_UPLOAD_H_ */
