/*
 * utils.h
 *
 *  Created on: 4 apr 2025
 *      Author: elseby
 */

#pragma once
#include "esp_err.h"
#include <stdint.h>

/**
Converte il dato da binario a esadecimale
*/
void btox(char *xp, uint8_t *bb, int n);

esp_err_t putCharHexToByteArray(uint8_t *byteVar, uint8_t numByte, char *hexStr);



