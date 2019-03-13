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
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "pm_if.h"
#include "sd_if.h"

#define GPIO_PM_RESET	17
#define GPIO_PM_SET		5
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_PM_RESET) | (1ULL<<GPIO_PM_SET))

#define PM_DATA_IN BIT0
#define PM_TIMER_TIMEOUT_MS 5000

static const char* TAG = "PM";
static const char *DATA_PKT = "%li,%s,%.2f,%.2f,%.2f\n";	/* timestamp, MAC, PM1, PM2.5, PM10 */

static void _pm_accum_rst(void);
static esp_err_t _get_packet_from_buffer(void);
static esp_err_t _get_data_from_packet(uint8_t *packet);
static uint8_t _pm_checksum();
static void uart_pm_event_mgr(void *pvParameters);
static void vTimerCallback(TimerHandle_t xTimer);

/* Global variables */
static QueueHandle_t pm_event_queue;
static TimerHandle_t pm_timer;
static pm_data_t pm_accum;
static uint8_t pm_buf[BUF_SIZE];
EventGroupHandle_t pm_event_group;
EventBits_t uxBits;
static char DEVICE_MAC[13];

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


void pm_sd_task(void *pvParameters) {
	char packet[100];

	pm_event_group = xEventGroupCreate();
	time_t posix;

	for (;;) {
		ESP_LOGI(TAG, "Waiting for PM data...");
		uxBits = xEventGroupWaitBits(pm_event_group, PM_DATA_IN, pdTRUE, pdTRUE, portMAX_DELAY);
		if (uxBits & PM_DATA_IN){
			_get_packet_from_buffer();
			_pm_accum_rst();
//			ESP_LOGI(TAG, "PM: (%.2f, %.2f, %.2f)", pm_accum.pm1, pm_accum.pm2_5, pm_accum.pm10);
			time(&posix);
			sprintf(packet, DATA_PKT, posix, DEVICE_MAC, pm_accum.pm1, pm_accum.pm2_5, pm_accum.pm10);
			sd_write_data(packet);
		}
	}

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

  // Config the SET/RESET GPIO
  gpio_config_t io_conf;
  // disable interrupt
  io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
  // set as output
  io_conf.mode = GPIO_MODE_OUTPUT;
  // bit mask of the pins
  io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
  // disable pull-down mode
  io_conf.pull_down_en = 0;
  // disable pull-up mode
  io_conf.pull_up_en = 0;
  // configure GPIO with the given settings
  gpio_config(&io_conf);

  PMS_SET(1);
  PMS_RESET(1);

  // create a task to handler UART event from ISR for the PM sensor
  xTaskCreate(uart_pm_event_mgr, "vPM_task", 2048, NULL, 12, NULL);

  // create the timer to determine validity of pm data
  pm_timer = xTimerCreate("pm_timer",
						  (PM_TIMER_TIMEOUT_MS / portTICK_PERIOD_MS),
						  pdFALSE, (void *)NULL,
						  vTimerCallback);

  // clear out the pm data accumulator
  _pm_accum_rst();

  // start the first timer
  xTimerStart(pm_timer, 0);

  xTaskCreate(&pm_sd_task, "data_task", 4096, NULL, 1, &pm_sd_task);
  uint8_t tmp[6];
  esp_efuse_mac_get_default(tmp);
  sprintf(DEVICE_MAC, "%02X%02X%02X%02X%02X%02X", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
  ESP_LOGI(TAG, "\nMAC Address: %s\n", DEVICE_MAC);

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

void PMS_SET(uint32_t level)
{
	gpio_set_level(GPIO_PM_SET, level);
}

uint8_t PMS_Active()
{
	return gpio_get_level(GPIO_PM_RESET);
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
//            _get_packet_from_buffer();
            xEventGroupSetBits(pm_event_group, PM_DATA_IN);
//            ESP_LOGI(TAG, "Event MGR set PM_DATA_IN...");
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
static esp_err_t _get_packet_from_buffer(){
  if(pm_buf[0] == 'B' && pm_buf[1] == 'M'){
	  if(_pm_checksum()){
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
static esp_err_t _get_data_from_packet(uint8_t *packet)
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
static uint8_t _pm_checksum()
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


