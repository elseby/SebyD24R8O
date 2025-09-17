/*
 * btsetupcommand.c
 *
 *  Created on: 19 mar 2025
 *      Author: elseby
 */
#include "include/utils.h"
#include "esp_http_server.h"
#include "include/channels.h"
#include "esp_cpu.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>

static const char *TAG = "BtCmd";

static const char *CHECKBOX_ON_VALUE = "on";

#define BUF_LEN 64

void commandResetCallback(char* msg, regmatch_t *matches,
						  regex_t *regex) {
	esp_cpu_reset(0);
}

//char *rtrim(char *str){
//    size_t len;
//    char *p;
//    len = strlen(str);
//    if (len > 0){
//        p = str + len;
//        do {
//            p--;
//            if (p[0]!=' ')
//                break;
//            *p = '\0';
//        } while (p > str);
//    }
//    return str;
//}

void reformatLabel(char *lbl){
	bool tr=true;
	for(int i=19;i>=0;i--){
		if(lbl[i]=='"' || lbl[i]<32 || lbl[i]>126) {
			lbl[i]=' ';
		}
		if(tr && lbl[i]==' '){
			lbl[i]='\0';
		}else{
			tr=false;
		}
	}
//	rtrim(lbl);	
	lbl[19]=0;
}

esp_err_t putChannelLabel(channel_setting_t *chan,char *buf, char *msg, char *retMsg){
    if (httpd_query_key_value(msg, "lbl", buf, BUF_LEN) == ESP_OK){
		// converto decimal
		memcpy(chan->label, buf, MIN(19,strlen(buf)));
		reformatLabel(chan->label);
		return ESP_OK;
	}
	strcpy(retMsg,"Label not found!");
	return ESP_ERR_INVALID_ARG;
}

esp_err_t putChannelWhere(channel_setting_t *chan,char *key, int whIndex, char *buf, char *msg, char *retMsg){
	if (httpd_query_key_value(msg, key, buf, BUF_LEN) == ESP_OK){
		if(putCharHexToByteArray(chan->where+whIndex, 1, buf)!=ESP_OK){
			strcpy(retMsg,"Where error!");
			return ESP_ERR_INVALID_ARG;
		}
	}else{
		strcpy(retMsg,"Where missing!");
		return ESP_ERR_INVALID_ARG;
	}
	return ESP_OK;
}

esp_err_t putChannelGroup(channel_setting_t *chan,char *buf, char *msg, char *retMsg){
	for(uint8_t idgrp=0;idgrp<CH_GROUP_SIZE;idgrp++){
		char key[5]="grp1";
		key[3]='1'+idgrp;
		if (httpd_query_key_value(msg, key, buf, BUF_LEN) == ESP_OK){
			uint8_t grp = buf[0]-'0';
			if(grp>0 && grp<10){
				chan->groups[idgrp] = grp;
			}
		}
	}
	ESP_LOGI(TAG, "groups: %X %X %X %X %X", chan->groups[0], chan->groups[1], chan->groups[2], chan->groups[3], chan->groups[4]);
	return ESP_OK;
}

esp_err_t putChannelSlaveIgnoreGenAIfExists(channel_setting_t *chan,char *buf, char *msg, char *retMsg){
    if (httpd_query_key_value(msg, "sl", buf, BUF_LEN) == ESP_OK
			&& strcmp(buf, CHECKBOX_ON_VALUE)==0)
	{
			chan->flags |= CH_FLAG_BIT_SLAVE;	
	}
    if (httpd_query_key_value(msg, "igen", buf, BUF_LEN) == ESP_OK
			&& strcmp(buf, CHECKBOX_ON_VALUE)==0)
	{
			chan->flags |= CH_FLAG_BIT_IGNORE_A_GEN;	
	}
	return ESP_OK;
}

esp_err_t putChannelRetainTime(channel_setting_t *chan,char *buf, char *msg, char *retMsg){
    if (httpd_query_key_value(msg, "rt", buf, BUF_LEN) == ESP_OK){
		// converto decimal
		chan->retain_time = atol(buf);
		return ESP_OK;
	}
	strcpy(retMsg,"Val not found!");
	return ESP_ERR_INVALID_ARG;
}

