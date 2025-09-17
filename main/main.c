#include "busrtx.h"
#include "esp_app_desc.h"
#include "include/channels.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "include/output.h"
#include "include/busrtx.h"
#include "include/panelbuttons.h"
#include "include/httpserver.h"
#include "freertos/idf_additions.h"
#include "portmacro.h"
#include <stdbool.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "include/buzzer.h"

void app_main(void) {

	initNvs();
	
	// init I2C
    i2c_master_bus_handle_t i2c_handle = NULL;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_I2C_SDA,
        .scl_io_num = GPIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };
    i2c_new_master_bus(&bus_config, &i2c_handle);	
	
	panelButtonsInit(i2c_handle);
	
	outputInit(i2c_handle);

	initChannels();

	#ifdef GPIO_BUZZER
	initBuzzer();
	#endif
	
	initApWebServer(false);	

	// led init sequence
	for(int i=0;i<=NUM_OF_LEDS;i++){
		if(i>0){
			ledColors[i-1].red = 0;
			ledColors[i-1].green = 0;
			ledColors[i-1].blue = 0;
		}
		if(i<NUM_OF_LEDS){
			ledColors[i].red = 30;
			ledColors[i].green = 30;
			ledColors[i].blue = 30;
		}
		updateLedColors();
		vTaskDelay(30 / portTICK_PERIOD_MS);
	}

	hibus_config_t busConfig = {
		.gpio_rx_port = GPIO_HIBUS_RX_PIN,
		.gpio_tx_port = GPIO_HIBUS_TX_PIN,
		.rx_handler = &rxDataDecoder
	};

	hibusInit(&busConfig);

	const esp_app_desc_t* app = esp_app_get_description();

	printf("SebyD Ready! App:%s ver:%s date:%s %s \n",app->project_name,app->version,app->date,app->time);

	// imposto stato led
	LedColor chSetColor[] = {ledUnsetColor,ledSwitchOffColor,ledCoverStopColor,ledThermoOffColor};
	for(int i=0;i<NUM_OF_CHANNELS;i++){
		ledColors[i] = chSetColor[channels[i].type];
	}	
	updateLedColors();

	#ifdef GPIO_BUZZER
	playBeep(1200, 20);
	#endif

	vTaskDelete(NULL);

}

