#define LED_status_ESP_active   0
#define LED_status_data_RX      1
#define LED_status_data_TX      2
#define LED_status_LAN_blue     3
#define LED_status_LAN_green    4
#define LED_status_LAN_red      5
#define LED_status_error_green  6
#define LED_status_error_red    7

#define PIN_CS_RFM      17
#define PIN_INT_RFM     25
#define PIN_CS_W5500    16
#define PIN_BUTTON      4


#define NODEID          0x01    // keep UNIQUE for each node on same network
#define NETWORKID       33      // keep IDENTICAL on all nodes that talk to each other
#define FREQUENCY       RF69_433MHZ
#define ENCRYPTKEY      "1234567812345678"


// MQTT / Ethernet
// Source: https://github.com/jozala/ESP32_W5500_MQTT
#define ETHERNET_MAC            "FF:FF:FF:FF:FF:FF" // Ethernet MAC address
#define ETHERNET_IP             "192.168.150.100"   // IP address of Ethernet connection
#define ETHERNET_RESET_PIN      33                  // ESP32 pin where reset pin from W5500 is connected
#define ETHERNET_CS_PIN         16                  // ESP32 pin where CS pin from W5500 is connected
#define MQTT_HOSTNAME           "192.168.150.101"
#define MQTT_PORT                   1883
#define MQTT_PUBLISH_INTERVAL_MS    250


// MQTT tree example
/*
base_0x01_rx
           ├── nodes_0x10
           │            ├── node_0x11 = {...}
           │            └── node_0x12 = {...}
           └── nodes_0x20
                        ├── node_0x21 = {...}
                        └── node_0x22 = {...}
base_0x01_tx
           ├── node_0x11 = {...}
           └── node_0x32 = {...}

base_0x01_stats
              ├── uptime = 
              ├── packets_rx = 
              └── packets_tx =
*/
