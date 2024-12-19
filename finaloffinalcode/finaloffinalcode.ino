#include <WiFi.h>
#include <PubSubClient.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <time.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

TinyGPSPlus gps;
#define RXD2 16
#define TXD2 17

#define SS_PIN 5
#define RST_PIN 15

#define blue 12   
#define red1 27    
#define buz 26//32

MFRC522 rfid(SS_PIN, RST_PIN);
String lastRFID = "";        
String currentRFID = "";      
byte nuidPICC[4];

const char* ssid = "gpstracker9801";
const char* password = "Wemade@iQube";

const char* mqtt_server = "182.72.162.13";
const int mqtt_port = 1883; 
const char* mqtt_user = "iqube";
const char* mqtt_pass = "iQube@2022";

WiFiClient espClient;
PubSubClient client(espClient);
WiFiServer telnetServer(23); 
WiFiClient telnetClient;

unsigned long lastMsg = 0;
unsigned long lastGPSSave = 0;
unsigned long lastValidFix = 0;
unsigned long lastNTPAttempt = 0;  

#define MSG_BUFFER_SIZE 300
char msg[MSG_BUFFER_SIZE] = {0};
//uint32_t busid = 3062;  
char busid[] = "9801";
bool ntpObtained = false;  
bool systemOperational = false;  

const int maxRecords = 100;  
struct GPSRecord {
  double lat;
  double lng;
  String timestamp;
};
GPSRecord gpsDataBuffer[maxRecords];  
int bufferIndex = 0;  
bool dataPending = false;

const char* ntpServer = "pool.ntp.org";  
const char* backupNtpServer = "time.google.com";  
const long gmtOffset_sec = 19800;  
const int daylightOffset_sec = 0;  

int reconnectDelay = 300;  
int maxReconnectDelay = 32000; 

#define LOG_BUFFER_SIZE 10   // Store up to 10 recent log entries
String logBuffer[LOG_BUFFER_SIZE]; 
int logIndex = 0;

// Function to log messages to the Telnet client and serial output (including errors)
void logToTelnet(const String& message, bool isError = false) {
  String prefix = isError ? "ERROR: " : "";  // Add prefix to error messages
  String logMessage = prefix + message;

  // Print to Serial Monitor
  Serial.println(logMessage);
  
  // Print to Telnet client if connected
  if (telnetClient && telnetClient.connected()) {
    telnetClient.println(logMessage);
  }
  
  // Save the log into the circular buffer
  logBuffer[logIndex] = logMessage;
  logIndex = (logIndex + 1) % LOG_BUFFER_SIZE;  // Circular buffer
}

// Send recent logs to Telnet when a client connects
void sendRecentLogsToTelnet() {
  for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
    int idx = (logIndex + i) % LOG_BUFFER_SIZE;
    if (logBuffer[idx].length() > 0) {
      telnetClient.println(logBuffer[idx]);
    }
  }
}

// Log failures or critical events
void logFailure(const String& failureMessage) {
  logToTelnet(failureMessage, true);  // Log as an error
}

// Telnet initialization and handling
void startTelnet() {
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  logToTelnet("Telnet server started on port 23");
}

// Handle Telnet connections and status reporting
void handleTelnet() {
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) {
        telnetClient.stop();
      }
      telnetClient = telnetServer.available();
      logToTelnet("New Telnet client connected");
      sendRecentLogsToTelnet();  // Send recent logs to the client on connection
    } else {
      telnetServer.available().stop();  // Close extra connections
    }
  }
}

// Log current system status
void logCurrentStatus() {
  logToTelnet("===== Current System Status =====");
  logToTelnet(WiFi.status() == WL_CONNECTED ? "Wi-Fi: Connected" : "Wi-Fi: Not connected");
  logToTelnet(client.connected() ? "MQTT: Connected" : "MQTT: Not connected");
  if (gps.location.isValid()) {
    logToTelnet("GPS: Valid");
    logToTelnet("Latitude: " + String(gps.location.lat(), 6));
    logToTelnet("Longitude: " + String(gps.location.lng(), 6));
    logToTelnet("Speed: " + String(gps.speed.kmph(), 2) + " km/h");
    logToTelnet("Satellites Connected: " + String(gps.satellites.value())); // Number of satellites

  } else {
    logToTelnet("GPS: Not valid");
  }
  logToTelnet("===========================");
}

