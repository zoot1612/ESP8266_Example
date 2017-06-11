
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Hash.h>
#include <FS.h>


#include <Adafruit_NeoPixel.h>
#define RGBPIN D2 //RGB
#define colorSaturation 255
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, RGBPIN, NEO_GRB + NEO_KHZ800);

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#define DHTPIN D4 //DHT
#define DHTTYPE DHT11  // DHT 11
DHT_Unified dht(DHTPIN, DHTTYPE);
uint32_t delayMS;

char charHum[10], charTemp[10];

unsigned long previousMillis = 0;        // will store last temp was read
const long interval = 30000;              // interval at which to read sensor
const long WebSocketsPort = 81;

const char* ssid     = "*******************";
const char* password = "*******************";
const char* mDNSid   = "esp8266";

ESP8266WebServer server(80);
//holds the current upload
File fsUploadFile;
WebSocketsServer webSocket = WebSocketsServer(WebSocketsPort);

//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //Serial.print("handleFileUpload Data: "); Serial.println(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileCreate: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  Serial.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  
  output += "]";
  server.send(200, "text/json", output);
}



void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

  switch(type) {
    case WStype_DISCONNECTED:
    Serial.printf("[%u] Disconnected!\n", num);
    break;
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      webSocket.sendTXT(num, "Connected");
      char THbuf[10], RBGbuf[11];
      uint32_t currentColour = pixels.getPixelColor(0);
      currentColour = (currentColour&0xff0000) | ((currentColour<<8)&0xff00) | ((currentColour>>8)&0xff);
      Serial.printf("%X\n",currentColour);
      sprintf(RBGbuf,"a%06X", currentColour);
      webSocket.sendTXT(num, RBGbuf);
      if (gettemperature() == 1) {
        sprintf(THbuf,"t%s,h%s", charTemp,charHum);
        webSocket.sendTXT(num,THbuf);
      }
    }
    break;
    case WStype_TEXT:
    Serial.printf("[%u] get Text: %s\n", num, payload);
    if(payload[0] == '#') {
      uint32_t rgb = (uint32_t) strtol((const char *) &payload[1], NULL, 16);
      pixels.setPixelColor(0, pixels.Color(((rgb >> 16) & 0xFF), ((rgb >> 0) & 0xFF), ((rgb >> 8) & 0xFF)));
      pixels.show();
      char RBGbuf[11];
      uint32_t currentColour = pixels.getPixelColor(0);
      currentColour = (currentColour&0xff0000) | ((currentColour<<8)&0xff00) | ((currentColour>>8)&0xff);
      Serial.printf("%X\n",currentColour);
      sprintf(RBGbuf,"a%06X", currentColour);
      webSocket.broadcastTXT(RBGbuf);
    }
  break;
  }
}

void setup() {
  
  dht.begin();
  Serial.begin(115200);
  delay(10);
  
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Print temperature sensor details.
  sensor_t sensor;
  // Set delay between sensor readings based on sensor details.
  delayMS = sensor.min_delay / 1000;

  SPIFFS.begin();
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {    
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
  
    if (!SPIFFS.exists ("/config.txt"))
    {
      
      Serial.println("file open failed: Not exists");
      Serial.println("====== Writing to SPIFFS file =========");
      File f = SPIFFS.open("/config.txt", "w");
      f.println("Password=None");
      f.println("ssid=NONE");
      f.close();
      Serial.println("====== Reading from the SPIFFS file: config.txt recently created =========");

      f = SPIFFS.open("/config.txt", "r");
      while (f.available())
      {
         String line = f.readStringUntil('\n');
         Serial.println(line);
      } 

      f.close();

    }
    else
    {
      Serial.println("file exists!!");
      File f = SPIFFS.open("/config.txt", "r");
      Serial.println("====== Reading from the SPIFFS file:config.txt existing =========");

      while (f.available())
      {
         char charBuf[50];
         char* command;
         String line = f.readStringUntil('\n');
         int str_len = line.length() + 1;
         line.toCharArray(charBuf, str_len);
         command = strtok(charBuf, "=");
         while (command != NULL)
         {
           Serial.printf ("%s\n",command);
           command = strtok (NULL, "=");
         }
         Serial.printf ("%s\n","------");
      } 

     f.close();
    }

  // start webSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  // Set up mDNS responder:
  if (!MDNS.begin(mDNSid)) {
    Serial.println("Error setting up MDNS responder!");
    while(1) { 
      delay(1000);
    }
  }
  
  Serial.println("mDNS responder started");
  Serial.print("Open http://");
  Serial.print(mDNSid);
  Serial.println(".local/edit to see the file browser");

  //SERVER INIT
  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on("/edit", HTTP_GET, [](){
    if(!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);

  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });

  server.begin();
  Serial.println("HTTP server started");
  
  // Print the IP address
  Serial.println(WiFi.localIP());

  // Add service to MDNS
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);

  // Initialize NeoPixel pixels
  pixels.begin();
  pixels.setPixelColor(0, 0, 0, 0);
  pixels.show();
}

void loop() {
  char THbuf[10];
  webSocket.loop();
  server.handleClient();
  unsigned long currentMillis = millis();
  if(currentMillis - previousMillis >= interval) {
    // save the last time you read the sensor 
    previousMillis = currentMillis;   
    if (gettemperature() == 1) {
      sprintf(THbuf,"t%s,h%s\n", charTemp,charHum);
      webSocket.broadcastTXT(THbuf);
    }
  }
}

int gettemperature() {
  float humidity, temp;  // Values read from sensor
  sensors_event_t event;  
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    Serial.println("Error reading temperature!");
   }
  else {
    temp = event.temperature; 
    dtostrf(temp, 5, 0, charTemp);
  }
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    Serial.println("Error reading humidity!");
  }
  else {
    humidity = event.relative_humidity;
    dtostrf(humidity, 5, 0, charHum);
  }
  if (isnan(humidity) || isnan(temp)) {
    Serial.println("Failed to read from DHT sensor!");
    return 0;
  }
  return 1;
}


