#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <RemoteDebug.h>
#include <VitoWiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

VitoWiFi_setProtocol(KW);

////**********START CUSTOM PARAMS******************//
 
//Define parameters for the http firmware update
const char* host = "esp8266";
const char* update_path = "/WebFirmwareUpgrade";
const char* update_username = "admin";
const char* update_password = "admin";

#define WIFI_SSID "xxx"
#define WIFI_PASSWORD "xxx"

#define MQTT_HOST IPAddress(192, 168, 178, 10)
#define MQTT_PORT 1883
#define MQTT_USER "mosquitto"
#define MQTT_PASSWORD "mr7nzonHoDY"

bool RemoteSerial = true; //true = Remote and local serial, false = local serial only

//Define Datapoints 
DPTemp aussenTemp("aussenTemp", "boiler", 0x5525);
DPTemp kesselTemp("kesselTemp", "boiler", 0x0810);
DPTemp vorlaufTemp2("vorlaufTemp2", "boiler", 0x3900);
DPStat partyBetrieb("partyBetrieb", "boiler", 0x3303);
DPMode warmwasserBereitung("warmwasserBereitung", "boiler", 0x650A);
 
//************END CUSTOM PARAMS********************
 
RemoteDebug RSerial;
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
 
WiFiManager wifiManager;
 
const char compile_date[] = __DATE__ " " __TIME__;

void setupDPs() {
  partyBetrieb.setWriteable(true);
}

void globalCallbackHandler(const IDatapoint& dp, DPValue value) {
  // RSerial.print(dp.getGroup());
  // RSerial.print(" - ");
  // RSerial.print(dp.getName());
  // RSerial.print(" is ");
  char value_str[15] = {0};
  value.getString(value_str, sizeof(value_str));
  RSerial.println(value_str);

  char topicName[sizeof(host) + sizeof(dp.getGroup()) + sizeof(dp.getName()) + 2];
  sprintf(topicName, "%s/%s/%s", host, dp.getGroup(), dp.getName()); // with word space
  RSerial.printf("Publishing to destination: %s, Value: %s\n", topicName, value_str);
  mqttClient.publish(topicName, 1, true, value_str);
}

void connectToWifi() {
  RSerial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  RSerial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  RSerial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  RSerial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}


void onMqttConnect(bool sessionPresent) {
  RSerial.println("Connected to MQTT.");
  mqttClient.subscribe("esp8266/boiler/partyBetrieb/set", 1);
  mqttClient.publish("esp8266/hello", 1, true, "Hello");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  RSerial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  RSerial.println("Subscribe acknowledged.");
}

void onMqttUnsubscribe(uint16_t packetId) {
  RSerial.println("Unsubscribe acknowledged.");
}

DPValue valueFromChar(char* payload) {
  if (strcmp(payload, "false") == 0) {
    return DPValue(false);
  } else if (strcmp(payload, "true") == 0) {
    return DPValue(true);
  } else {
    return DPValue(payload);
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  RSerial.printf("Publish received in topic: %s, Payload: %s", topic, payload);
  DPValue value = valueFromChar(payload);

  char value_str[15] = {0};
  value.getString(value_str, sizeof(value_str));
  RSerial.printf(", Value: %s\n", value_str);
  VitoWiFi.writeDatapoint(partyBetrieb, value);
}

void onMqttPublish(uint16_t packetId) {
  RSerial.println("Publish acknowledged.");
}

void setup() {

  // Setup MQTT
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(MQTT_USER, MQTT_PASSWORD);

  connectToWifi();

  //setup http firmware update page.
  MDNS.begin(host);
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);

  RSerial.begin(host); 
  RSerial.setSerialEnabled(false);  
  RSerial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and your password\n", host, update_path, update_username);

  // setup VitoWifi
  VitoWiFi.setGlobalCallback(globalCallbackHandler);  // this callback will be used for all DPs without specific callback
  VitoWiFi.setLogger(&RSerial);
  VitoWiFi.enableLogger();

  setupDPs();

  VitoWiFi.setup(&Serial);

}

void loop() {
 
  httpServer.handleClient(); //handles requests for the firmware update page
  
  if (RemoteSerial) RSerial.handle();
  //  RSerial.printf( "Time: %ld\n", millis());
  //  RSerial.printf( "Heap Free: %ld\n", system_get_free_heap_size());
  static unsigned long lastMillis = 0;
  if (millis() - lastMillis > 60 * 1000UL) {  // read all values every 60 seconds
    lastMillis = millis();
    VitoWiFi.readAll();
  }
  VitoWiFi.loop();

  // to write a value, call VitoWifi.writeDatapoint("roomtemp", 12); for example
  // method is VitoWifi.writeDatapoint(const char*, uint8_t);
}
