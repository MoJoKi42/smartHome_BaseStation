#include <stdlib.h>
#include <string.h>
#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "disp.h"


/* Private define -------------------------------------------------------------------------------*/

#define DISP_SCREENSAVER 99


/* Private function prototypes ------------------------------------------------------------------*/

uint32_t disp_time_func(uint32_t time_diff);
void     disp_sort_slave_list(disp_t* obj);
bool     disp_get_voltage_from_json_string(float *voltage, char *string);
void 	 disp_print_item(disp_t* obj, uint8_t item_num);

void disp_write_page_1(disp_t* obj);	  // general information
void disp_write_page_2(disp_t* obj);	  // last msg RX
void disp_write_page_3(disp_t* obj);	  // last msg TX
void disp_write_page_4_to_x(disp_t* obj); // slave list


/* Public functions -----------------------------------------------------------------------------*/

void disp_init(disp_t* obj, Adafruit_SSD1306 *display, const char* ip_base, const char* ip_mqtt) {
	if (obj != NULL) {

		// init variables
		obj->current_page = 0;
		obj->last_page = 0;
		obj->sort_type = disp_sort_type__by_TIME;
		obj->uptime_hours = 0;
		obj->uptime_seconds = disp_time_func(0);
		obj->time_display = disp_time_func(0);
		for (int i = 0; i < DISP_MAX_IP_SIZE; i++) { obj->addr_ip[i] = 0; }
		for (int i = 0; i < DISP_MAX_IP_SIZE; i++) { obj->addr_mqtt[i] = 0; }
		obj->rx_cnt = 0;
		obj->tx_cnt = 0;
		obj->last_msg_rx_valid = false;
		for (int i = 0; i < DISP_MAX_MSG_SIZE; i++) { obj->last_msg_rx[i] = 0; }
		obj->last_msg_tx_valid = false;
		for (int i = 0; i < DISP_MAX_MSG_SIZE; i++) { obj->last_msg_tx[i] = 0; }
		for (int i = 0; i < DISP_SLAVE_LIST_SIZE; i++) { obj->slave_list[i].valid = false; }

		obj->display = display;
		strcpy(obj->addr_ip, ip_base);
		strcpy(obj->addr_mqtt, ip_mqtt);

		// init display
		obj->display->clearDisplay();
		obj->display->setCursor(0,0);
		obj->display->display();
	}
}

void disp_refresh_display(disp_t* obj) {
	if (obj != NULL) {

		// check uptime value.
		if ((disp_time_func(obj->uptime_seconds) / 1000) >= 3600) { // 3600 seconds = 1 hour
			obj->uptime_hours++;
			obj->uptime_seconds = disp_time_func(disp_time_func(obj->uptime_seconds) % (3600 * 1000));
		}

		// check time of every slave in slave_list
		for (int i=0; i<DISP_SLAVE_LIST_SIZE; i++) {
			if ((disp_time_func(obj->slave_list[i].time) / 1000) >= 9999) { // 9999 seconds = 2.7 hours
				obj->slave_list[i].time_valid = false;
			}
		}

		// check time of last msg RX + TX
		if ((disp_time_func(obj->last_msg_rx_time) / 1000) >= 86400) { // 86400 seconds = 24 hours
			obj->last_msg_rx_valid = false;
		}

		// enable screensaver
		if (((disp_time_func(obj->time_display) / 1000) >= DISP_SCREENSAVER_TIME) && obj->current_page != DISP_SCREENSAVER) {
			obj->last_page = obj->current_page;
			obj->current_page = DISP_SCREENSAVER;
		}

		// sort slave_list
		disp_sort_slave_list(obj);

		// refresh display
		obj->display->clearDisplay();
		obj->display->setCursor(0,0);
		switch (obj->current_page) {
			case 0: disp_write_page_1(obj);			break;
			case 1: disp_write_page_2(obj);			break;
			case 2: disp_write_page_3(obj);			break;
			case DISP_SCREENSAVER: 					break;
			default: disp_write_page_4_to_x(obj);	break;
		}
		obj->display->display();
	}
}

