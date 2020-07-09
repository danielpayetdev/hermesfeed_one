/**
   Load cell config
*/
#include "HX711.h"
#define DOUT 12
#define SCK 14
HX711 scale;
float calibration_factor = -970.00;

/**
   Motor config
*/
int motorPinIn3 = 0; //D3
int motorPinIn4 = 2; //D2

/**
   Serveur config
*/
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <LittleFS.h>

/**
 * Time config 
 */
#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

bool debug = false;

#define topic_root "homeassistant/switch/hermeshome/cat_feeder"
#define give_topic "homeassistant/switch/hermeshome/cat_feeder/give"
#define state_topic "homeassistant/switch/hermeshome/cat_feeder/state"
#define available_topic "homeassistant/switch/hermeshome/cat_feeder/available"
#define config_topic "homeassistant/switch/hermeshome/cat_feeder/config"
#define resetConfig "homeassistant/switch/hermeshome/cat_feeder/resetConfig"
#define weight_topic "homeassistant/switch/hermeshome/cat_feeder/weight"
#define weight_config_topic "homeassistant/switch/hermeshome/cat_feeder/weight_config"
#define set_weight_config_topic "homeassistant/switch/hermeshome/cat_feeder/set_weight_config"
#define hermesfeedapp_discovery_config_topic "hermesfeed/cat_feeder/config"
#define last_execution_topic "homeassistant/switch/hermeshome/cat_feeder/last_execution"

/* #define topic_root "homeassistant/switch/hermeshome/cat_feeder_test"
#define give_topic "homeassistant/switch/hermeshome/cat_feeder_test/give"
#define state_topic "homeassistant/switch/hermeshome/cat_feeder_test/state"
#define available_topic "homeassistant/switch/hermeshome/cat_feeder_test/available"
#define config_topic "homeassistant/switch/hermeshome/cat_feeder_test/config"
#define resetConfig "homeassistant/switch/hermeshome/cat_feeder_test/resetConfig"
#define weight_topic "homeassistant/switch/hermeshome/cat_feeder_test/weight"
#define weight_config_topic "homeassistant/switch/hermeshome/cat_feeder_test/weight_config"
#define set_weight_config_topic "homeassistant/switch/hermeshome/cat_feeder_test/set_weight_config"
#define hermesfeedapp_discovery_config_topic "hermesfeed/cat_feeder_test/config"
#define last_execution_topic "homeassistant/switch/hermeshome/cat_feeder_test/last_execution" */

char mqtt_server[] = "192.168.1.35";
char mqtt_port[] = "1883";
char mqtt_user[] = "mqttassistant";
char mqtt_pass[] = "mqttassistant";
int weightToDistributed = 18;

char configPayload[350];

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

bool shouldSaveMqttConfig = false;



bool distributionEnCours = false;

unsigned long currentMillis = 0;
unsigned long lastMillis = 0;
int intervalWeight = 500;
int lastWeight = -1000;

WiFiClient espClient;
PubSubClient client(espClient);

/**
 *  LED STATUS
 */
#define STATUS_LED 13

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.println("] ");
  char message[length];
  memcpy(message, payload, length);
  message[length] = '\0';
  if (strcmp(topic, give_topic) == 0 && strcmp(message, "ON") == 0)
  {
    startDistribution();
  }
  else if (strcmp(topic, give_topic) == 0 && strcmp(message, "OFF") == 0)
  {
    endDistribution();
  }
  else if (strcmp(topic, resetConfig) == 0)
  {
    resetWifiConfig();
  }
  else if (strcmp(topic, set_weight_config_topic) == 0)
  {
    editDistributionConfig(atoi(message));
  }
}

void resetWifiConfig()
{
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  StaticJsonDocument<256> json;
  json["mqtt_server"] = "192.168.1.x";
  json["mqtt_port"] = "1883";
  json["mqtt_user"] = "";
  json["mqtt_pass"] = "";

  File configFile = LittleFS.open("/mqtt_config.json", "w");
  if (!configFile)
  {
    Serial.println("failed to open config file for writing");
  }

  serializeJson(json, Serial);
  serializeJson(json, configFile);

  configFile.close();
  ESP.restart();
}

