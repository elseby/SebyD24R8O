#pragma once

#include "config.h"
#include "soc/gpio_num.h"
#include <stdbool.h>
#include <stdint.h>

#define HIBUS_STARTBITLEN 34
#define HIBUS_TOTALBITLEN 104
#define HIBUS_STOPBITLEN 134

//#define RX_BUF_SIZE 1024
#define MAXMESSAGESIZE 17
//#define TX_MSG_FLAG_WAIT_ACK 0
#define TX_BUS_IDLE_WAIT_TIMEOUT_uS ((uint64_t) 3* 1000 * 1000)

#define TX_BUFFER_SIZE 10

#define RMT_BYTE_DATASIZE 9

#define HIBUS_START_BYTE 0xA8
#define HIBUS_STOP_BYTE 0xA3
#define HIBUS_ACK_BYTE 0xA5


/**************************
*	RECEIVER
***************************/

typedef struct{
	int8_t len;
	uint8_t data[MAXMESSAGESIZE];
}hibus_rx_frame_t;

typedef void (*hibus_rx_frame_handler)(hibus_rx_frame_t *rxFrame);

/**************************
*	TRANSMITTER
***************************/

 typedef struct {
	const int8_t msgCount;
	const uint64_t startDelay;
	const uint64_t pauseDelay;
	const bool waitAck;
}hibus_tx_msg_mode_t;

extern const hibus_tx_msg_mode_t* TX_MODE_ACK;
extern const hibus_tx_msg_mode_t* TX_MODE_STATUS_X3;
extern const hibus_tx_msg_mode_t* TX_MODE_COMMAND_X1;
extern const hibus_tx_msg_mode_t* TX_MODE_COMMAND_X3;
extern const hibus_tx_msg_mode_t* TX_MODE_COMMAND_X3_IF_NOT_ACK;
extern const hibus_tx_msg_mode_t* TX_MODE_SINGLE_LOW_PRIORITY;

//typedef enum{
//    TX_MODE_ACK,
//    TX_MODE_STATUS_X3,
//    TX_MODE_COMMAND_X1,
//    TX_MODE_COMMAND_X3,
//    TX_MODE_COMMAND_X3_IF_NOT_ACK,
//    TX_MODE_SINGLE_LOW_PRIORITY
//} hibus_tx_msg_mode_t;

typedef struct{
	uint8_t len;
	int8_t msgCount;
	const hibus_tx_msg_mode_t* mode;
	uint8_t data[MAXMESSAGESIZE];
	uint64_t nextSendTime;
	
}hibus_tx_message_t;

void hibusTxData(uint8_t *data, uint8_t len, const hibus_tx_msg_mode_t* mode);
void hibusTxAck();

/**************************
*	COMMON
***************************/
typedef struct{
    uint32_t busFreeTimeout;
    uint32_t notSameDataRxAfterTx;
    uint32_t txWaitTimeout;
    uint32_t txBufferFull;
} hibus_errors_t;


typedef struct {
	gpio_num_t gpio_tx_port;
	gpio_num_t gpio_rx_port;
	hibus_rx_frame_handler rx_handler;	
} hibus_config_t;

void hibusInit(hibus_config_t *hibus_config);




