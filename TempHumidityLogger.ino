/*
* TempHumidityLogger
* Pablo Sanchez
* Jan 2016
* Description: Use ESP8266 as a node in gathering temperature & Humidity data. The data is transmitted
*              to an MQTT broker. The MQTT parameters (broker,ip:port topic, user, pw are all set at runtime.
*
*/

#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include "DHT.h"
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
uint8_t MAC_array[6];
char MAC_char[18];

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <PubSubClient.h>     //https://github.com/knolleary/pubsubclient
WiFiClient espClient;
PubSubClient client(espClient);
long mqtt_lastMsg = 0;
char mqtt_msg[50];
int  mqtt_value = 0;

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = "";
char mqtt_port[6] = "1883";
char mqtt_topic[34] = "";
char mqtt_topic_full[50] = "";
char mqtt_clientId[15] = "";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_topic, json["mqtt_clientId"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read


  // get the MAC address for a clientID
  WiFi.macAddress(MAC_array);
  for (int i = 0; i < sizeof(MAC_array); ++i){
    sprintf(MAC_char,"%s%02x",MAC_char,MAC_array[i]);
  }

  strcpy(mqtt_clientId,MAC_char);

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_clientId("clientId", "mqtt clientId", mqtt_clientId, 15);
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 32);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10, 0, 1, 99), IPAddress(10, 0, 1, 1), IPAddress(255, 255, 255, 0));

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_clientId);
  wifiManager.addParameter(&custom_mqtt_topic);

  //reset settings - for testing
  wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_clientId, custom_mqtt_clientId.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_clientId"] = mqtt_clientId;
    json["mqtt_topic"] = mqtt_topic;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  
  strcpy(mqtt_topic_full,mqtt_clientId);
  strcat(mqtt_topic_full,"/");
  strcat(mqtt_topic_full,mqtt_topic);

  client.setServer(mqtt_server, stringToNumber(mqtt_port));
  client.setCallback(callback);
  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  dht.begin();
}

int stringToNumber(String thisString) {
  int i, value, length;
  length = thisString.length();
  char blah[(length + 1)];
  for (i = 0; i < length; i++) {
    blah[i] = thisString.charAt(i);
  }
  blah[i] = 0;
  value = atoi(blah);
  return value;
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    Serial.print(mqtt_server);
    Serial.print(":");
    Serial.print(mqtt_port);
    Serial.print(" as ");
    Serial.println(mqtt_clientId);
    // Attempt to connect
    if (client.connect(mqtt_clientId)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      // client.publish(mqtt_topic, "Reconnected");
      // ... and resubscribe
      //client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  // put your main code here, to run repeatedly:

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(t) || isnan(h)) {
    Serial.println("Failed to read from DHT");
    delay(5000);  // retry after a small wait
  } else {
    Serial.print("Temp:");
    Serial.print(t);
    Serial.print( "*C  Humidity:");
    Serial.print(h);
    Serial.println("%");
    if (!client.connected()) {
      reconnect();
    }
    client.loop();

    //long now = millis();
    //if (now - mqtt_lastMsg > 2000) {
    //  lastMsg = now;
    //  ++mqtt_value;
    char temp[7];
    dtostrf(t,4,2,temp); //convert float to char
    snprintf (mqtt_msg, 50, "%s C", temp);
    //  Serial.print("Publish message: ");
    //  Serial.println(msg);
    //  client.publish(mqtt_topic, msg);
    //}
    char pub_topic[70];
    strcpy(pub_topic,mqtt_topic_full);
    strcat(pub_topic,"/temperature");
    client.publish( pub_topic , mqtt_msg);
    Serial.print("Publish: ");
    Serial.print(pub_topic);
    Serial.print(" , ");
    Serial.println(mqtt_msg);
    
    dtostrf(h,4,2,temp); //convert float to char
    snprintf (mqtt_msg, 50, "%s %", temp);

    strcpy(pub_topic,mqtt_topic_full);
    strcat(pub_topic,"/humidity");
    client.publish(pub_topic,mqtt_msg);
    Serial.print("Publish: ");
    Serial.print(pub_topic);
    Serial.print(" , ");
    Serial.println(mqtt_msg);
    delay(55000);
  }
}