void clignoterLed(int count)
{
  digitalWrite(STATUS_LED, LOW);
  for (int i = 0; i < count; i++)
  {
    digitalWrite(STATUS_LED, HIGH);
    delay(200);
    digitalWrite(STATUS_LED, LOW);
    delay(200);
  }
}

void setup()
{
  Serial.begin(9600);
  Serial.println();

  // Led init
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);

  // Motor init
  pinMode(motorPinIn3, OUTPUT);
  pinMode(motorPinIn4, OUTPUT);

  // Load cell init
  if (!debug)
  {
    scale.begin(DOUT, SCK);
    scale.set_scale(calibration_factor);
    scale.tare();
  }

  setup_connections();
  loadDistributionConfig();
  timeClient.begin();
  timeClient.setTimeOffset(0);
}

void loadMqttConfig()
{
  if (LittleFS.begin())
  {
    Serial.println("mounted file system");
    if (LittleFS.exists("/mqtt_config.json"))
    {
      Serial.println("reading config file");
      File configFile = LittleFS.open("/mqtt_config.json", "r");
      if (configFile)
      {
        Serial.println("opened config file");
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        StaticJsonDocument<256> json;
        auto error = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if (!error)
        {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_pass, json["mqtt_pass"]);
        }
        else
        {
          Serial.println("failed to load json config");
        }
      }
    }
  }
  else
  {
    Serial.println("failed to mount FS");
  }
}

void loadDistributionConfig()
{
  if (LittleFS.begin())
  {
    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json"))
    {
      Serial.println("reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile)
      {
        Serial.println("opened config file");
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        StaticJsonDocument<64> json;
        auto error = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if (!error)
        {
          Serial.println("\nparsed json");
          weightToDistributed = json["weightToDistributed"];
        }
        else
        {
          Serial.println("failed to load json config");
        }
      }
    }
  }
  else
  {
    Serial.println("failed to mount FS");
  }
}

void editDistributionConfig(int weight)
{
  weightToDistributed = weight;
  char sWeightToDistributed[16];
  itoa(weightToDistributed, sWeightToDistributed, 10);
  client.publish(weight_config_topic, sWeightToDistributed, true);
  StaticJsonDocument<64> json;
  json["weightToDistributed"] = weight;

  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile)
  {
    Serial.println("failed to open config file for writing");
  }

  serializeJson(json, Serial);
  serializeJson(json, configFile);
}

char *getConfigDiscoveryforHA()
{
  StaticJsonDocument<350> config;
  config["~"] = topic_root;
  if (!debug)
    config["uniq_id"] = "hermeshome_feeder_one";
  if (debug)
    config["uniq_id"] = "hermeshome_feeder_one_test";
  if (!debug)
    config["name"] = "hermeshome_feeder_one";
  if (debug)
    config["name"] = "[TEST] HermesFeed One";
  config["cmd_t"] = "~/give";
  config["stat_t"] = "~/state";
  config["avty_t"] = "~/available";
  config["ic"] = "mdi:cat";
  config["dev"]["ids"] = system_get_chip_id();
  config["dev"]["mf"] = "HermesSoft";
  config["dev"]["mdl"] = "HermesFeed";
  config["dev"]["name"] = "HermesFeedOne";
  serializeJson(config, configPayload);
  return configPayload;
}

char *getConfigDiscoveryforHermesFeedApp()
{
  StaticJsonDocument<350> config;
  config["~"] = topic_root;
  if (!debug)
    config["uniq_id"] = "hermeshome_feeder_one";
  if (debug)
    config["uniq_id"] = "hermeshome_feeder_one_test";
  if (!debug)
    config["name"] = "hermeshome_feeder_one";
  if (debug)
    config["name"] = "[TEST] HermesFeed One";
  config["c_w"] = "~/weight";
  config["g_w"] = "~/weight_config";
  config["s_w"] = "~/set_weight_config";
  config["cmd_t"] = "~/give";
  config["stat_t"] = "~/state";
  config["avty_t"] = "~/available";
  config["last_exe"] = "~/last_execution";
  serializeJson(config, configPayload);
  return configPayload;
}

void saveConfigCallback()
{
  Serial.println("Should save config");
  shouldSaveMqttConfig = true;
}

