/*
  SMART Node - Environmental Monitoring System
  Project: Susquehanna Microclimate Assessment and Recovery for Tracking (SMART)

  Description:
  This code is designed for an environmental monitoring node used to collect
  temperature and humidity data for microclimate assessment as part of the SMART initiative.

  Features:
  - Reads real-time temperature and humidity data from a DHT11 sensor.
  - Logs sensor data to an SD card with accurate timestamps provided by an RTC module.
  - Hosts a local WiFi access point to serve live data and historical logs via a web interface.

  Purpose:
  Enables reliable, timestamped environmental data collection in the field, supporting ecological
  research, community science projects, and conservation efforts within the Susquehanna River Valley.

  Author: Douglas Fessler
  Date: 05222025
*/

// ---------- Libraries ----------
#include <DHT.h>                   // Library for DHT sensor
#include <SPI.h>                   // SPI for SD card
#include <SD.h>                    // SD card file handling
#include <Wire.h>                  // I2C communication for RTC
#include <RTClib.h>                // Real-time clock
#include <WiFiS3.h>                // WiFi for Arduino Uno R4 / Nano ESP32
#include <OneWire.h>               // OneWire communication for DS18B20
#include <DallasTemperature.h>     // Dallas library for DS18B20 sensor
#include "arduino_secrets.h"       // Contains SECRET_SSID and SECRET_PASS

// ---------- Sensor & Hardware Pins ----------
#define DHTPIN 2                   // DHT sensor connected to pin 2
#define DHTTYPE DHT11              // Define sensor type (DHT11)
#define ONE_WIRE_BUS 3             // DS18B20 connected to pin 3
const int chipSelect = 10;         // CS pin for SD card module

// ---------- Object Instantiation ----------
DHT dht(DHTPIN, DHTTYPE);          // Create DHT object
RTC_DS1307 rtc;                    // Create RTC object
OneWire oneWire(ONE_WIRE_BUS);     // Setup OneWire bus
DallasTemperature sensors(&oneWire); // Create DS18B20 object

// ---------- WiFi Config ----------
char ssid[] = SECRET_SSID;         // WiFi SSID
char pass[] = SECRET_PASS;         // WiFi password
WiFiServer server(80);             // Start web server on port 80
int status = WL_IDLE_STATUS;       // WiFi status variable

// ---------- Timing ----------
unsigned long interval = 900000;   // Log data every 15 minutes
unsigned long previousMillis = 0;  // Tracks time for interval

void setup() {
  Serial.begin(9600);
  dht.begin();
  sensors.begin();
  Wire.begin();

  // ---------- Initialize RTC ----------
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1); // Halt if RTC is not found
  }
  // Uncomment line below to manually set RTC time (only once)
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // ---------- Initialize SD Card ----------
  if (!SD.begin(chipSelect)) {
    Serial.println("SD card initialization failed!");
    while (1); // Halt if SD card not found
  }
  Serial.println("SD card initialized.");

  // ---------- Initialize Log File ----------
  File dataFile = SD.open("datalog.csv", FILE_WRITE);
  if (dataFile && dataFile.size() == 0) {
    // Write headers if new file
    dataFile.println("Date,Time,Humidity (%),Air Temp (C),Air Temp (F),Heat Index (C),Heat Index (F),Water Temp (C)");
  }
  dataFile.close();

  // ---------- Setup WiFi Access Point ----------
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found");
    while (true); // Halt if no module
  }

  WiFi.config(IPAddress(192, 168, 4, 1));  // Optional: static IP
  status = WiFi.beginAP(ssid, pass);      // Start access point
  delay(10000);                           // Allow WiFi to stabilize
  server.begin();                         // Start server

  printWiFiStatus();                      // Show connection info
}

void loop() {
  // ---------- Handle Web Client ----------
  WiFiClient client = server.available();
  if (client) {
    Serial.println("New client connected");
    String currentLine = "";

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {
          if (currentLine.length() == 0) {
            // Send HTTP header
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html");
            client.println();

            // Send HTML body
            client.println("<html><head><title>Sensor Dashboard</title></head><body>");
            client.println("<h2>Live Sensor Readings</h2>");

            // Read sensors
            float h = dht.readHumidity();
            float t = dht.readTemperature();
            float f = dht.readTemperature(true);
            float hic = dht.computeHeatIndex(t, h, false);
            float hif = dht.computeHeatIndex(f, h);
            sensors.requestTemperatures();
            float waterTempC = sensors.getTempCByIndex(0);

            // Display live data
            client.print("<p><strong>Air Humidity:</strong> "); client.print(h); client.println("%</p>");
            client.print("<p><strong>Air Temperature:</strong> "); client.print(t); client.print("&deg; C / "); client.print(f); client.println("&deg; F</p>");
            client.print("<p><strong>Heat Index:</strong> "); client.print(hic); client.print("&deg; C / "); client.print(hif); client.println("&deg; F</p>");
            client.print("<p><strong>Water Temperature:</strong> "); client.print(waterTempC); client.println("&deg; C</p>");

            // Display log data
            client.println("<h3>Log Data</h3><table border='1'><tr><th>Date</th><th>Time</th><th>Humidity</th><th>Air Temp (C)</th><th>Air Temp (F)</th><th>HI (C)</th><th>HI (F)</th><th>Water Temp (C)</th></tr>");

            File dataFile = SD.open("datalog.csv");
            if (dataFile) {
              while (dataFile.available()) {
                String line = dataFile.readStringUntil('\n');
                int startIdx = 0;
                int endIdx = 0;

                client.print("<tr>");
                while ((endIdx = line.indexOf(',', startIdx)) != -1) {
                  client.print("<td>");
                  client.print(line.substring(startIdx, endIdx));
                  client.print("</td>");
                  startIdx = endIdx + 1;
                }
                client.print("<td>");
                client.print(line.substring(startIdx));
                client.println("</td></tr>");
              }
              dataFile.close();
            } else {
              client.println("<p>Error opening datalog.csv</p>");
            }

            client.println("</table></body></html>");
            break; // Done sending response
          }
          currentLine = "";
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    client.stop();
    Serial.println("Client disconnected");
  }

  // ---------- Handle Data Logging ----------
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    DateTime now = rtc.now();              // Get current time
    float h = dht.readHumidity();          // Humidity
    float t = dht.readTemperature();       // Temp in C
    float f = dht.readTemperature(true);   // Temp in F
    float hic = dht.computeHeatIndex(t, h, false);
    float hif = dht.computeHeatIndex(f, h);

    sensors.requestTemperatures();
    float waterTempC = sensors.getTempCByIndex(0);

    // Check sensor validity
    if (isnan(h) || isnan(t) || isnan(f) || waterTempC == DEVICE_DISCONNECTED_C) {
      Serial.println("Sensor read failed!");
      return;
    }

    // Save data to SD card
    File dataFile = SD.open("datalog.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.print(now.timestamp(DateTime::TIMESTAMP_DATE)); dataFile.print(",");
      dataFile.print(now.timestamp(DateTime::TIMESTAMP_TIME)); dataFile.print(",");
      dataFile.print(h); dataFile.print(",");
      dataFile.print(t); dataFile.print(",");
      dataFile.print(f); dataFile.print(",");
      dataFile.print(hic); dataFile.print(",");
      dataFile.print(hif); dataFile.print(",");
      dataFile.println(waterTempC);
      dataFile.close();
    } else {
      Serial.println("Error opening datalog.csv");
    }
  }
}

// ---------- Print WiFi Status ----------
void printWiFiStatus() {
  Serial.print("Access Point: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Open browser to access the sensor dashboard.");
}
