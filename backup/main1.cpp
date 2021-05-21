/**
 * 
 * 1. compress measuring message to JSON format.
 * 2. platformio run -t upload --upload-port xxx.xxx.xxx.xxx
 * ainconController1: 10.10.200.60
 * ainconController2: 10.10.200.57
 * ainconController3: 10.10.200.59
 * 
 * */
#include <Arduino.h>
#include <PZEM004Tv30.h>
#include <WiFi.h>
#include <MQTT.h>
#include <string.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

TaskHandle_t Task0;
TaskHandle_t Task1;

const char *ssid = "cpciot1";
const char *password = "10987654";
const char *mqtt_server = "10.10.100.196";
/*const char *ssid = "true_home2G_UM3";
const char *password = "Kk67Dc54";
const char *mqtt_server = "203.158.131.196";*/
const int mqtt_port = 1883;
String clientId = "";
String deviceTopic = "";
float voltage, current, power, energy, frequency;
int ledState = LOW, bootCount = 0, pzemErrorCount = 0, mqttReconnectCount = 0, wifiReconnectCount = 0;
bool cmdFromServer = false, serverIsOnline = false;
char strbuf[15];

const size_t electricalVariableJsonSize = JSON_OBJECT_SIZE(5);
char electricalVariableJsonOutput[electricalVariableJsonSize + 80];

const size_t devicePropertiesJsonSize = JSON_OBJECT_SIZE(3);
char devicePropertiesJsonOutput[electricalVariableJsonSize + 60];

void setup_wifi();
void on_message(String &topic, String &payload);
void mqtt_connect();
void getPzem();
void handle_ota(void *parameter);
void handle_mqtt(void *parameter);
void handle_mainProgram(void *parameter);
void ledBlink(int interval, int delaytime);

PZEM004Tv30 pzem(&Serial2);
WiFiClient espClient;
MQTTClient client(1024);
DynamicJsonDocument electricalVariableJsonDoc(electricalVariableJsonSize);
DynamicJsonDocument devicePropertiesJsonDoc(devicePropertiesJsonSize);
SemaphoreHandle_t binarySemaphores[2] = {NULL, NULL};

void setup()
{
    Serial.begin(115200);
    WiFi.setAutoReconnect(true);
    if (WiFi.macAddress() == "F0:08:D1:D7:6D:F8")
    {
        clientId = "airconController3";
        deviceTopic = "myFinalProject/airconController3/";
    }
    if (WiFi.macAddress() == "8C:AA:B5:93:69:34")
    {
        clientId = "airconController2";
        deviceTopic = "myFinalProject/airconController2/";
    }
    if (WiFi.macAddress() == "8C:AA:B5:94:1E:5C")
    {
        clientId = "airconController1";
        deviceTopic = "myFinalProject/airconController1/";
    }
    else{
        clientId = "airconController4";
        deviceTopic = "myFinalProject/airconController4/";
    }

    EEPROM.begin(1);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(25, OUTPUT);
    digitalWrite(25, LOW);
    ledBlink(50, 1000);
    bootCount = EEPROM.read(0);
    bootCount++;
    EEPROM.write(0, bootCount);
    EEPROM.commit();

    setup_wifi();
    client.begin(mqtt_server, mqtt_port, espClient);
    client.onMessage(on_message);

    devicePropertiesJsonDoc["wifiLocalIP"] = WiFi.localIP().toString().c_str();
    devicePropertiesJsonDoc["online"] = false;
    devicePropertiesJsonDoc["bootcount"] = bootCount;
    serializeJson(devicePropertiesJsonDoc, devicePropertiesJsonOutput);

    client.setWill((deviceTopic + "properties").c_str(), devicePropertiesJsonOutput, true, 2);
    mqtt_connect();

    for (int i = 0; i < 2; i++)
    {
        binarySemaphores[i] = xSemaphoreCreateBinary(); // create a binary semaphore
    }
    xSemaphoreGive(binarySemaphores[0]);
    xSemaphoreGive(binarySemaphores[1]);

    electricalVariableJsonDoc["voltage"] = "null";
    electricalVariableJsonDoc["current"] = "null";
    electricalVariableJsonDoc["power"] = "null";
    electricalVariableJsonDoc["energy"] = "null";
    electricalVariableJsonDoc["frequency"] = "null";

    //pzem.resetEnergy();

    xTaskCreatePinnedToCore(
        &handle_mainProgram,         // Function that should be called
        "Handle ota upload", // Name of the task (for debugging)
        8096,                // Stack size (bytes)
        NULL,                // Parameter to pass
        0,                   // Task priority
        &Task0,                // Task handle
        1                    // Core you want to run the task on (0 or 1)
    );
    xTaskCreatePinnedToCore(
        &handle_mqtt,             // Function that should be called
        "Handle mqtt connection", // Name of the task (for debugging)
        8096,                     // Stack size (bytes)
        NULL,                     // Parameter to pass
        0,                        // Task priority
        &Task1,                     // Task handle
        1                         // Core you want to run the task on (0 or 1)
    );
}

void loop()
{
    // do nothing.
    getPzem();
    ledBlink(1000, 2000);
    //delay(1);
}

