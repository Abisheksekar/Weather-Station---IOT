#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>
#include <Arduino_JSON.h>
#include "LittleFS.h"
// WiFi Credentials
const char* ssid = "OnePlus Nord CE3 5G";
const char* password = "ajay3690";
const char* server = "http://eu.thingsboard.cloud/api/v1/hn7ht392ipiraefyadbc/telemetry";

// Sensor Definitions
#define DHTPIN D4
#define DHTTYPE DHT11
#define MQ5_PIN A0
DHT dht(DHTPIN, DHTTYPE);
Adafruit_BMP085 bmp;
Adafruit_MPU6050 mpu;

// Web Server & WebSockets
AsyncWebServer serverWeb(80);
WebSocketsServer webSocket(81);
AsyncEventSource events("/events");

// JSON Object for WebSocket
JSONVar readings;

void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    
    Serial.print("Connecting to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");
    Serial.println(WiFi.localIP());

    // Initialize Sensors
    if (!bmp.begin()) {
        Serial.println("Could not find BMP180 sensor!");
        while (1);
    }
    dht.begin();
    if (!mpu.begin()) {
        Serial.println("MPU6050 not found!");
        while (1);
    }
    
    // Initialize LittleFS for Web Server
    if (!LittleFS.begin()) {
        Serial.println("LittleFS Mount Failed");
        return;
    }
    
    serverWeb.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/index.html", "text/html");
    });
    serverWeb.serveStatic("/", LittleFS, "/");
    serverWeb.addHandler(&events);
    serverWeb.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    serverWeb.begin();
    Serial.println("Web Server Started!");
}

void loop() {
    webSocket.loop();
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;
        
        // Read Sensors
        float temperature = dht.readTemperature();
        float humidity = dht.readHumidity();
        float pressure = bmp.readPressure() / 100.0;
        int gasLevel = analogRead(MQ5_PIN);
        float co = gasLevel * 0.02;
        float no2 = gasLevel * 0.01;
        float ch4 = gasLevel * 0.015;
        float aqi = (co + no2 + ch4) / 3.0;
        
        // Read MPU6050 data
        sensors_event_t a, g, temp;
        mpu.getEvent(&a, &g, &temp);

        // Send MPU6050 data over WebSockets
        String json = "{";
        json += "\"accX\":" + String(a.acceleration.x) + ",";
        json += "\"accY\":" + String(a.acceleration.y) + ",";
        json += "\"accZ\":" + String(a.acceleration.z) + ",";
        json += "\"gyroX\":" + String(g.gyro.x) + ",";
        json += "\"gyroY\":" + String(g.gyro.y) + ",";
        json += "\"gyroZ\":" + String(g.gyro.z) + ",";
        json += "\"temp\":" + String(temp.temperature);
        json += "}";
        webSocket.broadcastTXT(json);

        // Prepare JSON Payload for ThingsBoard
        JSONVar payload;
        payload["temperature"] = temperature;
        payload["humidity"] = humidity;
        payload["pressure"] = pressure;
        payload["gas"] = gasLevel;
        payload["co"] = co;
        payload["no2"] = no2;
        payload["ch4"] = ch4;
        payload["aqi"] = aqi;
        
        String jsonString = JSON.stringify(payload);
        char jsonPayload[256];
        jsonString.toCharArray(jsonPayload, 256);
        
        Serial.println("Sending Data: " + jsonString);
        
        http.begin(client, server);
        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.POST(jsonPayload);
        
        Serial.print("HTTP Response: ");
        Serial.println(httpResponseCode);
        
        http.end();
    } else {
        Serial.println("WiFi Disconnected!");
    }
    
    delay(2000);
}

// WebSocket Event Handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_CONNECTED) {
        Serial.println("WebSocket Client Connected");
    }
}
