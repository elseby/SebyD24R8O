#include "include/channels.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "include/output.h"
#include "lwip/err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "include/output.h"
#include "panelbuttons.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "Chan";
static const char * CHANNEL_NVS_KEY = "channels";

/***********************************
*	SPAZIO RAM CANALI
************************************/
static const channel_setting_t DEFAULT_CH_SETTING = {.type=NONE};
channel_setting_t channels[NUM_OF_CHANNELS];
uint64_t ch_next_check_time[NUM_OF_CHANNELS];
uint8_t ch_active_command[NUM_OF_CHANNELS];

// alloco i bit per lo stato zone degli attuatori pompa
uint64_t ch_pump_act_zonestatus[NUM_OF_CHANNELS][2];

/***********************************/

void sendChannelCurrentState(uint8_t idchan){
//	uint8_t chState = getRelaysState(idchan);
	uint8_t data[4] = {0xb8,channels[idchan].where[0],0x12,ch_active_command[idchan] };
	switch(channels[idchan].type){
		case SWITCH:
			if(ch_active_command[idchan]==0xff){
				ch_active_command[idchan]=CH_STATE_SWITCH_OFF;
			}
			data[3] = ch_active_command[idchan];
			// B8 85 12 01   off
			// B8 85 12 00   on
			hibusTxData(data, 4, TX_MODE_STATUS_X3);
			break;
		case COVER:
			if(ch_active_command[idchan]==0xff){
				ch_active_command[idchan]=CH_STATE_COVER_STOP;
			}
			data[3] = ch_active_command[idchan];
			//B8 wh 12 0A stop
			//B8 wh 12 08 up
			//B8 wh 12 09 down
//			uint8_t chOppState = getRelaysState(channels[idchan].sets[CH_SET_INDEX_OPPOSITE_CHANNEL]);	// opposite channel
//			if(chState){
//				data[3] = CH_STATE_COVER_UP;
//			}else if(chOppState){
//				data[3] = CH_STATE_COVER_DOWN;
//			}else{
//				data[3] = CH_STATE_COVER_STOP;
//			}
			hibusTxData(data, 4, TX_MODE_STATUS_X3);
			break;
		case THERMO:
//			risposta stato attuatore
//			B7 [80+Z] [30+N] [01-off,00-on]
			if(ch_active_command[idchan]==0xff){
				ch_active_command[idchan]=CH_STATE_THERMO_OFF;
			}
			data[0] = 0xB7;
			data[1] = channels[idchan].where[0];
			data[2] = channels[idchan].where[1];
			data[3] = ch_active_command[idchan];
			hibusTxData(data, 4, TX_MODE_STATUS_X3);
			break;
		case NONE:
//		default:
			break;
	}
}

/***********************************
*	CHANNEL ACTIONS
************************************/
static uint32_t SWITCH_FIXED_TIMES[]={
	1*60*1000000,
	2*60*1000000,
	3*60*1000000,
	4*60*1000000,
	5*60*1000000,
	15*60*1000000,
	30*1000000,
	500000,
};
static uint32_t BLINK_TIMES[]={
	500*1000,	//500ms
	1000*1000,	//1sec
	1500*1000,	//1,5sec
	2000*1000,
	2500*1000,
	3000*1000,
	3500*1000,
	4000*1000,
	4500*1000,
	5000*1000,	//5sec
};

void setChannelSwitch(uint8_t idchan, uint8_t cmd, bool ignoreRetain, bool sendAck, bool sendStatus){
	if(channels[idchan].type!=SWITCH){
		return;
	}
	uint64_t now = esp_timer_get_time();
	uint64_t ms = 1000;
	if(cmd==CH_STATE_SWITCH_ON){			// ON
		// accensione
		if(sendAck) hibusTxAck();
		setRelays(idchan, 1, ledSwitchOnColor, true);
		ch_next_check_time[idchan] = channels[idchan].max_on_time ? (now + (ms*channels[idchan].max_on_time)) : // altrimenti se c'e' il massimo nel canale 
			0; // tempo max 0 
		ch_active_command[idchan] = cmd;
//		if(sendAck) hibusTxAck();
		if(sendStatus) sendChannelCurrentState(idchan);
	}else if(cmd==CH_STATE_SWITCH_OFF){	//OFF
		// spegnimento
		if(sendAck) hibusTxAck();
		if(getRelaysState(idchan) && !ignoreRetain && channels[idchan].retain_time>0){
			ch_next_check_time[idchan] = now+(ms*channels[idchan].retain_time); // spegnimento tra... 
		}else{
			setRelays(idchan, 0, ledSwitchOffColor, true);
			ch_next_check_time[idchan] = 0;
		}
		ch_active_command[idchan] = cmd;
//		if(sendAck) hibusTxAck();
		if(sendStatus) sendChannelCurrentState(idchan);
	}else if((cmd & 0x0f)==0x06){
		// accensione temporizzata
		//"16",ot+"1 Min",
		//"26",ot+"2 Min",
		//"36",ot+"3 Min",
		//"46",ot+"4 Min",
		//"56",ot+"5 Min",
		//"66",ot+"15 Min",
		//"76",ot+"30 Sec",
		//"86",ot+"0.5 Sec",				
		if(sendAck) hibusTxAck();
		setRelays(idchan, 1, ledSwitchOnColor, true);	
		ch_next_check_time[idchan] = now + SWITCH_FIXED_TIMES[((cmd>>4) & 0xf)-1];
		ch_active_command[idchan] = cmd;
		if(sendStatus) sendChannelCurrentState(idchan);
	}else if((cmd & 0x0f)==0x0b){
		// lampeggio
		//"0b",b+"0.5 sec",
		//"1b",b+"1 sec",
		//"2b",b+"1.5 sec",
		//"3b",b+"2 sec",
		//"4b",b+"2.5 sec",
		//"5b",b+"3 sec",
		//"6b",b+"3.5 sec",
		//"7b",b+"4 sec",
		//"8b",b+"4.5 sec",
		//"9b",b+"5 sec"				
		if(sendAck) hibusTxAck();
		setRelays(idchan, 1, ledSwitchOnColor, true);	
		ch_next_check_time[idchan] = now + BLINK_TIMES[(cmd>>4) & 0xf];
		ch_active_command[idchan] = cmd;
		if(sendStatus) sendChannelCurrentState(idchan);
	}
}

