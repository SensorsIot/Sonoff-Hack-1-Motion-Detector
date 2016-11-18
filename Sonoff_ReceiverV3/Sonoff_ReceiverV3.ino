/* This sketch connects to the iopappstore and loads the assigned firmware down. The assignment is done on the server based on the MAC address of the board

    On the server, you need PHP script "iotappstore.php" and the bin files are in the .\bin folder

    This work is based on the ESPhttpUpdate examples

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

  Version 3.0

*/

#define VERSION "V3.4"
#define FIRMWARE "SonoffReceiver "VERSION

#include <credentials.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "RemoteDebug.h"        //https://github.com/JoaoLopesF/RemoteDebug

#define EEPROM_SIZE 1024

#define SWITCHpin 12
#define DELAYSEC 7*60   // 7 minutes
#define ON true
#define OFF false
#define GPIO0 0
#define RTCMEMBEGIN 68

#define REMOTEDEBUG

#define SERVICENAME   "SONOFF" // name of the MDNS service used in this group of ESPs

extern "C" {
#include "user_interface.h" // this is for the RTC memory read/write functions
}

WiFiServer server(80);
ESP8266WebServer webServer(80);

// remoteDebug
#ifdef REMOTEDEBUG
RemoteDebug Debug;
uint32_t mLastTime = 0;
uint32_t mTimeSeconds = 0;
#endif

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
  "CFG"
};


long delayCount = -1;
String ssid, password;
String constant1, constant2, constant3, constant4, constant5, constant6, IOTappStore1, IOTappStorePHP1, IOTappStore2, IOTappStorePHP2;

#include "ESPConfig.h"
#include "Sparkfun.h"

//-------------------------------------------------------------------

void setup() {
  char progMode;

  Serial.begin(115200);
  system_rtc_mem_read(RTCMEMBEGIN + 100, &progMode, 1);   // Read the "progMode" flag RTC memory to decide, if to go to config
  if (progMode == 'S') configESP();

  for (int i = 0; i < 5; i++) Serial.println("");
  Serial.println("Start "FIRMWARE);
  pinMode(GPIO0, INPUT_PULLUP);  // GPIO0 as input for Config mode selection
  pinMode(SWITCHpin, OUTPUT);
  // pinMode(3, OUTPUT);

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
  while (WiFi.status() != WL_CONNECTED) {
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
     MDNS.addService("SERVICENAME", "tcp", 8080);
  }

  server.begin();
  Serial.println("Server started");

  // Print the IP address
  Serial.print("Use this URL to connect: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");


#ifdef REMOTEDEBUG
  remoteDebugSetup();
  Debug.println(config.constant3);
#endif

  sendSparkfun(); // send boot statistics to sparkfun

  ESP.wdtEnable(WDTO_8S);
}

//----------------------------------------------------------------------------

void loop() {
  ESP.wdtFeed();

#ifdef REMOTEDEBUG
  Debug.handle();
#endif

  yield();
  if (digitalRead(GPIO0) == OFF) espRestart('S');
  if (WiFi.status() != WL_CONNECTED) espRestart('H');

  WiFiClient client = server.available();
  if (client) {
    Serial.println("new client");
    while (!client.available()) {
      delay(1);
    }

    // Read the first line of the request
    String request = client.readStringUntil('\r');
    Serial.print("Request ");
    Serial.println(request);
    client.flush();

    // Match the request

    if (request.indexOf("/SWITCH=ON") != -1) {
      delayCount = 0;    // last time an "ON" was received
      Serial.println("ON received ");
#ifdef REMOTEDEBUG
      if (Debug.ative(Debug.DEBUG)) Debug.println("ON received ");
#endif
      //   digitalWrite(3,ON);
    }
    if (request.indexOf("/SWITCH=OFF") != -1) {
      delayCount = -1;
      digitalWrite(SWITCHpin, OFF);
      Serial.println("OFF received ");
#ifdef REMOTEDEBUG
      if (Debug.ative(Debug.DEBUG)) Debug.println("OFF received ");
#endif
    }
    if (request.indexOf("/STATUS") != -1) {
      Serial.println("Status request ");
#ifdef REMOTEDEBUG
      if (Debug.ative(Debug.DEBUG)) Debug.println("STATUS request received ");
#endif
    }
    // Return the response
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println(""); //  do not forget this one
    client.println("<!DOCTYPE HTML>");
    client.println("<html>");
    //client.println("<br><br>");
    if (delayCount < 0) client.println("Off ");
    else {
      client.print("On ");
      client.print(delayCount / 10);
      client.print(" sec");
    }
    client.print(" Booard Name ");
    client.print(config.constant3);
    client.println("<br><br>");
    client.println("Click <a href=\"/SWITCH=ON\">here</a> turn the SWITCH on pin 12 ON<br>");
    client.println("Click <a href=\"/SWITCH=OFF\">here</a> turn the SWITCH on pin 12 OFF<br>");
    client.println("Click <a href=\"/STATUS\">here</a> get status<br>");
    client.println("</html>");
    delay(1);
    Serial.println("Client disonnected");
    Serial.println("");
  }

  if (delayCount >= 0) {
    if (delayCount > DELAYSEC * 10) {
      delayCount = -1;
      Serial.print("Switch Off ");
      digitalWrite(SWITCHpin, OFF);
    } else {
      digitalWrite(SWITCHpin, ON);
      delayCount++;     // count delayCount higher
      delay(100);
      //     digitalWrite(3,OFF);
    }
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
