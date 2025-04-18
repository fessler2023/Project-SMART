// Created by: [Your Name]
// Date: April 18, 2025
// Description: This is an IoT solution for monitoring temperature and humidity using a DHT11 sensor.
// The data is logged to an SD card and made accessible via a web server running on an access point (AP).
// This code includes a Real-Time Clock (RTC) for timestamping data and serves the data via a local web server.

#include "WiFiS3.h"        // Wi-Fi library for managing Wi-Fi connections
#include "DHT.h"            // DHT sensor library for reading temperature and humidity
#include "SD.h"             // SD card library for file handling
#include "arduino_secrets.h" // File containing secret credentials for Wi-Fi (not shared in code)
#include "RTC.h"            // RTC library for managing real-time clock

// Wi-Fi credentials
char ssid[] = SECRET_SSID;  // Wi-Fi SSID (retrieved from arduino_secrets.h)
char pass[] = SECRET_PASS;  // Wi-Fi password (retrieved from arduino_secrets.h)
int keyIndex = 0;           // Wi-Fi key index (for networks with multiple keys)
int led = LED_BUILTIN;      // Built-in LED pin on the board
int status = WL_IDLE_STATUS; // Wi-Fi status
WiFiServer server(80);      // HTTP server on port 80

// DHT sensor configuration
#define DHTPIN 2            // Pin where DHT sensor is connected
#define DHTTYPE DHT11       // DHT11 temperature and humidity sensor
DHT dht(DHTPIN, DHTTYPE);  // Initialize DHT sensor

File dataFile;             // File object for SD card access

const unsigned long interval = 900000; // Interval (in milliseconds) to log data (15 minutes)
unsigned long previousMillis = 0;     // Stores last timestamp for data logging

void setup() {
  // Initialize serial communication for debugging
  Serial.begin(9600);

  // Initialize RTC and set initial time (manually set before deployment)
  RTC.begin();
  RTCTime startTime(17, Month::APRIL, 2025, 15, 52, 0, DayOfWeek::THURSDAY, SaveLight::SAVING_TIME_ACTIVE);
  RTC.setTime(startTime);  // Set RTC time manually (customize as per requirement)

  // Wait for serial to initialize before proceeding (for boards like Leonardo)
  while (!Serial) {
    ;
  }
  Serial.println("Access Point Web Server");

  pinMode(led, OUTPUT);  // Set the built-in LED pin as output

  // Check if Wi-Fi module is present
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);  // Loop forever if Wi-Fi module is not detected
  }

  String fv = WiFi.firmwareVersion();  // Get Wi-Fi firmware version
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  WiFi.config(IPAddress(192, 48, 56, 2));  // Set static IP address

  // Start the Wi-Fi access point (AP) with specified SSID and password
  Serial.print("Creating access point named: ");
  Serial.println(ssid);
  status = WiFi.beginAP(ssid, pass);  // Begin creating an AP
  if (status != WL_AP_LISTENING) {
    Serial.println("Creating access point failed");
    while (true);  // Loop forever if AP creation fails
  }

  delay(10000);  // Wait for 10 seconds before starting the server
  server.begin();  // Start the server

  printWiFiStatus();  // Print the status of the Wi-Fi AP

  dht.begin();  // Initialize the DHT sensor

  // Initialize SD card
  if (!SD.begin(10)) {
    Serial.println("SD card initialization failed!");
    while (true);  // Loop forever if SD card fails to initialize
  }

  // Open the SD card file for writing headers
  dataFile = SD.open("data.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println("Date,Time,Temperature,Humidity");  // Write headers to the file
    dataFile.close();
  } else {
    Serial.println("Error opening data.txt file!");
    while (true);  // Loop forever if file opening fails
  }
}