void setChannelCover(uint8_t idchan, uint8_t cmd, bool sendAck, bool sendStatus){
	if(channels[idchan].type!=COVER){
		return;
	}
	uint8_t idOppCh = channels[idchan].sets[CH_SET_INDEX_OPPOSITE_CHANNEL];
	switch (cmd) {
		case CH_STATE_COVER_STOP:
			if(sendAck) hibusTxAck();
			ch_next_check_time[idchan]=0;	// resetto il tempo 
			//spegnimento dei due rele
			setRelays(idchan, 0, ledCoverStopColor, false);
			setRelays(idOppCh, 0, ledCoverStopColor, true);
			ch_active_command[idchan] = cmd;
			if(sendStatus) sendChannelCurrentState(idchan);
			break;
		case CH_STATE_COVER_UP:
		case CH_STATE_COVER_DOWN:
			if(sendAck) hibusTxAck();
			if(cmd==CH_STATE_COVER_UP){
				setRelays(idchan, 1, ledCoverActiveColor, false);
				setRelays(idOppCh, 0, ledCoverInactiveOppColor, true);
			}else{
				setRelays(idchan, 0, ledCoverInactiveOppColor, false);
				setRelays(idOppCh, 1, ledCoverActiveColor, true);
			}
			uint64_t now = esp_timer_get_time();
			uint64_t ms = 1000;
			ch_next_check_time[idchan]=now + (ms*(channels[idchan].max_on_time ? channels[idchan].max_on_time : 60000));	// resetto il tempo di accensione
			ch_active_command[idchan] = cmd;
			if(sendStatus) sendChannelCurrentState(idchan);
			break;
	}
}

void setChannelThermo(uint8_t idchan, uint8_t zoneId, uint8_t cmd, bool sendStatus){
	if(channels[idchan].type!=THERMO){
		return;
	}
	ch_next_check_time[idchan]=0;
	if(zoneId>0){
		//attuatore pompa
		uint8_t bitPos = zoneId%64;
		uint8_t bank = zoneId/64;
		if(cmd==CH_STATE_THERMO_OFF){
			//9E [zoneid] 30 FF
			uint8_t data[] = {0x9E, zoneId, 0x30, 0xFF};
			hibusTxData(data, 4, TX_MODE_STATUS_X3);

			// cancello il bit dello stato della zona
			ch_pump_act_zonestatus[idchan][bank] &= 0xffffffffffffffff ^ (1<<bitPos);
			if(!ch_pump_act_zonestatus[idchan][0] && !ch_pump_act_zonestatus[idchan][1]){
				// tutti off per le zone spengo relè
				setRelays(idchan, 0, ledThermoOffColor, true);
				ch_active_command[idchan] = cmd;
			}
			if(sendStatus){
				sendChannelCurrentState(idchan);
			}
		}else if(cmd==CH_STATE_THERMO_ON){
			// setto il bit dello stato della zona
			ch_pump_act_zonestatus[idchan][bank] |= 1<<bitPos;
			setRelays(idchan, 1, ledThermoOnColor, true);
			if(ch_active_command[idchan] == cmd && sendStatus){
				sendChannelCurrentState(idchan);
			}
			ch_active_command[idchan] = cmd;
		}
	}else{
		//attuatore zona
		setRelays(idchan, cmd==CH_STATE_THERMO_ON, cmd==CH_STATE_THERMO_ON ? ledThermoOnColor : ledThermoOffColor, true);
		ch_active_command[idchan] = cmd;	
		if(sendStatus){
			sendChannelCurrentState(idchan);
		}
	}
}


