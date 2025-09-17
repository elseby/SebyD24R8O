/*
 * output.c
 *
 *  Created on: 22 apr 2025
 *      Author: elseby
 */

#include "include/output.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "include/panelbuttons.h"
#include "soc/gpio_num.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdint.h>

//static const char *TAG = "output";

i2c_master_dev_handle_t tca6424handle = NULL;

uint32_t currentRelaysStatus = 0x000000; 

static const gpio_num_t auxPorts[8] = {
	GPIO_NUM_7,
	GPIO_NUM_8,
	GPIO_NUM_9,
	GPIO_NUM_10,
	GPIO_NUM_11,
	GPIO_NUM_12,
	GPIO_NUM_13,
	GPIO_NUM_14
};

typedef struct {
	uint8_t address0;
	uint8_t data[3]; 
} register_data_t;

QueueHandle_t xQueueSendOutputState = NULL;
void sendRelaysStatus();

/***********************************
*	OUTPUT TASK
************************************/
void outputTask(void *arg){
	for(;;){
		if (xQueueReceive(xQueueSendOutputState, NULL, (TickType_t)portMAX_DELAY)) {
			sendRelaysStatus();
		}
	}
}

void outputInit(i2c_master_bus_handle_t i2c_handle){
	
    // Add new I2C device
    const i2c_device_config_t i2c_dev_cfg = {
        .device_address = TCA6424A_ADDRESS,
        .scl_speed_hz = I2C_CLK_SPEED,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_handle, &i2c_dev_cfg, &tca6424handle));
	
	// pin direction
	register_data_t data = {
		.address0 = REG_CONF_0 | REG_AI_PREFIX,
		.data[0] = 0x00,
		.data[1] = 0x00,
		.data[2] = 0x00
	};

	ESP_ERROR_CHECK(
		i2c_master_transmit(tca6424handle, (uint8_t *) &data, 4, I2C_TIMEOUT_MS)
	);

	// set all output to 0
	data.address0 = REG_OUTPUT_0 | REG_AI_PREFIX;
	ESP_ERROR_CHECK(
		i2c_master_transmit(tca6424handle, (uint8_t *) &data, 4, I2C_TIMEOUT_MS)
	);

	for(uint8_t i=0;i<8;i++){
		// set GPIO aux port to output
	    gpio_set_direction(auxPorts[i], GPIO_MODE_OUTPUT);
	    gpio_set_level(auxPorts[i],  0);
	}

	xQueueSendOutputState = xQueueCreate(1, 0);

	// avvio il task per il cambio stato dei rele 
	xTaskCreate(outputTask,"outpTask",2048,NULL,1,NULL);	

}

uint8_t getRelaysState(uint8_t relayId){
	if(relayId>31){
		return 0;
	}
	return (currentRelaysStatus & (1ULL << relayId)) ? 1 : 0;
}

void setRelays(uint8_t relayId, uint8_t state, LedColor ledColor, bool sendToDevice){
	if(relayId>31){
		return;
	}
	if(state){
		currentRelaysStatus |=  (1ULL << relayId);
	}else{
		currentRelaysStatus &= 0xffffffff ^ (1ULL << relayId);
	}
	ledColors[relayId] = ledColor;
	xQueueSend(xQueueSendOutputState, NULL, 0);		
}

void sendRelaysStatus(){
	// imposto gli AUX
	for(int i=24;i<32;i++){
		// managed by Esp32 GPIO
		gpio_set_level(auxPorts[i-24], (currentRelaysStatus & (1ULL << i)) ? 1 : 0 );
	}
	// managed by TCA6424A
	register_data_t data = {
		.address0 = REG_OUTPUT_0 | REG_AI_PREFIX,
		.data[0] = (currentRelaysStatus) & 0xff,
		.data[1] = (currentRelaysStatus>>8) & 0xff,
		.data[2] = currentRelaysStatus>>16 & 0xff
	};
	ESP_ERROR_CHECK(
		i2c_master_transmit(tca6424handle, (uint8_t *) &data, 4, I2C_TIMEOUT_MS)
	);
	updateLedColors();
	
}



