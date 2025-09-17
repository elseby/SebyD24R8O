#pragma once
/*
 * buzzer.h
 *
 *  Created on: 9 ago 2025
 *      Author: elseby
 */
#include "config.h"

#ifdef GPIO_BUZZER

#include <stdint.h>

void initBuzzer();
void playBeep(uint32_t freq, uint8_t duration_ms);

#endif
