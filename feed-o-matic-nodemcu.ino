#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> // Relies on v5.x4
#include <IotWebConf.h> // see iotwebconf.ino
#include <EspHtmlTemplateProcessor.h>
#define OTA_DEBUG Serial

// Disable iotwebconf mdns support
#undef IOTWEBCONF_CONFIG_USE_MDNS

DNSServer dnsServer;
WebServer server(80);
EspHtmlTemplateProcessor templateProcessor(&server);

const char thingName[] = "nutrigatti";
const char wifiInitialApPassword[] = "miaomiaomiao";

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword);

typedef struct {
  bool enable;
  byte hr;
  byte mn;
  byte qty;
} feedProgram;

// Default schedule. Do not use leading zeroes.
feedProgram prog[4] = {
  {true ,  7,  2,  2},
  {true , 19,  2,  2},
  {false,  0,  0,  0},
  {false,  0,  0,  0},
};

#include <Timezone.h>

//Central European Time (Frankfurt, Paris)
const TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
const TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time

Timezone myTZ(CEST, CET);

// If TimeChangeRules are already stored in EEPROM, comment out the three
// lines above and uncomment the line below.
//Timezone myTZ(100);       //assumes rules stored at EEPROM address 100

TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev

// Declare L298N Controller pins
// Motor 1
const int dir1PinA = D3;
const int dir2PinA = D4;
const int speedPinA = D1; // PWM control

// Manual feed button pin
const int pinManualFeed = D5;

const char ntpServer[] = "it.pool.ntp.org";
IPAddress mqttServer(192, 168, 42, 2);
// MQTT TOPICS (change these topics as you wish)
#define MQTT_BASE_TOPIC "home/catfeeder"
const char lastfed_topic[] = MQTT_BASE_TOPIC "/lastfed"; // UTF date
const char remaining_topic[] = MQTT_BASE_TOPIC "/remaining"; //Remain % fix distance above
const char feed_topic[] = MQTT_BASE_TOPIC "/feed";  // command topic
const char schedule_topic[] = MQTT_BASE_TOPIC "/schedule"; // schedule update
const char update_schedule_topic[] = MQTT_BASE_TOPIC "/schedule/set"; // schedule update

WiFiUDP udp;
NTPClient timeClient(udp, ntpServer, 0, 60000);

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
int status = WL_IDLE_STATUS;
volatile bool refreshNTP = false;


WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;

time_t ntpTime() {
  return timeClient.getEpochTime();
}

volatile bool needForFeed = false;

void ICACHE_RAM_ATTR handleButtonInterrupt() {
  needForFeed = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("Starting"));

  // Motor stopped
  analogWrite(speedPinA, 0);
  digitalWrite(dir1PinA, LOW);
  digitalWrite(dir2PinA, LOW);
  // Define L298N Dual H-Bridge Motor Controller Pins
  pinMode(dir1PinA, OUTPUT);
  pinMode(dir2PinA, OUTPUT);
  pinMode(speedPinA, OUTPUT);

  // Input pullups for switch
  pinMode(pinManualFeed, INPUT_PULLUP);
  // Interrupt handling function
  attachInterrupt(digitalPinToInterrupt(pinManualFeed), handleButtonInterrupt, FALLING);

  IWC_setup();

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(iotWebConf.getThingName());

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
//  ArduinoOTA.begin();

  mqttClient.setServer(mqttServer, 1883);
  mqttClient.setCallback(callback);

  timeClient.begin();
  timeClient.setUpdateInterval(1000 * 60 * 30); // check NTP every half hour
  setTime(timeClient.getEpochTime());

  setSyncProvider(ntpTime);

  // Internal led off
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
}

unsigned char lastMinute = 255; // This should get reset at first loop iteration
unsigned long previousMillis = millis();
time_t lastFed = 0;


void loop() {
//  configManagerLoop();
  IWC_loop();
  ArduinoOTA.handle();

  if (IWC_isConnected()) {
    if (!mqttClient.loop()) {
      if (!mqttClient.connected()) {
        reconnect();
      }
    }
  
    if (refreshNTP) {
      Serial.print("Force NTP update... ");
      
      timeClient.setUpdateInterval(1000 * 60 * 30); // check NTP every half hour
      unsigned long refreshStarted = millis();
      while (!timeClient.update() && (millis() - refreshStarted < 2000)) {
        timeClient.forceUpdate();
      }
      setTime(timeClient.getEpochTime());
      Serial.println("Done");
      refreshNTP = false;
    }
  
    if (timeClient.update()) {
      Serial.println(F("NTP update"));
    }
  }

  if (millis() - previousMillis >= 100) {
    previousMillis = millis();

    time_t utc = now();
    time_t local = myTZ.toLocal(utc, &tcr);

    if (minute(local) != lastMinute) { // Entriamo in un minuto tutto nuovo! è ora di pappa?
      lastMinute = minute(local);
      byte revolutions = isFeedTime(local);
      if (revolutions > 0) {
        rotateDispenser(revolutions);
      }
    }
  }

  if (needForFeed) {
    Serial.println(F("MIAO!MIAO!MIAO!"));
    rotateDispenser(1);
    needForFeed = false;
  }

  delay(10);
}


