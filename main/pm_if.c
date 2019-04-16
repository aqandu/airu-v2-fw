/*
*	pm_if.c
*	
*	Last Modified: October 6, 2018
*	 Author: Trenton Taylor
*
*/

/*
*   To do:
*
* -  add a timer that clears the packet buffer every so often and maybe when you get data.
* -  finish reset function (need gpios set up)
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "soc/rtc.h"
#include "esp_log.h"
#include "pm_if.h"

#define GPIO_PM_RESET	17
#define GPIO_PM_SET		5
#define GPIO_OUTPUT_PIN_SEL ((1ULL << GPIO_PM_RESET) | (1ULL << GPIO_PM_SET))
#define PM_TIMER_TIMEOUT_MS 5000
#define PM_WAIT_FOR_VALID_DATA BIT0

static const char* TAG = "PM";

static void _pm_accum_rst(void);
static esp_err_t get_packet_from_buffer(void);
static esp_err_t get_data_from_packet(uint8_t *packet);
static uint8_t pm_checksum();
static void uart_pm_event_mgr(void *pvParameters);
static void vTimerCallback(TimerHandle_t xTimer);

/* Global variables */
static QueueHandle_t pm_event_queue;
static TimerHandle_t pm_timer;
static pm_data_t pm_accum;
static uint8_t pm_buf[BUF_SIZE];

static volatile unsigned long long valid_sample_count = 0;

EventGroupHandle_t pm_event_group = NULL;
EventBits_t uxBits;

/*
 * @brief 	PM data timer callback. If no valid PM data is received
 * 			for PM_TIMER_TIMEOUT_MS then we clear out the pm data accumulator
 * 			to ensure we don't use old stagnant data.
 *
 * @param 	xTimer - the timer handle
 *
 * @return 	N/A
 */
static void vTimerCallback(TimerHandle_t xTimer)
{
	ESP_LOGI(TAG, "PM Timer Timeout -- Resetting PM Sample Accumulator");
	xTimerStop(xTimer, 0);
	_pm_accum_rst();
}

/*
 * @brief	Reset the pm accumulator struct
 *
 * @param
 *
 * @return
 */
static void _pm_accum_rst()
{
	pm_accum.pm1   = 0;
	pm_accum.pm2_5 = 0;
	pm_accum.pm10  = 0;
	pm_accum.sample_count = 0;
}

