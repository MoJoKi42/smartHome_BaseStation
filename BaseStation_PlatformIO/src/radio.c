/////////////////////////////////////////////////////
// FILENAME:    radio.c                            //
// DESCRIPTION: function handler for RFM69 modules //
// AUTHOR:      Moritz Kimmig                      //
// DATE:        see header                         //
// VERSION:     see header                         //
/////////////////////////////////////////////////////

#include <string.h>
#include <stdlib.h>
#include "radio.h"

typedef struct{
	uint8_t part;
	uint8_t parts_total;
	uint8_t reserved;
	uint8_t crc8;
} radio_header_t;

typedef enum {
	radio_BUFFER_TX,
	radio_BUFFER_RX
} radio_buffer_t;

#define RADIO_MSG_MAX_LENGTH_RFM  61
#define RADIO_MSG_HEADER_SIZE    (uint8_t) sizeof(radio_header_t)
#define RADIO_MSG_MAX_DATA_SIZE  (uint8_t)(RADIO_MSG_MAX_LENGTH_RFM - RADIO_MSG_HEADER_SIZE)
#define RADIO_RFM_DELAY_BEFORE_ACK_TIME 5 // ms


/* Private function prototypes ------------------------------------------------------------------*/

void     radio_throw_error     (radio_t* obj, radio_error_code_t error);
uint8_t  radio_cal_CRC         (radio_t* obj, uint8_t* data, uint16_t len);
uint8_t* radio_generate_tx_data(radio_t* obj, radio_message_t* msg);

// buffer functions
void radio_buffer_rx_add             (radio_t* obj, uint8_t src,  uint8_t* data, uint8_t len);
void radio_buffer_tx_add             (radio_t* obj, uint8_t dest, uint8_t* data, uint16_t len);
void radio_buffer_rx_merge_single_msg(radio_t* obj);
uint8_t radio_buffer_tx_get          (radio_t* obj, radio_message_t* msg);
void radio_buffer_rx_get             (radio_t* obj, radio_message_t* msg);
void radio_buffer_sort               (radio_t* obj, radio_buffer_t buffer);

// header functions
void    radio_generate_header       (radio_t* obj, radio_header_t* header, radio_message_t* msg);
uint8_t radio_header_get_PART       (radio_t* obj, radio_header_t* header);
uint8_t radio_header_get_PARTS_TOTAL(radio_t* obj, radio_header_t* header);
uint8_t radio_header_get_CRC        (radio_t* obj, radio_header_t* header);
uint8_t radio_header_del_CRC        (radio_t* obj, radio_header_t* header);
uint8_t radio_header_cal_CRC        (radio_t* obj, radio_message_t* msg);


/* Public functions -----------------------------------------------------------------------------*/

void radio_init (radio_t* obj, uint8_t address) {
	obj->address = address;
	obj->error_cnt = 0;
	for (int i=0; i<RADIO_BUFFER_RX_SIZE; i++) { obj->buffer_rx[i].valid = false; }
	for (int i=0; i<RADIO_BUFFER_TX_SIZE; i++) { obj->buffer_tx[i].valid = false; }
	obj->rfm_transmit = NULL;
	obj->rfm_receive = NULL;
	obj->rfm_sendACK = NULL;
	obj->rfm_ACKReceived = NULL;
	obj->rfm_ACKRequested = NULL;
	obj->rfm_receiveDone = NULL;
	obj->receive = NULL;
	obj->delay = NULL;
	obj->error_handler = NULL;
}

void radio_set_cb_rfm(radio_t* obj, void* transmit, void* receive, void* sendACK, void* ACKReceived, void* ACKRequested, void* receiveDone) {
	if (transmit != NULL)		{ obj->rfm_transmit =		transmit; }
	if (receive != NULL)		{ obj->rfm_receive =		receive; }
	if (sendACK != NULL)		{ obj->rfm_sendACK =		sendACK; }
	if (ACKReceived != NULL)    { obj->rfm_ACKReceived =    ACKReceived; }
	if (ACKRequested != NULL)	{ obj->rfm_ACKRequested =	ACKRequested; }
	if (receiveDone != NULL)	{ obj->rfm_receiveDone =	receiveDone; }
}

