#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <Adafruit_SSD1306.h>


#define DISP_SCREENSAVER_TIME	30
#define DISP_MAX_MSG_SIZE		100
#define DISP_MAX_IP_SIZE		20
#define DISP_SLAVE_LIST_PAGES	3		// Anzahl der Seiten. Siehe "Slave RX List"
#define DISP_SLAVE_LIST_ROWS	5		// Anzahl der Listeneinträge pro Seite. Siehe "Slave RX List"
#define DISP_SLAVE_LIST_SIZE	DISP_SLAVE_LIST_PAGES * DISP_SLAVE_LIST_ROWS


typedef struct {
	bool	valid;
	uint8_t addr;
	float	voltage;
	bool	voltage_valid; 	// false = no battery voltage is parsable
	uint32_t time;
	bool	 time_valid;	// false = time > 9999s
} disp_slave_obj;

typedef enum {
	disp_sort_type__by_ADDR = 0,
	disp_sort_type__by_VOLT = 1,
	disp_sort_type__by_TIME = 2,
} disp_sort_type;

typedef struct {

	uint8_t current_page;
	uint8_t last_page;
	disp_sort_type sort_type;
	Adafruit_SSD1306 *display;
	uint32_t time_display;

	// page 1 - general
	uint32_t uptime_hours;
	uint32_t uptime_seconds;
	char addr_ip[DISP_MAX_IP_SIZE];
	char addr_mqtt[DISP_MAX_IP_SIZE];
	uint32_t rx_cnt;
	uint32_t tx_cnt;

	// page 2 - last msg RX
	bool	 last_msg_rx_valid;
	uint8_t  last_msg_rx_addr;
	uint32_t last_msg_rx_time;
	char last_msg_rx[DISP_MAX_MSG_SIZE];

	// page 3 - last msg TX
	bool	 last_msg_tx_valid;
	uint8_t  last_msg_tx_addr;
	uint32_t last_msg_tx_time;
	char last_msg_tx[DISP_MAX_MSG_SIZE];

	// page 4 to x - slave list
	disp_slave_obj slave_list[DISP_SLAVE_LIST_SIZE];

} disp_t;




void disp_init(disp_t* obj, Adafruit_SSD1306* display, const char* ip_base, const char* ip_mqtt);

void disp_refresh_display(disp_t* obj);
void disp_set_next_page(disp_t* obj);
void disp_set_frist_page(disp_t* obj);

void disp_add_rx(disp_t* obj, uint8_t addr, char* data, uint16_t data_length);
void disp_add_tx(disp_t* obj, uint8_t addr, char* data, uint16_t data_length);

