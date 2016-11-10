/* This sketch monitors a PIR sensor and transmitts an "ON" or "OFF" signal to a receiving device. Together with a
   Sonoff wireless switch, it can be used as a wireless motion detector. At startup, it connects to the IOTappstore to check for updates.

  Copyright (c) [2016] [Andreas Spiess]

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Version 1.0

*/


#define VERSION "V3.2"
#define SKETCH "SonoffSender "
#define FIRMWARE SKETCH VERSION




#include <credentials.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#define DEBUG 3
#include <DebugUtils.h>


extern "C" {
#include "user_interface.h" // this is for the RTC memory read/write functions
}

#define GPIO0 D3
#define LEDpin D8
#define PIRpin D5

#define ON true
#define OFF false

#define RTCMEMBEGIN 68

ESP8266WebServer webServer(80);


#define MAXRETRIES 50
#define DELAYPIR 10   // seconds till switch off

enum statusDef {
  LEDon,
  Renew,
  LEDoff
};

statusDef loopStatus;

// Tell it where to store your config data in EEPROM
#define CONFIG_START 0

typedef struct {
  char ssid[20];
  char password[20];
  byte  IP[4];
  byte  Netmask[4];
  byte  Gateway[4];
  boolean dhcp;
  char constant1[30];
  char constant2[30];
  char constant3[30];
  char constant4[30];
  char constant5[30];
  char constant6[30];
  char IOTappStore1[40];
  char IOTappStorePHP1[40];
  char IOTappStore2[40];
  char IOTappStorePHP2[40];
  char magicBytes[4];
} strConfig;

strConfig config = {
  mySSID,
  myPASSWORD,
  0, 0, 0, 0,
  255, 255, 255, 0,
  192, 168, 0, 1,
  true,
  "cred1",
  "cred2",
  "cred3",
  "cred4",
  "cred5",
  "cred6",
  "192.168.0.200",
  "/iotappstore/iotappstorev20.php",
  "iotappstore.org",
  "/iotappstore/iotappstorev20.php",
  "CFG"  // Magic Bytes
};

String ssid, password;
String constant1, constant2, constant3, constant4, constant5, constant6, IOTappStore1, IOTappStorePHP1, IOTappStore2, IOTappStorePHP2;
long onEntry;


// declare telnet server
WiFiServer TelnetServer(23);
WiFiClient Telnet;

WiFiClient client;
HTTPClient http;

void espRestart(char mmode) {
  while (digitalRead(GPIO0) == OFF) yield();    // wait till GPIOo released
  delay(500);
  Serial.print("Restart ");
  Serial.println(mmode);
  system_rtc_mem_write(RTCMEMBEGIN + 100, &mmode, 1);
  ESP.restart();
}

bool switchSonoff(bool command) {
  String payload = "";
  if (WiFi.status() != WL_CONNECTED) espRestart('H');

  //  Serial.print("[HTTP] begin...\n");
  // configure traged server and url
  String address5(config.constant5);
  String address6(config.constant6);
  if (command == ON) {
    Serial.print(millis() / 1000);
    Serial.print(" ");
    if (address5.length() > 0) {
      Serial.println("http://" + address5 + "/SWITCH=ON");
      http.begin("http://" + address5 + "/SWITCH=ON");
    }
    if (address6.length() > 0) {
      Serial.println("http://" + address6 + "/SWITCH=ON");
      http.begin("http://" + address6 + "/SWITCH=ON");
    }
  }
  else {
    if (address5.length() > 0) {
      Serial.println("http://" + address5 + "/SWITCH=OFF");
      http.begin("http://" + address5 + "/SWITCH=OFF");
    }
    if (address6.length() > 0) {
      Serial.println("http://" + address6 + "/SWITCH=OFF");
      http.begin("http://" + address6 + "/SWITCH=OFF");
    }
  }

  //  Serial.print("[HTTP] GET...\n");
  // start connection and send HTTP header
  bool ok = false;
  bool ret = false;

  while (ok == false) {
    int httpCode = http.GET();
    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      //      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        payload = http.getString();
        ok = true;
        ret = true;
        yield();
      }
    } else Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    Serial.println();
    yield();
  }
  http.end();
  return ret;
}

#include "SaveConfig.h"


//-------------------------------------------------------------------

void setup() {
  char progMode;

  Serial.begin(115200);
  for (int i = 0; i < 5; i++) Serial.println("");
  Serial.println("Start "FIRMWARE);

  system_rtc_mem_read(RTCMEMBEGIN + 100, &progMode, 1);
  Serial.print("progMode ");
  Serial.println(progMode);
  if (progMode == 'S') configESP();

  pinMode(PIRpin, INPUT);
  pinMode(GPIO0, INPUT_PULLUP);  // GPIO0 as input for Config mode selection
  pinMode(LEDpin, OUTPUT);
  digitalWrite(LEDpin, ON);    // inverse logic
  readConfig();
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  WiFi.begin(config.ssid, config.password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < MAXRETRIES) {
    delay(500);
    Serial.print(".");
    if (digitalRead(GPIO0) == OFF) espRestart('S');
    if (retries >= MAXRETRIES) espRestart('H');
    retries++;
  }
  Serial.println("");
  Serial.println("connected");
  if (iotUpdater(config.IOTappStore1, config.IOTappStorePHP1, FIRMWARE, false, true) == 0) {
    iotUpdater(config.IOTappStore2, config.IOTappStorePHP2, FIRMWARE, true, true);
  }
  switchSonoff(OFF);
  loopStatus = LEDoff;
  ESP.wdtEnable(WDTO_8S);
}

void loop() {
  ESP.wdtFeed();
  yield();
  bool PIRstatus = digitalRead(PIRpin);
  digitalWrite(LEDpin, !PIRstatus);
  if (digitalRead(GPIO0) == OFF) espRestart('S');

  switch (loopStatus) {
    case LEDon:
      // exit
      if (PIRstatus == OFF) {
        Serial.print("Status: ");
        Serial.println("OFF");
        loopStatus = LEDoff;
      }
      else if (abs(millis() - onEntry) > 10000) loopStatus = Renew;
      break;

    case Renew:
      while (!switchSonoff(ON));
      onEntry = millis();
      // exit
      loopStatus = LEDon;
      break;

    case LEDoff:

      // exit
      if (PIRstatus == ON) {
        Serial.print("Status: ");
        Serial.println("ON");
        while (!switchSonoff(ON));
        loopStatus = LEDon;
      }
      break;

    default:
      break;
  }
}