void setup_wifi()
{
    delay(10);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        wifiReconnectCount++;
        if(wifiReconnectCount == 120){
            ESP.restart();
        }
        delay(500);
        Serial.print(".");
    }

    randomSeed(micros());

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void on_message(String &topic, String &payload)
{
    if (topic == "myFinalProject/server/electricalAppliances/airconController2/command")
    {
        Serial.print("Command: ");
        Serial.print(payload);
        Serial.println();
        if (payload == "true")
        {
            cmdFromServer = true;
        }
        else if (payload == "false")
        {
            cmdFromServer = false;
        }
    }
    if (topic == "myFinalProject/server/properties/online")
    {
        Serial.print("server is online?: ");
        Serial.print(payload);
        Serial.println();
        if (payload == "false")
        {
            serverIsOnline = false;
        }
        else if (payload == "true")
        {
            serverIsOnline = true;
        }
    }
}

void mqtt_connect()
{
    if (client.connect(clientId.c_str(), "admin", "5617091"))
    {
        devicePropertiesJsonDoc["wifiLocalIP"] = WiFi.localIP().toString().c_str();
        devicePropertiesJsonDoc["online"] = true;
        devicePropertiesJsonDoc["bootcount"] = bootCount;
        serializeJson(devicePropertiesJsonDoc, devicePropertiesJsonOutput);
        client.publish((deviceTopic + "properties").c_str(), devicePropertiesJsonOutput, true, 2);

        Serial.println("MQTT Connected");
        client.subscribe("myFinalProject/server/electricalAppliances/airconController2/command", 2); //second parameter is QoS.
        client.subscribe("myFinalProject/server/properties/online", 2);
        Serial.printf("bootcount: %d\n", bootCount);
        Serial.printf("MQTT error code: %d, return code: %d\n", client.lastError(), client.returnCode());
    }
    else
    {
        mqttReconnectCount++;
        Serial.printf("MQTT error code: %d, return code: %d\n", client.lastError(), client.returnCode());
        Serial.println("Attempting MQTT connection");
        Serial.println(mqttReconnectCount);
        if (WiFi.status() != WL_CONNECTED)
        {
            wifiReconnectCount++;
            if(wifiReconnectCount == 60){
                ESP.restart();
            }
        }
        if(mqttReconnectCount == 60){
            ESP.restart();
        }
        digitalWrite(25, LOW);
        ledBlink(100, 1000);
    }
}

/*void handle_ota(void *parameter)
{
    while (true)
    {
        xSemaphoreTake(binarySemaphores[0], portMAX_DELAY);
        ArduinoOTA.handle();
        xSemaphoreGive(binarySemaphores[1]);
    }
}*/

void handle_mqtt(void *parameter)
{
    while (true)
    {
        xSemaphoreTake(binarySemaphores[1], portMAX_DELAY);
        client.loop();
        xSemaphoreGive(binarySemaphores[0]);
    }
}

void handle_mainProgram(void *parameter)
{
    while (true)
    {
        xSemaphoreTake(binarySemaphores[0], portMAX_DELAY);
        if (Serial2.available() != 0)
        {
            pzemErrorCount++;
            Serial.println(pzemErrorCount);
            if (pzemErrorCount >= 5)
            {
                ESP.restart();
            }
        }
        while(!client.connected()){
            mqtt_connect();
        }

        if (serverIsOnline == true)
        {
            if (cmdFromServer == true)
            {
                digitalWrite(25, HIGH);
            }
            else if (cmdFromServer == false)
            {
                digitalWrite(25, LOW);
            }
        }
        else if (serverIsOnline == false)
        {
            digitalWrite(25, LOW);
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            wifiReconnectCount++;
            delay(500);
            if(wifiReconnectCount == 120){
                ESP.restart();
            }   
        }
        //taskYIELD();
        xSemaphoreGive(binarySemaphores[1]);
    }
}

void ledBlink(int interval, int delaytime)
{
    for (int i = 0; i < (delaytime / interval); i++)
    {
        if (ledState == LOW)
        {
            ledState = HIGH;
        }
        else
        {
            ledState = LOW;
        }
        digitalWrite(LED_BUILTIN, ledState);
        delay(interval);
    }
}

void getPzem()
{
    voltage = pzem.voltage();
    if (!isnan(voltage))
    {
        electricalVariableJsonDoc["voltage"] = voltage;
    }
    else
    {
        electricalVariableJsonDoc["voltage"] = 0.0;
    }

    current = pzem.current();
    if (!isnan(current))
    {
        electricalVariableJsonDoc["current"] = current;
    }
    else
    {
        electricalVariableJsonDoc["current"] = 0.0;
    }

    power = pzem.power();
    if (!isnan(power))
    {
        electricalVariableJsonDoc["power"] = power;
    }
    else
    {
        electricalVariableJsonDoc["power"] = 0.0;
    }

    energy = pzem.energy();
    if (!isnan(energy))
    {
        electricalVariableJsonDoc["energy"] = energy;
    }
    else
    {
        electricalVariableJsonDoc["energy"] = 0.0;
    }

    frequency = pzem.frequency();
    if (!isnan(frequency))
    {
        electricalVariableJsonDoc["frequency"] = frequency;
    }
    else
    {
        electricalVariableJsonDoc["frequency"] = 0.0;
    }

    if(client.connected()){
        serializeJson(electricalVariableJsonDoc, electricalVariableJsonOutput);
        client.publish((deviceTopic + "measure").c_str(), electricalVariableJsonOutput, false, 0);
    } 
}
