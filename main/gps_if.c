/*
 * gps_if.c
 *
 *  Created on: Jan 3, 2019
 *      Author: tombo
 *
 *  Some code was taken from Limor Fried/Ladyada,
 *  from the Adafruit GPS library for Adafruit Industries.
 *
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "gps_if.h"
#include "math.h"

#define GPS_UART_NUM 		UART_NUM_1
#define GPS_TX_GPIO 		22
#define GPS_RX_GPIO 		23
#define MAX_SENTENCE_LEN 	1024
#define NMEA_RDY_BIT		BIT0

static uint8_t nmea[MAX_SENTENCE_LEN];
//static EventGroupHandle_t gps_event_group;
static QueueHandle_t gps_event_queue;

static const char* TAG = "GPS";
static esp_err_t parse(char *nmea);
static uint8_t parseHex(char c);

static esp_gps_t esp_gps = {
		.lat 	= -1,
		.lon 	= -1,
		.alt 	= -1,
		.year 	= 0,
		.month 	= 0,
		.day 	= 0,
		.hour 	= 0,
		.min 	= 0,
		.sec 	= 0
};

static void uart_gps_event_mgr(void *pvParameters)
{
    uart_event_t event;
    esp_err_t err;
    size_t buffered_size;

    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(gps_event_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            bzero(nmea, MAX_SENTENCE_LEN);
            switch(event.type) {

                case UART_DATA:
                    break;

                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "hw fifo overflow");
                    uart_flush_input(GPS_UART_NUM);
                    xQueueReset(gps_event_queue);
                    break;

                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "ring buffer full");
                    uart_flush_input(GPS_UART_NUM);
                    xQueueReset(gps_event_queue);
                    break;

                case UART_BREAK:
                    ESP_LOGW(TAG, "uart rx break");
                    break;

                case UART_PARITY_ERR:
                    ESP_LOGW(TAG, "uart parity error");
                    break;

                case UART_FRAME_ERR:
                    ESP_LOGW(TAG, "uart frame error");
                    break;

				case UART_PATTERN_DET:
					uart_get_buffered_data_len(GPS_UART_NUM, &buffered_size);
					int pos = uart_pattern_pop_pos(GPS_UART_NUM);
					if (pos != -1) {
						int read_len = uart_read_bytes(GPS_UART_NUM, nmea, pos + 1, 100 / portTICK_PERIOD_MS);
						nmea[read_len] = '\0';
						parse((char*)nmea);
					}
					else {
						uart_flush_input(GPS_UART_NUM);
					}
					break;

                default:
                    break;
            }
        }
    }
    vTaskDelete(NULL);
}

esp_err_t GPS_Initialize()
{
	esp_err_t err = ESP_FAIL;

	/* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    err = uart_param_config(GPS_UART_NUM, &uart_config);
    if(err != ESP_OK)
		return err;

    err = uart_set_pin(GPS_UART_NUM, GPS_TX_GPIO, GPS_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	if(err != ESP_OK)
		return err;

	err = uart_driver_install(GPS_UART_NUM, MAX_SENTENCE_LEN * 2, 0, 20, &gps_event_queue, 0);
	if(err != ESP_OK)
		return err;

    uart_enable_pattern_det_intr(GPS_UART_NUM, '\n', 1, 10000, 10, 10);

    /* Set pattern queue size */
    uart_pattern_queue_reset(GPS_UART_NUM, 20);
    uart_flush(GPS_UART_NUM);

	xTaskCreate(uart_gps_event_mgr, "uart_pms_event_task", 2048, NULL, 12, NULL);

	return err;
}

/*
 *	This function was taken from Limor Fried/Ladyada,
 * 	from the Adafruit GPS library for Adafruit Industries.
 */
