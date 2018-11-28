/*
 * hdc1080_if.c
 *
 *  Created on: Nov 13, 2018
 *      Author: Thomas Becnel
 */

#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "hdc1080_if.h"
#include "driver/i2c.h"

static const char *TAG = "HDC1080";

/*
 *
 */
esp_err_t HDC1080_Initialize(void)
{
	esp_err_t ret;
	uint16_t hdc1080_conf = 0;

	i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_GPIO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_MASTER_SCL_GPIO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    hdc1080_conf |= HDC1080_CONF_COMB;				// Configure HDC1080 to read both T&H in one go

    i2c_param_config(I2C_NUM_1, &conf);
    i2c_driver_install(I2C_NUM_1, conf.mode, 0, 0, 0);

    // Write the initial configuration
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (HDC1080_DEV_ADDR << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
	i2c_master_write_byte(cmd, HDC1080_CONF_ADDR, ACK_CHECK_EN);
	i2c_master_write_byte(cmd, (hdc1080_conf >> 8), ACK_CHECK_EN);		// Send MSB
	i2c_master_write_byte(cmd, (hdc1080_conf & 0xff), ACK_CHECK_EN);	// Send LSB
	i2c_master_stop(cmd);
	ret = i2c_master_cmd_begin(I2C_NUM_1, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	if (ret != ESP_OK) {
		ESP_LOGI(TAG, "Couldn't configure HDC1080");
	}else {
		ESP_LOGI(TAG, "HDC1080 was properly configured");
	}
	return ret;
}

/*
 *
 */
esp_err_t HDC1080_Poll(double *temp, double *hum)
{
	uint8_t data[4];
    esp_err_t ret;

    // 1. Start measurement by writing Temperature Address (0x00) into Pointer Register (0x02)
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, HDC1080_DEV_ADDR << 1 | I2C_MASTER_WRITE, ACK_CHECK_EN); 	// 7-bit Serial Bus Address Byte (0x40) + WRITE BIT (0)
    i2c_master_write_byte(cmd, HDC1080_TEMP_REG, ACK_CHECK_EN);					 			// Send Temp Addr to Pointer Register Byte
    i2c_master_stop(cmd);																	// Send the Stop Bit
    ret = i2c_master_cmd_begin(I2C_NUM_1, cmd, 1000 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
		ESP_LOGW(TAG, "Couldn't start measurement");
		return ret;
	}
    i2c_cmd_link_delete(cmd);

    // 2. Wait for the measurement to complete
    vTaskDelay(100 / portTICK_RATE_MS);

    // 3. Read the data from Temperature (0x00) then Humidity (0x01)
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (HDC1080_DEV_ADDR << 1) | I2C_MASTER_READ, ACK_CHECK_EN);	// 7-bit Serial Bus Address Byte (0x40) + READ BIT (1)
    i2c_master_read_byte(cmd, &data[0], ACK_VAL);	// Read Temperature MSB and Master ACK
    i2c_master_read_byte(cmd, &data[1], ACK_VAL);	// Read Temperature LSB and Master ACK
    i2c_master_read_byte(cmd, &data[2], ACK_VAL);	// Read Humidity MSB and Master ACK
    i2c_master_read_byte(cmd, &data[3], NACK_VAL);	// Read Humidity LSB and Master NACK
    i2c_master_stop(cmd);							// Send the Stop Bit
    ret = i2c_master_cmd_begin(I2C_NUM_1, cmd, 1000 / portTICK_RATE_MS); // Send the commands
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
		ESP_LOGW(TAG, "Couldn't read measurement");
		return ret;
	}
    *temp = ((double)(data[0] * 256 + data[1]) / 0x10000) * 165 - 40;
    *hum  = ((double)(data[2] * 256 + data[3]) / 0x10000) * 100;

    return ret;
}
