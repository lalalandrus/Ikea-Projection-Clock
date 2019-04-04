//We always have to include the library
#include "LedControl.h"
#include "ledmatrixv22.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>

// Helps with connecting to internet
#include <WiFiManager.h>

// time_t getNtpTime()
void getNtpTime() {
  time_t now;
  
  int i = 0;
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);
  while((now = time(nullptr)) < NTP_MIN_VALID_EPOCH) {
    delay(500);
    i++;
    if (i > 60)
      break;
  }
}

void setupDisplay() 
{
   /*
   The MAX72XX is in power-saving mode on startup,
   we have to do a wakeup call
   */
  lc.shutdown(0,false);
  /* Set the brightness to a medium values */
  lc.setIntensity(0,brightness);
  /* and clear the display */
  lc.clearDisplay(0);
}

void setupOTA()
{
  ArduinoOTA.setHostname(ota_hostname);
  ArduinoOTA.onStart([]() { });
  ArduinoOTA.onEnd([]() {});
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});
  ArduinoOTA.onError([](ota_error_t error) {});
}

void setup() 
{
  setupDisplay();

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  // Uncomment for testing wifi manager
  //wifiManager.resetSettings();

  //or use this for auto generated name ESP + ChipID

  String hostname(ota_hostname);
  
  hostname = hostname + "-" + String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.autoConnect();

  Serial.begin(115200);
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  setupOTA();
  ArduinoOTA.begin();
  // TelnetServer.begin();
  //  TelnetServer.setNoDelay(true);

  MDNS.addService("http","tcp",80);
  
  setupHTTPServer();
  getNtpTime();
}

void updateTimeWords() 
{
  // Adjust 2.5 minutes = 150 seconds forward
  // So at 12:03 it already reads "five past 12"
  char *dstAbbrev;
  time_t now = dstAdjusted.time(&dstAbbrev);
  struct tm * timeinfo = localtime (&now);
  
  int disp_sec = timeinfo->tm_sec%2;
  int disp_min = timeinfo->tm_min/5;
  int disp_hrs = timeinfo->tm_hour;

  if(disp_min >= MIN_OFFSET) 
    ++disp_hrs %= 12;
  else
    disp_hrs   %= 12;

  for (int i = 0; i < 8 ; i++)
  {
    lc.setColumn(0,7-i,(HOURS_WORDS[disp_hrs][i] | MINUTES_WORDS[disp_min][i] | BLINKY[disp_sec][i]));
  }
}

void updateTimev2() 
{
  // Adjust 2.5 minutes = 150 seconds forward
  // So at 12:03 it already reads "five past 12"
  char *dstAbbrev;
  time_t now = dstAdjusted.time(&dstAbbrev);
  struct tm * timeinfo = localtime (&now);
  
  int disp_sec = timeinfo->tm_sec;
  int disp_min = timeinfo->tm_min;
  int disp_hrs = timeinfo->tm_hour;
  
  static int crude_ani = 0;
  static unsigned long timeout = 0;
  if (timeout < millis())
  {
    timeout = millis() + 1000/(28-disp_min*28/60);
    if (crude_ani>=(28- disp_min*28/60))
    {
      timeout += 1000;
      crude_ani = 0;
    }
    else
      crude_ani++;
  }
    
  for (int i = 0; i < 8 ; i++)
  {
    lc.setColumn(0,7-i,(HOURS[disp_hrs%12][i] | MINUTES[disp_min*28/60][i] | ANIMATION[28-crude_ani][i]));
   // lc.setColumn(0,7-i,(HOURS[hour()%12][i] | MINUTES[minute()*28/60][i] | (crude_ani%2 == 0 ? RING[0][i] : ANIMATION[28-crude_ani/2][i])));
    yield();
  }
}



const char webpage[] = "change screen <cr>" 
"<form method='POST' action='/'><input type='hidden' name='change' value='1'><input type='submit' value='Change'></form>"
"<form method='POST' action='/'><input type='range' value='8' min='0' max='16' name='Brightness'><input type='submit' value='Send'></form>";

void setupHTTPServer()
{

  // Simple Firmware Update Form
  server.on("/", HTTP_GET, [](){
    server.send(200, "text/html", webpage);
  });
  server.on ( "/", HTTP_POST, [] ()
    { if (server.hasArg("change"))
         display_words = !display_words;
      if (server.hasArg("Brightness"))
      {
         lc.setIntensity(0,server.arg("Brightness").toInt());
      } 
    server.send(200, "text/html", webpage);} );
    
  server.on("/update", HTTP_GET, [](){
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
    });
  server.on("/update", HTTP_POST, [](){
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
      ESP.restart();
    },[](){
      HTTPUpload& upload = server.upload();
      if(upload.status == UPLOAD_FILE_START){
        Serial.setDebugOutput(true);
        WiFiUDP::stopAll();
        Serial.printf("Update: %s\n", upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if(!Update.begin(maxSketchSpace)){//start with max available size
          Update.printError(Serial);
        }
      } else if(upload.status == UPLOAD_FILE_WRITE){
        if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
          Update.printError(Serial);
        }
      } else if(upload.status == UPLOAD_FILE_END){
        if(Update.end(true)){ //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
      yield();
    });

  server.begin() ;
}

void loop() { 
  ArduinoOTA.handle();

  if (display_words)
    updateTimeWords();
  else
    updateTimev2();

  if (millis() - lastDownloadUpdate > REFRESH_RATE) {
    getNtpTime();
    lastDownloadUpdate = millis();
  }

  server.handleClient();    
}



