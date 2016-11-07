
#define MAGICBYTE 85
#define RTCMEMBEGIN 68


typedef struct {
  byte markerFlag;
  int updateSpaces;
  int runSpaces;
  long lastSubscribers;
} 
rtcMemDef __attribute__((aligned(4)));

rtcMemDef rtcMem = {
  MAGICBYTE,
  9999,
  9999,
  0
};

void handleTelnet() {
  if (TelnetServer.hasClient()) {
    // client is connected
    if (!Telnet || !Telnet.connected()) {
      if (Telnet) Telnet.stop();         // client disconnected
      Telnet = TelnetServer.available(); // ready for new client
    } else {
      TelnetServer.available().stop();  // have client, block new conections
    }
  }
}

void printRTCmem() {
  DEBUGPRINTLN2("");
  DEBUGPRINTLN2("rtcMem ");
  DEBUGPRINT2("markerFlag ");
  DEBUGPRINTLN2(rtcMem.markerFlag);
  DEBUGPRINT2("runSpaces ");
  DEBUGPRINTLN2(rtcMem.runSpaces);
  DEBUGPRINT2("updateSpaces ");
  DEBUGPRINTLN2(rtcMem.updateSpaces);
  DEBUGPRINT2("lastSubscribers ");
  DEBUGPRINTLN2(rtcMem.lastSubscribers);
}


bool readRTCmem() {
  bool ret = true;
  system_rtc_mem_read(RTCMEMBEGIN, &rtcMem, sizeof(rtcMem));
  if (rtcMem.markerFlag != MAGICBYTE || rtcMem.lastSubscribers < 0 ) {
    rtcMem.markerFlag = MAGICBYTE;
    rtcMem.lastSubscribers = 0;
    rtcMem.updateSpaces = 0;
    rtcMem.runSpaces = 0;
    system_rtc_mem_write(RTCMEMBEGIN, &rtcMem, sizeof(rtcMem));
    ret = false;
  }
  printRTCmem();
  return ret;
}

void writeRTCmem() {
  rtcMem.markerFlag = MAGICBYTE;
  system_rtc_mem_write(RTCMEMBEGIN, &rtcMem, sizeof(rtcMem));
}

String getMACaddress() {
  uint8_t mac[6];
  char macStr[18] = {0};
  WiFi.macAddress(mac);
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],  mac[1], mac[2], mac[3], mac[4], mac[5]);
  return  String(macStr);
}


bool iotUpdater(String server, String url, String firmware, bool immediately, bool debugWiFi) {
  bool retValue = true;

  if (debugWiFi) {
    getMACaddress();
    DEBUGPRINT1("IP = ");
    DEBUGPRINTLN1(WiFi.localIP());
    DEBUGPRINT1("Update_server ");
    DEBUGPRINTLN1(server);
    DEBUGPRINT1("UPDATE_URL ");
    DEBUGPRINTLN1(url);
    DEBUGPRINT1("FIRMWARE_VERSION ");
    DEBUGPRINTLN1(firmware);
    DEBUGPRINTLN1("Updating...");
  }
  t_httpUpdate_return ret = ESPhttpUpdate.update(server, 80, url, firmware);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      retValue = false;
      if (debugWiFi) Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      DEBUGPRINTLN1();
      break;

    case HTTP_UPDATE_NO_UPDATES:
      if (debugWiFi) DEBUGPRINTLN1("HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      if (debugWiFi) DEBUGPRINTLN1("HTTP_UPDATE_OK");
      break;
  }
  return retValue;
}