void radio_set_cb_func(radio_t* obj, void* receive, void* delay, void* error_handler) {
	if (receive != NULL)	   { obj->receive =       receive; }
	if (delay != NULL)		   { obj->delay =         delay; }
	if (error_handler != NULL) { obj->error_handler = error_handler; }
}

void radio_loop (radio_t* obj) {
	
	// RX: check for new data
	if(obj->rfm_receiveDone()) {
		
		// receive
		uint8_t buffer[RADIO_MSG_MAX_LENGTH_RFM];
		uint8_t source = 0x00;
		uint8_t len = 0;
		obj->rfm_receive(&source, buffer, &len);
		
		// add to RX buffer
		if (len > RADIO_MSG_HEADER_SIZE)
			radio_buffer_rx_add(obj, source, buffer, len);

		// send ACK
		if (obj->rfm_ACKRequested(source)) {
			if (RADIO_RFM_DELAY_BEFORE_ACK) {
				obj->delay(RADIO_RFM_DELAY_BEFORE_ACK_TIME);
			}
			obj->rfm_sendACK(source);
		}
	}
	
	// Process RX data
	if (!radio_buffer_empty_rx(obj)) {
		
		// merge splitted messages
		radio_buffer_rx_merge_single_msg(obj);
		
		// get next msg (a not splited one)
		radio_message_t msg;
		radio_buffer_rx_get(obj, &msg);
		if (msg.valid == true) {
			
			// call external receive function
			if (obj->receive != NULL)
				obj->receive(msg.source, msg.data, msg.data_length);
			free(msg.data);
		}

		// sort buffer
		radio_buffer_sort(obj, radio_BUFFER_RX);
	}
	
	// TX: send next message from TX buffer
	if (!radio_buffer_empty_tx(obj)) {
		
		// get next msg
		radio_message_t msg;
		uint8_t tx_buffer_pos = radio_buffer_tx_get(obj, &msg);
		uint8_t* data = radio_generate_tx_data(obj, &msg);

		// send
		if (msg.data_length && data != NULL) {

			// transmit
			obj->rfm_transmit(msg.destination, data, (uint8_t)msg.data_length + RADIO_MSG_HEADER_SIZE);

			// TEST ###############################################################################################################
			//radio_buffer_rx_add(obj, msg.destination, data, (uint8_t)msg.data_length + RADIO_MSG_HEADER_SIZE);

			// handle ACK
			bool ACKReceived = true;
			uint16_t wait_time_ACK = 0;
			while (!obj->rfm_ACKReceived(msg.destination)) {
				if (wait_time_ACK > RADIO_RFM_MAX_ACK_TIMEOUT) {
					ACKReceived = false;
					break;
				}
				obj->delay(1);
				wait_time_ACK++;
			}
			if (ACKReceived) {
				free(msg.data);
			} else {
				obj->buffer_tx[tx_buffer_pos].retries++;
				if (obj->buffer_tx[tx_buffer_pos].retries >= RADIO_RFM_MAX_RETRIES) {
					obj->buffer_tx[tx_buffer_pos].valid = false; // remove from TX buffer
					free(msg.data);
					radio_throw_error(obj, radio_error_RFM_ACK_TIMEOUT);
				} else {
					obj->buffer_tx[tx_buffer_pos].valid = true; // keep valid for next transmission attempt
				}
			}
		}

		// free data
		if (data != NULL) { free(data); }

		// sort buffer
		radio_buffer_sort(obj, radio_BUFFER_TX);
	}
}

bool radio_buffer_empty_tx(radio_t* obj) {
	for (int i = 0; i < RADIO_BUFFER_TX_SIZE; i++) {
		if (obj->buffer_tx[i].valid == true) {
			return false;
		}
	}
	return true;
}

bool radio_buffer_empty_rx(radio_t* obj) {
	for (int i = 0; i < RADIO_BUFFER_RX_SIZE; i++) {
		if (obj->buffer_rx[i].valid == true) {
			return false;
		}
	}
	return true;
}

void radio_transmit(radio_t* obj, uint8_t dest, uint8_t* data, uint8_t len) {
	if (data == NULL || len == 0) { return; }
	radio_buffer_tx_add(obj, dest, data, len);
}


