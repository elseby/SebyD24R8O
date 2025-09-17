/*
 * hibus_rx_tx.c
 *
 *  Created on: 15 feb 2025
 *      Author: elseby
 */

#include "busrtx.h"
#include "bootloader_random.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include <driver/rmt_rx.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_intsup.h>
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include "hal/rmt_types.h"
#include "hal/uart_types.h"
#include "include/busrtx.h"
#include "portmacro.h"
#include "soc/clk_tree_defs.h"
#include "soc/gpio_num.h"
#include "soc/uart_struct.h"

static const hibus_tx_msg_mode_t TX_MODE_ACK_s = {
	.msgCount=1,
	.startDelay=200,	// considero il tempo di elaborazione poi c'e' il random 0-63 us
	.pauseDelay=0,
	.waitAck=false
};
static const hibus_tx_msg_mode_t TX_MODE_STATUS_X3_s = {
	.msgCount=3,
	.startDelay=15000,
	.pauseDelay=3000,
	.waitAck=false
};
static const hibus_tx_msg_mode_t TX_MODE_COMMAND_X1_s = {
	.msgCount=1,
	.startDelay=1000,
	.pauseDelay=0,
	.waitAck=false
};
static const hibus_tx_msg_mode_t TX_MODE_COMMAND_X3_s = {
	.msgCount=3,
	.startDelay=1000,
	.pauseDelay=15000,
	.waitAck=false
};
static const hibus_tx_msg_mode_t TX_MODE_COMMAND_X3_IF_NOT_ACK_s = {
	.msgCount=8,
	.startDelay=100,
	.pauseDelay=8900,
	.waitAck=true
};
static const hibus_tx_msg_mode_t TX_MODE_SINGLE_LOW_PRIORITY_s = {
	.msgCount=1,
	.startDelay=1000,
	.pauseDelay=0,
	.waitAck=false
};

const hibus_tx_msg_mode_t* TX_MODE_ACK = &TX_MODE_ACK_s;
const hibus_tx_msg_mode_t* TX_MODE_STATUS_X3 = &TX_MODE_STATUS_X3_s;
const hibus_tx_msg_mode_t* TX_MODE_COMMAND_X1= &TX_MODE_COMMAND_X1_s;
const hibus_tx_msg_mode_t* TX_MODE_COMMAND_X3= &TX_MODE_COMMAND_X3_s;
const hibus_tx_msg_mode_t* TX_MODE_COMMAND_X3_IF_NOT_ACK= &TX_MODE_COMMAND_X3_IF_NOT_ACK_s;
const hibus_tx_msg_mode_t* TX_MODE_SINGLE_LOW_PRIORITY= &TX_MODE_SINGLE_LOW_PRIORITY_s;

volatile static hibus_rx_frame_t lastRxFrame;
hibus_errors_t hibus_errors;
static const char *TAG = "BusRxTx";
void HibusTxTask(void *arg);
void HibusRxTask(void *arg);
TaskHandle_t hibusRxTaskHandler = NULL;
hibus_rx_frame_handler bus_rx_handler = NULL;
QueueHandle_t uart_queue = NULL;
static hibus_tx_message_t tx_buffer[TX_BUFFER_SIZE];
static esp_timer_handle_t tx_timer_handle = NULL;
rmt_symbol_word_t txPayload[RMT_BYTE_DATASIZE * (MAXMESSAGESIZE+1)];

static hibus_tx_message_t txAckBuffer={
	.data={HIBUS_ACK_BYTE},
	.len=1,
	.mode = &TX_MODE_ACK_s,
	.msgCount=0
};

//static uint8_t rxData[RX_BUF_SIZE];
static hibus_rx_frame_t rxFrame;
static volatile int expectedFrameLen=0;
gpio_num_t rxPinGPIO;

volatile static uint64_t lastRxByteTimestamp=0;

QueueHandle_t xQueueHibusWaitTxResponse = NULL;
static const int TX_ACK_RESP_ACKRECEIVED = 1;
static const int TX_ACK_RESP_TIMEOUT = 2;
static const int TX_ACK_RESP_NOEVENT = -1;

