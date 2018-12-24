typedef unsigned char byte;
// Return the minimum of two values a and b
#define minimum(a,b)     (((a) < (b)) ? (a) : (b))

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
    tft.fillScreen(TFT_BLACK); // THIS SPI Communication here breaks the JPEG
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


//####################################################################################################
// Draw a JPEG on the TFT, images will be cropped on the right/bottom sides if they do not fit
//####################################################################################################
// This function assumes xpos,ypos is a valid screen coordinate. For convenience images that do not
// fit totally on the screen are cropped to the nearest MCU size and may leave right/bottom borders.
void renderJPEG(int xpos, int ypos) {

  // retrieve infomration about the image
  uint16_t *pImg;
  uint16_t mcu_w = JpegDec.MCUWidth;
  uint16_t mcu_h = JpegDec.MCUHeight;
  uint32_t max_x = JpegDec.width;
  uint32_t max_y = JpegDec.height;

  // Jpeg images are draw as a set of image block (tiles) called Minimum Coding Units (MCUs)
  // Typically these MCUs are 16x16 pixel blocks
  // Determine the width and height of the right and bottom edge image blocks
  uint32_t min_w = minimum(mcu_w, max_x % mcu_w);
  uint32_t min_h = minimum(mcu_h, max_y % mcu_h);

  // save the current image block size
  uint32_t win_w = mcu_w;
  uint32_t win_h = mcu_h;

  // record the current time so we can measure how long it takes to draw an image
  uint32_t drawTime = millis();

  // save the coordinate of the right and bottom edges to assist image cropping
  // to the screen size
  max_x += xpos;
  max_y += ypos;

  // read each MCU block until there are no more
  while (JpegDec.readSwappedBytes()) {
	  
    // save a pointer to the image block
    pImg = JpegDec.pImage ;

    // calculate where the image block should be drawn on the screen
    int mcu_x = JpegDec.MCUx * mcu_w + xpos;  // Calculate coordinates of top left corner of current MCU
    int mcu_y = JpegDec.MCUy * mcu_h + ypos;

    // check if the image block size needs to be changed for the right edge
    if (mcu_x + mcu_w <= max_x) win_w = mcu_w;
    else win_w = min_w;

    // check if the image block size needs to be changed for the bottom edge
    if (mcu_y + mcu_h <= max_y) win_h = mcu_h;
    else win_h = min_h;

    // copy pixels into a contiguous block
    if (win_w != mcu_w)
    {
      uint16_t *cImg;
      int p = 0;
      cImg = pImg + win_w;
      for (int h = 1; h < win_h; h++)
      {
        p += mcu_w;
        for (int w = 0; w < win_w; w++)
        {
          *cImg = *(pImg + w + p);
          cImg++;
        }
      }
    }

    // draw image MCU block only if it will fit on the screen
    if (( mcu_x + win_w ) <= tft.width() && ( mcu_y + win_h ) <= tft.height())
    {
      tft.pushRect(mcu_x, mcu_y, win_w, win_h, pImg);
    }
    else if ( (mcu_y + win_h) >= tft.height()) JpegDec.abort(); // Image has run off bottom of screen so abort decoding
  }

  // calculate how long it took to draw the image
  drawTime = millis() - drawTime;

  // print the results to the serial port
  Serial.print(F(  "Total render time was    : ")); Serial.print(drawTime); Serial.println(F(" ms"));
  Serial.println(F(""));
}

//####################################################################################################
// Draw a JPEG on the TFT pulled from a program memory array
//####################################################################################################
void drawArrayJpeg(const uint8_t arrayname[], uint32_t array_size, int xpos, int ypos) {

  int x = xpos;
  int y = ypos;

  JpegDec.decodeArray(arrayname, array_size);
  
  //jpegInfo(); // Print information from the JPEG file (could comment this line out)
  
  renderJPEG(x, y);
}