// Log startup information
void logStartup() {
  logToTelnet("===== System Startup =====");
  logToTelnet("Bus ID: " + String(busid));
  logToTelnet("Wi-Fi SSID: " + String(ssid));
  logToTelnet("==========================");
}

//Setup SPIFFS - TO store RFID keys in offline mode
void setupSPIFFS() {
  if (!SPIFFS.begin(true)) {
    logFailure("An error occurred while mounting SPIFFS");
    return;
  }
  logToTelnet("SPIFFS mounted successfully");
}

// File Management to limit file size
void storeDataInSPIFFS(double lat, double lon, String rfid) {
  File file = SPIFFS.open("/data.json", FILE_APPEND);
  if (SPIFFS.usedBytes() > 10000) { 
    logFailure("SPIFFS full, removing oldest entry.");
    file.close();
    SPIFFS.remove("/data.json");
    return;
  }
  
  StaticJsonDocument<200> doc;
  doc["ID"] = busid;
  doc["lat"] = lat;
  doc["lon"] = lon;
  doc["rfid"] = rfid;
  doc["ts"] = getISO8601Timestamp();
  
  serializeJson(doc, file);
  file.println();  
  file.close();
  logToTelnet("Data stored in SPIFFS.");
}

// Send stored RFID data when connected
void sendStoredData() {
  File file = SPIFFS.open("/data.json", FILE_READ);
  if (!file) {
    logToTelnet("No stored data found.");
    return;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, line);

    if (error) {
      logFailure("Failed to read JSON from file");
      continue;
    }

    String topic = "kctgps/" + String(busid);
    String output;
    serializeJson(doc, output);

    if (client.publish(topic.c_str(), output.c_str())) {
      logToTelnet("Published stored data: " + output);
    } else {
      logFailure("Failed to publish stored data.");
      break;
    }
  }

  file.close();
  SPIFFS.remove("/data.json");  // Clear the stored data after sending
  logToTelnet("Stored data sent and deleted.");
}

// Function to get current time in ISO 8601 format
String getISO8601Timestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    logFailure("Failed to obtain time from NTP");
    return "N/A";  
  }

  char timeStringBuff[20];  
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &timeinfo);  
  return String(timeStringBuff);
}

// Setup NTP time with retries and fallback
void setupNTP(const char* primaryNtpServer, const char* secondaryNtpServer) {
  configTime(gmtOffset_sec, daylightOffset_sec, primaryNtpServer);  
  logToTelnet("NTP setup initiated with pool.ntp.org.");

  for (int i = 0; i < 3; i++) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      logToTelnet("NTP time obtained successfully from pool.ntp.org.");
      ntpObtained = true;
      return;
    }
    delay(500); 
  }

  logToTelnet("Switching to Google's NTP server (time.google.com).");
  configTime(gmtOffset_sec, daylightOffset_sec, secondaryNtpServer);

  for (int i = 0; i < 3; i++) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      logToTelnet("NTP time obtained successfully from Google.");
      ntpObtained = true;
      return;
    }
    delay(500);  
  }

  logFailure("Failed to obtain time from both NTP servers.");
}

// Setup Wi-Fi with logging
void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  logToTelnet("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(reconnectDelay);
    reconnectDelay *= 2;  
    if (reconnectDelay > maxReconnectDelay) {
      reconnectDelay = maxReconnectDelay;
    }
  }
  reconnectDelay = 500;  
  logToTelnet("Connected to Wi-Fi.");
  setupNTP(ntpServer, backupNtpServer);  
  Serial.println("connecting to WiFi");
}

// Reconnect to MQTT with logging
void reconnect() {
  digitalWrite(blue, LOW);
  while (!client.connected()) {
    logToTelnet("Attempting MQTT connection...");
    String clientID = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientID.c_str(), mqtt_user, mqtt_pass)) {
      logToTelnet("MQTT connected");
      sendStoredData();  
    } else {
      logFailure("Failed MQTT connection. State: " + String(client.state()));
      delay(1000);
    }
  }
  digitalWrite(blue, HIGH);
}

