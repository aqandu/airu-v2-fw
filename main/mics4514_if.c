/*
 * mics4514_if.c
 *
 *  Created on: Nov 13, 2018
 *      Author: tombo
 */

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc_cal.h"
#include "mics4514_if.h"

#define GPIO_MICS_ENABLE	33
#define GPIO_MICS_HEATER	32
#define GPIO_OUTPUT_PIN_SEL ((1ULL << GPIO_MICS_ENABLE) | (1ULL << GPIO_MICS_HEATER))
#define NO_OF_SAMPLES		64
#define DEFAULT_VREF		1100 	// Use adc2_vref_to_gpio() to obtain a better estimate

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

void MICS4514_GPIOEnable()
{
	// SET and RESET GPIOs
	gpio_config_t io_conf;
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	gpio_config(&io_conf);
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
	adc1_config_channel_atten(ADC_CHANNEL_6, ADC_ATTEN_DB_11); 	// WROOM Pin 6 - GPIO 34 - OX - NOx
	adc1_config_channel_atten(ADC_CHANNEL_7, ADC_ATTEN_DB_11);	// WROOM Pin 7 - GPIO 35 - RE - CO

	//Characterize ADC
	adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
	val_type = esp_adc_cal_characterize(ADC_UNIT_1,
										ADC_ATTEN_DB_11,
										ADC_WIDTH_BIT_12,
										DEFAULT_VREF,
										adc_chars);
	print_char_val_type(val_type);

	MICS4514_GPIOEnable();

	MICS4514_Disable();

	return;
}

/*
 *
 */
void MICS4514_Poll(int *ox_val, int *red_val)
{
	int64_t ch6 = 0;
	int64_t ch7 = 0;

	for (int i = 0; i < NO_OF_SAMPLES; i++) {
		ch6 += adc1_get_raw(ADC_CHANNEL_6);
		ch7 += adc1_get_raw(ADC_CHANNEL_7);
	}
	ch6 /= NO_OF_SAMPLES;
	ch7 /= NO_OF_SAMPLES;

	//Convert adc_reading to voltage in mV
	*ox_val  = (int) ch6;
	*red_val = (int) ch7;
//	*ox_val  = esp_adc_cal_raw_to_voltage(ch6, adc_chars);
//	*red_val = esp_adc_cal_raw_to_voltage(ch7, adc_chars);
	return;
}

//#define GPIO_MICS_ENABLE	33
//#define GPIO_MICS_HEATER	32
void MICS4514_Enable()
{
	gpio_set_level(GPIO_MICS_ENABLE, 0);
}

void MICS4514_Disable()
{
	gpio_set_level(GPIO_MICS_ENABLE, 1);
}

void MICS4514_HeaterEnable()
{
	gpio_set_level(GPIO_MICS_HEATER, 1);
}

void MICS4514_HeaterDisable()
{
	gpio_set_level(GPIO_MICS_HEATER, 0);
}