esp_err_t parse(char *nmea) {
	uint8_t hour = 0;
	uint8_t minute = 0;
	uint8_t seconds = 0;
	uint8_t year = 0;
	uint8_t month = 0;
	uint8_t day = 0;
	uint16_t milliseconds;
	float latitude, longitude;
	int32_t latitude_fixed, longitude_fixed;
	float latitudeDegrees = 0;
	float longitudeDegrees = 0;
	float altitude = 0;
	float geoidheight;
	float speed, angle, magvariation, HDOP;
	char lat, lon, mag;
	bool fix;
	uint8_t fixquality, satellites;

	// first look if we even have one
	if (nmea[strlen(nmea)-4] == '*') {
		uint16_t sum = parseHex(nmea[strlen(nmea)-3]) * 16;
		sum += parseHex(nmea[strlen(nmea)-2]);

		// check checksum
		for (uint8_t i=2; i < (strlen(nmea)-4); i++) {
			sum ^= nmea[i];
		}
		if (sum != 0) {
		  // bad checksum :(
		  return false;
		}
	}
	int32_t degree;
	long minutes;
	char degreebuff[10];

	if (strstr(nmea, "$GPGGA")) {
		char *p = nmea;
		// get time
		p = strchr(p, ',')+1;
		float timef = atof(p);
		uint32_t time = timef;
		hour = time / 10000;
		minute = (time % 10000) / 100;
		seconds = (time % 100);
		milliseconds = (uint16_t)(fmod(timef, 1.0) * 1000);

		// parse out latitude
		p = strchr(p, ',')+1;
		if (',' != *p) {
			strncpy(degreebuff, p, 2);
			p += 2;
			degreebuff[2] = '\0';
			degree = atol(degreebuff) * 10000000;
			strncpy(degreebuff, p, 2); // minutes
			p += 3; // skip decimal point
			strncpy(degreebuff + 2, p, 4);
			degreebuff[6] = '\0';
			minutes = 50 * atol(degreebuff) / 3;
			latitude_fixed = degree + minutes;
			latitude = degree / 100000 + minutes * 0.000006F;
			latitudeDegrees = (latitude - 100 * (int)(latitude / 100)) / 60.0;
			latitudeDegrees += (int)(latitude / 100);
		}

		p = strchr(p, ',')+1;
		if (',' != *p) {
			if (p[0] == 'S') latitudeDegrees *= -1.0;
			if (p[0] == 'N') lat = 'N';
			else if (p[0] == 'S') lat = 'S';
			else if (p[0] == ',') lat = 0;
			else return ESP_FAIL;
		}

		// parse out longitude
		p = strchr(p, ',')+1;
		if (',' != *p) {
			strncpy(degreebuff, p, 3);
			p += 3;
			degreebuff[3] = '\0';
			degree = atol(degreebuff) * 10000000;
			strncpy(degreebuff, p, 2); // minutes
			p += 3; // skip decimal point
			strncpy(degreebuff + 2, p, 4);
			degreebuff[6] = '\0';
			minutes = 50 * atol(degreebuff) / 3;
			longitude_fixed = degree + minutes;
			longitude = degree / 100000 + minutes * 0.000006F;
			longitudeDegrees = (longitude - 100 * (int)(longitude / 100)) / 60.0;
			longitudeDegrees += (int)(longitude / 100);
		}

		p = strchr(p, ',')+1;
		if (',' != *p) {
			if (p[0] == 'W') longitudeDegrees *= -1.0;
			if (p[0] == 'W') lon = 'W';
			else if (p[0] == 'E') lon = 'E';
			else if (p[0] == ',') lon = 0;
			else return ESP_FAIL;
		}

		p = strchr(p, ',')+1;
		if (',' != *p) fixquality = atoi(p);
		p = strchr(p, ',')+1;
		if (',' != *p) satellites = atoi(p);
		p = strchr(p, ',')+1;
		if (',' != *p) HDOP = atof(p);
		p = strchr(p, ',')+1;
		if (',' != *p) altitude = atof(p);
		p = strchr(p, ',')+1;
		p = strchr(p, ',')+1;
		if (',' != *p) geoidheight = atof(p);

		esp_gps.alt 	= altitude;
		esp_gps.lat 	= latitudeDegrees;
		esp_gps.lon 	= longitudeDegrees;
		esp_gps.hour 	= hour;
		esp_gps.min 	= minute;
		esp_gps.sec 	= seconds;

		return ESP_OK;
	}

	if (strstr(nmea, "$GPRMC")) {
		// found RMC
		char *p = nmea;

		// get time
		p = strchr(p, ',')+1;
		float timef = atof(p);
		uint32_t time = timef;
		hour = time / 10000;
		minute = (time % 10000) / 100;
		seconds = (time % 100);
		milliseconds = fmod(timef, 1.0) * 1000;

		p = strchr(p, ',')+1;
		if (p[0] == 'A') fix = true;
		else if (p[0] == 'V') fix = false;
		else return ESP_FAIL;

		// parse out latitude
		p = strchr(p, ',')+1;
		if (',' != *p) {
			strncpy(degreebuff, p, 2);
			p += 2;
			degreebuff[2] = '\0';
			long degree = atol(degreebuff) * 10000000;
			strncpy(degreebuff, p, 2); // minutes
			p += 3; // skip decimal point
			strncpy(degreebuff + 2, p, 4);
			degreebuff[6] = '\0';
			long minutes = 50 * atol(degreebuff) / 3;
			latitude_fixed = degree + minutes;
			latitude = degree / 100000 + minutes * 0.000006F;
			latitudeDegrees = (latitude - 100 * (int)(latitude / 100)) / 60.0;
			latitudeDegrees += (int)(latitude / 100);
		}

		p = strchr(p, ',')+1;
		if (',' != *p) {
		  if (p[0] == 'S') latitudeDegrees *= -1.0;
		  if (p[0] == 'N') lat = 'N';
		  else if (p[0] == 'S') lat = 'S';
		  else if (p[0] == ',') lat = 0;
		  else return ESP_FAIL;
		}

		// parse out longitude
		p = strchr(p, ',')+1;
		if (',' != *p) {
			strncpy(degreebuff, p, 3);
			p += 3;
			degreebuff[3] = '\0';
			degree = atol(degreebuff) * 10000000;
			strncpy(degreebuff, p, 2); // minutes
			p += 3; // skip decimal point
			strncpy(degreebuff + 2, p, 4);
			degreebuff[6] = '\0';
			minutes = 50 * atol(degreebuff) / 3;
			longitude_fixed = degree + minutes;
			longitude = degree / 100000 + minutes * 0.000006F;
			longitudeDegrees = (longitude - 100 * (int)(longitude / 100)) / 60.0;
			longitudeDegrees += (int)(longitude / 100);
		}

		p = strchr(p, ',')+1;
		if (',' != *p) {
			if (p[0] == 'W') longitudeDegrees *= -1.0;
			if (p[0] == 'W') lon = 'W';
			else if (p[0] == 'E') lon = 'E';
			else if (p[0] == ',') lon = 0;
			else return ESP_FAIL;
		}
		// speed
		p = strchr(p, ',')+1;
		if (',' != *p) speed = atof(p);

		// angle
		p = strchr(p, ',')+1;
		if (',' != *p) angle = atof(p);

		p = strchr(p, ',')+1;
		if (',' != *p) {
		  uint32_t fulldate = atof(p);
		  day = fulldate / 10000;
		  month = (fulldate % 10000) / 100;
		  year = (fulldate % 100);
		}

		esp_gps.day 	= day;
		esp_gps.month 	= month;
		esp_gps.year 	= year;
		esp_gps.hour 	= hour;
		esp_gps.min 	= minute;
		esp_gps.sec 	= seconds;

		return ESP_OK;
	}

	return ESP_FAIL;
}

/*
 * Read a Hex value and return the decimal equivalent
 */
uint8_t parseHex(char c) {
	if (c <  '0') return 0;
	if (c <= '9') return c - '0';
	if (c <  'A') return 0;
	if (c <= 'F') return (c - 'A') + 10;
	return 0;
}


void GPS_Poll(esp_gps_t* gps)
{
	gps->alt = esp_gps.alt;
	gps->lat = esp_gps.lat;
	gps->lon = esp_gps.lon;
	gps->year = esp_gps.year;
	gps->month = esp_gps.month;
	gps->day = esp_gps.day;
	gps->hour = esp_gps.hour;
	gps->min = esp_gps.min;
	gps->sec = esp_gps.sec;
}