esp_err_t putChannelOpposite(uint8_t idChannel, channel_setting_t *chan,char *buf, char *msg, char *retMsg){
    if (httpd_query_key_value(msg, "oppch", buf, BUF_LEN) == ESP_OK){
		int oppChanId = atoi(buf);
		if(oppChanId>=0 && oppChanId<NUM_OF_CHANNELS){
			// controllo che il canale non sia settato
			if(channels[oppChanId].type==NONE){
				// verifico che non si tratti dello stesso canale
				if(idChannel==oppChanId){
					strcpy(retMsg,"Same opposite channel!");
					return ESP_ERR_INVALID_ARG;
				}
				// cancello tutto del canale opposto
				memset(&channels[oppChanId], 0x0, sizeof(channel_setting_t));
			}else if(channels[oppChanId].type!=NONE){
				strcpy(retMsg,"Opposite channel already set!");
				return ESP_ERR_INVALID_ARG;
			}
			chan->sets[CH_SET_INDEX_OPPOSITE_CHANNEL] = oppChanId;
			return ESP_OK;
		}
	}
	strcpy(retMsg,"Opposite channel invalid or not found!");
	return ESP_ERR_INVALID_ARG;
}

esp_err_t checkChannelFreeByOpposite(uint8_t chanId,char *retMsg){
	// il canale non deve essere associato a nessun canale opposto sui cover
	for(int i=0;i<NUM_OF_CHANNELS;i++){
		if(channels[i].type==COVER && channels[i].sets[CH_SET_INDEX_OPPOSITE_CHANNEL] == chanId){
			strcpy(retMsg,"Channel used on Opposite of ch:");
			itoa(i+1, retMsg+strlen(retMsg), 10);
			return ESP_ERR_INVALID_ARG;
		}
	}
	return ESP_OK;
}

esp_err_t putChannelMaxTime(channel_setting_t *chan,char *buf, char *msg, char *retMsg){
    if (httpd_query_key_value(msg, "mt", buf, BUF_LEN) == ESP_OK){
		// converto decimal
		chan->max_on_time = atol(buf);
		return ESP_OK;
	}
	strcpy(retMsg,"Val not found!");
	return ESP_ERR_INVALID_ARG;
}

void processSetupCommand(char *msg, char *retMsg){

	char buf[BUF_LEN];
	// cerco prima se c'e' un comando scmd
	//scmd: REBOOT
    if (httpd_query_key_value(msg, "scmd", buf, BUF_LEN) == ESP_OK){
        ESP_LOGI(TAG, "Found command => %s", buf);
        if(strcmp("REBOOT", buf)==0){
			//reboot
			esp_cpu_reset(0);
			return;
		}
    }	

	uint8_t chanId;

	// cerco il canale
	// canale da modificare
	channel_setting_t chan; // = channels[chanId];
	// porto a default tutto a 0x00
	memset(&chan, 0x0, sizeof(channel_setting_t));
    /* Get value of expected key from query string */
    if (httpd_query_key_value(msg, "id", buf, BUF_LEN) == ESP_OK){
		chanId = atoi(buf);
		ESP_LOGI(TAG, "CHSET chanid:'%i'", chanId);
		if(chanId>=NUM_OF_CHANNELS){
			strcpy(retMsg,"Wrong ID Channel 0-31");
			return;
		}
    }else{
		strcpy(retMsg,"No channel selected!");
		return;
	}	

	// cerco il tipo di canale
    if (httpd_query_key_value(msg, "type", buf, BUF_LEN) == ESP_OK){
		if(strlen(buf)){
			if(checkChannelFreeByOpposite(chanId,retMsg)!=ESP_OK) return;
		}
		//"", "-Select command-",
		//"0", "Not Set",
		//"1", "Switch",
		//"2", "Cover",
		// "8", "Thermo"
		if(strcmp("0", buf)==0){
			//not set
			chan.type = NONE;
		}else if(strcmp("1", buf)==0){
			// switch
			chan.type = SWITCH;
			if(putChannelLabel(&chan, buf, msg, retMsg)!=ESP_OK) return;
			if(putChannelRetainTime(&chan, buf, msg, retMsg)!=ESP_OK) return;
			if(putChannelMaxTime(&chan, buf, msg, retMsg)!=ESP_OK) return;
			putChannelSlaveIgnoreGenAIfExists(&chan, buf, msg, retMsg);
			putChannelGroup(&chan, buf, msg, retMsg);
			if(putChannelWhere(&chan,"wh0",0, buf, msg, retMsg)!=ESP_OK) return;			
		}else if(strcmp("2", buf)==0){
			chan.type = COVER;
			if(putChannelLabel(&chan, buf, msg, retMsg)!=ESP_OK) return;
			if(putChannelOpposite(chanId,&chan, buf, msg, retMsg)!=ESP_OK) return;
			if(putChannelMaxTime(&chan, buf, msg, retMsg)!=ESP_OK) return;
			putChannelSlaveIgnoreGenAIfExists(&chan, buf, msg, retMsg);
			putChannelGroup(&chan, buf, msg, retMsg);
			if(putChannelWhere(&chan,"wh0",0, buf, msg, retMsg)!=ESP_OK) return;
			if(!chan.max_on_time){
				// imposto tempo massimo di default su cover
				chan.max_on_time = 60000;
			}
		}else if(strcmp("3", buf)==0){
			chan.type = THERMO;
			if(putChannelLabel(&chan, buf, msg, retMsg)!=ESP_OK) return;
			if(putChannelWhere(&chan,"wh0",0, buf, msg, retMsg)!=ESP_OK) return;
			if(putChannelWhere(&chan,"wh1",1, buf, msg, retMsg)!=ESP_OK) return;
			
		}else{
			strcpy(retMsg,"CMD unknown!");
			return;
		}
       
    }else{
		strcpy(retMsg,"No CMD selected !");
		return;
	}
	
//	ESP_LOGI(TAG,"chtype:%x set0:%x flags:%x where:%x %x int:%x",chan.type,chan.sets[0],chan.flags,chan.where[0],chan.where[1],chan.whereInt);

	channels[chanId] = chan;

	//salvo i dati
	saveChannels();
	
	strcpy(retMsg,"OK. Channel Saved.");
	
}


