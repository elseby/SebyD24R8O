
#include "include/panelbuttons.h"
#include "include/channels.h"
#include "include/buzzer.h"
#include "driver/i2c_types.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "hal/gpio_types.h"
#include "httpserver.h"
#include "include/output.h"
#include "led_strip.h"
#include "led_strip_types.h"
#include <stdint.h>
#include <sys/types.h>
#include "driver/gpio.h"
#include "output.h"



#define KEY_DOWN_TIMEOUT_US	100000	// time key up from down (antibouce)

static const char *TAG = "Btn";

static led_strip_handle_t led_strip = NULL;
esp_io_expander_handle_t io_keyboard = NULL;

QueueHandle_t xQueueButtonQuery = NULL;

LedColor ledColors[NUM_OF_LEDS];

void buttonActionHandler(uint8_t idch) {
	
	ESP_LOGI(TAG, "BUTTON_PRESS %d", idch+1);
	#ifdef GPIO_BUZZER
	playBeep(1600, 15);
	#endif							
	
	// verifico se il canale è abilitato per trasmissione sul bus
	// altrimenti faccio solo il toggle del rele
	uint8_t chState = getRelaysState(idch);
	channel_setting_t ch = channels[idch];
	if(ch.type==NONE){
		// cerco se è un opposite del cover
		for(uint8_t idchup=0;idchup<NUM_OF_CHANNELS;idchup++){
			if(channels[idchup].type==COVER && channels[idchup].sets[CH_SET_INDEX_OPPOSITE_CHANNEL]==idch){
				setChannelCover(
			 		idchup,
					chState ? CH_STATE_COVER_STOP : CH_STATE_COVER_DOWN, 
					false, 
					true
				);
				return;
			}
		}
		// altrimenti faccio il toggle
		setRelays(idch, chState ? 0 : 1, chState ? ledSwitchOffColor : ledSwitchOnColor, true);
	}else if(ch.type==SWITCH){
		setChannelSwitch(
			idch,
			chState ? CH_STATE_SWITCH_OFF: CH_STATE_SWITCH_ON,
			true,
			false,
			ch.flags & CH_FLAG_BIT_SLAVE ? false : true
		);
	}else if(ch.type==COVER){
		setChannelCover(
			idch,
			chState ? CH_STATE_COVER_STOP : CH_STATE_COVER_UP,
			false, 
			true
		);
	}
}

static void IRAM_ATTR gpio_keyboard_isr_handler(void *arg) { 
	// disable interrupt
	gpio_intr_disable(GPIO_INTTCA9555);	
	xQueueSendFromISR(xQueueButtonQuery, NULL, 0);
}

