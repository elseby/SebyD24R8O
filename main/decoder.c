/*
 * decoder.c
 *
 *  Created on: 31 mar 2025
 *      Author: elseby
 */

#include "httpserver.h"
#include "include/channels.h"
#include <stdint.h>
#include <string.h>
#include <strings.h>

hibus_rx_frame_t lastRxMsg;

void decodeLen7Commands(uint8_t idch, hibus_rx_frame_t *rxMsg);

/************************************************************************************
 *	RXDATA DECODER
 *************************************************************************************/
 
 void btox1(char *xp, uint8_t *bb, int n) {
	const char xx[] = "0123456789ABCDEF";
	for(int i=0;i<n;i++){
		xp[i*3] = xx[bb[i]>>4];
		xp[(i*3)+1] = xx[bb[i]&0xf];
		xp[(i*3)+2] = ' ';
	}
}
 
void sendHibusMsgToWs(hibus_rx_frame_t *rxMsg, char prefix) {
	if(isWsConnected()){
		size_t slen = (rxMsg->len*3)+2;
		char wsMsgTxt[slen+1];
		wsMsgTxt[0] = prefix;
		btox1(wsMsgTxt+1, rxMsg->data, rxMsg->len);
		wsMsgTxt[slen-1]=0;
		// invio msg
		sendStringToWs(wsMsgTxt);
	}
}
 
void rxDataDecoder(hibus_rx_frame_t *rxMsg) {
	// scarto l'A5 ack
	if (rxMsg->len == 1 && rxMsg->data[0] == 0xA5) {
		lastRxMsg.len=0;
		goto rxDecoderEnd;
	}
	
	// ignoro stesso messaggio
	if(lastRxMsg.len==rxMsg->len && memcmp(lastRxMsg.data, rxMsg->data, rxMsg->len)==0){
		// stesso messaggio dell'ultimo elaborato
		goto rxDecoderEnd;
	}
	
	// copio il messaggio nell'ultimo
	memcpy(&lastRxMsg,&rxMsg, sizeof(hibus_rx_frame_t));

	if (rxMsg->len == 7) {
		// decodifica messaggi di stato semplici
		for (int idCh = 0; idCh < NUM_OF_CHANNELS; idCh++) {
			decodeLen7Commands(idCh, rxMsg);
		}
	} else if (rxMsg->len == 11) {
		// msg da len=11
		if(rxMsg->data[1] == 0xec && rxMsg->data[3] == 0x00 && rxMsg->data[4] == 0x00){
			// interfaccia altro livello
//			onEveryChannel(rxMsg, &decodeInterfaceLevel);
		}else if(rxMsg->data[1] == 0xd1 
			&& rxMsg->data[3] == 0x00 
			&& rxMsg->data[4] == 0x00
		){
			// termo
			//19.5
			//I (672898) Chan: l:11->a8 d1 0 3 2 c2 c 12 27 2b a3
						
		}
	}
	
	rxDecoderEnd:
	// send to websocket (if is connected)
	sendHibusMsgToWs(rxMsg,'<');

}

bool isChannelInGroup(channel_setting_t *ch, uint8_t grp) {
	for(uint8_t idgrp=0;idgrp<CH_GROUP_SIZE;idgrp++){
		if(ch->groups[idgrp]==grp){
			return true;
		}
	}
	return false;
}