byte isFeedTime(const time_t cTime) {
  for (byte i = 0; i < 4; i++) {
    if (prog[i].enable && (hour(cTime) == prog[i].hr) && (minute(cTime) == prog[i].mn)) {
      return prog[i].qty;
    }
  }
  return 0;
}

void rotateDispenser(byte revs) {
  Serial.println(F("Pappa."));
  motorMove(0, 512);
  delay(revs * 4000); // 1944
  motorMove(0, 0);
  // time_t utc = now();
  // lastFed = myTZ.toLocal(utc, &tcr);
  lastFed = now();
  publishLastFed();
}

void motorMove(byte dir, int spd) {
  if (spd == 0) {
    digitalWrite(dir1PinA, LOW);
    analogWrite(speedPinA, 0);
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    if (dir == 0) { // CW
      digitalWrite(dir1PinA, HIGH);
    } else {
      digitalWrite(dir1PinA, LOW);
    }
    analogWrite(speedPinA, spd);
    digitalWrite(LED_BUILTIN, LOW);
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  char *message = (char *) malloc(length * sizeof(char));
  if (message == NULL) {
    Serial.println(F("Error allocating message"));
    return;
  }

  for (int i = 0; i < length; i++) {
    message[i] = ((char)payload[i]);
  }
  message[length] = '\0';
  if (strcmp(topic, feed_topic) == 0) {
    if (strcmp(message, "feed") == 0) {
      needForFeed = true;
    }
  } else if (strcmp(topic, update_schedule_topic) == 0) {
    Serial.print("New schedule: ");
    Serial.println(message);
    IWC_setScheduleJson(message);
    parseSchedule(message);
    publishSchedule();
  } else {
    Serial.println(topic);
  }
  free(message);
}

unsigned long lastMqttTry = 0;
void reconnect() {
  if ((millis() - lastMqttTry) < 5000) {
    return;
  }
  lastMqttTry = millis();
  // Attempt to connect
  Serial.print(F("Attempting to connect to MQTT broker: "));
  Serial.println(mqttServer);
  mqttClient.disconnect();
  delay(10);
  if (mqttClient.connect(iotWebConf.getThingName())) {
    Serial.println(F("MQTT connected"));
    // Once connected, publish an announcement...
    publishSchedule();
    publishLastFed();
    mqttClient.subscribe(feed_topic);
    mqttClient.subscribe(update_schedule_topic);
    mqttClient.loop();
  }
}

void publishSchedule() {
  if (!mqttClient.connected()) return;
  const size_t capacity = 4 * JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(4) + 30;
  DynamicJsonBuffer jsonBuffer(capacity);
  JsonArray& d = jsonBuffer.createArray();
  for (byte i = 0; i < 4; i++) {
    if (prog[i].enable) {
      JsonArray& l = d.createNestedArray();
      l.add((int)prog[i].hr);
      l.add((int)prog[i].mn);
      l.add((int)prog[i].qty);
    }
  }
  char output[64];
  d.printTo(output, sizeof(output));
  mqttClient.publish(schedule_topic, output, true);
}

void publishLastFed() {
  if (!mqttClient.connected()) return;
  char datestring[21];
  snprintf_P(datestring,
             21,
             PSTR("%04u-%02u-%02uT%02u:%02u:%02uZ"),
             year(lastFed),
             month(lastFed),
             day(lastFed),
             hour(lastFed),
             minute(lastFed),
             second(lastFed)
            );
  mqttClient.publish(lastfed_topic, datestring, true);
}


void parseSchedule(const char* message) {
  const size_t capacity = 4 * JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(4) + 30;
  DynamicJsonBuffer jsonBuffer(capacity);
  JsonArray& root = jsonBuffer.parseArray(message);
  if (! root.success()) {
    Serial.println(F("Json parseArray failed"));
    return;
  } else {
    for (byte i = 0; i < 4; i++) {
      JsonArray& line = root[i];
      if (line.success()) {
        prog[i].enable = true;
        prog[i].hr = line[0];
        prog[i].mn = line[1];
        prog[i].qty = line[2];
      } else {
        prog[i].enable = false;
        prog[i].hr = 0;
        prog[i].mn = 0;
        prog[i].qty = 0;
      }
    }
  }
}