/* Private functions ----------------------------------------------------------------------------*/

void radio_generate_header(radio_t* obj, radio_header_t* header, radio_message_t* msg) {
	header->reserved =    0;
	header->part =        msg->part;
	header->parts_total = msg->parts_total;
	header->crc8 =        0x00;
}

uint8_t* radio_generate_tx_data(radio_t* obj, radio_message_t* msg) {
	uint8_t* data = malloc(RADIO_MSG_HEADER_SIZE + msg->data_length);
	if (data == NULL) {
		radio_throw_error(obj, radio_error_RAM_FULL);
		return NULL;
	}

	radio_header_t header;
	radio_generate_header(obj, &header, msg);
	memcpy(data, &header, RADIO_MSG_HEADER_SIZE);						// add header
	memcpy(data + RADIO_MSG_HEADER_SIZE, msg->data, msg->data_length);  // add data
	((radio_header_t*)data)->crc8 = radio_cal_CRC(obj, data, RADIO_MSG_HEADER_SIZE + msg->data_length);
	return data;
}

void radio_buffer_rx_add (radio_t* obj, uint8_t src, uint8_t* data, uint8_t len) {
	if (data == NULL || len == 0) { return; }

	// check CRC
	uint8_t crc_received = radio_header_get_CRC(obj, (radio_header_t*)data);
	radio_header_del_CRC(obj, (radio_header_t*)data);
	uint8_t crc_calulated = radio_cal_CRC(obj, data, len);
	if (crc_received != crc_calulated) {
		radio_throw_error(obj, radio_error_RX_CRC_WRONG);
		return;
	}

	// get next free buffer slot
	uint8_t pos = 0; bool pos_found = false;
	for (int i = 0; i < RADIO_BUFFER_RX_SIZE; i++) {
		if (obj->buffer_rx[i].valid == false) {
			pos_found = true;
			pos = i;
			break;
		}
	}
	if (!pos_found) {
		radio_throw_error(obj, radio_error_RX_BUFFER_FULL);
		return;
	}

	// allocate bytes for message
	uint8_t data_length = len - RADIO_MSG_HEADER_SIZE;
	obj->buffer_rx[pos].data = malloc(data_length);
	if (obj->buffer_rx[pos].data == NULL) {
		radio_throw_error(obj, radio_error_RAM_FULL);
		return;
	}

	// copy data
	obj->buffer_rx[pos].valid =			true;
	obj->buffer_rx[pos].source =		src;
	obj->buffer_rx[pos].destination =	obj->address;
	obj->buffer_rx[pos].data_length =	data_length;
	memcpy(obj->buffer_rx[pos].data, data + RADIO_MSG_HEADER_SIZE, data_length);
	obj->buffer_rx[pos].part =			radio_header_get_PART       (obj, (radio_header_t*)data);
	obj->buffer_rx[pos].parts_total =	radio_header_get_PARTS_TOTAL(obj, (radio_header_t*)data);
	obj->buffer_rx[pos].retries =       0;
}

void radio_buffer_tx_add (radio_t* obj, uint8_t dest, uint8_t* data, uint16_t len) {
	if (data == NULL || len == 0) { return; }

	// get positions of all free buffer slots
	uint8_t pos[RADIO_BUFFER_TX_SIZE]; uint8_t pos_count = 0;
	for (int i = 0; i < RADIO_BUFFER_TX_SIZE; i++) {
		if (obj->buffer_tx[i].valid == false) {
			pos[pos_count] = i;
			pos_count++;
		}
	}

	// calculate number of single packets
	uint8_t MSG_MAX_DATA_SIZE = RADIO_MSG_MAX_DATA_SIZE;
	uint8_t single_packets = ((len - 1) / MSG_MAX_DATA_SIZE) + 1;
	if (single_packets > pos_count) {
		radio_throw_error(obj, radio_error_TX_BUFFER_FULL);
		return;
	}

	for (int i = 0; i < single_packets; i++) {

		// calculate parameters
		uint8_t data_length = 0;
		if (i + 1 == single_packets) {
			data_length = len % MSG_MAX_DATA_SIZE;
			if (!data_length) { data_length = MSG_MAX_DATA_SIZE; }
		} else {
			data_length = MSG_MAX_DATA_SIZE;
		}
		uint16_t pointer_offset = (uint16_t)(i * MSG_MAX_DATA_SIZE);

		// allocate bytes for message
		obj->buffer_tx[pos[i]].data = malloc(data_length);
		if (obj->buffer_tx[pos[i]].data == NULL) {
			radio_throw_error(obj, radio_error_RAM_FULL);
			return;
		}

		// copy data
		obj->buffer_tx[pos[i]].valid = true;
		obj->buffer_tx[pos[i]].source = obj->address;
		obj->buffer_tx[pos[i]].destination = dest;
		obj->buffer_tx[pos[i]].data_length = data_length;
		memcpy(obj->buffer_tx[pos[i]].data, data + pointer_offset, data_length);
		obj->buffer_tx[pos[i]].part = i;
		obj->buffer_tx[pos[i]].parts_total = single_packets;
		obj->buffer_tx[pos[i]].retries = 0;
	}
}

