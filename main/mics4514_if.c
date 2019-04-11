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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "mics4514_if.h"
#include "led_if.h"

#define GPIO_MICS_PWR	33
#define GPIO_MICS_HTR	32
#define GPIO_OUTPUT_PIN_SEL ((1ULL << GPIO_MICS_PWR) | (1ULL << GPIO_MICS_HTR))
#define HEATER_TIMER_TIMEOUT_MS (1000UL * 60UL)
#define NO_OF_SAMPLES	64
#define DEFAULT_VREF	1100 	// Use adc2_vref_to_gpio() to obtain a better estimate

static const char* TAG = "MICS";
static esp_adc_cal_characteristics_t *adc_chars;
static TimerHandle_t heater_timer;

static void check_efuse(void);
static void print_char_val_type(esp_adc_cal_value_t val_type);
static void vTimerCallback(TimerHandle_t xTimer);


static void vTimerCallback(TimerHandle_t xTimer)
{
	ESP_LOGI(TAG, "Timer timeout reached...");
	xTimerStop(xTimer, 0);
	MICS4514_Heater(0);
}

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

void MICS4514_Enable()
{
	gpio_set_level(GPIO_MICS_PWR, 0);
	LED_SetEventBit(LED_EVENT_MICS_HEATER_ON_BIT);
}

void MICS4514_Disable()
{
	gpio_set_level(GPIO_MICS_PWR, 1);
	LED_SetEventBit(LED_EVENT_MICS_HEATER_OFF_BIT);
}

void MICS4514_Heater(uint32_t level)
{
	gpio_set_level(GPIO_MICS_HTR, !level);	// !level b/c P-FET
}

void MICS4514_HeaterTimed(uint32_t ms_on)
{
	MICS4514_Enable();
	MICS4514_Heater(1);
	xTimerChangePeriod(heater_timer, (1000 * ms_on) / portTICK_PERIOD_MS, 0);
	xTimerStart(heater_timer, 0);
}

uint8_t MIC4514_HeaterActive()
{
	return gpio_get_level(GPIO_MICS_HTR);
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

void MICS4514_GPIO_Init(void)
{
	// Set up the GPIO for power and heater
	gpio_config_t io_conf;
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	gpio_config(&io_conf);

	MICS4514_Disable();
}

/*
 *
 */
void MICS4514_Initialize(void)
{
	esp_adc_cal_value_t val_type;

	MICS4514_GPIO_Init();

	//Check if Two Point or Vref are burned into eFuse
	check_efuse();

	adc1_config_width(ADC_WIDTH_BIT_12);
	adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11); 	// Pin 6, GPIO34
	adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);		// Pin 7, GPIO35

	//Characterize ADC
	adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
	val_type = esp_adc_cal_characterize(ADC_UNIT_1,
										ADC_ATTEN_DB_11,
										ADC_WIDTH_BIT_12,
										DEFAULT_VREF,
										adc_chars);
	print_char_val_type(val_type);

	heater_timer = xTimerCreate("heater_timer",
								(HEATER_TIMER_TIMEOUT_MS / portTICK_PERIOD_MS),
								pdFALSE, (void*)NULL,
								vTimerCallback);
}

/*
 *
 */
void MICS4514_Poll(uint16_t *ox_val, uint16_t *red_val)
{
	*ox_val = 0;
	*red_val = 0;

	for (int i = 0; i < NO_OF_SAMPLES; i++) {
		*ox_val  += adc1_get_raw(ADC1_CHANNEL_6);
		*red_val += adc1_get_raw(ADC1_CHANNEL_7);
	}
	*ox_val  /= NO_OF_SAMPLES;
	*red_val /= NO_OF_SAMPLES;

	//Convert adc_reading to voltage in mV
	*ox_val = esp_adc_cal_raw_to_voltage(*ox_val, adc_chars);
	*red_val = esp_adc_cal_raw_to_voltage(*red_val, adc_chars);
}
