/*
*	pm_if.h
*	
*	Last Modified: October 6, 2018
*	 Author: Trenton Taylor
*
*/
#ifndef _PM_IF_H
#define _PM_IF_H

#include "freertos/queue.h"
#include "esp_err.h"

#define PM_UART_CH   UART_NUM_2
#define PM_RXD_PIN   16
#define PM_TXD_PIN   17
#define BUF_SIZE     144 // NOTE: Rx_buffer_size should be greater than UART_FIFO_LEN (128 bytes)
#define PM_PKT_LEN   24
#define MAX_PKTS_IN_BUFFER 6
#define MAX_NUM_PKT  5
#define TIMEOUT      50
#define PKT_PM1_HIGH 4
#define PKT_PM1_LOW  5
#define PKT_PM2_5_HIGH  6
#define PKT_PM2_5_LOW   7
#define PKT_PM10_HIGH   8
#define PKT_PM10_LOW    9
//#define PM_SET_PIN    X
//#define PM_RESET_PIN  X


/*
* @brief PM data struct
*
* Samples of three different sizes of particles will be recorded 
* represensted as PM1, PM2.5, PM10 in the documentaion.
* PM sensor data packets are defined as follows:
*
* PM Data is transmitted over UART in 24 byte packets. The first two 
* bytes are the packet header [0x42 0x4D] or [“BM”] in ASCII. Each piece 
* of the packet is 2 bytes, with the Most Significant Byte transmitted 
* first. The final two bytes are the packet checksum and represent a 16 
* bit (2 byte) number. This number should equal the sum of the first 22 
* bytes in the packet.
*
* Refer to PMS3003 documentation for more details.
*/
typedef struct 
{
  uint32_t sample_count;    // Number of valid data points recieved
  float pm1;             // Most recent PM1 samples
  float pm2_5;           // Most recent PM2.5 samples
  float pm10;            // Most recent PM10 samples
} pm_data_t;


/*
* @brief
*
* @param
*
* @return
*
*/
esp_err_t PMS_Initialize();

/*
* @brief
*
* @param
*
* @return
*
*/
//esp_err_t PM_get_data();

esp_err_t PMS_Poll(pm_data_t *dat);

void PMS_RESET(uint32_t level);
void PMS_SET(uint32_t level);
void PMS_Disable(void);
void PMS_Enable(void);
uint8_t PMS_Active( void );
esp_err_t PMS_WaitForData(pm_data_t *dat);
esp_err_t PMS_Sleep(void);



#endif /*_PM_IF_H*/

