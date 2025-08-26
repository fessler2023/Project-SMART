/*
  SMART Node - Environmental Monitoring System
  Project: Susquehanna Microclimate Assessment and Recovery for Tracking (SMART)

  Description:
  - Reads temperature and humidity from DHT11.
  - Reads water temperature from DS18B20.
  - Logs hourly data to SD card.
  - Archives older data.
  - Hosts a WiFi dashboard showing last 7 days of data with color-coded indicators.

  Author: Douglas Fessler
  Date: 05262025
*/

// ---------- Libraries ----------
#include <DHT.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>
#include <WiFiS3.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "arduino_secrets.h"

// ---------- Sensor & Hardware Pins ----------
#define DHTPIN 2
#define DHTTYPE DHT11
#define ONE_WIRE_BUS 3
const int chipSelect = 10;

// ---------- Object Instantiation ----------
DHT dht(DHTPIN, DHTTYPE);
RTC_DS1307 rtc;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// ---------- WiFi Config ----------
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
WiFiServer server(80);
int status = WL_IDLE_STATUS;

// ---------- Timing ----------
unsigned long interval = 3600000;   // 1 hour 3600000
unsigned long previousMillis = 0;

// ---------- Function Prototypes ----------
void printWiFiStatus();
String colorIndicator(float value, float greenMin, float greenMax, float yellowMin, float yellowMax);

// ---------- Setup ----------
void setup() {
  Serial.begin(9600);
  dht.begin();
  sensors.begin();
  Wire.begin();

  // ---------- Initialize RTC ----------
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  // ---------- Initialize SD Card ----------
  if (!SD.begin(chipSelect)) {
    Serial.println("SD card initialization failed!");
    while (1);
  }
  Serial.println("SD card initialized.");

  // ---------- Initialize Log File ----------
  File dataFile = SD.open("datalog.csv", FILE_WRITE);
  if (dataFile && dataFile.size() == 0) {
    dataFile.println("Date,Time,Humidity (%),Air Temp (C),Air Temp (F),HI (C),HI (F),Water Temp (C)");
  }
  dataFile.close();

  // ---------- Setup WiFi ----------
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found");
    while (true);
  }

  WiFi.config(IPAddress(192, 168, 4, 1));
  status = WiFi.beginAP(ssid, pass);
  delay(10000);
  server.begin();

  printWiFiStatus();
}

// ---------- Loop ----------
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
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html");
            client.println();

            // ---------- HTML & CSS ----------
            client.println("<!DOCTYPE html><html><head>");
            client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
            client.println("<title>SMART Node Dashboard</title>");
            client.println("<style>");
            client.println("body { font-family: Arial, sans-serif; background-color:#f4f4f4; margin:0; padding:20px; }");
            client.println("h2, h3 { color:#2E8B57; }");
            client.println(".card { background-color:#fff; padding:15px; margin:10px 0; border-radius:10px; box-shadow:0 2px 5px rgba(0,0,0,0.2);}"); 
            client.println("table { border-collapse: collapse; width:100%; }");
            client.println("th, td { text-align:center; padding:8px; border-bottom:1px solid #ddd; }");
            client.println("th { background-color:#2E8B57; color:white; }");
            client.println(".green { background-color: #a0e6a0; }");
            client.println(".orange { background-color: #f4d27a; }");
            client.println(".red { background-color: #f28c8c; }");
            client.println("</style></head><body>");

            client.println("<h2>SMART Node Environmental Dashboard</h2>");

            // ---------- Live Sensor Data ----------
            float h = dht.readHumidity();
            float t = dht.readTemperature();
            float f = dht.readTemperature(true);
            float hic = dht.computeHeatIndex(t, h, false);
            float hif = dht.computeHeatIndex(f, h);
            sensors.requestTemperatures();
            float waterTempC = sensors.getTempCByIndex(0);

            client.println("<div class='card'>");
            client.print("<p><strong>Air Humidity:</strong> <span class='"); client.print(colorIndicator(h, 30, 60, 20, 70)); client.print("'>"); client.print(h); client.println("%</span></p>");
            client.print("<p><strong>Air Temperature:</strong> <span class='"); client.print(colorIndicator(t, 20, 25, 15, 30)); client.print("'>"); client.print(t); client.print(" &deg;C / "); client.print(f); client.println(" &deg;F</span></p>");
            client.print("<p><strong>Heat Index:</strong> "); client.print(hic); client.print(" &deg;C / "); client.print(hif); client.println(" &deg;F</p>");
            client.print("<p><strong>Water Temperature:</strong> <span class='"); client.print(colorIndicator(waterTempC, 15, 25, 10, 30)); client.print("'>"); client.print(waterTempC); client.println(" &deg;C</span></p>");
            client.println("</div>");

            // ---------- Last 7 Days Log ----------
            client.println("<h3>Last 7 Days Log</h3>");
            client.println("<table><tr><th>Date</th><th>Time</th><th>Humidity</th><th>Air Temp (C)</th><th>Air Temp (F)</th><th>HI (C)</th><th>HI (F)</th><th>Water Temp (C)</th></tr>");

            File dataFile = SD.open("datalog.csv");
            if (dataFile) {
              DateTime now = rtc.now();
              while (dataFile.available()) {
                String line = dataFile.readStringUntil('\n');
                if (line.startsWith("Date")) continue;

                int commaIndex = line.indexOf(',');
                String dateStr = line.substring(0, commaIndex);
                int year = dateStr.substring(0,4).toInt();
                int month = dateStr.substring(5,7).toInt();
                int day = dateStr.substring(8,10).toInt();
                DateTime entryDate(year, month, day, 0, 0, 0);
                TimeSpan diff = now - entryDate;

                if (diff.days() <= 7) {
                  client.print("<tr>");
                  int startIdx = 0;
                  int endIdx = 0;
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
              }
              dataFile.close();
            } else {
              client.println("<tr><td colspan='8'>Error opening datalog.csv</td></tr>");
            }

            client.println("</table>");
            client.println("</body></html>");
            break;
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

    DateTime now = rtc.now();
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    float f = dht.readTemperature(true);
    float hic = dht.computeHeatIndex(t, h, false);
    float hif = dht.computeHeatIndex(f, h);
    sensors.requestTemperatures();
    float waterTempC = sensors.getTempCByIndex(0);

    if (isnan(h) || isnan(t) || isnan(f) || waterTempC == DEVICE_DISCONNECTED_C) {
      Serial.println("Sensor read failed!");
      return;
    }

    // ---------- Save Data to SD ----------
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

// ---------- Color Indicator Function ----------
String colorIndicator(float value, float greenMin, float greenMax, float yellowMin, float yellowMax) {
  if (value >= greenMin && value <= greenMax) return "green";
  else if (value >= yellowMin && value <= yellowMax) return "orange";
  else return "red";
}

// ---------- WiFi Status ----------
void printWiFiStatus() {
  Serial.print("Access Point: "); Serial.println(WiFi.SSID());
  Serial.print("IP address: "); Serial.println(WiFi.localIP());
  Serial.println("Open browser to access the sensor dashboard.");
}
