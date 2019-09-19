/*
 * mics4514_if.h
 *
 *  Created on: Nov 13, 2018
 *      Author: tombo
 */

#ifndef MAIN_INCLUDE_MICS4514_IF_H_
#define MAIN_INCLUDE_MICS4514_IF_H_

void MICS4514_GPIOEnable(void);
void MICS4514_Initialize(void);
void MICS4514_Poll(int *ox_val, int *red_val);
void MICS4514_Enable(void);
void MICS4514_Disable(void);
void MICS4514_HeaterEnable(void);
void MICS4514_HeaterDisable(void);


#endif /* MAIN_INCLUDE_MICS4514_IF_H_ */
