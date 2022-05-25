#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>

// create an instance of the library
// Replace by 2 if you aren't enable to use Serial Monitor... Don't forget to Rewire R1 to GPIO2!

#define INPUT_PIN 4

#define DEBOUNCE_TIMEOUT 50  //-- 50 ms for debounce
unsigned long trigger_start=0;
boolean triggered=false;
unsigned long counter=0;
unsigned long trigger_end;
boolean reset_interrupt =false;

WiFiClient espClient;
PubSubClient client;

int lastValue;
int oldLastValue;
double value;

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
  
  double offset         = 0.0; /* starting point */
  double value          = 0.0; /* the value calculated since startup */
  long ticks            = 0; /*  */
  double magick_number  = 3456.78910;
} data;

void setup() {
  Serial.begin(115200);

  EEPROM.begin(sizeof(data));
  EEPROM.get(0,data);

  if (data.magick_number!=123456.789) {
    // Data never persisted.
    memset(&data,'\0', sizeof(data));
    
    strncpy(&(data.wifi.ssid[0]), "domestic_ssid", 32);
    strncpy(&(data.wifi.password[0]), "domestic_password", 16);

    strncpy(&(data.mqtt.ip[0]), "192.168.x.y", 16);
    data.mqtt.port=1883;
    
    strncpy(&(data.mqtt.username[0]), "mqtt_username", 16);
    strncpy(&(data.mqtt.password[0]), "mqtt_password", 16);
    strncpy(&(data.mqtt.in_topic[0]), "sensor/in", 64);
    strncpy(&(data.mqtt.out_topic[0]), "sensor/out", 64);

    data.offset=0;
    data.magick_number=123456.789;
    data.value=0.0;
    data.ticks=0;

    EEPROM.put(0, data);
    EEPROM.commit(); 
  }
    
  delay(1000);
  setup_wifi();

  client.setClient(espClient);
  client.setServer(&(data.mqtt.ip[0]), data.mqtt.port);
  client.setCallback(callback);
  client.subscribe(&(data.mqtt.in_topic[0]));
    
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(INPUT_PIN , INPUT);

  attachInterrupt(digitalPinToInterrupt(INPUT_PIN), electricCounterChanged, RISING);
}

ICACHE_RAM_ATTR void electricCounterChanged() {
  triggered=true;
  detachInterrupt(digitalPinToInterrupt(INPUT_PIN));
}

void setup_wifi() {
  delay(10);

  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(&(data.wifi.ssid[0]));

  WiFi.begin(&(data.wifi.ssid[0]), &(data.wifi.password[0]));

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    // if (client.connect("ESP8266Client")) {
    if (client.connect("ESP8266Client", &(data.mqtt.username[0]), &(data.mqtt.password[0]))) {
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
            data.value=doc["setup"]["value"];
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

void loop() {
  if (!client.connected()) {
    reconnect();
    Serial.println("Connection done");
  }   

  if(triggered && !reset_interrupt) {
    triggered=false;
    reset_interrupt=true;
    data.value += 0.001;
    String strNumber = String(data.value + data.offset, 3);
    String message = "{\"status\":\"online\", \"value\":" + strNumber + "}";          
    client.publish(&(data.mqtt.out_topic[0]), message.c_str(), true);       
    trigger_end=millis();
  }else {
    if(reset_interrupt) {
      // wait until reactivate the interrupts
      if(millis()-trigger_end < DEBOUNCE_TIMEOUT) {
        if(digitalRead(INPUT_PIN)==1) {
          //while input=1 still active refresh trigger_end
          trigger_end=millis();
        }
      }else {
        reset_interrupt=false;
        // reactivate interrupts when we are sure that no more bounce will occur.
        attachInterrupt(digitalPinToInterrupt(INPUT_PIN), electricCounterChanged, RISING);
      }
    }
  }
  
  client.loop(); 
}

#define PI 3.1415926535897932384626433832795
/* PI * 5^2 cm */
#define COLLECTOR_SURFACE 78,539816339744830961566084581988

/* The bucket volume in ml */
#define BUCKET_VOLUME_PER_TICK 8

/**
 * Increment the value variable and store all that in the ESP8266 Memory 
 **/
void upgradeSensorValue() {
  // increment tickets
  data.ticks += 1;
  
  // calculate volume 
  double totalVolume = data.ticks * BUCKET_VOLUME_PER_TICK;
  
  
  EEPROM.put(0, data);    
  EEPROM.commit();
}

void loop() {
  upgradeSensorValue();
      
  Serial.println("Going into deep sleep mode. Next wake up on sensor transition");
  ESP.deepSleep(0); 
}