/********************************************************************
*
*	CHANNEL INFO
*
*********************************************************************/

esp_err_t sendChannelInfo(httpd_req_t *req){
	char buf[200];

	httpd_resp_set_type(req, "application/json");

	esp_err_t ret=httpd_resp_sendstr_chunk(req, "[");	
	if(ret!=ESP_OK) return ret;
	
	for(int i=0;i<NUM_OF_CHANNELS;i++){
		memset(buf, 0, 200);
		if(i>0){
			httpd_resp_sendstr_chunk(req, ",");
		}
		channel_setting_t ch=channels[i];

		// cerco l'opposite'
		uint8_t opposite = 0;
		for(int u=0;u<NUM_OF_CHANNELS;u++){
			if(channels[u].type==COVER && channels[u].sets[CH_SET_INDEX_OPPOSITE_CHANNEL]==i){
				opposite = u+1;
				break;
			}
		}
		reformatLabel(ch.label);
		// genero il json con le info
		sprintf(buf,"{'id':%i,'type':%i,'lbl':'%s','oppof':%i,'wh0':%i,'wh1':%i,'rt':%"PRIu32",'mt':%"PRIu32",'sl':%i,'igen':%i,'gr':[%i,%i,%i,%i,%i],'oppch':%i}"
			,i
			,ch.type
			,ch.label
			,opposite
			,ch.where[0]
			,ch.where[1]
			,ch.retain_time
			,ch.max_on_time
			,(ch.flags&CH_FLAG_BIT_SLAVE)?1:0
			,(ch.flags&CH_FLAG_BIT_IGNORE_A_GEN)?1:0
			,ch.groups[0],ch.groups[1],ch.groups[2],ch.groups[3],ch.groups[4]
			,ch.sets[CH_SET_INDEX_OPPOSITE_CHANNEL]
		);
		//spaghetti prg ma non ho voglia di cercare come mettere " sulla stringa, \" non funziona
		for(int i=0;i<strlen(buf);i++){
			if(buf[i]=='\'') buf[i]='"';
		}
		ret = httpd_resp_sendstr_chunk(req, buf);
		if(ret!=ESP_OK) return ret;
	}

	ret=httpd_resp_sendstr_chunk(req, "]");
	if(ret!=ESP_OK) return ret;

	ret = httpd_resp_send_chunk(req, buf,0);
	if(ret!=ESP_OK) return ret;


	return ret;
}















