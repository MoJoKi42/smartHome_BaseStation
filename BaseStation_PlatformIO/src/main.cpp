#include <Arduino.h>
#include <PCF8574.h>
#include <Ethernet.h>
#include <Wire.h>
#include <SPI.h>
#include <RFM69.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PubSubClient.h>
#include "disp.h"
#include "main.h"
#include "radio.h"

// Debug Konsole:
// sudo minicom -D /dev/ttyUSB0 -b 115200
// (to exit: Strg+A -> Q -> Enter)


radio_t radio_drv;
IPAddress ipAddress;
PubSubClient mqttClient;
EthernetClient ethClient;
uint32_t lastMqttPublishTime = 0;


// PCF8574 (for LEDs)
PCF8574 LEDs_PCF8574(0x39);

// OLED
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT);
disp_t disp;

// RFM69
SPIClass * vspi = NULL;
RFM69 radio(PIN_CS_RFM, PIN_INT_RFM, true, vspi);

// prototypes
void macCharArrayToBytes(const char* str, byte* bytes);
void ethernetWizReset(const uint8_t resetPin);
void connectEthernet();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttReconnect();
void publish_mqtt(uint8_t nodeID, char* payload, uint8_t payload_lenght);
uint32_t time_func(uint32_t time_diff);

// prototypes rfm + receive function
uint8_t rfm_transmit(uint8_t dest, uint8_t* data, uint8_t len);
uint8_t rfm_receive(uint8_t* src, uint8_t* data, uint8_t* len);
uint8_t rfm_sendACK(uint8_t dest);
uint8_t rfm_ACKReceived(uint8_t dest);
uint8_t rfm_ACKRequested(uint8_t src);
uint8_t rfm_receiveDone(void);
void receive(uint8_t source, uint8_t* data, uint16_t len);

// variables
static uint32_t time_rxLED = time_func(0);

void setup() {
  Serial.begin(115200);   // init UART
  LEDs_PCF8574.begin();   // init PCF8574
  LEDs_PCF8574.write(LED_status_LAN_red,   0);
  LEDs_PCF8574.write(LED_status_error_red, 0);

  // Outputs
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Display Lib init
  disp_init(&disp, &display, ETHERNET_IP, MQTT_HOSTNAME);

  // MQTT / Ethernet
  connectEthernet();
  ethClient.setConnectionTimeout(1000);
  mqttClient.setClient(ethClient);
  mqttClient.setServer(MQTT_HOSTNAME, MQTT_PORT);
  mqttReconnect();

  // RFM69 init
  vspi = new SPIClass(VSPI);
  radio.initialize(FREQUENCY,NODEID,NETWORKID);
  radio.setHighPower();
  radio.encrypt(ENCRYPTKEY);

  // radio lib init
  radio_init(&radio_drv, NODEID);
  radio_set_cb_rfm (&radio_drv, (void*)rfm_transmit, (void*)rfm_receive, (void*)rfm_sendACK, (void*)rfm_ACKReceived, (void*)rfm_ACKRequested, (void*)rfm_receiveDone);
  radio_set_cb_func(&radio_drv, (void*)receive, (void*)delay, (void*)NULL);
}

void loop() {

  // MQTT / Ethernet connecting functions
  if (!ethClient.connected()) {
    LEDs_PCF8574.write(LED_status_LAN_red, 0);
  } else {
    LEDs_PCF8574.write(LED_status_LAN_red, 1);
  }
  if (!mqttClient.connected()) {
    LEDs_PCF8574.write(LED_status_error_red, 0);
    mqttReconnect();
  } else {
    LEDs_PCF8574.write(LED_status_error_red, 1);
  }
  mqttClient.loop();

  // LED status
  static uint32_t time_StatusLED = time_func(0);
  if (time_func(time_StatusLED) > 800) {
    LEDs_PCF8574.toggle(LED_status_ESP_active);
    time_StatusLED = time_func(0);
  }

  // LEDs tx + rx
  static uint32_t time_txLED = time_func(0);
  if (!radio_buffer_empty_tx(&radio_drv)) {
    LEDs_PCF8574.write(LED_status_data_TX, 0);
    time_txLED = time_func(0);
  }
  if (time_func(time_rxLED) > 200) {
    LEDs_PCF8574.write(LED_status_data_RX, 1);
  }
  if (time_func(time_txLED) > 200) {
    LEDs_PCF8574.write(LED_status_data_TX, 1);
  }

  // radio
  radio_loop(&radio_drv);

  // button
  static uint32_t time_Button01 = time_func(0);
  if (!digitalRead(PIN_BUTTON) && time_func(time_Button01) > 200) {
    disp_set_next_page(&disp);
    time_Button01 = time_func(0);
  }

  // display refresh
  static uint32_t time_DisplayRefresh = time_func(0);
  if (time_func(time_DisplayRefresh) > 100) {
    disp_refresh_display(&disp);
    time_DisplayRefresh = time_func(0);
  }
}