QueueHandle_t xQueueHibusRx = NULL;

rmt_channel_handle_t tx_chan = NULL;
rmt_encoder_handle_t encoder = NULL;
rmt_transmit_config_t tx_config = {
	.loop_count = 0, // no transfer loop
	.flags = {.eot_level = 0, .queue_nonblocking = 1}
};
SemaphoreHandle_t txWaitSemaphore = NULL;
//SemaphoreHandle_t txWaitAckSemaphore = NULL;
void byteReceived(uint8_t incomeByte);

/******************************************************************
 *	RX
 *******************************************************************/
static void IRAM_ATTR gpio_rx_isr_handler(void *arg) {
//	testOutPulse2(1);
//	testOutPulse(1); 
	#ifdef TESTPOINT_A
	gpio_set_level(TESTPOINT_A,  1);
	#endif
	#ifdef TESTPOINT_B
	gpio_set_level(TESTPOINT_B,  1);
	#endif

	lastRxByteTimestamp = esp_timer_get_time()+HIBUS_TOTALBITLEN;
}
/////////////////////////////////////////////////////////////////
int8_t getMessageLenght(uint8_t firstByte) {
	switch (firstByte) {
		case 0xEC: // extended frame
		case 0xD2:
		case 0xD1:
			return 11;
		default:
			return 7;
	}
}
static void uart_event_task(void *pvParameters){
    uart_event_t event;
    uint8_t dtmp[16];
    uint8_t incomeByte;
    for (;;) {
        //Waiting for UART event.
        if (xQueueReceive(uart_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
            //Event of UART receiving data
            /*We'd better handler data event fast, there would be much more data events than
            other types of events. If we take too much time on data event, the queue might
            be full.*/
            case UART_DATA:
				#ifdef TESTPOINT_B
				gpio_set_level(TESTPOINT_B,  0);
				#endif

                uart_read_bytes(UART_NUM_1, dtmp, event.size, portMAX_DELAY);
				for(int byteId=0;byteId<event.size;byteId++){
					incomeByte = dtmp[byteId];
					if (rxFrame.len == 0) {
						// attendo l' 0xA5 o 0xA8 altrimenti il len rimane 0 e resta qua
						if (incomeByte == HIBUS_ACK_BYTE) {
							rxFrame.data[rxFrame.len++] = incomeByte;

							#ifdef TESTPOINT_A
							gpio_set_level(TESTPOINT_A,  0);
							#endif
			
							xQueueSend(xQueueHibusRx, &rxFrame, 0);
							rxFrame.len = 0;
							expectedFrameLen = 0;

							// se ACK inserisco e resetto
							xQueueSend(xQueueHibusWaitTxResponse, &TX_ACK_RESP_ACKRECEIVED, 0);
//							ackReceived=true;
//							xSemaphoreGive(txWaitAckSemaphore);
							continue;
						} else if (incomeByte == HIBUS_START_BYTE) {
							rxFrame.data[rxFrame.len++] = incomeByte;
							continue;
						}
					}
					if (rxFrame.len >= MAXMESSAGESIZE) {
						// se sono oltre la capacità del buffer
						// svuoto e scarto il messaggio
						rxFrame.len = 0;
						expectedFrameLen = 0;
						continue;
						
					} else if (expectedFrameLen==0 && rxFrame.len > 0) {
						// determino la lunghezza del pacchetto dal primo byte dopo A8
						expectedFrameLen = getMessageLenght(incomeByte)-1;
					}
				
					rxFrame.data[rxFrame.len++] = incomeByte;
				
					if (rxFrame.len >= expectedFrameLen && incomeByte == HIBUS_STOP_BYTE) {
						// sono alla fine del pacchetto verifico 0xA3 finale e check sum
						// controllo il checksum
						uint8_t chk = 0;
						// salto l'a8 iniziale e mi fermo prima dell'a3 e chk che ho già
						// inserito
						for (int i = 1; i < rxFrame.len - 2; i++) {
							chk = chk ^ rxFrame.data[i];
						}
						if (chk == rxFrame.data[rxFrame.len - 2]) {
							// checksum valido metto messaggio nella coda
							lastRxFrame=rxFrame;

							#ifdef TESTPOINT_A
							gpio_set_level(TESTPOINT_A,  0);
							#endif

							xQueueSend(xQueueHibusRx, &rxFrame, 0);
						}
						rxFrame.len = 0;
						expectedFrameLen = 0;
					}
				}
                break;
            //Others
            default:
                ESP_LOGI(TAG, "uart event type: %d", event.type);
                break;
            }
        }
    }
}

