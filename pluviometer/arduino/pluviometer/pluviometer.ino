#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>


const char* ap_ssid     = "Pluviometer-AP";

struct {
  struct {
    char ssid[32]            = "";
    char password[16]        = "";  
  }wifi;
  
  struct {
    char ip[16]              = "";
    int port                 = 0;
    char username[16]        = "";
    char password[16]        = "";
    char in_topic[64]        = "";
    char out_topic[64]       = "";
  } mqtt;
  
  double offset              = 0.0;
  long ticks                 = 0;
  double magick_number       = 0;
} data;


WiFiClient espClient;
PubSubClient client;

/**
 * Initializes the global configuration stored in EEPROM
 */
void initializeConfiguration(){
    // Data never persisted.
    EEPROM.get(0, data);
            
    memset(&data,'\0', sizeof(data));
    
    strncpy(&(data.wifi.ssid[0]), "local_ssid", 32);
    strncpy(&(data.wifi.password[0]), "local_password", 16);

    strncpy(&(data.mqtt.ip[0]), "127.0.0.1", 16);
    data.mqtt.port=1883;
    
    strncpy(&(data.mqtt.username[0]), "mqtt_username", 16);
    strncpy(&(data.mqtt.password[0]), "mqtt_password", 16);
    strncpy(&(data.mqtt.in_topic[0]), "sensors/in/pluviometer", 64);
    strncpy(&(data.mqtt.out_topic[0]), "sensors/out/pluviometer", 64);

    data.offset=0;
    data.magick_number=123456.789;
    data.ticks=0;
    
    EEPROM.put(0, data);
    EEPROM.commit();  
}

/**
 * Saves and stores current data oject into eeprom
 */
void saveDataAndConfiguration() {
    /* persist changes to the flash */
    EEPROM.put(0, data);    
    EEPROM.commit();
}


/**
 * Setup the wifi as AP_STA. 
 */
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");

  WiFi.mode(WIFI_AP_STA);  
  WiFi.hostname("sensor-reader.local");
  WiFi.softAP(ap_ssid);    
  WiFi.begin(&(data.wifi.ssid[0]), &(data.wifi.password[0]));

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  IPAddress IP = WiFi.softAPIP();    
  Serial.print("AP IP address: ");
  Serial.println(IP);
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());  
}

/**
 * MQTT data received callback
 */
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  /* {"setup": {"index": 123456.123}} */
  DynamicJsonDocument doc(256);
  deserializeJson(doc, payload);
   
  if(doc.containsKey("setup")){
    if(doc.containsKey("offset")&& doc.containsKey("value")) {
            data.offset=doc["setup"]["offset"];
            data.ticks=doc["setup"]["value"];
    }
    if(doc.containsKey("wifi")) {
      strncpy(&(data.wifi.ssid[0]), doc["setup"]["wifi"]["ssid"], 32);
      strncpy(&(data.wifi.password[0]), doc["setup"]["wifi"]["password"], 16);
    }

    if(doc.containsKey("mqtt") ){
      strncpy(&(data.mqtt.ip[0]), doc["setup"]["mqtt"]["ip"], 16);
      data.mqtt.port=doc["setup"]["mqtt"]["port"];
      strncpy(&(data.mqtt.username[0]), doc["setup"]["mqtt"]["username"], 16);
      strncpy(&(data.mqtt.password[0]), doc["setup"]["mqtt"]["password"], 16);
      strncpy(&(data.mqtt.in_topic[0]), doc["setup"]["mqtt"]["in_topic"], 64);
      strncpy(&(data.mqtt.out_topic[0]),doc["setup"]["mqtt"]["out_topic"] , 64);
    }

    data.magick_number=123456.789;
    
    /* persist changes to the flash */
    EEPROM.put(0, data);    
    EEPROM.commit();
    Serial.println("Saved in EEPROM new configuration");
  }else{
      Serial.println(" Object {\"setup\": { ... }} not found");
  }
}

/**
 * Setup MQTT client
 */
void setup_mqtt_client() {
    client.setClient(espClient);
    client.setServer(&(data.mqtt.ip[0]), data.mqtt.port);
    client.setCallback(callback);
    client.subscribe(&(data.mqtt.in_topic[0]));    
}

void setup() {
  // put your main code here, to run repeatedly:
  Serial.begin(115200);
  Serial.setTimeout(2000);

  // Wait for serial to initialize.
  while(!Serial) { }

  // load configuration
  EEPROM.begin(sizeof(data));
  EEPROM.get(0,data);

  // check if flash is empty. if true create and store a brand new module configuration
  if(data.magick_number!=123456.789) {
    initializeConfiguration();
  }


  // will connect wifi as AP or STA
  setup_wifi();


  // only active when wifi setup as STA
  setup_mqtt_client();
}



void loop() {
  //mqtt
  client.loop(); 
  
  data.ticks++;
  saveDataAndConfiguration();

  
  
  // Deep sleep mode for 30 seconds, the ESP8266 wakes up by itself when GPIO 16 (D0 in NodeMCU board) is connected to the RESET pin
  Serial.println("I'm awake, but I'm going into deep sleep mode for 30 seconds");
  ESP.deepSleep(30e6); 
}