void buttonQueryTask(void *arg) {
	uint32_t inputRegs;
	int8_t buttonPressed=-1;
	uint64_t timeOut;
	bool webServerOn;
	for(;;){
		if (xQueueReceive(xQueueButtonQuery, NULL,(TickType_t)portMAX_DELAY)) {
			webServerOn = false;
//			ESP_LOGI(TAG, "BUTTON_PRESSED ??");
			buttonPressed=-1;
			// spazzolo le uscite per vedere quale tasto è premuto
			for(int8_t row=8 ; row<12 && buttonPressed<0 ; row++){
				// imposto una uscita bassa alla volta
				ESP_ERROR_CHECK(io_keyboard->write_output_reg(io_keyboard, 0x0f00 ^ (1ULL << row)));
				// leggo se qualche column è impostato
				ESP_ERROR_CHECK(io_keyboard->read_input_reg(io_keyboard, &inputRegs));
				inputRegs = (~inputRegs) & 0xff;

				// se sono premuti i tasti 25-32 attivo l'httpserver
				if(row==11 && inputRegs & 0x81){
					// attendo 100ms per vedere se sono premuti tutti e due
					timeOut = esp_timer_get_time()+100000;
					do{
						vTaskDelay(10 / portTICK_PERIOD_MS);
						//leggo la porta
						ESP_ERROR_CHECK(io_keyboard->read_input_reg(io_keyboard, &inputRegs));
						inputRegs = (~inputRegs) & 0xff;
						
//						ESP_LOGI(TAG, "inreg %x",inputRegs);
						
						if(inputRegs == 0x81){
							webServerOn = true;
							ESP_LOGI(TAG, "HTTP SERVER ENABLED");		
							wifiToggle();
							vTaskDelay(500 / portTICK_PERIOD_MS);
						}
					}while(timeOut > esp_timer_get_time());
				}
				
				if(!webServerOn && (inputRegs & 0x00ff)){
					// c'e' un tasto premuto rilevo un'unica colonna
					for(u_int8_t i=0 ; i<8 && buttonPressed<0 ; i++){
						if(inputRegs & (1ULL<<i)){
							buttonPressed = (8*(row-8))+i;
						}
					}
				}
			}
			// reimposto tutte le uscite a 0
			ESP_ERROR_CHECK(io_keyboard->write_output_reg(io_keyboard, 0x0000));
			if(!webServerOn && buttonPressed>=0){
				// routine di gestione del tasto premuto
				buttonActionHandler(buttonPressed);
			}
			// attendo che i tasti siano rilasciati per un tempo di xxx us
			timeOut = esp_timer_get_time()+KEY_DOWN_TIMEOUT_US;
			do{
				vTaskDelay(10 / portTICK_PERIOD_MS);
				//leggo la porta
				ESP_ERROR_CHECK(io_keyboard->read_input_reg(io_keyboard, &inputRegs));
				inputRegs = ~inputRegs;
				if(inputRegs & 0x00ff){
					timeOut = esp_timer_get_time()+KEY_DOWN_TIMEOUT_US;
				}
			}while(timeOut > esp_timer_get_time());
			// renable interrupt
			gpio_intr_enable(GPIO_INTTCA9555);	

		}
	}
}
    
//*******************************************************************************************/
    
void panelButtonsInit(i2c_master_bus_handle_t i2c_handle) {
	
	for(uint8_t i=0;i<NUM_OF_LEDS;i++) ledColors[i] = ledUnsetColor;
	
	// LED strip initialization with the GPIO and pixels number
	led_strip_config_t strip_config = {
		.led_model = LED_MODEL_SK6812,
		.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
		.strip_gpio_num =led_strip_gpio,
		.max_leds = NUM_OF_LEDS,
	};
	led_strip_rmt_config_t rmt_config = {
		.mem_block_symbols = 64,
		.resolution_hz = 10000000, // 10MHz
		.flags.with_dma = false,
	};
	ESP_ERROR_CHECK(
		led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));	
	
	// configurazione driver tastiera
    esp_io_expander_new_i2c_tca95xx_16bit(i2c_handle, ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_111, &io_keyboard);	
	
	io_keyboard->write_direction_reg(io_keyboard,0x00ff);
	io_keyboard->write_output_reg(io_keyboard,0x0000);
	
	// conrfigure interrupt pin
	// configuro interrupt per l'attesa di linea libera per il tx
	// zero-initialize the config structure.
	gpio_config_t io_conf = {
		.intr_type = GPIO_INTR_LOW_LEVEL,
		.pin_bit_mask = (1ULL << GPIO_INTTCA9555),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = 1,
		.pull_down_en = 0
	};
	gpio_config(&io_conf);
	gpio_set_intr_type(GPIO_INTTCA9555, GPIO_INTR_NEGEDGE);
	gpio_install_isr_service(0);
	gpio_isr_handler_add(
		GPIO_INTTCA9555, 
		gpio_keyboard_isr_handler,
		 (void *)GPIO_INTTCA9555
	);

	gpio_intr_enable(GPIO_INTTCA9555);	
	
	// attivo il task per la gestione della matrice dei tasti
	xQueueButtonQuery = xQueueCreate(2, 0);
	xTaskCreate(buttonQueryTask,"btnQryTask",4096,NULL,1,NULL);
	
	
	
}

//*******************************************************************************************/
void setAllLed(LedColor color){
	// default led colors	
	for(int i=0;i<NUM_OF_LEDS;i++){
		ledColors[i]=color;
	}
	updateLedColors();	
}

void updateLedColors(void){
	for (size_t i = 0; i < NUM_OF_LEDS; i++) {
		led_strip_set_pixel(led_strip, i, ledColors[i].red, ledColors[i].green, ledColors[i].blue);
	}
	led_strip_refresh(led_strip);
}