static void hibusRxInit(hibus_config_t *hibus_config) {

	// set the handler	
	bus_rx_handler = hibus_config->rx_handler;

	xQueueHibusRx = xQueueCreate(40, sizeof(hibus_tx_message_t));

	rxFrame.len = 0;

    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1_5,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 1024, 0, 10, &uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(
		UART_NUM_1, 
		UART_PIN_NO_CHANGE, 
		hibus_config->gpio_rx_port, 
		UART_PIN_NO_CHANGE, 
		UART_PIN_NO_CHANGE
	));
	
#ifdef HIBUS_INVERT_RX
	UART1.conf0.rxd_inv=1;
	UART1.conf0.irda_rx_inv=1;
#endif
//	UART1.conf0.irda_tx_inv=0;
//	UART1.conf0.irda_rx_inv=0;
//	UART1.conf0.rxfifo_rst=1;
//	UART1.conf0.txfifo_rst=1;
//	UART1.conf0.irda_tx_en=1;
//	UART1.conf0.irda_en=1;

	ESP_ERROR_CHECK(uart_set_mode(UART_NUM_1, UART_MODE_IRDA));
	
//	uart_set_rx_full_threshold(UART_NUM_1, 1);
	UART1.conf1.rxfifo_full_thrhd = 1;

	////////////////////////////////////////////////
	// INSTALL GPIO INTERRUPT
	
	// configuro interrupt per l'attesa di linea libera per il tx
	// zero-initialize the config structure.
	gpio_config_t io_conf = {
		#ifdef HIBUS_INVERT_RX
		.intr_type = GPIO_INTR_POSEDGE,
		#else
		.intr_type = GPIO_INTR_NEGEDGE,
		#endif
		.pin_bit_mask = (1ULL << hibus_config->gpio_rx_port),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = 0
	};
	gpio_config(&io_conf);
	// change gpio interrupt type for one pin
		#ifdef HIBUS_INVERT_RX
		gpio_set_intr_type(hibus_config->gpio_rx_port, GPIO_INTR_POSEDGE);
		#else
		gpio_set_intr_type(hibus_config->gpio_rx_port, GPIO_INTR_NEGEDGE);
		#endif
	
	// install gpio isr service
//	gpio_uninstall_isr_service();
	gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
	// hook isr handler for specific gpio pin
	gpio_isr_handler_add(hibus_config->gpio_rx_port, gpio_rx_isr_handler,
						 (void *)hibus_config->gpio_rx_port);
	gpio_intr_enable(hibus_config->gpio_rx_port);

    //Create a task to handler UART event from ISR
    xTaskCreate(uart_event_task, "uart_event_task", 1024*3, NULL, 12, NULL);

	ESP_LOGI(TAG, "RX Started.");

}


/*********************************************************************************
 *	RX TASK
 **********************************************************************************/
void HibusRxTask(void *arg) {
	
	hibus_config_t *hibus_config = (hibus_config_t *)arg;
	hibusRxInit(hibus_config);

	hibus_rx_frame_t rxFr;

    for(;;){
		if (xQueueReceive(xQueueHibusRx, &rxFr, (TickType_t)portMAX_DELAY)) {
			if(bus_rx_handler!=NULL){
				bus_rx_handler(&rxFr);
			}
		}
    }
}


