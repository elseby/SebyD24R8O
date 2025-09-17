/*
 * output.h
 *
 *  Created on: 22 apr 2025
 *      Author: elseby
 */
#pragma once

#include "driver/i2c_types.h"
#include "panelbuttons.h"
#include <stdbool.h>
#include <stdint.h>

#define TCA6424A_ADDRESS 0x22
#define I2C_TIMEOUT_MS          (1000)
#define I2C_CLK_SPEED           (400000)

#define REG_AI_PREFIX BIT(7)

#define REG_INPUT_0  0x00
#define REG_INPUT_1  0x01
#define REG_INPUT_2  0x02
#define REG_OUTPUT_0 0x04
#define REG_OUTPUT_1 0x05
#define REG_OUTPUT_2 0x06
#define REG_POL_0    0x08 
#define REG_POL_1    0x09
#define REG_POL_2    0x0a
#define REG_CONF_0   0x0c
#define REG_CONF_1   0x0d
#define REG_CONF_2   0x0e

void outputInit(i2c_master_bus_handle_t i2c_handle);
void setRelays(uint8_t relayId, uint8_t state, LedColor ledColor, bool sendToDevice);
uint8_t getRelaysState(uint8_t relayId);





