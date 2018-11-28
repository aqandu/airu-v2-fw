/*
 * hdc1080_if.h
 *
 *  Created on: Nov 13, 2018
 *      Author: tombo
 */

#ifndef MAIN_HDC1080_IF_H_
#define MAIN_HDC1080_IF_H_

#include "esp_system.h"

#define I2C_MASTER_SCL_GPIO		27			/*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_GPIO		26			/*!< gpio number for I2C master data  */
#define I2C_MASTER_FREQ_HZ		100000		/*!< I2C master clock frequency */
#define HDC1080_CONF_COMB		(1<<12)		/*!< HDC Configure Read Temp & Hum in one shot */
#define HDC1080_DEV_ADDR		0x40        /*!< slave address for HDC1080 sensor */
#define HDC1080_CONF_ADDR		0x02        /*!< HDC1080 configuration register */
#define HDC1080_TEMP_REG		0x00		/*!< HDC1080 Temperature Register */
#define HDC1080_HUM_REG			0x01		/*!< HDC1080 Humidity Register */
#define ACK_CHECK_EN			0x1			/*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS			0x0			/*!< I2C master will not check ack from slave */
#define ACK_VAL					0x0			/*!< I2C ack value */
#define NACK_VAL				0x1			/*!< I2C nack value */

esp_err_t HDC1080_Initialize(void);
esp_err_t HDC1080_Poll(double *temp, double *hum);

#endif /* MAIN_HDC1080_IF_H_ */
