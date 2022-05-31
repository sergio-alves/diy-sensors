#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>


#define MAGICK_NUMBER 1234.56789
const char* ap_ssid     = "Pluviometer-AP";

struct {
  time_t startRain;
  time_t measureDate;
  long ticks;
}measure;

struct {
  struct {
    char ssid[32]            = "";
    char password[16]        = "";  
  }wifi;
  
  struct {
    char ip[16]              = "";
    char url[32]             = "";
    int port                 = 0;
    char username[16]        = "";
    char password[16]        = "";
    char in_topic[64]        = "";
    char out_topic[64]       = "";
  } mqtt;
  
  double offset              = 0.0;
  long ticks                 = 0;
  double magick_number       = 0;
  boolean is_raining         = false;
} data;


WiFiClient espClient;
PubSubClient client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

/**
 * Initializes the global configuration stored in EEPROM
 */
void initializeConfiguration(){
    // Data never persisted.
    EEPROM.get(0, data);
            
    memset(&data,'\0', sizeof(data));
    
    strncpy(&(data.wifi.ssid[0]), "ssid", 32);
    strncpy(&(data.wifi.password[0]), "ssid_password", 16);

    strncpy(&(data.mqtt.url[0]), "mqtt.mydomain.local", 32);
    strncpy(&(data.mqtt.ip[0]), "www.xxx.yyy.zzz", 16);
    
    data.mqtt.port=1883;
    
    strncpy(&(data.mqtt.username[0]), "mqtt_username", 16);
    strncpy(&(data.mqtt.password[0]), "mqtt_password", 16);
    strncpy(&(data.mqtt.in_topic[0]), "sensors/in/pluviometer", 64);
    strncpy(&(data.mqtt.out_topic[0]), "sensors/out/pluviometer", 64);

    data.offset=0;
    data.magick_number=MAGICK_NUMBER;
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

    data.magick_number=MAGICK_NUMBER;
    
    /* persist changes to the flash */
    EEPROM.put(0, data);    
    EEPROM.commit();
    Serial.println("Saved in EEPROM new configuration");
  }else{
      Serial.println(" Object {\"setup\": { ... }} not found");
  }
}

/**
 * Attempt to reconnect to mqtt server. 
 */
void reconnectMqtt() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), &(data.mqtt.username[0]), &(data.mqtt.password[0]))) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/**
 * Setup MQTT client
 */
void setup_mqtt_client() {
    client.setClient(espClient);

    Serial.print("Connecting to the mqtt: ");
    Serial.print(data.mqtt.url);
    Serial.print(" and port ");
    Serial.println(data.mqtt.port);
    
    client.setServer(&(data.mqtt.ip[0]), data.mqtt.port);
    client.setCallback(callback);    
    client.subscribe(&(data.mqtt.in_topic[0]));    
}

/**
 * Setup app after wake up
 */
void setup() {
  // put your main code here, to run repeatedly:
  Serial.begin(115200);
  Serial.setTimeout(2000);

  // Wait for serial to initialize.
  while(!Serial) { }

  timeClient.begin();

  // load configuration
  EEPROM.begin(sizeof(data));
  EEPROM.get(0,data);

  // check if flash is empty. if true create and store a brand new module configuration
  if(data.magick_number!=MAGICK_NUMBER) {
    initializeConfiguration();
  }


  // will connect wifi as AP or STA
  setup_wifi();


  // only active when wifi setup as STA
  setup_mqtt_client();
}


/* PI * 5^2 cm */
#define COLLECTOR_SURFACE 78,539816339744830961566084581988
#define SQARE_METER_IN_SQUARE_CENTIMETERS 10000

/* The bucket volume in ml */
#define BUCKET_VOLUME_PER_TICK 8

/**
 * Attempt mqtt transmission of data
 */
void transmitToMQTT() {
    /* calculates the mm/L per square meter equivalent */
    DynamicJsonDocument doc(64);
    doc["ticks"]=data.ticks;
    doc["mm_per_sqm"]=BUCKET_VOLUME_PER_TICK * SQARE_METER_IN_SQUARE_CENTIMETERS / COLLECTOR_SURFACE;
    doc["timestamp"]=timeClient.getEpochTime();
    doc["status"]="online";
    doc["raining"]=data.is_raining;
    
    String serializedJson = "";
    serializeJson(doc, serializedJson);

    if (!client.connected()) {
      reconnectMqtt();
    }

    Serial.print("Going to send data to mqtt on topic :  ");
    Serial.println(&(data.mqtt.out_topic[0]));
    serializeJson(doc, Serial);   
    Serial.println();
    client.publish(&(data.mqtt.out_topic[0]), serializedJson.c_str(), true);
}

/**
 * Gets current date time formatted 
 */
String getFullyFormattedDateTimeUTC() {

  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime);  
  
  int monthDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  int currentYear = ptm->tm_year+1900;
  
  //Print complete date:
  return String(monthDay) + "/" + String(currentMonth) + "/" + String(currentYear) + " " + timeClient.getFormattedTime();
}

/**
 * The main loop
 */
void loop() {
  timeClient.update();  
  
  //Get a time structure  
  Serial.println(getFullyFormattedDateTimeUTC());
  
  // increment the tickets and store into eeprom
  data.ticks++;
  saveDataAndConfiguration();

  // mqtt get messages
  if (!client.connected()) {
    reconnectMqtt();
    Serial.println("Connection done");
  }   

  // transmit data to mqtt
  transmitToMQTT();

  client.loop();
  
  // Deep sleep mode for 30 seconds, the ESP8266 wakes up by itself when GPIO 16 (D0 in NodeMCU board) is connected to the RESET pin
  Serial.println("I'm awake, but I'm going into deep sleep mode for 30 seconds");
  ESP.deepSleep(30e6); 
}
