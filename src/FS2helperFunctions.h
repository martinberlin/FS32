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
  if (displayClear) {
    // Clear buffer and reset cursor to first line
    tft.fillScreen(TFT_BLACK);
    u8cursor = u8newline;
  }
  if (newline) {
    u8cursor = u8cursor+u8newline;
  }
  Serial.println(message);
  tft.setCursor(0, u8cursor);
  tft.print(message);
  
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

/**
 * Update camera settings (Effects / Exposure only on OV5642)
 */
void serverCameraSettings() {
  String argument = server.argName(0);
  String setValue = server.arg(0);
  cameraSetExposure = setValue.toInt();
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

void progressBar(long processed, long total, char *message) {
 int width = round( processed*128 / total );
 /* u8g2.drawBox(127, 1,  1, 4);  // end of upload
 u8g2.drawBox(0, 1, width, 4);
 u8g2.drawStr(0, 18, message);
 u8g2.sendBuffer(); */
}