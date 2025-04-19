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
  Date: 04192025
*/

// Include necessary libraries for WiFi, DHT sensor, SD card, RTC, and credentials
#include "WiFiS3.h"         // Library for WiFi on Nano 33 IoT
#include "DHT.h"            // Library for DHT temperature/humidity sensor
#include "SD.h"             // SD card library
#include "arduino_secrets.h" // Contains SSID and password (keep this file secure)
#include "RTC.h"            // Real-time clock library

// WiFi credentials from arduino_secrets.h
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

int keyIndex = 0; // Not used for Nano 33 IoT but required for WiFi.begin()
int led = LED_BUILTIN; // Built-in LED pin
int status = WL_IDLE_STATUS; // WiFi connection status
WiFiServer server(80); // Web server listening on port 80

// DHT Sensor configuration
#define DHTPIN 2           // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11      // Type of DHT sensor
DHT dht(DHTPIN, DHTTYPE);  // Initialize DHT sensor

File dataFile; // File object for reading/writing SD data

// Data logging interval (in milliseconds) — 15 minutes
const unsigned long interval = 900000;
unsigned long previousMillis = 0; // Used to track elapsed time for data logging

void setup() {
  Serial.begin(9600); // Initialize serial communication for debugging

  // Initialize the RTC and set the initial time
  RTC.begin();
  RTCTime startTime(16, Month::JULY, 2023, 10, 14, 03, DayOfWeek::SATURDAY, SaveLight::SAVING_TIME_ACTIVE);
  RTC.setTime(startTime);

  // Wait for Serial monitor to open (helpful during development)
  while (!Serial) {
    ;
  }

  Serial.println("Access Point Web Server");

  pinMode(led, OUTPUT); // Set LED pin as output

  // Check for the presence of the WiFi module
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true); // Halt program
  }

  // Optional firmware check
  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // Configure a static IP address for the access point
  WiFi.config(IPAddress(192, 48, 56, 2));

  // Create WiFi access point
  Serial.print("Creating access point named: ");
  Serial.println(ssid);
  status = WiFi.beginAP(ssid, pass);
  if (status != WL_AP_LISTENING) {
    Serial.println("Creating access point failed");
    while (true);
  }

  delay(10000); // Delay to allow AP to fully initialize

  server.begin(); // Start the web server
  printWiFiStatus(); // Output the AP IP address

  dht.begin(); // Initialize DHT sensor

  // Initialize the SD card
  if (!SD.begin(10)) {
    Serial.println("SD card initialization failed!");
    while (true);
  }

  // Create and write header to data log file
  dataFile = SD.open("data.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println("Date,Time,Temperature,Humidity");
    dataFile.close();
  } else {
    Serial.println("Error opening data.txt file!");
    while (true);
  }
}

void loop() {
  RTCTime currentTime;
  RTC.getTime(currentTime); // Get current date/time from RTC

  // Monitor and print WiFi connection status changes
  if (status != WiFi.status()) {
    status = WiFi.status();
    if (status == WL_AP_CONNECTED) {
      Serial.println("Device connected to AP");
    } else {
      Serial.println("Device disconnected from AP");
    }
  }

  WiFiClient client = server.available(); // Listen for incoming HTTP clients

  if (client) {
    Serial.println("New client connected");
    String currentLine = "";

    while (client.connected()) {
      delayMicroseconds(10);

      if (client.available()) {
        char c = client.read();
        Serial.write(c);

        if (c == '\n') {
          // End of HTTP header
          if (currentLine.length() == 0) {
            // Send HTTP response
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            // Get current sensor readings
            float temperature = dht.readTemperature();
            float temperaturef = dht.readTemperature(true);
            float humidity = dht.readHumidity();

            // Output sensor data to browser
            client.print("<p style=\"font-size:2vw;\"> Current Temperature Celsius: ");
            client.print(temperature);
            client.print(" &#8451;<br></p>");

            client.print("<p style=\"font-size:2vw;\"> Current Temperature Fahrenheit: ");
            client.print(temperaturef);
            client.print(" &#8457;<br></p>");

            client.print("<p style=\"font-size:2vw;\">Current Humidity: ");
            client.print(humidity);
            client.print(" %<br></p>");

            // Display logged data from SD card
            client.println("<h2>Data from data.txt:</h2>");
            client.println("<table>");
            client.println("<tr><th>Date</th><th>Time</th><th>Temperature</th><th>Humidity</th></tr>");

            dataFile = SD.open("data.txt", FILE_READ);
            if (dataFile) {
              while (dataFile.available()) {
                String line = dataFile.readStringUntil('\n');
                if (line.startsWith("Date,Time,Temperature,Humidity")) continue;

                // Parse and display table rows
                client.println("<tr>");
                client.println("<td>" + line.substring(0, line.indexOf(',')) + "</td>");
                client.println("<td>" + line.substring(line.indexOf(',') + 1) + "</td>");
                client.println("</tr>");
              }
              dataFile.close();
            } else {
              Serial.println("Error opening data.txt file for reading!");
            }

            client.println("</table>");
            client.println();
            break; // Exit response loop
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }

    client.stop(); // Close client connection
    Serial.println("Client disconnected");
  }

  // Handle data logging every 15 minutes
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    dataFile = SD.open("data.txt", FILE_WRITE);
    if (dataFile) {
      // Write date in DD/MM/YYYY format
      dataFile.print(currentTime.getDayOfMonth());
      dataFile.print("/");
      dataFile.print(Month2int(currentTime.getMonth()));
      dataFile.print("/");
      dataFile.print(currentTime.getYear());
      dataFile.print(",");

      // Write time in HH:MM:SS format
      dataFile.print(currentTime.getHour());
      dataFile.print(":");
      dataFile.print(currentTime.getMinutes());
      dataFile.print(":");
      dataFile.print(currentTime.getSeconds());
      dataFile.print(",");

      // Write sensor readings
      dataFile.print(temperature);
      dataFile.print(",");
      dataFile.println(humidity);
      dataFile.close();
    } else {
      Serial.println("Error opening data.txt file for writing!");
    }
  }
}

// Outputs the current status of the WiFi access point to the Serial Monitor
void printWiFiStatus() {
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);
}
