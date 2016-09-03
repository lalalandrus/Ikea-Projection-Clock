//We always have to include the library
#include "LedControl.h"
#include "ledmatrixv2.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <TimeLib.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

// time_t getNtpTime()
void sendNTPpacket()
{
  WiFi.hostByName(NTP_SERVER_NAME, timeServerIP);  
  // while (udp.parsePacket() > 0) ; // discard any previously received packets
  for (int i = 0 ; i < 5 ; i++) { // 5 retries.
    if (udp.beginPacket(timeServerIP, 123)) {
      udp.write(SENDBUFFER, NTP_PACKET_SIZE);
      udp.endPacket();
    }
    uint32_t beginWait = millis();
  }
}


time_t getNtpTime()
{
  udp.begin(LOCAL_PORT);
  while (udp.parsePacket() > 0) yield(); // discard any previously received packets
  // Serial.println("Transmit NTP Request");
  sendNTPpacket();
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      // Serial.println("Receive NTP Response");
      udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      udp.stopAll();
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
    yield();
  }
  // Serial.println("No NTP Response :-(");
  udp.stopAll();
  return 0; // return 0 if unable to get the time
}

void setupDisplay() 
{
   /*
   The MAX72XX is in power-saving mode on startup,
   we have to do a wakeup call
   */
  lc.shutdown(0,false);
  /* Set the brightness to a medium values */
  lc.setIntensity(0,0x0F);
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

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID_SET, PASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart();
  }
  Serial.begin(115200);
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  setupOTA();
  ArduinoOTA.begin();
  // TelnetServer.begin();
  //  TelnetServer.setNoDelay(true);

  MDNS.addService("http","tcp",80);
  
  setupHTTPServer(server1);
  setSyncProvider(getNtpTime);
  setSyncInterval(5);

  while (timeStatus()==timeNotSet) 
  {
     setTime(getNtpTime());
     yield();
  }
  setDST();
  setSyncInterval(3600);

  
}

void updateTimeWords() 
{
  // Adjust 2.5 minutes = 150 seconds forward
  // So at 12:03 it already reads "five past 12"

  tmElements_t temp_time;

  breakTime(now()+150,temp_time);


  int disp_sec = temp_time.Second%2;
  int disp_min = temp_time.Minute/5;
  int disp_hrs = temp_time.Hour;

  if(disp_min >= MIN_OFFSET) 
    ++disp_hrs %= 12;
  else
    disp_hrs   %= 12;

  for (int i = 0; i < 8 ; i++)
  {
    lc.setColumn(0,i,(HOURS_WORDS[disp_hrs][i] | MINUTES_WORDS[disp_min][i] | BLINKY[disp_sec][i]));
  }
}
void updateTime() 
{
  // Adjust 2.5 minutes = 150 seconds forward
  // So at 12:03 it already reads "five past 12"
  static int crude_pwm = 0;

  crude_pwm = (crude_pwm + 1) % 10 ;
  for (int i = 0; i < 8 ; i++)
  {
    lc.setColumn(0,i,(HOURS[hour()%12][i] | MINUTES[minute()*28/60][i] | (crude_pwm == 0 ? RING[0][i] : RING[1][i])));
    yield();
  }
}

int dayofweek(int y, int m, int d)  /* 1 <= m <= 12,  y > 1752 (in the U.K.) */
  {
      static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
      y -= m < 3;
      return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7 + 1;
  }

int calcDateOffset (int dst_month, int dst_nth, int dst_day)
{
  int temp = (7 + dst_day - dayofweek(year(),dst_month,0))%7;

  int month_days[13];
  if ((year()%4) == 0)
    memcpy(month_days,LEAP_MONTH_DAYS, sizeof(month_days));
  else
    memcpy(month_days,MONTH_DAYS, sizeof(month_days));
  
  for ( int i=1; i < dst_nth; i++)
  {
     if (temp < (month_days[dst_month]-6))
      temp += 7;
  }

  return temp;
}

void setDST() 
{
  int ordinal[13];
  
  if ((year()%4) == 0)
    memcpy(ordinal, LEAP_ORDINAL, sizeof(ordinal));
  else
    memcpy(ordinal,ORDINAL, sizeof(ordinal));
    
  int start_day = ordinal[DST_START[0]] + calcDateOffset(DST_START[0], DST_START[1], DST_START[2]);
  int end_day = ordinal[DST_END[0]] + calcDateOffset(DST_END[0], DST_END[1], DST_END[2]);
  int curr_day = ordinal[month()] + day();

  if (curr_day >= start_day and curr_day < end_day)
    timeZone = BASE_TIMEZONE + 1;
  else
    timeZone = BASE_TIMEZONE;

  setTime(getNtpTime());
}

void setupHTTPServer(AsyncWebServer &httpd)
{

  // Simple Firmware Update Form
  httpd.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "change screen <cr> <form method='POST' action='/' enctype='text/plain'><input type='submit' value='Change'></form>");
  });
  httpd.on ( "/", HTTP_POST, [] ( AsyncWebServerRequest * request )
    { request->send(200, "text/html", "change screen <cr> <form method='POST' action='/' enctype='text/plain'><input type='submit' value='Change'></form>");
      display_words = !display_words ; } );
    
  
  httpd.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "version 1.0 <cr><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
  });
  httpd.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", !Update.hasError()?"OK":"FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
    if (!Update.hasError())
      ESP.restart();
        
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      Serial.printf("Update Start: %s\n", filename.c_str());
      Update.runAsync(true);
      if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
        Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("Update Success: %uB\n", index+len);
      } else {
        Update.printError(Serial);
      }
    }
  });

  httpd.begin() ;
}

void onRequest(AsyncWebServerRequest *request){
  //Handle Unknown Request
  request->send(404);
}

void loop() { 
  ArduinoOTA.handle();

  if (display_words==true && (hour() > 8 || hour() < 3 ))
    updateTimeWords();
  else
    updateTime();

    
  if ( hour() == 2 and minute() == 0 and second() == 0)
    setDST();

  yield();
    
}



