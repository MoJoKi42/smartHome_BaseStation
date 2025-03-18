/////////////////////////////////////////////////////
// FILENAME:    radio.h                            //
// DESCRIPTION: function handler for RFM69 modules //
// AUTHOR:      Moritz Kimmig                      //
// DATE:        30.05.2024                         //
// VERSION:     0.1                                //
/////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include "radio_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	radio_error_RX_BUFFER_FULL,               // RX buffer full
	radio_error_TX_BUFFER_FULL,               // TX buffer full
	radio_error_RAM_FULL,                     // no more memory allocation possible with malloc()
	radio_error_RX_CRC_WRONG,                 // CRC of received message is wrong
	radio_error_RFM_ACK_TIMEOUT               // ACK timeout after the configured number of retries
} radio_error_code_t;

typedef struct {
	bool valid;
	uint8_t source;
	uint8_t destination;
	uint16_t data_length;
	uint8_t* data;
	uint8_t part;
	uint8_t parts_total;
	uint8_t retries;
} radio_message_t;

typedef struct {
	uint8_t address;
	uint16_t error_cnt;
	radio_message_t buffer_rx[RADIO_BUFFER_RX_SIZE];
	radio_message_t buffer_tx[RADIO_BUFFER_TX_SIZE];

	// rfm functions
	uint8_t(*rfm_transmit)    (uint8_t dest, uint8_t* data, uint8_t  len);
	uint8_t(*rfm_receive)     (uint8_t* src, uint8_t* data, uint8_t* len);
	uint8_t(*rfm_sendACK)     (uint8_t dest);
	uint8_t(*rfm_ACKReceived) (uint8_t dest);
	uint8_t(*rfm_ACKRequested)(uint8_t src);
	uint8_t(*rfm_receiveDone) (void);

	// other external functions
	// error_handler() and receive() are optional
	void (*delay)        (uint32_t ms);
	void (*error_handler)(radio_error_code_t error);
	void (*receive)      (uint8_t source, uint8_t* data, uint16_t len);
} radio_t;


/* Public function prototypes -------------------------------------------------------------------*/

void radio_init(radio_t* obj, uint8_t address);
void radio_set_cb_rfm(radio_t* obj, void* transmit, void* receive, void* sendACK, void* ACKReceived, void* ACKRequested, void* receiveDone);
void radio_set_cb_func(radio_t* obj, void* receive, void* delay, void* error_handler);
void radio_loop(radio_t* obj);

bool radio_buffer_empty_rx(radio_t* obj);
bool radio_buffer_empty_tx(radio_t* obj);
void radio_transmit(radio_t* obj, uint8_t dest, uint8_t* data, uint8_t len);


#ifdef __cplusplus
}
#endif