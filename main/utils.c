/*
 * utils.c
 *
 *  Created on: 4 apr 2025
 *      Author: elseby
 */


/**
Converte il dato da binario a esadecimale
*/
#include "esp_err.h"
#include "esp_log.h"
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

static const char *TAG = "UTILS";

void btox(char *xp, uint8_t *bb, int n) {
	const char xx[] = "0123456789ABCDEF";
	int nbck = n;
	while (--n >= 0)
		xp[n] = xx[(bb[n >> 1] >> ((1 - (n & 1)) << 2)) & 0xF];
	xp[nbck] = 0;
}

uint8_t getNibble(uint8_t ch){
	if(ch>='0' && ch<='9'){
		return ch-'0';
	}else if(ch>='a' && ch<='f'){
		return (ch-'a')+0x0a;
	}else if(ch>='A' && ch<='F'){
		return (ch-'A')+0x0a;
	}else{
		return 0xff;
	}
}

esp_err_t putCharHexToByteArray(uint8_t *byteVar, uint8_t numByte, char *hexStr){
	uint strLen = strlen(hexStr);
	ESP_LOGI(TAG, "Hex : %s", hexStr);
	if(strLen < numByte*2){
		ESP_LOGI(TAG, "HexLen < numbyte:%u -> %s",numByte, hexStr);
		return ESP_ERR_INVALID_ARG; 
	}
	uint8_t b;
	for(uint i=0;i<numByte;i++){
		b=getNibble(hexStr[i*2]);
		if(b>0xf){
			ESP_LOGI(TAG, "Hex >0x0f: %s", hexStr);
			return ESP_ERR_INVALID_ARG;
		}
		b=b<<4;
		byteVar[i]=b;
		b=getNibble(hexStr[(i*2)+1]);
		if(b>0xf){
			ESP_LOGI(TAG, "Hex >0x0f: %s", hexStr);
			return ESP_ERR_INVALID_ARG;
		}
		byteVar[i]|=b;
		ESP_LOGI(TAG, "hex : %x %x",byteVar[i], b);
	}
	return ESP_OK;
}


