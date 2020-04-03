/*
   Code by:         Florian Langer
   Last Updated:    07.09.2019
   Version:         1.3
   Features:        WiFi, OTA, SPIFFS, Webpage, Websockets, RGB-Control
   Planed features: , Animations

*/

//Include librarys
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <FastLED.h>
#include <FS.h>
#include <WebSocketsServer.h>
//Define Pins & LEDs
#define NUM_LEDS 36
#define DATA_PIN 4
#define SWITCH_PIN 15
#define POTI_PIN A0

//Init Classes
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
File fsUploadFile;
CRGB leds[NUM_LEDS];
CRGB ledsLast[NUM_LEDS];

//Define Variables
const char* ssid = "UPC1D9D7D7";
const char* password = "pr47htmjdcCc";

const char* OTAName = "DeskHue";
const char* OTAPassword = "esp8266";

byte colors[3] = {0, 0, 0};
unsigned long nextReading = 0;
bool lastButtonState = false;
int lastPotiReading = 0;


void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("\r\n");

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(POTI_PIN, INPUT);
  lastPotiReading = analogRead(POTI_PIN);

  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);

  startWiFi();
  startOTA();
  startSPIFFS();
  startWebSocket();
  startServer();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  webSocket.loop();
  checkHardwareInput();
}
/*
   Function: Connecting to WiFi
   ------------------------------
   Establishing a connection to the local network with the credentials stated above
*/
void startWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("\nConnecting to ");
  Serial.println(ssid);
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED ) {
    Serial.print(".");
    delay(500);
  }
  Serial.print("\nConnected succesfully to ");
  Serial.print(WiFi.SSID());
  Serial.print(" with IP Adress: ");
  Serial.print(WiFi.localIP());
  Serial.println("\n________________________________________\n");
}
/*
   Function: Start OTA Connection
   ------------------------------
   Setting all relevant options in order to serve OTA Updates
*/
void startOTA() {
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready\r\n");
}

//Start SPIFFS
void startSPIFFS() {
  SPIFFS.begin();
  Serial.println("SPIFFS started. Content:");
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {                      // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("\tFS File: %s, size: \r\n", fileName.c_str());
    }
    Serial.printf("\n");
  }
}

//Start WebSockets
void startWebSocket() {
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocketserver started!");
}

//Start WebServer
void startServer () {
  server.on("/edit.html", HTTP_POST, [] () {
    server.send(200, "text/plain", "");
  }, handleFileUpload);

  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/", HTTP_GET, handleRoot);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP Server started.");
}

void handleNotFound() {
  if (!handleFileRead(server.uri())) {
    server.send(404, "text/plain", "404: File on Server Not Found");
  }
}

bool handleFileRead(String path) {
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/"))
    path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);
  return false;
}

void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  String path;
  if (upload.status == UPLOAD_FILE_START) {
    path = upload.filename;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith(".gz")) {
      String pathWithGz = path + ".gz";
      if (SPIFFS.exists(pathWithGz))
        SPIFFS.remove(pathWithGz);
    }
    Serial.print("handleFileUpload Name: ");
    Serial.println(path);
    fsUploadFile = SPIFFS.open(path, "w");
    path = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
      Serial.print("handleFileUpload Size: ");
      Serial.println(upload.totalSize);
      server.sendHeader("Location", "/success.html");
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

void handleRoot() {
  handleFileRead("/index.html");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\n", num, payload);
      if (payload[0] == '#') {
        uint32_t rgb = (uint32_t) strtol((const char *) &payload[1], NULL, 16);
        for (int i = 0; i < 3; i++) {
          colors[i] = ((rgb >> (16 - i * 8)) & 0x3FF);
          Serial.println(colors[i]);
        }

        for (int dot = 0; dot < NUM_LEDS; dot++) {
          ledsLast[dot] = leds[dot];
          leds[dot] = CRGB( colors[0], colors[1], colors[2]);
        }
        FastLED.show();
        Serial.println("SHOW");
      }
      break;
  }
}

String getContentType(String filename) { // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void checkHardwareInput() {
  bool buttonState = digitalRead(SWITCH_PIN);
  bool curLedState = false;
  int margin = 10;
  int curPotiReading;
  if (buttonState != lastButtonState) {
    lastButtonState = buttonState;
    for (int j = 0; j < 3; j++) {
      if (leds[0][j] != 0) {
        curLedState = true;
        break;
      }
    }
    if (curLedState == true) {
      for (int dot = 0; dot < NUM_LEDS; dot++) {
        ledsLast[dot] = leds[dot];
        leds[dot] = CRGB(0, 0, 0);
      }
    }
    if (curLedState == false) {
      for (int dot = 0; dot < NUM_LEDS; dot++) {
        leds[dot] = ledsLast[dot];
      }
    }
    FastLED.show();
  }
  /*
  if (nextReading <= millis()) {
    curPotiReading = analogRead(POTI_PIN);
    if (curPotiReading > lastPotiReading + margin ||
     curPotiReading < lastPotiReading - margin) {
      for (int dot = 0; dot < NUM_LEDS; dot++) {
        for (int j = 0; j < 3; j++) {
          leds[dot][j] = map(curPotiReading, 0,1023,0,255); //leds[dot][j]
        }
      }
    }
    lastPotiReading = curPotiReading;
    nextReading = millis() + 100;
    FastLED.show();
  }*/
}
