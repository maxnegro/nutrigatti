

void wifiConnected();

void IWC_setup() {
  iotWebConf.setWifiConnectionCallback(&wifiConnected);
  IWC_setCustomParameters();
  iotWebConf.init();
  WiFi.hostname(iotWebConf.getThingName());
  IWC_configSaved();
  server.on("/", IWC_handleRoot);
  server.on("/status.js", IWC_handleStatusJS);
  server.on("/status.css", [](){ IWC_streamFile("/status.css", "text/css"); });
  server.on("/status", HTTPMethod::HTTP_GET, []() {
    const size_t capacity = JSON_OBJECT_SIZE(3) + 4 * JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(4) + 44;
    DynamicJsonBuffer jsonBuffer(capacity);
    JsonObject &statusObj = jsonBuffer.createObject();
 
    time_t t = now();
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
    statusObj["lastFed"] = datestring;
 
    snprintf_P(datestring,
      21,
      PSTR("%04u-%02u-%02uT%02u:%02u:%02uZ"),
      year(t),
      month(t),
      day(t),
      hour(t),
      minute(t),
      second(t)
    );
    statusObj["currentTime"] = datestring;

    JsonArray& d = jsonBuffer.createArray();
    for (byte i = 0; i < 4; i++) {
      if (prog[i].enable) {
        JsonArray& l = d.createNestedArray();
        l.add((int)prog[i].hr);
        l.add((int)prog[i].mn);
        l.add((int)prog[i].qty);
      }
    }
    statusObj["schedule"] = d;
    String message;
    statusObj.printTo(message);

    server.send(200, "application/json", message);
  });
  server.on("/config", []{ iotWebConf.handleConfig(); });
  server.onNotFound([](){ iotWebConf.handleNotFound(); });
}

String indexKeyProcessor(const String& key)
{
  if (key == "DEVICENAME") {
    return iotWebConf.getThingName();
  } else if (key == "VAR1") { 
    return "It works!";
  }

  return "Key not found";
}

void IWC_handleRoot() {
  SPIFFS.begin();

  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  templateProcessor.processAndSend("/index.html", indexKeyProcessor);
}

inline void IWC_loop() {
  iotWebConf.doLoop();
}

void IWC_handleStatusJS() {
  IWC_streamFile("/status.js", "application/javascript");
}

void IWC_streamFile(const char *fileName, const char *mime) {
  SPIFFS.begin();
  
  File f = SPIFFS.open(fileName, "r");
  if (!f) {
      iotWebConf.handleNotFound();
      return;
  }
  
  server.streamFile(f, mime);
  f.close();
}

void wifiConnected()
{
  Serial.println("WiFi was connected.");
  refreshNTP = true;
  ArduinoOTA.begin(true);
}

bool IWC_isConnected() {
  return iotWebConf.getState() == IOTWEBCONF_STATE_ONLINE;
}

char _p_mqttServer[80];
char _p_scheduleJson[120];
IotWebConfParameter mqttServerParam = IotWebConfParameter("MQTT broker address", "mqttServerParam", _p_mqttServer, 80);
IotWebConfSeparator separator1 = IotWebConfSeparator("Configurazioni locali");
IotWebConfParameter scheduleJsonParam = IotWebConfParameter("Schedule (Json)", "scheduleJsonParam", _p_scheduleJson, 80);
//IotWebConfParameter intParam = IotWebConfParameter("Int param", "intParam", intParamValue, NUMBER_LEN, "number", "1..100", NULL, "min='1' max='100' step='1'");
//// -- We can add a legend to the separator
//IotWebConfSeparator separator2 = IotWebConfSeparator("Calibration factor");
//IotWebConfParameter floatParam = IotWebConfParameter("Float param", "floatParam", floatParamValue, NUMBER_LEN, "number", "e.g. 23.4", NULL, "step='0.1'");

void IWC_setCustomParameters() {
  iotWebConf.addParameter(&separator1);
  iotWebConf.addParameter(&mqttServerParam);
  iotWebConf.addParameter(&scheduleJsonParam);
  iotWebConf.setConfigSavedCallback(&IWC_configSaved);
  iotWebConf.setFormValidator(&IWC_formValidator);
}

boolean IWC_formValidator()
{
  Serial.println("Validating form.");
  boolean valid = true;

  IPAddress _submitted_mqtt_addr;

  int ret = WiFi.hostByName(server.arg(mqttServerParam.getId()).c_str(), _submitted_mqtt_addr);
  if (ret != 1) {
    mqttServerParam.errorMessage = "You need to specify a valid IP or FQDN";
    valid = false;
  }

  const size_t capacity = 4 * JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(4) + 30;
  DynamicJsonBuffer jsonBuffer(capacity);
  JsonArray& root = jsonBuffer.parseArray(server.arg(scheduleJsonParam.getId()));
  if (! root.success()) {
    scheduleJsonParam.errorMessage = "You need to specify a valid JSON array for feed scheduling (ie [[7,2,2],[19,2,2]])";
    valid = false;
  }
  if (root.size() > 4) {
    scheduleJsonParam.errorMessage = "JSON array for scheduling should be 4 entries or less";
    valid = false;    
  }

  return valid;
}

void IWC_configSaved() {
  int ret = WiFi.hostByName(_p_mqttServer, mqttServer);
  if (ret == 1) {
    mqttClient.setServer(mqttServer, 1883);
  }

  parseSchedule(_p_scheduleJson);
  publishSchedule();
}

void IWC_setScheduleJson(const char *jsonArray) {
  strncpy(_p_scheduleJson, jsonArray, 120);
  iotWebConf.configSave();
}