void setup_connections()
{
  loadMqttConfig();
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 20);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt password", mqtt_pass, 20);

  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  if (!wifiManager.autoConnect("HermesFeedOne"))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.reset();
    delay(5000);
  }
  Serial.println("Connected.");

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());

  if (shouldSaveMqttConfig)
  {
    Serial.println("saving config");
    StaticJsonDocument<256> json;
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_pass"] = mqtt_pass;

    File configFile = LittleFS.open("/mqtt_config.json", "w");
    if (!configFile)
    {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
    shouldSaveMqttConfig = false;
  }

  Serial.println();
  Serial.print("IP :");
  Serial.println(WiFi.localIP());
  client.setServer(mqtt_server, atoi(mqtt_port));
  client.setBufferSize(400);
  client.setCallback(callback);
}

void reconnect()
{
  while (!client.connected())
  {
    digitalWrite(STATUS_LED, HIGH);
    Serial.print("Attempting MQTT connection...");
    String clientId;
    if (!debug)
      clientId = "HermesFeedOne";
    if (debug)
      clientId = "HermesFeedTest";
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass, available_topic, 0, true, "offline"))
    {
      Serial.println("connected");
      clignoterLed(5);
      client.publish(config_topic, getConfigDiscoveryforHA(), true);
      client.publish(hermesfeedapp_discovery_config_topic, getConfigDiscoveryforHermesFeedApp(), true);
      client.publish(available_topic, "online", true);
      client.publish(state_topic, "OFF", true);
      char sWeightToDistributed[16];
      itoa(weightToDistributed, sWeightToDistributed, 10);
      client.publish(weight_config_topic, sWeightToDistributed, true);
      client.subscribe(give_topic);
      client.subscribe(set_weight_config_topic);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void startMotor()
{
  digitalWrite(motorPinIn3, HIGH);
  digitalWrite(motorPinIn4, LOW);
}

void stopMotor()
{
  digitalWrite(motorPinIn3, LOW);
  digitalWrite(motorPinIn4, LOW);
}

int getWeight()
{
  if (scale.is_ready())
  {
    double weight = scale.get_units(10);
    int floorWeight = floor(weight);
    if (floorWeight < 0)
    {
      floorWeight = 0;
    }
    return floorWeight;
  }
  else
  {
    Serial.println("HX711 not found.");
  }
}

void sendExecutionTime()
{
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  char sEpochTime[10];
  itoa(epochTime, sEpochTime, 10);
  client.publish(last_execution_topic, sEpochTime, true);
}

void startDistribution()
{
  if (scale.is_ready() && weightToDistributed != 0)
  {
    clignoterLed(5);
    Serial.println("Distribution en cours...");
    client.publish(state_topic, "ON", true);
    startMotor();
    distributionEnCours = true;
    sendExecutionTime();
  }
  else
  {
    Serial.println("Scale not ready retry in 500ms.");
    delay(500);
    startDistribution();
  }
}

void endDistribution()
{
  stopMotor();
  distributionEnCours = false;
  lastMillis = millis();
  client.publish(state_topic, "OFF", true);
  clignoterLed(5);
  Serial.println("Done.");
}

void distribution()
{
  if (distributionEnCours)
  {
    Serial.print(".");
    int weight = getWeight();
    lastWeight = weight;
    char sWeight[5];
    itoa(weight, sWeight, 10);
    client.publish(weight_topic, sWeight);
    if (weight >= weightToDistributed)
    {
      stopMotor();
      Serial.println();
      Serial.println("Attente verification...");
      delay(1000);
      Serial.println("Nouvel verification...");
      weight = getWeight();
      lastWeight = weight;
      char sWeight[5];
      itoa(weight, sWeight, 10);
      client.publish(weight_topic, sWeight);
      if (weight >= weightToDistributed)
      {
        endDistribution();
      }
      else
      {
        startMotor();
      }
    }
    else
    {
      clignoterLed(1);
    }
  }
}

void sendWeightPeriode()
{
  if (!distributionEnCours)
  {
    currentMillis = millis();
    if (currentMillis - lastMillis >= intervalWeight)
    {
      int weight = getWeight();
      if (weight != lastWeight)
      {
        lastWeight = weight;
        char sWeight[5];
        itoa(weight, sWeight, 10);
        client.publish(weight_topic, sWeight);
      }
      lastMillis = millis();
    }
  }
}

void loop()
{
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();
  distribution();
  sendWeightPeriode();
}