void radio_buffer_rx_merge_single_msg(radio_t* obj) {

	typedef struct {
		bool valid;
		uint8_t buffer_pos;
	} positions_t;

	// check for splitted messages
	for (int i = 0; i < RADIO_BUFFER_RX_SIZE; i++) {
		if (obj->buffer_rx[i].valid == true && obj->buffer_rx[i].parts_total > 1) {
			uint8_t address = obj->buffer_rx[i].source;
			uint8_t parts_total = obj->buffer_rx[i].parts_total;

			// check if splitted message is complete + get positions
			uint8_t msg_cnt = 0;
			positions_t pos[RADIO_BUFFER_RX_SIZE];
			for (int j = 0; j < RADIO_BUFFER_RX_SIZE; j++) { pos[j].valid = false; }
			for (int j = 0; j < RADIO_BUFFER_RX_SIZE; j++) {
				if (obj->buffer_rx[j].valid == true && obj->buffer_rx[j].parts_total == parts_total && obj->buffer_rx[j].source == address) {
					pos[obj->buffer_rx[j].part % RADIO_BUFFER_RX_SIZE].valid = true;
					pos[obj->buffer_rx[j].part % RADIO_BUFFER_RX_SIZE].buffer_pos = j;
					msg_cnt++;
				}
			}
			if (msg_cnt != parts_total) { return; } // parts are missing
			for (int j = 0; j < parts_total; j++) {
				if (pos[j].valid == false) { return; } // parts are missing (e.g. in case of the same part number twice)
			}

			// allocate bytes for full message
			uint8_t data_length = 0;
			for (int j = 0; j < parts_total; j++) {
				data_length = data_length + obj->buffer_rx[pos[j].buffer_pos].data_length;
			}
			uint8_t* data = malloc(data_length);
			if (data == NULL) {
				radio_throw_error(obj, radio_error_RAM_FULL);
				return;
			}

			// copy data
			uint8_t pos_first = pos[0].buffer_pos;
			obj->buffer_rx[pos_first].valid = true;
			obj->buffer_rx[pos_first].source = address;
			obj->buffer_rx[pos_first].destination = obj->address;
			uint16_t pointer_offset = 0;
			for (int j = 0; j < parts_total; j++) {
				memcpy(data + pointer_offset, obj->buffer_rx[pos[j].buffer_pos].data, obj->buffer_rx[pos[j].buffer_pos].data_length);
				pointer_offset = pointer_offset + obj->buffer_rx[pos[j].buffer_pos].data_length;
			}
			obj->buffer_rx[pos_first].data_length = data_length;
			obj->buffer_rx[pos_first].part =		0;
			obj->buffer_rx[pos_first].parts_total = 1;

			// free + delete old splitted messages
			for (int j = 0; j < parts_total; j++) {
				free(obj->buffer_rx[pos[j].buffer_pos].data);
				obj->buffer_rx[pos[j].buffer_pos].valid = false;
			}
			obj->buffer_rx[pos_first].valid = true;
			obj->buffer_rx[pos_first].data  = data;
		}
	}
}