/******************************************************************
 *	TX
 *******************************************************************/
 
/*******************************************
	Genera il payload RMT dal messaggio
	ritorna la lunghezza
*/
int generateRmtPayload(hibus_tx_message_t *msg) {
	int l=-1;
	for(int i=0;i<msg->len;i++){
		txPayload[++l].level0 = 1;
		txPayload[l].duration0 = HIBUS_STARTBITLEN;
		txPayload[l].level1 = 0;
		txPayload[l].duration1 = HIBUS_TOTALBITLEN - HIBUS_STARTBITLEN;
		// processo bit per bit
		for (int bit = 0; bit < 8; bit++) {
			if (msg->data[i] & (0x01 << bit)) {
				// il bit = 1 , aggiungo tempo di pausa
				txPayload[l].duration1 += HIBUS_TOTALBITLEN;
			} else {
				// il bit = 0, genero un altro impulso
				txPayload[++l].level0 = 1;
				txPayload[l].duration0 = HIBUS_STARTBITLEN;
				txPayload[l].level1 = 0;
				txPayload[l].duration1 = HIBUS_TOTALBITLEN - HIBUS_STARTBITLEN;
			}
		}
		// bit di stop
		txPayload[l].duration1 += HIBUS_STOPBITLEN;
	}
	return l+1;
}
 
