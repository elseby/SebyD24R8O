/*
 * buzzer.c
 *
 *  Created on: 9 ago 2025
 *      Author: elseby
 */

#include "config.h"
#include "hal/ledc_types.h"

#ifdef GPIO_BUZZER
//------------------------------------------------------------------------
#include <stdint.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_timer.h"


#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          GPIO_BUZZER // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (4096) // Set duty to 50%. (2 ** 13) * 50% = 4096
//#define LEDC_FREQUENCY          (1000) // Frequency in Hertz. Set frequency at 4 kHz

static esp_timer_handle_t beep_timer_handle = NULL;

void beep_timer_callback(void* arg) {
	ledc_stop(LEDC_MODE,LEDC_CHANNEL,0);
}

void initBuzzer(){
		// create timer to wake up task queue
	const esp_timer_create_args_t beep_timer_args = {
		.callback = beep_timer_callback,
		.arg = NULL,
		.name = "beeptimer"
	};
	ESP_ERROR_CHECK(esp_timer_create(&beep_timer_args, &beep_timer_handle));
	
//    // Prepare and then apply the LEDC PWM timer configuration
//    ledc_timer_config_t ledc_timer = {
//        .speed_mode       = LEDC_MODE,
//        .duty_resolution  = LEDC_DUTY_RES,
//        .timer_num        = LEDC_TIMER,
//        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 4 kHz
//        .clk_cfg          = LEDC_AUTO_CLK
//    };
//    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
//
//    // Prepare and then apply the LEDC PWM channel configuration
//    ledc_channel_config_t ledc_channel = {
//        .speed_mode     = LEDC_MODE,
//        .channel        = LEDC_CHANNEL,
//        .timer_sel      = LEDC_TIMER,
//        .intr_type      = LEDC_INTR_DISABLE,
//        .gpio_num       = LEDC_OUTPUT_IO,
//        .duty           = 0, // Set duty to 0%
//        .hpoint         = 0
//    };
//    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));	
	
}


void playBeep(uint32_t freq, uint8_t duration_ms){
	if(!duration_ms) {
		return;
	}
	ledc_stop(LEDC_MODE,LEDC_CHANNEL,0);	
	esp_timer_stop(beep_timer_handle);
	
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = freq,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };

	// Set configuration of timer0 for high speed channels
	ledc_timer_config(&ledc_timer);
	
    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = LEDC_DUTY, // Set duty to 0%
        .hpoint         = 0
    };

	ledc_channel_config(&ledc_channel);
	
	// avvio il timer
	
	uint64_t time_us = ((uint64_t)duration_ms) * 1000;
	
	ESP_ERROR_CHECK(esp_timer_start_once(
		beep_timer_handle,
		time_us
	));	
	
}	
	


//------------------------------------------------------------------------
#endif



