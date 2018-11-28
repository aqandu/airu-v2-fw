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
#include "driver/uart.h"
#include "esp_log.h"
#include "pm_if.h"

#define PM_TIMER_TIMEOUT_MS 5000

static void _pm_accum_rst(void);
static esp_err_t get_packet_from_buffer(void);
static esp_err_t get_data_from_packet(uint8_t *packet);
static uint8_t pm_checksum();
static void uart_pm_manager_task(void *pvParameters);
static void vTimerCallback(TimerHandle_t xTimer);

/* Global variables */
static QueueHandle_t PM_event_queue;
static TimerHandle_t pm_timer;
static pm_data_t pm_accum;
static uint8_t pm_buf[BUF_SIZE];

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
	ESP_LOGI(TAG_PM, "PM Timer Timeout -- Resetting PM Sample Accumulator");
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

  // set UART pins
  err = uart_set_pin(PM_UART_CH, PM_TXD_PIN, PM_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  // install UART driver
  err = uart_driver_install(PM_UART_CH, BUF_SIZE, 0, 20, &PM_event_queue, 0);

  // create a task to handler UART event from ISR for the PM sensor
  xTaskCreate(uart_pm_manager_task, "vPM_task", 2048, NULL, 12, NULL);

  // create the timer to determine validity of pm data
  pm_timer = xTimerCreate("pm_timer",
						  (PM_TIMER_TIMEOUT_MS / portTICK_PERIOD_MS),
						  pdFALSE, (void *)NULL,
						  vTimerCallback);

  // clear out the pm data accumulator
  _pm_accum_rst();

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
esp_err_t PMS_Reset()
{
  /* need gpio library set up first */

	return ESP_FAIL;
}

esp_err_t PMS_Poll(pm_data_t *dat)
{
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


/*
* @brief
*
* @param
*
* @return
*
*/
static void uart_pm_manager_task(void *pvParameters)
{
  uart_event_t event;

  for(;;) 
  {
    //Waiting for UART event.
    if(xQueueReceive(PM_event_queue, (void * )&event, (portTickType)portMAX_DELAY)) 
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
          ESP_LOGI(TAG_PM, "hw fifo overflow");
          uart_flush_input(PM_UART_CH);
          xQueueReset(PM_event_queue);
          break;
                
        case UART_BUFFER_FULL:
          ESP_LOGI(TAG_PM, "ring buffer full");
          uart_flush_input(PM_UART_CH);
          xQueueReset(PM_event_queue);
          break;
            
        case UART_BREAK:
          ESP_LOGI(TAG_PM, "uart rx break");
          break;
                
        case UART_PARITY_ERR:
          ESP_LOGI(TAG_PM, "uart parity error");
          break;
                
        case UART_FRAME_ERR:
          ESP_LOGI(TAG_PM, "uart frame error");
          break;

        default:
          ESP_LOGI(TAG_PM, "uart event type: %d", event.type);
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
		  pm_accum.pm1   += (float)((pm_buf[PKT_PM1_HIGH]   << 8) | pm_buf[PKT_PM1_LOW]);
		  pm_accum.pm2_5 += (float)((pm_buf[PKT_PM2_5_HIGH] << 8) | pm_buf[PKT_PM2_5_LOW]);
		  pm_accum.pm10  += (float)((pm_buf[PKT_PM10_HIGH]  << 8) | pm_buf[PKT_PM10_LOW]);
		  pm_accum.sample_count++;
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
static esp_err_t get_data_from_packet(uint8_t *packet)
{
  uint16_t tmp;
  uint8_t tmp2;

    
  if(packet == NULL)
    return ESP_FAIL;

  // PM1 data
  tmp = packet[PKT_PM1_HIGH];
  tmp2 = packet[PKT_PM1_LOW];
  tmp = tmp << sizeof(uint8_t);
  tmp = tmp | tmp2;
  pm_accum.pm1 += tmp;

  // PM2.5 data
  tmp = packet[PKT_PM2_5_HIGH];
  tmp2 = packet[PKT_PM2_5_LOW];
  tmp = tmp << sizeof(uint8_t);
  tmp = tmp | tmp2;
  pm_accum.pm2_5 += tmp;

  // PM10 data
  tmp = packet[PKT_PM10_HIGH] << 8 ;
  tmp2 = packet[PKT_PM10_LOW];
  tmp = tmp << sizeof(uint8_t);
  tmp = tmp | tmp2;
  pm_accum.pm10 += tmp;

  pm_accum.sample_count++;

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
