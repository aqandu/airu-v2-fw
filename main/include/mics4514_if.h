/*
 * mics4514_if.h
 *
 *  Created on: Nov 13, 2018
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_MICS4514_IF_H_
#define MAIN_INCLUDE_MICS4514_IF_H_

void MICS4514_Initialize(void);
void MICS4514_Poll(uint32_t *ox_val, uint32_t *red_val);


#endif /* MAIN_INCLUDE_MICS4514_IF_H_ */
