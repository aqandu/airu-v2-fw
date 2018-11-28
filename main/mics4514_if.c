/*
 * mics4514_if.c
 *
 *  Created on: Nov 13, 2018
 *      Author: tombo
 */


#include "driver/adc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc_cal.h"
#include "mics4514_if.h"

#define NO_OF_SAMPLES	64
#define DEFAULT_VREF	1100 	// Use adc2_vref_to_gpio() to obtain a better estimate

static const char* TAG = "MICS4514";
static esp_adc_cal_characteristics_t *adc_chars;

static void check_efuse(void);
static void print_char_val_type(esp_adc_cal_value_t val_type);

/*
 *
 */
static void check_efuse()
{
    //Check TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        ESP_LOGI(TAG, "eFuse Two Point: Supported\n");
    } else {
    	ESP_LOGI(TAG, "eFuse Two Point: NOT supported\n");
    }

    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
    	ESP_LOGI(TAG, "eFuse Vref: Supported\n");
    } else {
    	ESP_LOGI(TAG, "eFuse Vref: NOT supported\n");
    }
}

/*
 *
 */
static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
    	ESP_LOGI(TAG, "Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
    	ESP_LOGI(TAG, "Characterized using eFuse Vref\n");
    } else {
    	ESP_LOGI(TAG, "Characterized using Default Vref\n");
    }
}

/*
 *
 */
void MICS4514_Initialize(void)
{
	esp_adc_cal_value_t val_type;

	//Check if Two Point or Vref are burned into eFuse
	check_efuse();

	adc1_config_width(ADC_WIDTH_BIT_12);
	adc1_config_channel_atten(ADC_CHANNEL_5, ADC_ATTEN_DB_11); 	// Pin 9, GPIO33
	adc1_config_channel_atten(ADC_CHANNEL_3, ADC_ATTEN_DB_11);	// Pin 5, GPIO39

	//Characterize ADC
	adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
	val_type = esp_adc_cal_characterize(ADC_UNIT_1,
										ADC_ATTEN_DB_11,
										ADC_WIDTH_BIT_12,
										DEFAULT_VREF,
										adc_chars);
	print_char_val_type(val_type);
	return;
}

/*
 *
 */
void MICS4514_Poll(uint32_t *ox_val, uint32_t *red_val)
{
	*ox_val = 0;
	*red_val = 0;

	for (int i = 0; i < NO_OF_SAMPLES; i++) {
		*ox_val  += adc1_get_raw(ADC_CHANNEL_5);
		*red_val += adc1_get_raw(ADC_CHANNEL_3);
	}
	*ox_val  /= NO_OF_SAMPLES;
	*red_val /= NO_OF_SAMPLES;

	//Convert adc_reading to voltage in mV
	*ox_val = esp_adc_cal_raw_to_voltage(*ox_val, adc_chars);
	*red_val = esp_adc_cal_raw_to_voltage(*red_val, adc_chars);
	return;
}
