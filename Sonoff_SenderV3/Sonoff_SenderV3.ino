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


#define VERSION "V3.3"
#define SKETCH "SonoffSender "
#define FIRMWARE SKETCH VERSION

#define REMOTEDEBUG

#define MAXDEVICES 5

#define SERVICENAME "SONOFF"  // name of the MDNS service used in this group of ESPs


#include <credentials.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include "RemoteDebug.h"        //https://github.com/JoaoLopesF/RemoteDebug
#define EEPROM_SIZE 1024


extern "C" {
#include "user_interface.h" // this is for the RTC memory read/write functions
}

#define GPIO0 D3
#define LEDpin D7
#define ERRORpin D6
#define PIRpin D5

#define ON true
#define OFF false
#define RTCMEMBEGIN 68


ESP8266WebServer webServer(80);

// remoteDebug
#ifdef REMOTEDEBUG
RemoteDebug Debug;
uint32_t mLastTime = 0;
uint32_t mTimeSeconds = 0;
#endif

#define MAXRETRIES 50
#define DELAYPIR 10   // seconds till switch off

enum statusDef {
  LEDon,
  Renew,
  LEDoff
};

statusDef loopStatus;


typedef struct {
  char ssid[20];
  char password[20];
  byte  IP[4];
  byte  Netmask[4];
  byte  Gateway[4];
  boolean dhcp;
  char constant1[50];
  char constant2[50];
  char constant3[50];
  char constant4[50];
  char constant5[50];
  char constant6[50];
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
  "constant1",
  "constant2",
  "constant3",
  "constant4",
  "constant5",
  "constant6",
  "192.168.0.200",
  "/iotappstore/iotappstorev20.php",
  "iotappstore.org",
  "/iotappstore/iotappstorev20.php",
  "CFG"  // Magic Bytes
};

String ssid, password;
String constant1, constant2, constant3, constant4, constant5, constant6, IOTappStore1, IOTappStorePHP1, IOTappStore2, IOTappStorePHP2;
long onEntry;
IPAddress sonoffIP[MAXDEVICES];
String deviceName[MAXDEVICES];

String payload = "";
String switchString = "";


// declare telnet server
WiFiServer TelnetServer(23);
WiFiClient Telnet;

WiFiClient client;
HTTPClient http;


#include "ESPConfig.h"
#include "Sparkfun.h"


//-------------------------------------------------------------------

void setup() {
  char progMode;

  Serial.begin(115200);
  system_rtc_mem_read(RTCMEMBEGIN + 100, &progMode, 1);  // Read the "progMode" flag RTC memory to decide, if to go to config
  if (progMode == 'S') configESP();

  for (int i = 0; i < 5; i++) Serial.println("");
  Serial.println("Start "FIRMWARE);
  pinMode(GPIO0, INPUT_PULLUP);  // GPIO0 as input for Config mode selection

  pinMode(PIRpin, INPUT);
  pinMode(LEDpin, OUTPUT);
  pinMode(ERRORpin,OUTPUT);
  digitalWrite(ERRORpin,ON);   // inverse logic
  digitalWrite(LEDpin, ON);    // inverse logic

  // read and increase boot statistics (optional)
  readRTCmem();
  rtcMem.bootTimes++;
  writeRTCmem();
  printRTCmem();

  readConfig();  // configuration in EEPROM

  // connect to Wi-Fi
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

  // update from IOTappstory.com
  if (iotUpdater(config.IOTappStore1, config.IOTappStorePHP1, FIRMWARE, false, true) == 0) {
    iotUpdater(config.IOTappStore2, config.IOTappStorePHP2, FIRMWARE, true, true);
  }

  // Register host name in WiFi and mDNS
  String hostNameWifi = config.constant3;   // constant3 is device name
  hostNameWifi.concat(".local");
  WiFi.hostname(hostNameWifi);
  if (MDNS.begin(config.constant3)) {
    Serial.print("* MDNS responder started. http://");
    Serial.print(config.constant3);
    Serial.println(".local");
  }

#ifdef REMOTEDEBUG
  remoteDebugSetup();
  Debug.println(config.constant3);

#endif


  discovermDNSServices();
  for (int i = 0; i < 5; i++) Serial.println(sonoffIP[i]);

  switchSonoff(OFF);
  loopStatus = LEDoff;

  sendSparkfun();   // send boot statistics to sparkfun

  ESP.wdtEnable(WDTO_8S);
}


//--------------- LOOP ----------------------------------

void loop() {
  ESP.wdtFeed();    // feed the watchdog

#ifdef REMOTEDEBUG
  Debug.handle();
#endif

  yield();
  bool PIRstatus = digitalRead(PIRpin);
  digitalWrite(LEDpin, !PIRstatus);
  digitalWrite(ERRORpin, ON);

  if (digitalRead(GPIO0) == OFF) espRestart('S');  // go to setup if GPIO0 pressed

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
#ifdef REMOTEDEBUG
      if (Debug.ative(Debug.DEBUG)) Debug.println("OFF");
#endif
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

// ------------------------- END LOOP ----------------------------------


bool switchSonoff(bool command) {
  bool ret;

  if (WiFi.status() != WL_CONNECTED) espRestart('H');

  for (int i = 0; i < MAXDEVICES; i++) {
    payload = "";
    switchString = "";
    Serial.println(i);

    if (deviceName[i] == constant5 || deviceName[i] == constant6) {
      switchString = "http://" + sonoffIP[i].toString();

      if (command == ON) switchString = switchString + "/SWITCH=ON";
      else switchString = switchString + "/SWITCH=OFF";

      Serial.println(switchString);
      http.begin(switchString);

      //  Serial.print("[HTTP] GET...\n");
      // start connection and send HTTP header

      int httpCode = http.GET();
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);
      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        if (httpCode == HTTP_CODE_OK) payload = http.getString();
        else Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        yield();
        ret = true;
      } else {
        digitalWrite(LEDpin,ON); // off
        digitalWrite(ERRORpin, OFF);
        ret = false;
      }
      http.end();
    } else {
      digitalWrite(LEDpin,ON); // off
      digitalWrite(ERRORpin, OFF);
    }
    return ret;
  }
}

#ifdef REMOTEDEBUG
void remoteDebugSetup() {
  MDNS.addService("telnet", "tcp", 23);
  // Initialize the telnet server of RemoteDebug
  Debug.begin(config.constant3); // Initiaze the telnet server
  Debug.setResetCmdEnabled(true); // Enable the reset command
  // Debug.showProfiler(true); // To show profiler - time between messages of Debug
  // Good to "begin ...." and "end ...." messages
  // This sample (serial -> educattional use only, not need in production)

  // Debug.showTime(true); // To show time
}
#endif

void discovermDNSServices() {
  int j;
  for (j = 0; j < 5; j++) sonoffIP[j] = (0, 0, 0, 0);
  j = 0;
  Serial.println("Sending mDNS query");
  int n = MDNS.queryService("SERVICENAME", "tcp"); // Send out query for esp tcp services
  Serial.println("mDNS query done");
  if (n == 0) {
    Serial.println("no services found");
    espRestart('H');
  }
  else {
    Serial.print(n);
    Serial.println(" service(s) found");
    for (int i = 0; i < n; ++i) {
      // Print details for each service found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(MDNS.hostname(i));
      Serial.print(" (");
      Serial.print(MDNS.IP(i));
      deviceName[j] = MDNS.hostname(i);
      sonoffIP[j++] = MDNS.IP(i);
      Serial.print(":");
      Serial.print(MDNS.port(i));
      Serial.println(")");
    }
  }
  Serial.println();
}


