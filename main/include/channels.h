#pragma once


#include <stdint.h>
#include <sys/cdefs.h>
#include "busrtx.h"
#include "config.h"
#include "esp_err.h"

#define STORAGE_NAMESPACE "storage"
#define CH_GROUP_SIZE 5

#define CH_STATE_SWITCH_ON 0x00
#define CH_STATE_SWITCH_OFF 0x01

#define CH_STATE_COVER_STOP 0x0a
#define CH_STATE_COVER_UP 0x08
#define CH_STATE_COVER_DOWN 0x09

#define CH_STATE_THERMO_OFF 0x01
#define CH_STATE_THERMO_ON 0x00

#define CH_SET_INDEX_OPPOSITE_CHANNEL 		0

#define CH_FLAG_BIT_IGNORE_A_GEN 	(1ULL<<0)
#define CH_FLAG_BIT_SLAVE 			(1ULL<<1)

typedef enum{
    NONE=0,
    SWITCH=1,
    COVER=2,
    THERMO=3
} channel_type_t;

typedef struct {
	char label[20];				//20
	channel_type_t type;		//1
	/**
		Byte n.
			0:	Opposite channel (Cover type)
	*/
	uint8_t sets[8];			//8
	/**
		Bit n.
			0:	Ignore A and GEN
			1:	Slave (Switch)
	*/
	uint32_t flags;				//4
	uint8_t where[2];			//2
	uint8_t whereInt;			//1
	uint32_t retain_time;		//4
	uint32_t max_on_time;		//4
	uint8_t groups[CH_GROUP_SIZE];			//5
	uint8_t reserved[15];		//64-44 -> 20
	
} channel_setting_t;

void rxDataDecoder(hibus_rx_frame_t *rxMsg);

extern channel_setting_t channels[NUM_OF_CHANNELS];
//extern BusMessageRxHandler busMessageRxHandler;

void initNvs(void);
void initChannels(void);
esp_err_t saveChannels(void);
esp_err_t loadChannels(void);


//void setChannelBinaryState(uint8_t idchan, uint8_t state, uint8_t ignoreRetain);
//void processChannelCommand(uint8_t idch, uint8_t cmd, bool sendAck);

void setChannelSwitch(uint8_t idchan, uint8_t cmd, bool ignoreRetain, bool sendAck, bool sendStatus);
void setChannelCover(uint8_t idchan, uint8_t cmd, bool sendAck, bool sendStatus);
void setChannelThermo(uint8_t idchan, uint8_t zoneId, uint8_t cmd, bool sendStatus);

void sendChannelCurrentState(uint8_t idchan);
void sendHibusMsgToWs(hibus_rx_frame_t *rxMsg, char prefix);

//void testPulse();