bool doTx(hibus_tx_message_t *msg){
	// prepare payload rmt data
	int txPayloadLen = generateRmtPayload(msg);
	// check if bus free...
	#ifdef HIBUS_INVERT_RX
	if(gpio_get_level(rxPinGPIO) || lastRxByteTimestamp>esp_timer_get_time()){
	#else
	if(!gpio_get_level(rxPinGPIO) || lastRxByteTimestamp>esp_timer_get_time()){
	#endif
		return false;
	}
	// svuoto coda
	xQueueReset(xQueueHibusWaitTxResponse);
//	ackReceived=false;
	// enable tx
	ESP_ERROR_CHECK(rmt_enable(tx_chan));
	// tx rmt data
	ESP_ERROR_CHECK(rmt_transmit(tx_chan, encoder, txPayload, txPayloadLen * 4, &tx_config));
	// wait tx
	ESP_ERROR_CHECK(rmt_tx_wait_all_done(tx_chan, -1));
	// disable rmt channel
	ESP_ERROR_CHECK(rmt_disable(tx_chan));
	return true;
}

int getBufferFreeIndex(){
	for(int i=0;i<TX_BUFFER_SIZE;i++){
		if(tx_buffer[i].msgCount==0){
			return i;
		}
	}
	return -1;
}

void setTimerNextTxFrame(){
	// find early frame
	esp_timer_stop(tx_timer_handle);
	hibus_tx_message_t *msg;
	msg = NULL;
	if(txAckBuffer.msgCount){
		msg = &txAckBuffer;
	}else{
		msg = NULL;
		for(int i=0;i<TX_BUFFER_SIZE;i++){
			if(tx_buffer[i].msgCount){
				if(msg==NULL || msg->nextSendTime > tx_buffer[i].nextSendTime){
					msg = &tx_buffer[i];	
				}
			}
		}
	}	
	if(msg!=NULL){
		uint64_t now = esp_timer_get_time();
		ESP_ERROR_CHECK(esp_timer_start_once(
			tx_timer_handle,
			msg->nextSendTime>now ? msg->nextSendTime-now : 10 
		));
	}	
}

void hibusTxAck(){
	txAckBuffer.nextSendTime = txAckBuffer.mode->startDelay + esp_timer_get_time() + (esp_random() & 0x3f);
	txAckBuffer.msgCount = 1;
//	xQueueSendToFront(xQueueHibusTx, &txAckBuffer , 0);
//	xSemaphoreGive(txWaitSemaphore);
	setTimerNextTxFrame();
}

void hibusTxData(uint8_t *data, uint8_t len, const hibus_tx_msg_mode_t* mode){
	int bidx = getBufferFreeIndex();
	if(bidx<0){
		hibus_errors.txBufferFull++;
		return;
	}
	
	int l=0;

	// insert start byte 0xA8
	tx_buffer[bidx].data[l++]=HIBUS_START_BYTE;
	uint8_t checkSum = 0;
	for (int i = 0; i < len; i++) {
		checkSum = checkSum ^ data[i];
		tx_buffer[bidx].data[l++]=data[i];
	}

	// insert checksum
	tx_buffer[bidx].data[l++]=checkSum;

	// insert end byte 0xA3
	tx_buffer[bidx].data[l++]=HIBUS_STOP_BYTE;	

	tx_buffer[bidx].mode = mode;
	tx_buffer[bidx].len = l;

	tx_buffer[bidx].nextSendTime = mode->startDelay + esp_timer_get_time() + (esp_random() & 0x3f);
	tx_buffer[bidx].msgCount = mode->msgCount;
//	xSemaphoreGive(txWaitSemaphore);
	setTimerNextTxFrame();
}

void tx_wait_timer_callback(void* arg) {
	xSemaphoreGiveFromISR(txWaitSemaphore, 0);
	xQueueSendFromISR(xQueueHibusWaitTxResponse, &TX_ACK_RESP_TIMEOUT, 0);
}

/**************************************************************************************************
 *	HIBUS TX TASK
 ***************************************************************************************************/
void HibusTxTask(void *arg) {
	hibus_tx_message_t *msg;
	uint64_t now;
	uint64_t busFreeTimeout=0;
	bool txOk;
	int txWaitResp;
	for(;;){
		// setto il timer al prossimo frame
//		setTimerNextTxFrame();		
		// attendo la prossima richiesta di tx 10 / portTICK_PERIOD_MS
		xSemaphoreTake(txWaitSemaphore, pdMS_TO_TICKS(5000));
		esp_timer_stop(tx_timer_handle);
		// find first message
		now = esp_timer_get_time();
		if(txAckBuffer.msgCount){
			msg = &txAckBuffer;
		}else{
			msg = NULL;
			for(int i=0;i<TX_BUFFER_SIZE;i++){
				if(tx_buffer[i].msgCount){
					if(msg==NULL || msg->nextSendTime > tx_buffer[i].nextSendTime){
						msg = &tx_buffer[i];	
					}
				}
			}
			if(msg==NULL){
				continue;
			}
		}

		now = esp_timer_get_time();
		if(msg->nextSendTime>now){
			setTimerNextTxFrame();
			continue;
		}
		txOk = doTx(msg);
//		if(msg->mode==TX_MODE_ACK){
//			msg->msgCount=0;
//			continue;
//		}
		now = esp_timer_get_time();
		if(txOk){
			busFreeTimeout=0;
		}else{
			ESP_LOGI(TAG, "HIBus BUS NOT FREE!");
			if(busFreeTimeout==0){
				busFreeTimeout = now + TX_BUS_IDLE_WAIT_TIMEOUT_uS;
			}else if(now>busFreeTimeout){
				//remove message
				msg->msgCount=0;
				ESP_LOGI(TAG, "HIBus BUSFREE TIMEOUT!");
				setTimerNextTxFrame();
				continue;
			}
			// set next check time minimum 8ms
			msg->nextSendTime = now + 8000 + (esp_random() & 0xff);
			setTimerNextTxFrame();
			continue;
		}
		
		ESP_LOGI(TAG, "msg count %i msg:%x %x %x %x",msg->msgCount,msg->data[0],msg->data[1],msg->data[2],msg->data[3]);
		
		if(--msg->msgCount>0){
			if(msg->mode->waitAck){
				// wait timeout set 2ms
				ESP_ERROR_CHECK(esp_timer_start_once(tx_timer_handle,2000));			
				
				// i have to wait ack...
				txWaitResp = TX_ACK_RESP_NOEVENT;
				xQueueReceive(xQueueHibusWaitTxResponse, &txWaitResp, pdMS_TO_TICKS(50));
				esp_timer_stop(tx_timer_handle);
				if(txWaitResp==TX_ACK_RESP_ACKRECEIVED){
//					#ifdef TESTPOINT_A
//					gpio_set_level(TESTPOINT_A,  1);
//					#endif				
					//remove message
					ESP_LOGI(TAG, "ACK OK!");
					msg->msgCount=0;
					setTimerNextTxFrame();
					continue;
				}else if(txWaitResp==TX_ACK_RESP_TIMEOUT){
					ESP_LOGI(TAG, "NOT ACK RECEIVED!");
				}else{
					ESP_LOGI(TAG, "ACK ERROR NOEVENT! %i",txWaitResp);
					msg->msgCount=0;
					setTimerNextTxFrame();
					continue;
				}
			}
//			esp_timer_stop(tx_timer_handle);
			msg->nextSendTime = now + msg->mode->pauseDelay + (esp_random() & 0xff);
			// next message check pause delay
//			ESP_ERROR_CHECK(esp_timer_start_once(tx_timer_handle,msg->nextSendTime-now));			
		}else{
			//remove message
			msg->msgCount=0;
		}
		setTimerNextTxFrame();
	}

}

//*******************************************************************************************/
void hibusTxInit(hibus_config_t *hibus_config) {
	// inizializzo RMT per trasmissione e velocità

	rmt_tx_channel_config_t tx_chan_config = {
		.clk_src = RMT_CLK_SRC_DEFAULT,
		.gpio_num = hibus_config->gpio_tx_port,
		.mem_block_symbols = 1024,
		.resolution_hz = 1 * 1000 * 1000,
		.trans_queue_depth = 1,
		.intr_priority = 0,
		.flags.invert_out = false, // do not invert output signal
		.flags.with_dma = true,	   // do not need DMA backend
	};

	ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan));

	rmt_copy_encoder_config_t config = {};

	rmt_new_copy_encoder(&config, &encoder);

	memset(&tx_buffer, 0, sizeof(tx_buffer));

	// salvo il pin del rx
	rxPinGPIO = hibus_config->gpio_rx_port;

	// abilito il random
	bootloader_random_enable();

	// create timer to wake up task queue
	const esp_timer_create_args_t tx_timer_args = {
		.callback = tx_wait_timer_callback,
		.arg = NULL,
		.name = "txtimer"
	};
	ESP_ERROR_CHECK(esp_timer_create(&tx_timer_args, &tx_timer_handle));

	txWaitSemaphore = xSemaphoreCreateBinary();