// Function to wait for a valid GPS fix before proceeding
void waitForValidGPSFix() {
  logToTelnet("Waiting for a valid GPS fix...");
  unsigned long startTime = millis();
  
  while (!gps.location.isValid()) {
    // Keep the GPS running in the background
   // while (Serial2.available() > 0) {
      gps.encode(Serial2.read());
    //}
    
    // If GPS data isn't valid after 60 seconds, log the failure
    if (millis() - startTime > 60000) {  // 60 seconds timeout
      logFailure("Unable to get a GPS fix within 60 seconds.");
      break;
    }
    
    delay(1000);  // Delay before the next check
  }

  if (gps.location.isValid()) {
    logToTelnet("GPS fix obtained.");
  } else {
    logFailure("Proceeding without valid GPS fix.");
  }
}


// Function to send GPS data to MQTT
void sendGPSData() {
  if (client.connected()) {
    if (gps.location.isValid()) {
      StaticJsonDocument<200> doc;
      doc["ID"] = busid;
      doc["lat"] = gps.location.lat();
      doc["lon"] = gps.location.lng();
      doc["ts"] = getISO8601Timestamp();
      doc["spd"] = gps.speed.kmph();  // Adding real-time speed of the bus
      doc["sat"] = gps.satellites.value();  // Adding number of satellites connected
      doc["HDOP"]=gps.hdop.value();

      String output;
      serializeJson(doc, output);
      String topic = "kctgps/" + String(busid);  // Ensure the correct topic format

      if (client.publish(topic.c_str(), output.c_str())) {
        logToTelnet("Published GPS data: " + output);
      } else {
        logFailure("Failed to publish GPS data via MQTT.");
      }
    } 
    else {
      logFailure("GPS data not valid, not sending.");
    }
  } 
  else {
    logFailure("MQTT not connected, cannot send GPS data.");
  }
}


// Function to send RFID data even if GPS is not yet available
void sendRFIDData(const String& rfid) {
  if (client.connected()) {
    StaticJsonDocument<200> doc;
    doc["ID"] = busid;
    doc["rfid"] = rfid;
    doc["ts"] = getISO8601Timestamp();

    // Fallback values if GPS data is not valid
    if (gps.location.isValid()) {
      doc["lat"] = gps.location.lat();
      doc["lon"] = gps.location.lng();
      doc["spd"] = gps.speed.kmph();  // Adding real-time speed of the bus
      doc["sat"] = gps.satellites.value();  // Adding number of satellites connected
    } else {
      doc["lat"] = 0.0;
      doc["lon"] = 0.0;
      doc["spd"] = 0.0;  // Default speed when GPS is unavailable
      doc["sat"] = 0;    // No satellites connected yet
    }

    String output;
    serializeJson(doc, output);
    String topic = "kctgps/" + String(busid) + "/rfid";  // Ensure the correct topic format

    if (client.publish(topic.c_str(), output.c_str())) {
      logToTelnet("Published RFID data: " + output);
    } else {
      logFailure("Failed to publish RFID data via MQTT.");
    }
  } else {
    logFailure("MQTT not connected, cannot send RFID data.");
  }
}



// Check RFID and send data to MQTT
void checkRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  currentRFID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    currentRFID += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    currentRFID += String(rfid.uid.uidByte[i], HEX);
    currentRFID.toUpperCase();
  }

  if (currentRFID==lastRFID) {
    digitalWrite(buz, HIGH);
   // digitalWrite(red, HIGH);
    delay(50);
    digitalWrite(buz, LOW);
    digitalWrite(buz, HIGH);
    delay(50);
    digitalWrite(buz, LOW);
    //digitalWrite(red, LOW);
      }
    else{
  if (currentRFID != lastRFID) {
    if (client.connected()) {
      StaticJsonDocument<200> doc;
      doc["ID"] = busid;
      doc["rfid"] = currentRFID;  // Send RFID value

      // Get timestamp (if available); if not, use "N/A"
      String timestamp = getISO8601Timestamp();
      if (timestamp == "N/A") {
        logFailure("Failed to obtain time from NTP, using 'N/A' as timestamp.");
      }
      doc["ts"] = timestamp;

      // If GPS location is valid, add real GPS values; otherwise, fallback to 0
      if (gps.location.isValid()) {
        doc["lat"] = gps.location.lat();
        doc["lon"] = gps.location.lng();
        doc["spd"] = gps.speed.kmph();  // Real-time speed of the bus
        doc["sat"] = gps.satellites.value();  // Number of satellites connected
      } else {
        doc["lat"] = 0.0;  // Fallback GPS values when not valid
        doc["lon"] = 0.0;
        doc["spd"] = 0.0;
        doc["sat"] = 0;
      }

      String output;
      serializeJson(doc, output);
      String topic = "kctgps/" + String(busid) + "/rfid";  // Ensure the correct topic format
    
    

      if (client.publish(topic.c_str(), output.c_str())) {
        logToTelnet("Published RFID data: " + output);
        lastRFID = currentRFID;  // Update the last sent RFID
        digitalWrite(buz, HIGH);
        digitalWrite(red1, HIGH);
        delay(500);
        digitalWrite(buz, LOW);
        digitalWrite(red1, LOW);
      } else {
        logFailure("Failed to publish RFID data via MQTT.");
      }
    } else {
      logFailure("MQTT not connected, storing RFID data in SPIFFS.");
      storeDataInSPIFFS(gps.location.isValid() ? gps.location.lat() : 0.0, 
                        gps.location.isValid() ? gps.location.lng() : 0.0, 
                        currentRFID);  // Store RFID with fallback GPS values
    }
  }
    }
  rfid.PICC_HaltA();
}



