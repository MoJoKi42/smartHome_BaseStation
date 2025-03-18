/////////////////////////////////////////////////////
// FILENAME:    radio_config.h                     //
// DESCRIPTION: config file for radio.c / .h       //
// AUTHOR:      Moritz Kimmig                      //
// DATE:        see header                         //
// VERSION:     see header                         //
/////////////////////////////////////////////////////

// internal message buffer size
#define RADIO_BUFFER_RX_SIZE      50
#define RADIO_BUFFER_TX_SIZE      50

// time before ACK timeout (milliseconds)
#define RADIO_RFM_MAX_ACK_TIMEOUT 200

// number of transmission retries before discard sending process
#define RADIO_RFM_MAX_RETRIES     3

// do a delay before sending ACK
#define RADIO_RFM_DELAY_BEFORE_ACK true
