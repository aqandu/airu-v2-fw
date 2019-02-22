/*
 * ble_if.h
 *
 *  Created on: Feb 14, 2018
 *      Author: tombo
 */
#ifndef BLE_IF_H
#define BLE_IF_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"


typedef struct {
    uint8_t *prepare_buf;
    int 	prepare_len;
} prepare_type_env_t;


/*
 * @brief   
 *
 * @param   
 *
 * @return  
 */
extern void ble_wifi_cred_recv_set_event_ready();

/*
 * @brief 	
 *
 * @param 	
 *
 * @return  
 */
esp_err_t BLE_Initialize();


/*
 * @brief 	
 *
 * @param 	
 *
 * @return  
 */
void gatts_event_handler(esp_gatts_cb_event_t event, 
	esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);


/*
 * @brief 	
 *
 * @param 	
 *
 * @return  
 */
void gatts_profile_event_handler(esp_gatts_cb_event_t event, 
	esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/*
 * @brief 	
 *
 * @param 	
 *
 * @return  
 */
void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, 
	esp_ble_gatts_cb_param_t *param);

/*
 * @brief 	
 *
 * @param 	
 *
 * @return  
 */
void example_write_event_env(esp_gatt_if_t gatts_if, 
	prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);

/*
 * @brief 	
 *
 * @param 	
 *
 * @return  
 */
void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);


#endif /* BLE_IF_H */