void loop() {
  RTCTime currentTime;
  RTC.getTime(currentTime);  // Retrieve current time from RTC

  // Check Wi-Fi status and reconnect if needed
  if (status != WiFi.status()) {
    status = WiFi.status();
    if (status == WL_AP_CONNECTED) {
      Serial.println("Device connected to AP");
    } else {
      Serial.println("Device disconnected from AP");
    }
  }

  // Check if a client has connected to the web server
  WiFiClient client = server.available();
  if (client) {
    Serial.println("New client");
    String currentLine = "";
    String request = "";

    // Read the HTTP request from the client
    while (client.connected()) {
      delayMicroseconds(10);  // Small delay to allow the client to send more data
      if (client.available()) {
        char c = client.read();
        request += c;
        Serial.write(c);

        if (c == '\n') {  // If we encounter a newline character
          if (currentLine.length() == 0) {  // If this is the end of the request header
            // Handle download request
            if (request.indexOf("GET /download") >= 0) {
              dataFile = SD.open("data.txt");
              if (dataFile) {
                // Send the data file as a download
                client.println("HTTP/1.1 200 OK");
                client.println("Content-Type: text/plain");
                client.println("Content-Disposition: attachment; filename=SMART_Node_Data.txt");
                client.println("Connection: close");
                client.println();
                while (dataFile.available()) {
                  client.write(dataFile.read());
                }
                dataFile.close();
              } else {
                client.println("HTTP/1.1 500 Internal Server Error");
                client.println("Content-Type: text/plain");
                client.println("Connection: close");
                client.println();
                client.println("Error opening data.txt for download.");
              }
              break;
            } else {  // Default page (sensor data)
              client.println("<html>");
              client.println("<head><title>SMART Node 01</title></head>");
              client.println("<body>");
              client.println("<h1 style=\"text-align:center;\">SMART Node 01</h1>");
              client.println("<p><a href=\"/download\" style=\"font-size:2vw;\">[Download All Data]</a></p>");
              client.print("<p style=\"font-size:2vw;\">Current Temperature Celsius: ");
              client.print(dht.readTemperature());
              client.print(" &#8451;<br></p>");
              client.print("<p style=\"font-size:2vw;\">Current Temperature Fahrenheit: ");
              client.print(dht.readTemperature(true));
              client.print(" &#8457;<br></p>");
              client.print("<p style=\"font-size:2vw;\">Current Humidity: ");
              client.print(dht.readHumidity());
              client.print(" %<br></p>");

              // Display data from SD card in a table
              client.println("<h2>Data from data.txt:</h2>");
              client.println("<table>");
              client.println("<tr><th>Date</th><th>Time</th><th>Temperature</th><th>Humidity</th></tr>");

              dataFile = SD.open("data.txt", FILE_READ);
              if (dataFile) {
                // Read and display each line of data from SD card
                while (dataFile.available()) {
                  String line = dataFile.readStringUntil('\n');
                  if (line.startsWith("Date,Time,Temperature,Humidity")) continue;  // Skip headers
                  client.println("<tr>");
                  int first = line.indexOf(',');
                  int second = line.indexOf(',', first + 1);
                  int third = line.indexOf(',', second + 1);
                  client.println("<td>" + line.substring(0, first) + "</td>");
                  client.println("<td>" + line.substring(first + 1, second) + "</td>");
                  client.println("<td>" + line.substring(second + 1, third) + "</td>");
                  client.println("<td>" + line.substring(third + 1) + "</td>");
                  client.println("</tr>");
                }
                dataFile.close();
              }
              client.println("</table>");
              client.println("</body></html>");
            }
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    client.stop();
    Serial.println("Client disconnected");
  }

  // Regularly log temperature and humidity to SD card every 15 minutes
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Read data from the DHT sensor
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    // Write the data to the SD card
    dataFile = SD.open("data.txt", FILE_WRITE);
    if (dataFile) {
      dataFile.print(currentTime.getDayOfMonth());
      dataFile.print("/");
      dataFile.print(Month2int(currentTime.getMonth()));
      dataFile.print("/");
      dataFile.print(currentTime.getYear());
      dataFile.print(",");
      dataFile.print(currentTime.getHour());
      dataFile.print(":");
      dataFile.print(currentTime.getMinutes());
      dataFile.print(":");
      dataFile.print(currentTime.getSeconds());
      dataFile.print(",");
      dataFile.print(temperature);
      dataFile.print(",");
      dataFile.println(humidity);
      dataFile.close();
    } else {
      Serial.println("Error opening data.txt file for writing!");
    }
  }
}

// Function to print Wi-Fi status (IP address and connection details)
void printWiFiStatus() {
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);
}