/***********************************
*	CHANNEL TASK
************************************/
void channelsTask(void *arg){
	uint64_t now;
	for(;;){
		// attendo ciclo 100ms
		vTaskDelay(100 / portTICK_PERIOD_MS);
		// carico il timer attuale
		now = esp_timer_get_time();
		for(uint8_t idchan=0;idchan<NUM_OF_CHANNELS;idchan++){
			// controllo se qualche canale è on con tempo massimo impostato
			if(ch_next_check_time[idchan]>0 && ch_next_check_time[idchan]<now){
				uint8_t cmd = ch_active_command[idchan];
				if(channels[idchan].type==SWITCH){
					if(cmd==0x00 || cmd==0x01 || ((cmd & 0x0f)==0x06)){			// ON
						// devo spegnere è un max o retain time o una fine temporizzata
						setRelays(idchan, 0, ledSwitchOffColor, true);
						ch_active_command[idchan]=CH_STATE_SWITCH_OFF;
						// invio lo stato sul bus
						if(!(channels[idchan].flags & CH_FLAG_BIT_SLAVE)){
							sendChannelCurrentState(idchan);	
						}
						ch_next_check_time[idchan]=0;
					}else if((cmd & 0x0f)==0x0b){
						// lampeggio intermittente
						//"0b",b+"0.5 sec",
						//"1b",b+"1 sec",
						//"2b",b+"1.5 sec",
						//"3b",b+"2 sec",
						//"4b",b+"2.5 sec",
						//"5b",b+"3 sec",
						//"6b",b+"3.5 sec",
						//"7b",b+"4 sec",
						//"8b",b+"4.5 sec",
						//"9b",b+"5 sec"
						if(getRelaysState(idchan)){
							setRelays(idchan, 0, ledSwitchOffColor, true);	
						}else{
							setRelays(idchan, 1, ledSwitchOnColor, true);								
						}
						ch_next_check_time[idchan] = now + BLINK_TIMES[(cmd>>4) & 0xf];
					}
				}else if(channels[idchan].type==COVER){
					// spengo i rele del cover
					setRelays(idchan,0, ledCoverStopColor, false);
					setRelays(channels[idchan].sets[CH_SET_INDEX_OPPOSITE_CHANNEL],0, ledCoverStopColor, true);
					ch_next_check_time[idchan]=0;
					ch_active_command[idchan]=CH_STATE_COVER_STOP;
					// invio lo stato sul bus
					if(!(channels[idchan].flags & CH_FLAG_BIT_SLAVE)){
						sendChannelCurrentState(idchan);	
					}					
				}
			}
		}
	}
}
/***********************************/

void initChannels(void) {
	// inizializzo variabili
	for(int i=0;i<NUM_OF_CHANNELS;i++){
		channels[i] = DEFAULT_CH_SETTING;
		ch_next_check_time[i]=0;
		ch_active_command[i]=0xff;
		ch_pump_act_zonestatus[i][0] = 0;
		ch_pump_act_zonestatus[i][1] = 0;
	}
	
//	busMessageRxHandler = &rxDataDecoder;
	ESP_ERROR_CHECK(loadChannels());
	
	// avvio il task per la gestione dei tempi di accensione e spegnimento
	xTaskCreate(channelsTask,"channelsTask",4096,NULL,1,NULL);	
	
}

esp_err_t loadChannels(void){

    nvs_handle_t my_handle;
    esp_err_t err;

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &my_handle);
	if(err==ESP_ERR_NVS_NOT_FOUND){
		return saveChannels();
	}else{
	    if (err != ESP_OK) return err;
	    size_t required_size = sizeof(channel_setting_t) * NUM_OF_CHANNELS;
		ESP_LOGI(TAG, "Reading channels size %d",required_size);
	    err = nvs_get_blob(my_handle, CHANNEL_NVS_KEY, &channels, &required_size);
	    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
	    // Close
	    nvs_close(my_handle);
	    return ESP_OK;	
	}
}

esp_err_t saveChannels(void){
	
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

//    size_t required_size = sizeof(ChannelSetting) * CH_LEN;
//    err = nvs_get_blob(my_handle, CHANNEL_NVS_KEY, channels, &required_size);
//    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;

	size_t required_size =  sizeof(channel_setting_t) * NUM_OF_CHANNELS;
	ESP_LOGI(TAG, "Writing channels size %d",required_size);

    err = nvs_set_blob(my_handle, CHANNEL_NVS_KEY, &channels, required_size);

    if (err != ESP_OK) return err;

    // Commit
    err = nvs_commit(my_handle);
    if (err != ESP_OK) return err;

	ESP_LOGI(TAG, "Committed ");

    // Close
    nvs_close(my_handle);

	return ESP_OK;
}

void initNvs(void){
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
		ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
}


























