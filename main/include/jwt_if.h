/*
 * jwt_if.h
 *
 *  Created on: Mar 25, 2019
 *      Author: sgale
 */

#ifndef MAIN_INCLUDE_JWT_IF_H_
#define MAIN_INCLUDE_JWT_IF_H_

#include <stdint.h>
#include <stdlib.h>

static char* mbedtlsError(int);
char* createGCPJWT(const char*, uint8_t*, size_t);



#endif /* MAIN_INCLUDE_JWT_IF_H_ */