// OTA setup function
void setup_ota() {
  String hostname = String("ESP32-GPSTracker ") + busid;
 // ArduinoOTA.setHostname("ESP32-GPSTracker "+ busid);
 ArduinoOTA.setHostname(hostname.c_str());  // Convert to C-string using c_str()
  ArduinoOTA.setPassword("2024");
  
  ArduinoOTA.onStart([]() {
    String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
    logToTelnet("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    logToTelnet("\nOTA Update Complete");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    logToTelnet("Progress: " + String(progress / (total / 100)) + "%");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    String errorMsg = "OTA Error[" + String(error) + "]: ";
    if (error == OTA_AUTH_ERROR) errorMsg += "Auth Failed";
    else if (error == OTA_BEGIN_ERROR) errorMsg += "Begin Failed";
    else if (error == OTA_CONNECT_ERROR) errorMsg += "Connect Failed";
    else if (error == OTA_RECEIVE_ERROR) errorMsg += "Receive Failed";
    else if (error == OTA_END_ERROR) errorMsg += "End Failed";
    logToTelnet(errorMsg);
  });

  ArduinoOTA.begin();
}


// Main setup function
void setup() {
  Serial.begin(115200);
  Serial.println("1");
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); 
  Serial.println("2");
  setupSPIFFS();
  Serial.println("3");
  setup_wifi();
  Serial.println("4");
  setup_ota();
  Serial.println("5");

  client.setServer(mqtt_server, mqtt_port);
  Serial.println("6");
  
  pinMode(blue, OUTPUT);  
  pinMode(red1, OUTPUT);   
  pinMode(buz, OUTPUT); 
  digitalWrite(blue,LOW);
  digitalWrite(red1,LOW);

  SPI.begin();
  Serial.println("7");             
  rfid.PCD_Init(); 
  Serial.println("8");        

  startTelnet(); 
  Serial.println("9");          
  logStartup();


  Serial.println("System setup complete.");

  // Wait for a valid GPS fix before proceeding
  waitForValidGPSFix();

}

// Main loop function
void loop() {

  if (WiFi.status() != WL_CONNECTED) {
        logToTelnet("Wi-Fi connection lost, reconnecting...");
    setup_wifi();
  }

  if (!client.connected()) {
        logToTelnet("Attempting to reconnect to MQTT...");
    reconnect();
    
  }

  client.loop();  
  ArduinoOTA.handle();  
  handleTelnet();  

  // Continuously update GPS in the background
  //while (Serial2.available() > 0) {
    gps.encode(Serial2.read()); 
 // }



  // Check for and handle RFID scanning
  checkRFID();


  // Send GPS data every 5 seconds
  if (millis() - lastGPSSave > 2000) {
    lastGPSSave = millis();
    sendGPSData();
  }



  // Log system operational status if all systems are working
  if (WiFi.status() == WL_CONNECTED && client.connected() && gps.location.isValid()) {
    if (!systemOperational) {
      logToTelnet("System operational: Wi-Fi, MQTT, and GPS are all active.");
      //digitalWrite(blue, HIGH);
      systemOperational = true;
    }
  }
}