void disp_add_rx(disp_t* obj, uint8_t addr, char* data, uint16_t data_length) {
	if (obj != NULL && data_length) {

		// increase rx_cnt
		obj->rx_cnt++;

		// set last msg rx
		obj->last_msg_rx_valid = true;
		obj->last_msg_rx_addr = addr;
		obj->last_msg_rx_time = disp_time_func(0);
		for (int i = 0; i < data_length && i < DISP_MAX_MSG_SIZE - 1; i++) {
			if (data[i] >= 32 && data[i] <= 126) {
				obj->last_msg_rx[i] = data[i];
			} else {
				obj->last_msg_rx[i] = '.';
			}
		}
		if (data_length >= DISP_MAX_MSG_SIZE) { obj->last_msg_rx[DISP_MAX_MSG_SIZE - 1] = '\0'; }
		if (data_length <  DISP_MAX_MSG_SIZE) { obj->last_msg_rx[data_length] = '\0'; }

		//////////////////////////////////////////////
		// add or replace slave in slave_list
		//////////////////////////////////////////////

		// 1. find slave in slave_list
		uint8_t position = DISP_SLAVE_LIST_SIZE;
		for (int i = 0; i < DISP_SLAVE_LIST_SIZE; i++) {
			if (obj->slave_list[i].valid == true && obj->slave_list[i].addr == addr) {
				position = i;
				break;
			}
		}

		// 2. find empty position or replace the last item (in case list is full)
		if (position == DISP_SLAVE_LIST_SIZE) {
			for (int i = 0; i < DISP_SLAVE_LIST_SIZE; i++) {
				if (obj->slave_list[i].valid == false) {
					position = i;
					break;
				}
			}
			if (position == DISP_SLAVE_LIST_SIZE) {
				position = DISP_SLAVE_LIST_SIZE - 1;
			}
		}

		// 3. write values
		obj->slave_list[position].valid = true;
		obj->slave_list[position].addr = addr;
		obj->slave_list[position].voltage_valid = disp_get_voltage_from_json_string(&obj->slave_list[position].voltage, obj->last_msg_rx);
		obj->slave_list[position].time = disp_time_func(0);
		obj->slave_list[position].time_valid = true;
	}
}

void disp_add_tx(disp_t* obj, uint8_t addr, char* data, uint16_t data_length) {
	if (obj != NULL && data_length) {

		// increase tx_cnt
		obj->tx_cnt++;

		// set last msg tx
		obj->last_msg_tx_valid = true;
		obj->last_msg_tx_addr = addr;
		obj->last_msg_tx_time = disp_time_func(0);
		for (int i = 0; i < data_length && i < DISP_MAX_MSG_SIZE - 1; i++) {
			if (data[i] >= 32 && data[i] <= 126) {
				obj->last_msg_tx[i] = data[i];
			}
			else {
				obj->last_msg_tx[i] = '.';
			}
		}
		if (data_length >= DISP_MAX_MSG_SIZE) { obj->last_msg_tx[DISP_MAX_MSG_SIZE - 1] = '\0'; }
		if (data_length <  DISP_MAX_MSG_SIZE) { obj->last_msg_tx[data_length] = '\0'; }
	}
}

void disp_set_frist_page(disp_t* obj) {
	obj->current_page = 0;
}

void disp_set_next_page(disp_t* obj) {
	obj->time_display = disp_time_func(0);
	if (obj->current_page == DISP_SCREENSAVER) {
		obj->current_page = obj->last_page;
	} else {
		obj->current_page++;
	}
	uint8_t number_of_pages = 1 + 1 + 1 + DISP_SLAVE_LIST_PAGES;
	if (obj->current_page >= number_of_pages) {
		disp_set_frist_page(obj);
	}
}


/* Private functions ----------------------------------------------------------------------------*/

uint32_t disp_time_func(uint32_t time_diff) {
	uint32_t time = millis();
	time = (0x7FFFFFFF & time) - (0x7FFFFFFF & time_diff);
	if ( time & 0x80000000) {
		time = time + 0x80000000;
	}
	return time;
}

void disp_sort_slave_list(disp_t* obj) {

}