void publish_mqtt(uint8_t nodeID, char* payload, uint8_t payload_lenght) {

  // generate address strings
  char node_id[3];
  char type_id[3];
  char base_id[3];
  uint8_t base_id_int = NODEID;
  sprintf(node_id, "%02x", nodeID);
  sprintf(type_id, "%02x", nodeID & 0xF0);
  sprintf(base_id, "%02x", base_id_int);
    
  // generate topic string
  char topic[50];
  strcpy(topic, "base_0x");
  strcat(topic, base_id);
  strcat(topic, "_rx/nodes_0x");
  strcat(topic, type_id);
  strcat(topic, "/node_0x");
  strcat(topic, node_id);

  // publish + workaround to avoid additional invalid characters
  // looks like something in the mqtt lib is ignoring the lenght and just looking for a \0 in the string
  uint8_t* pnt = (uint8_t*)malloc(payload_lenght + 1);
  memcpy(pnt, payload, payload_lenght);
  pnt[payload_lenght] = '\0';
  mqttClient.publish(topic, pnt, payload_lenght);
  free(pnt);

  // Debug Output
  Serial.print("<- ");
  Serial.print(topic);
  Serial.print(" = ");
  for (byte i = 0; i < payload_lenght; i++) {
      if (payload[i] >= 32 && payload[i] <= 126) {
        Serial.print(payload[i]);
      } else {
        Serial.print(".");
      }
  }
  Serial.print("\n\r");
}


/////////////////////////////////////////////////////////////////////////////
// W5500 & MQTT functions
// Source: https://github.com/jozala/ESP32_W5500_MQTT
/////////////////////////////////////////////////////////////////////////////

void macCharArrayToBytes(const char* str, byte* bytes) {
    for (int i = 0; i < 6; i++) {
        bytes[i] = strtoul(str, NULL, 16);
        str = strchr(str, ':');
        if (str == NULL || *str == '\0') {
            break;
        }
        str++;
    }
}

void ethernetWizReset(const uint8_t resetPin) {
    pinMode(resetPin, OUTPUT);
    digitalWrite(resetPin, HIGH);
    delay(250);
    digitalWrite(resetPin, LOW);
    delay(50);
    digitalWrite(resetPin, HIGH);
    delay(350);
}

void connectEthernet() {
    delay(500);
    byte* mac = new byte[6];
    macCharArrayToBytes(ETHERNET_MAC, mac);
    ipAddress.fromString(ETHERNET_IP);

    Ethernet.init(ETHERNET_CS_PIN);
    ethernetWizReset(ETHERNET_RESET_PIN);

    Serial.println("Starting ETHERNET connection...");
    Ethernet.begin(mac, ipAddress);
    delay(200);

    Serial.print("Ethernet IP is: ");
    Serial.println(Ethernet.localIP());
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {

  // get destination & len
  uint8_t topic_len = strlen(topic);
  uint8_t destination = 0x00;
  if (topic_len == 22) {
    destination = (uint8_t)strtol((topic + 18), NULL, 0);
  }
  if (destination == 0x00) {
    LEDs_PCF8574.write(LED_status_data_TX, 1);
    return;
  }

  // transmit
  radio_transmit(&radio_drv, destination, (uint8_t*)payload, length);
  
  // store msg to display lib
  disp_add_tx(&disp, destination, (char*)payload, length);
}

void mqttReconnect() {
  Serial.print("Connecting to MQTT broker ");
  Serial.println(MQTT_HOSTNAME);

  char deviceName[20];
  char base_id[3];
  uint8_t base_id_int = NODEID;
  sprintf(base_id, "%02x", base_id_int);
  strcpy(deviceName, "base_0x");
  strcat(deviceName, base_id);
  while(!mqttClient.connect(deviceName)) {
    Serial.print("Connecting to MQTT as ");
    Serial.println(deviceName);
    delay(1000);
  }
  Serial.print("Connected to MQTT as ");
  Serial.print(deviceName);
  Serial.print(" (");
  Serial.print(Ethernet.localIP());
  Serial.println(")");

  // subscribe "base_0x01_tx"
  strcat(deviceName, "_tx/+");
  mqttClient.setCallback(mqttCallback);
  mqttClient.subscribe(deviceName);
  Serial.print("Subscribed topic \"");
  Serial.print(deviceName);
  Serial.println("\"");
}


/////////////////////////////////////////////////////////////////////////////
// RFM + radio lib functions
/////////////////////////////////////////////////////////////////////////////

uint8_t rfm_transmit(uint8_t dest, uint8_t* data, uint8_t len) {
  radio.send(dest, data, len, true);
  return 0;
}

uint8_t rfm_receive(uint8_t* src, uint8_t* data, uint8_t* len) {
  *len = radio.DATALEN;
  *src = radio.SENDERID;
  for (int i=0; i<*len && i<RF69_MAX_DATA_LEN; i++) {
    data[i] = radio.DATA[i];
  }
  return 0;
}

uint8_t rfm_sendACK(uint8_t dest) {
  radio.sendACK();
  return 0;
}

uint8_t rfm_ACKReceived(uint8_t dest) {
  return radio.ACKReceived(dest);
}

uint8_t rfm_ACKRequested(uint8_t src) {
  return radio.ACKRequested();
}

uint8_t rfm_receiveDone(void) {
  return radio.receiveDone();
}

void receive(uint8_t source, uint8_t* data, uint16_t len) {

  // publish to MQTT
  publish_mqtt(source, (char*)data, len);

  // store msg to display lib
  disp_add_rx(&disp, source, (char*)data, len);

  // RX LED
  LEDs_PCF8574.write(LED_status_data_RX, 0);
  time_rxLED = time_func(0);
}


/////////////////////////////////////////////////////////////////////////////
// various functions
/////////////////////////////////////////////////////////////////////////////

uint32_t time_func(uint32_t time_diff) {
	uint32_t time = millis();
	time = (0x7FFFFFFF & time) - (0x7FFFFFFF & time_diff);
	if (time & 0x80000000) {
		time = time + 0x80000000;
	}
	return time;
}
