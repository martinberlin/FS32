typedef unsigned char byte;

template <class T> int EEPROM_writeAnything(int ee, const T& value)
{
    const byte* p = (const byte*)(const void*)&value;
    int i;
    for (i = 0; i < sizeof(value); i++)
    EEPROM.put(ee++, *p++);
    EEPROM.commit();
    return i;
}

template <class T> int EEPROM_readAnything(int ee, T& value)
{
    byte* p = (byte*)(void*)&value;
    int i;
    for (i = 0; i < sizeof(value); i++)
    *p++ = EEPROM.read(ee++);
    return i;
}

String getContentType(String filename) {
  if (server.hasArg("download")) {
    return "application/octet-stream";
  }
  if (filename.endsWith(".json")) {
    return "application/json";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } 
  return "text/plain";
}

/**
 * Generic message printer. Modify this if you want to send this messages elsewhere (Display)
 */
void printMessage(String message, bool newline = true, bool displayClear = false) {
  //u8g2.setDrawColor(1);
  if (displayClear) {
    // Clear buffer and reset cursor to first line
    u8g2.clearBuffer();
    u8cursor = u8newline;
  }
  if (newline) {
    u8cursor = u8cursor+u8newline;
    Serial.println(message);
  } else {
    Serial.print(message);
  }
  u8g2.setCursor(0, u8cursor);
  u8g2.print(message);
  u8g2.sendBuffer();
  u8cursor = u8cursor+u8newline;
  if (u8cursor > 60) {
    u8cursor = u8newline;
  }
  return;
}

void start_capture() {
  myCAM.clear_fifo_flag();
  myCAM.start_capture();
}

void serverStartTimelapse() {
    digitalWrite(ledStatusTimelapse, HIGH);
    printMessage("TIMELAPSE enabled");
    captureTimeLapse = true;
    lastTimeLapse = millis() + timelapseMillis;
    server.send(200, "text/html", "<div id='m'>Start Timelapse</div>"+ javascriptFadeMessage);
}

void serverStopTimelapse() {
    digitalWrite(ledStatusTimelapse, LOW);
    printMessage("TIMELAPSE disabled");
    captureTimeLapse = false;
    server.send(200, "text/html", "<div id='m'>Stop Timelapse</div>"+ javascriptFadeMessage);
}

void serverStopStream() {
    printMessage("STREAM stopped", true, true);
    server.send(200, "text/html", "<div id='m'>Streaming stoped</div>"+ javascriptFadeMessage);
}

/**
 * Update camera settings (Effects / Exposure only on OV5642)
 */
void serverCameraSettings() {
     String argument  = server.argName(0);
     String setValue = server.arg(0);
     if (argument == "effect") {
       cameraSetting = "effect";
       cameraSetValue= setValue.toInt();
     }
     if (argument == "exposure" && cameraModelId == 3) {
        cameraSetting = "exposure";
        cameraSetValue= setValue.toInt();
     }
     server.send(200, "text/html", "<div id='m'>"+argument+" updated to value "+setValue+"<br>See it on effect on next photo</div>"+ javascriptFadeMessage);
}

/**
 * Convert the IP to string so we can send the display
 */
String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") +\
  String(ipAddress[1]) + String(".") +\
  String(ipAddress[2]) + String(".") +\
  String(ipAddress[3])  ;
}