//	txWaitAckSemaphore = xSemaphoreCreateBinary();
//	xQueueHibusTx = xQueueCreate(32, sizeof(hibus_tx_message_t));

	// avvio i task 
	xTaskCreate(HibusTxTask,"hibusTxTask",1024*3,NULL,5,NULL);	

}

/**************************************************************************************************
 *	HIBUS INIT
 ***************************************************************************************************/
void hibusInit(hibus_config_t *hibus_config){
	

#ifdef TESTPOINT_A
    gpio_set_direction(TESTPOINT_A, GPIO_MODE_OUTPUT);
    gpio_set_level(TESTPOINT_A,  0);	
#endif
#ifdef TESTPOINT_B
    gpio_set_direction(TESTPOINT_B, GPIO_MODE_OUTPUT);
    gpio_set_level(TESTPOINT_B,  0);	
#endif
	
	// init queue shared rx tx
	xQueueHibusWaitTxResponse = xQueueCreate(1, sizeof(int));
	
	memset(&hibus_errors, 0, sizeof(hibus_errors));
	
	// start rx task on cpu1 high priority
	xTaskCreatePinnedToCore(
		HibusRxTask, 
		"hibusRxTask", 
		1024*3, 
		hibus_config, 
		12,
		&hibusRxTaskHandler, 
		1
	);	

	hibusTxInit(hibus_config);

}
 
 
 
 
 