uint8_t radio_buffer_tx_get(radio_t* obj, radio_message_t* msg) {
	
	// get next msg
	uint8_t pos = 0; bool pos_found = false;
	for (int i=0; i<RADIO_BUFFER_TX_SIZE; i++) {
		if (obj->buffer_tx[i].valid == true) {
			pos_found = true;
			pos = i;
			break;
		}
	}
	if (!pos_found) {
		msg->valid = false;
		return 0;
	}

	// copy data and return
	memcpy(msg, &obj->buffer_tx[pos], sizeof(radio_message_t));
	obj->buffer_tx[pos].valid = false;
	return pos;
}

void radio_buffer_rx_get(radio_t* obj, radio_message_t* msg) {
	
	// get next (not splitted) msg
	uint8_t pos = 0; bool pos_found = false;
	for (int i = 0; i < RADIO_BUFFER_RX_SIZE; i++) {
		if (obj->buffer_rx[i].valid == true && obj->buffer_rx[i].parts_total == 1) {
			pos_found = true;
			pos = i;
			break;
		}
	}
	if (!pos_found) {
		msg->valid = false;
		return;
	}

	// copy data and return
	memcpy(msg, &obj->buffer_rx[pos], sizeof(radio_message_t));
	obj->buffer_rx[pos].valid = false;
	return;
}

void radio_buffer_sort(radio_t* obj, radio_buffer_t buffer) {

	// set parameter
	uint16_t         BUFFER_SIZE = 0;
	radio_message_t* BUFFER = NULL;
	switch (buffer) {
		case radio_BUFFER_TX: BUFFER_SIZE = RADIO_BUFFER_TX_SIZE; BUFFER = obj->buffer_tx; break;
		case radio_BUFFER_RX: BUFFER_SIZE = RADIO_BUFFER_RX_SIZE; BUFFER = obj->buffer_rx; break;
		default: return; break;
	}

	// find next gap
	for (int pos_gap = 0; pos_gap < BUFFER_SIZE; pos_gap++) {
		if (BUFFER[pos_gap].valid == false) {

			// find next valid item
			for (int pos = pos_gap + 1; pos < BUFFER_SIZE; pos++) {
				if (BUFFER[pos].valid == true) {

					// move item to gap position
					BUFFER[pos_gap] = BUFFER[pos];
					BUFFER[pos].valid = false;
					break;
				}
			}
		}
	}
}

void radio_throw_error(radio_t* obj, radio_error_code_t error) {
	obj->error_cnt++;
	if (obj->error_handler != NULL) {
		obj->error_handler(error);
	}
}

uint8_t radio_header_get_PART(radio_t* obj, radio_header_t* header) {
	return header->part;
}

uint8_t radio_header_get_PARTS_TOTAL(radio_t* obj, radio_header_t* header) {
	return header->parts_total;
}

uint8_t radio_header_get_CRC(radio_t* obj, radio_header_t* header) {
	return header->crc8;
}

uint8_t radio_header_del_CRC(radio_t* obj, radio_header_t* header) {
	header->crc8 = 0x00;
	return 0;
}

uint8_t radio_header_cal_CRC(radio_t* obj, radio_message_t* msg) {

	// create header + data
	uint16_t len = RADIO_MSG_HEADER_SIZE + msg->data_length;
	uint8_t* data = radio_generate_tx_data(obj, msg);
	if (data == NULL) {
		radio_throw_error(obj, radio_error_RAM_FULL);
		return 0x00;
	}

	// crc
	uint8_t crc = 0xff;
	size_t i, j;
	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (j = 0; j < 8; j++) {
			if ((crc & 0x80) != 0)
				crc = (uint8_t)((crc << 1) ^ 0x31);
			else
				crc <<= 1;
		}
	}
	free(data);
	return crc;
}

uint8_t radio_cal_CRC(radio_t* obj, uint8_t* data, uint16_t len) {
	if (data == NULL) { return 0x00; }
	uint8_t crc = 0xff;
	size_t i, j;
	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (j = 0; j < 8; j++) {
			if ((crc & 0x80) != 0)
				crc = (uint8_t)((crc << 1) ^ 0x31);
			else
				crc <<= 1;
		}
	}
	return crc;
}