/*
* @brief
*
* @param
*
* @return
*
*/
esp_err_t PMS_Initialize()
{
  esp_err_t err = ESP_FAIL;

  // configure parameters of the UART driver
  uart_config_t uart_config = 
  {
    .baud_rate = 9600,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
  };
  err = uart_param_config(PM_UART_CH, &uart_config);
  if(err != ESP_OK)
  		return err;

  // set UART pins
  err = uart_set_pin(PM_UART_CH, PM_TXD_PIN, PM_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if(err != ESP_OK)
  		return err;

  // install UART driver
  err = uart_driver_install(PM_UART_CH, BUF_SIZE, 0, 20, &pm_event_queue, 0);
  if(err != ESP_OK)
  		return err;

  // create a task to handler UART event from ISR for the PM sensor
  xTaskCreate(uart_pm_event_mgr, "vPM_task", 2048, NULL, 12, NULL);

  // create the timer to determine validity of pm data
  pm_timer = xTimerCreate("pm_timer",
						  (PM_TIMER_TIMEOUT_MS / portTICK_PERIOD_MS),
						  pdFALSE, (void *)NULL,
						  vTimerCallback);

  // clear out the pm data accumulator
  _pm_accum_rst();

  // SET and RESET GPIOs
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
  io_conf.pull_down_en = 0;
  io_conf.pull_up_en = 0;
  gpio_config(&io_conf);

  PMS_SET(1);
  PMS_RESET(1);

  // start the first timer
  xTimerStart(pm_timer, 0);

  return err;
}

/*
* @brief
*
* @param
*
* @return
*
*/
void PMS_RESET(uint32_t level)
{
  gpio_set_level(GPIO_PM_RESET, level);
}


/*
* @brief
*
* @param
*
* @return
*
*/
void PMS_SET(uint32_t level)
{
  gpio_set_level(GPIO_PM_SET, level);
}


uint8_t PMS_Active()
{
	return gpio_get_level(GPIO_PM_RESET);
}


void PMS_Disable()
{
	PMS_SET(0);
	PMS_RESET(0);
//	rtc_gpio_hold_en(GPIO_PM_SET);
//	rtc_gpio_hold_en(GPIO_PM_RESET);
}

void PMS_Enable()
{
	PMS_SET(1);
	PMS_RESET(1);
//	rtc_gpio_hold_en(GPIO_PM_SET);
//	rtc_gpio_hold_en(GPIO_PM_RESET);
}

esp_err_t PMS_Poll(pm_data_t *dat)
{
	ESP_LOGI(TAG, "Polling PMS data now...");
	if(pm_accum.sample_count == 0) {
		dat->pm1   = -1;
		dat->pm2_5 = -1;
		dat->pm10  = -1;
		return ESP_FAIL;
	}

	dat->pm1   = pm_accum.pm1   / pm_accum.sample_count;
	dat->pm2_5 = pm_accum.pm2_5 / pm_accum.sample_count;
	dat->pm10  = pm_accum.pm10  / pm_accum.sample_count;

	_pm_accum_rst();

	return ESP_OK;
}


/**
 * @brief
 *
 * @param
 *
 * @return
 *
 */
esp_err_t PMS_WaitForData(pm_data_t *dat)
{
	// Start the mutex used to handle waiting for valid data
	ESP_LOGI(TAG, "PMS_WaitForData Called...");
	pm_event_group = xEventGroupCreate();
	ESP_LOGI(TAG, "PMS_WaitForData waiting for valid PM data...");
	xEventGroupWaitBits(pm_event_group, PM_WAIT_FOR_VALID_DATA, pdFALSE, pdTRUE, portMAX_DELAY);
	ESP_LOGI(TAG, "PMS_WaitForData retreived valid data flag...");
	vEventGroupDelete(pm_event_group);
	PMS_Poll(dat);
	return ESP_OK;
}



/**
 * @brief
 *
 * @param
 *
 * @return
 *
 */
esp_err_t PMS_Sleep()
{

	if (CONFIG_SLEEP_TIME_SEC < 120) {
		PMS_SET(0);
		PMS_RESET(1);
		ESP_LOGI(TAG, "Sleeping PM Sensor with SET=0, RST=1");
	}
	else {
		PMS_SET(1);
		PMS_RESET(0);
		ESP_LOGI(TAG, "Sleeping PM Sensor with SET=1, RST=0");
	}

	ESP_LOGI(TAG, "Locking PM SET/RESET States...");
	rtc_gpio_hold_en(GPIO_PM_SET);
	rtc_gpio_hold_en(GPIO_PM_RESET);

	return ESP_OK;
}


/*
* @brief
*
* @param
*
* @return
*
*/
static void uart_pm_event_mgr(void *pvParameters)
{
  uart_event_t event;

  for(;;) 
  {
    //Waiting for UART event.
    if(xQueueReceive(pm_event_queue, (void * )&event, (portTickType)portMAX_DELAY))
    {
      switch(event.type) 
      {
        case UART_DATA:
          if(event.size == 24) 
          {
            uart_read_bytes(PM_UART_CH, pm_buf, event.size, portMAX_DELAY);
            get_packet_from_buffer();
          }
          uart_flush_input(PM_UART_CH);
          break;

        case UART_FIFO_OVF:
          ESP_LOGI(TAG, "hw fifo overflow");
          uart_flush_input(PM_UART_CH);
          xQueueReset(pm_event_queue);
          break;
                
        case UART_BUFFER_FULL:
          ESP_LOGI(TAG, "ring buffer full");
          uart_flush_input(PM_UART_CH);
          xQueueReset(pm_event_queue);
          break;
            
        case UART_BREAK:
          ESP_LOGI(TAG, "uart rx break");
          break;
                
        case UART_PARITY_ERR:
          ESP_LOGI(TAG, "uart parity error");
          break;
                
        case UART_FRAME_ERR:
          ESP_LOGI(TAG, "uart frame error");
          break;

        default:
          ESP_LOGI(TAG, "uart event type: %d", event.type);
          break;
      }//case
    }//if
  }//for
    
  vTaskDelete(NULL);
}


/*
* @brief
*
* @param
*
* @return
*
*/
static esp_err_t get_packet_from_buffer(){
  if(pm_buf[0] == 'B' && pm_buf[1] == 'M'){
	  if(pm_checksum()){
		  valid_sample_count++;

//		  ESP_LOGI(TAG, "Valid PM Packet Received... [%llu]", valid_sample_count);
		  /**
		   * Don't start accumulating data until it is valid. If PM_RESET we need to wait
		   * 15 samples before valid data, if PM_SET we need to wait 3 samples. RESET vs SET
		   * are determined by sleep length.
		   * 	CONFIG_SLEEP_TIME_SEC < 120  --> toggle SET, RST == 1
		   * 	CONFIG_SLEEP_TIME_SEC >= 120 --> toggle RST, SET == 1
		   */
//		  if ((CONFIG_SLEEP_TIME_SEC < 120 && valid_sample_count >= 4) || valid_sample_count >= 15) {
//			  ESP_LOGI(TAG, "Valid sample threshold reached!");

		  pm_accum.pm1   += (float)((pm_buf[PKT_PM1_HIGH]   << 8) | pm_buf[PKT_PM1_LOW]);
		  pm_accum.pm2_5 += (float)((pm_buf[PKT_PM2_5_HIGH] << 8) | pm_buf[PKT_PM2_5_LOW]);
		  pm_accum.pm10  += (float)((pm_buf[PKT_PM10_HIGH]  << 8) | pm_buf[PKT_PM10_LOW]);
		  pm_accum.sample_count++;

		  ESP_LOGI(TAG, "PM1 Accum: %.2f", pm_accum.pm1);

			  // Function is waiting for this to return
//			  if (pm_event_group != NULL) {
//				  ESP_LOGI(TAG, "Setting PM Valid Data flag...");
//				  xEventGroupSetBits(pm_event_group, PM_WAIT_FOR_VALID_DATA);
//			  }
//		  }

		  // Reset timer, signaling valid data was received
		  xTimerReset(pm_timer, 0);
		  return ESP_OK;
	  }
  }
  return ESP_FAIL;
}


/*
* @brief
*
* @param
*
* @return
*
*/
static uint8_t pm_checksum()
{
	uint16_t checksum;
	uint16_t sum = 0;
	uint16_t i;

	checksum = ((uint16_t) pm_buf[PM_PKT_LEN-2]) << 8;
	checksum += (uint16_t) pm_buf[PM_PKT_LEN-1];

	for(i = 0; i < PM_PKT_LEN-2 ; i++)
		sum += pm_buf[i];

	return (sum == checksum);
}