// true  = ok
// false = no voltage found
bool disp_get_voltage_from_json_string(float *voltage, char *str) {

	// get position of first number character
	char parameter[] = "batt";
	char* position_ptr = strstr(str, parameter);
	int position = (position_ptr == NULL ? -1 : position_ptr - str);
	position = position + sizeof(parameter) + 1;
	if (position_ptr == NULL) {
		*voltage = 0.0;
		return false; // nothing found!
	}

	// copy number into "voltage_str"
	char voltage_str[20];
	for (int i = 0; i < sizeof(voltage_str); i++) { voltage_str[i] = '\0'; }
	for (int i = 0; i < sizeof(voltage_str) && (i + position) < strlen(str); i++) {
		if ((str[i + position] <= '9' && str[i + position] >= '0') || str[i + position] == '.') {
			voltage_str[i] = str[i + position];
		} else {
			break;
		}
	}

	// convert "voltage_str" string to float
	*voltage = atof(voltage_str);
	return true;
}

void disp_write_page_1(disp_t* obj) {

	obj->display->println("-- Smart Home Base --");
	obj->display->print("uptime: ");
	obj->display->print(obj->uptime_hours); 								obj->display->print("h ");
	obj->display->print((disp_time_func(obj->uptime_seconds)/ 1000)/ 60); 	obj->display->print("m ");
	obj->display->print((disp_time_func(obj->uptime_seconds)/ 1000)% 60); 	obj->display->println("s");
	
	obj->display->print("ip:   "); obj->display->println(obj->addr_ip);
	obj->display->print("mqtt: "); obj->display->println(obj->addr_mqtt);
	obj->display->print("rx_cnt: "); obj->display->println(obj->rx_cnt);
	obj->display->print("tx_cnt: "); obj->display->println(obj->tx_cnt);
}

void disp_write_page_2(disp_t* obj) {

	obj->display->println("--   Last RX msg   --");
	if (!obj->last_msg_rx_valid) { return; }

	obj->display->print("Source: 0x");
	char addr_hex[3]; sprintf(addr_hex, "%x", obj->last_msg_rx_addr);
	obj->display->println(addr_hex);
	obj->display->print("Time:   ");
	obj->display->print((disp_time_func(obj->last_msg_rx_time)/ 1000)/ 60); 	obj->display->print("m ");
	obj->display->print((disp_time_func(obj->last_msg_rx_time)/ 1000)% 60); 	obj->display->println("s");
	obj->display->println(" ");
	obj->display->println(obj->last_msg_rx);
	return;
}

void disp_write_page_3(disp_t* obj) {

	obj->display->println("--   Last TX msg   --");
	if (!obj->last_msg_tx_valid) { return; }

	obj->display->print("Dest: 0x");
	char addr_hex[3]; sprintf(addr_hex, "%.2x", obj->last_msg_tx_addr);
	obj->display->println(addr_hex);
	obj->display->print("Time: ");
	obj->display->print((disp_time_func(obj->last_msg_tx_time)/ 1000)/ 60); 	obj->display->print("m ");
	obj->display->print((disp_time_func(obj->last_msg_tx_time)/ 1000)% 60); 	obj->display->println("s");
	obj->display->println(" ");
	obj->display->println(obj->last_msg_tx);
	return;
}

void disp_write_page_4_to_x(disp_t* obj) {

	uint8_t page_offset = 3;
	obj->display->print("-- Slave List (");
	obj->display->print(obj->current_page - page_offset + 1);
	obj->display->print("/");
	obj->display->print(DISP_SLAVE_LIST_PAGES);
	obj->display->println(")--");

	obj->display->println("Addr  Voltage   Time ");
	obj->display->println(" ");
	for (int i=0; i<DISP_SLAVE_LIST_ROWS; i++) {
		disp_print_item(obj, ((obj->current_page - page_offset) * DISP_SLAVE_LIST_ROWS) + i);
	}
	return;
}

void disp_print_item(disp_t* obj, uint8_t item_num) {
	if (!obj->slave_list[item_num].valid) { return; }

	// Addr
	obj->display->print("0x");
	char addr_hex[3]; sprintf(addr_hex, "%.2x", obj->slave_list[item_num].addr);
	obj->display->print(addr_hex);

	// Voltage
	obj->display->print("  ");
	if (obj->slave_list[item_num].voltage_valid) {
		char voltage[10]; sprintf(voltage, "%.2f", obj->slave_list[item_num].voltage);
		obj->display->print(voltage);
		obj->display->print(" V");
	} else {
		obj->display->print("-     ");
	}

	// Time
	if (obj->slave_list[item_num].time_valid) {
		obj->display->print("    ");
		obj->display->print((disp_time_func(obj->slave_list[item_num].time)/ 1000)); obj->display->println("s");
	} else {
		obj->display->println("   >9999s");
	}
}