void decodeSwitchCover(uint8_t idch, channel_setting_t *ch, hibus_rx_frame_t *rxMsg) {
	uint8_t isSlave = (ch->flags & CH_FLAG_BIT_SLAVE) ? 1: 0;
	if(	(rxMsg->data[3]!=0x12 && rxMsg->data[3]!=0x15)
		|| (isSlave && rxMsg->data[1]!=0xb8)	//slave-> commands ignored...
		|| (!isSlave && rxMsg->data[1]==0xb8) //master-> states ignored...
		){
		return;	
	}
	
	uint8_t ignoreAGEN = (ch->flags & CH_FLAG_BIT_IGNORE_A_GEN) ? 1: 0; 
	// B8 85 12 01   off
	// B8 85 12 00   on
	/**
			rxMsg.data[1]==0xb1	//generale 
			rxMsg.data[1]==0xb3	//ambiente
			rxMsg.data[1]==0xb5	//gruppo
	A8 85 00 12 01 96 A3 
	A5 
	A8 B8 85 12 01 2E A3 
	A8 B8 85 12 01 2E A3 
	A8 B8 85 12 01 2E A3					
	*/
	bool isGeneral = rxMsg->data[1]==0xb1 && rxMsg->data[2]==0x00;
	bool isAmbient = rxMsg->data[1]==0xb3 && ((rxMsg->data[2]<<4) == (ch->where[0] & 0xf0));
	// if general or ambient and is ignored , return
	if(ignoreAGEN && (isGeneral || isAmbient)){
		return;
	}
//			bool isGenOrAmb = rxMsg.data[1]==0xb1 || (rxMsg.data[1]==0xb3 && ((rxMsg.data[2]<<4) & (ch.where[0] & 0xf0))); 
	if(
		isGeneral
		|| isAmbient
		|| (rxMsg->data[1]==0xb5 && isChannelInGroup(ch,rxMsg->data[2]))		// group
		|| (!isSlave && rxMsg->data[1]==ch->where[0])	// Point with command
		|| (isSlave && rxMsg->data[1]==0xb8 && rxMsg->data[2]==ch->where[0])	// Point with state (slave)
	){
		if(rxMsg->data[3]==0x15){
			// richiesta stato
			sendChannelCurrentState(idch);						
		}else{
			if(ch->type==SWITCH){
				setChannelSwitch(
					idch, 
					rxMsg->data[4], 
					false, 	//ignore retain
					!(isSlave || isGeneral || isAmbient), //send ack
					isSlave ? false : true		// send status
				);
			}else{
				setChannelCover(
					idch, 
					rxMsg->data[4],
					!(isSlave || isGeneral || isAmbient),  //send ack
					isSlave ? false : true		// send status
				);
			}
		}
	}
	
}

void decodeThermo(uint8_t idch, channel_setting_t *ch, hibus_rx_frame_t *rxMsg) {
	uint8_t *d = &rxMsg->data[1]; 
	//B9003010 richiesta stato tutti gli attuatori
	if(d[0]==0xB9 && d[1]==0x00 && d[2]==0x30 && d[3]==0x10){
		sendChannelCurrentState(idch);
	}else if(
		// attivazione zona...
		// //96 [80+Z] [30+N] [01-off,00-on]
		d[0]==0x96 
		&& d[1]>0x80 
		&& ch->where[0]==d[1]
		&& ch->where[1]==d[2]
	){
		// attivazione attuatore
		setChannelThermo(idch, 0, d[3], true);
	}else if(
		// attivazione pompa da zona...
		// 9D 80 35 0E -> 0E zona
		// zona d[3]
		d[0]==0x9d 
		&& d[1]==0x80 
		&& ch->where[0]==0x80 
		&& ch->where[1]==d[2]
	){
		// attivazione attuatore
		setChannelThermo(idch, d[3], CH_STATE_THERMO_ON, true);
	}else if(
		// disattivazione pompa da zona...
		// 9E 80 35 0E -> 0E zona
		// zona d[3]
		d[0]==0x9e 
		&& d[1]==0x80 
		&& ch->where[0]==0x80 
		&& ch->where[1]==d[2]
	){
		// disattivazione attuatore
		setChannelThermo(idch, d[3], CH_STATE_THERMO_OFF, true);

	}else if(ch->where[0]>0x80 && ch->where[1]>0x30){
		// attuatore zone
		//RICHIESTE STATI
		//99 89 31 10 		ZA:0 ZB1:9 N1:1
		//99 89 35 10  		ZA:0 ZB1:9 N1:5
		//99 B1 35 10 		ZA:4 ZB1:9 N1:5
		if(
			d[0]==0x99 && d[1]==ch->where[0] && d[2]==ch->where[1] && d[3]==0x10			
		){
			sendChannelCurrentState(idch);
			return;
		}
	}	
}

void decodeLen7Commands(uint8_t idch, hibus_rx_frame_t *rxMsg) {
	channel_setting_t *ch = &channels[idch];
	switch (ch->type) {
		case SWITCH:
		case COVER:
			decodeSwitchCover(idch,ch,rxMsg);
			break;
		case THERMO:
			decodeThermo(idch,ch,rxMsg);
			break;
		case NONE:
			break;
	}	

}











