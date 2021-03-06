//  ______ _____ ___                                https://github.com/martinberlin/FS32 
// |  ____/ ____|__ \ 
// | |__ | (___    ) |
// |  __| \___ \  / / 
// | |    ____) |/ /_ 
// |_|   |_____/3____|     WiFi instant Camera ESP32 version
          
// PINS     Please check in your board datasheet or comment the
//          Serial.print after  setup()  to use the right GPIOs
// Definition for the ESP-32 Heltec:
// CS   17  Camera CS. Check for conflicts with any other SPI (OLED, etc)
// MOSI 23
// MISO 19
// SCK  18
// SDA  21
// SCL  22
// SHU  04 Shutter button : Update it to whenever thin GPIO connects to GND to take a picture
// LED  12 ledStatus
// OLED Display /* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16
#include <Arduino.h>
#include "FS.h"
#include "SPIFFS.h"
#include <EEPROM.h>
#include <WiFiManager.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <ArduCAM.h>
#include <Wire.h>
#include <SPI.h>
#include <OneButton.h>
#include <ArduinoJson.h>    // Any version > 5.13.3 gave me an error on swap function
#include <WebServer.h>
#include <U8g2lib.h>        // OLED display I2C Settings are for Heltec board, change it to suit yours:
#include <MD5Builder.h>
MD5Builder _md5;
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);

// CAMERA CONFIGURATION
// camera_mosfet now moved to WM parameters please set it up on /data/config.json
// cameraMosfetReady on true will make exposition control work rarely since does not leave enough wake up time to the camera
const byte gpioCameraVcc = 5;                  // GPIO on HIGH will turn camera on only in the moment of taking the picture (energy saving)
// NOTE: Don't use Heltec VEXT but an external MOSFET with gate connected to gpioCameraVcc (VEXT supports only 50mA while camera will take up to 200mA)
byte  CS = 17;                                 // set GPIO17 as the slave select
bool saveInSpiffs = false;                     // Whether to save the jpg also in SPIFFS
// CONFIGURATION. NOTE! saveInSpiffs true makes everything slower in ESP32

// AP to Setup WiFi & Camera settings
const char* configModeAP = "CAM-autoconnect";  // Default config mode Access point
const char* localDomain  = "cam";              // mDNS: cam.local

#include "memorysaver.h"    // Uncomment the #define OV5642_MINI_5MP_PLUS
// NOTE:     ArduCAM owners please also make sure to choose your camera module in the ../libraries/ArduCAM/memorysaver.h
// INTERNAL GLOBALS

// Flag for saving data in config.json
bool shouldSaveConfig = false;

// Outputs / Inputs (Shutter button)
OneButton buttonShutter(4, true, false);
const int ledStatus = 12;

// Makes a div id="m" containing response message to dissapear after 6 seconds
String javascriptFadeMessage = "<script>setTimeout(function(){document.getElementById('m').innerHTML='';},6000);</script>";
String message;

// Note if saving to SPIFFS bufferSize needs to be 256, otherwise won't save correctly
static const size_t bufferSize = 1024;
static uint8_t buffer[bufferSize] = {0xFF};

// UPLOAD Settings
String start_request = "";
String boundary = "_cam_";
String end_request = "\n--"+boundary+"--\n";

uint8_t temp = 0, temp_last = 0;
int i = 0;
bool is_header = false;

// Fixed to a OV5642
ArduCAM myCAM(3, CS);
WiFiManager wm;
WiFiClient client;
WebServer server(80);

// jpeg_size_id Setting to set the camara Width/Height resolution. Smallest is default if no string match is done by config
uint8_t jpeg_size_id = 1;

// Definition for WiFi defaults
char timelapse[4] = "60";
char upload_host[120] = "api.slosarek.eu";
char upload_path[240] = "/your/upload.php";
char slave_cam_ip[16] = "";
char jpeg_size[10]    = "1600x1200";
char camera_mosfet[2] = "0";

// SPIFFS and memory to save photos
File fsFile;
String webTemplate = "";
bool onlineMode = true;
struct config_t
{
    byte photoCount = 1;
    bool resetWifiSettings;
    bool editSetup;
} memory;
byte u8cursor = 1;
byte u8newline = 5;
String cameraSetting;
byte   cameraSetExposure;
#include "FS2helperFunctions.h"  // Helper methods: printMessage + EPROM
#include "serverFileManager.h"   // Responds to the FS Routes
// ROUTING Definitions
void defineServerRouting() {
    server.on("/capture", HTTP_GET, serverCapture);
    server.on("/stream", HTTP_GET, serverStream);
    server.on("/stream/stop", HTTP_GET, serverStopStream);
    server.on("/fs/list", HTTP_GET, serverListFiles);           // FS
    server.on("/fs/download", HTTP_GET, serverDownloadFile);    // FS
    server.on("/fs/delete", HTTP_GET, serverDeleteFile);        // FS
    server.on("/wifi/reset", HTTP_GET, serverResetWifiSettings);
    server.on("/camera/settings", HTTP_GET, serverCameraParams);
    server.on("/set", HTTP_GET, serverCameraSettings);
    server.on("/deepsleep", HTTP_GET, serverDeepSleep);
    server.onNotFound(handleWebServerRoot);
    server.begin();
}
String camHash;
// Default image (LOGO?)
static unsigned char image[] U8X8_PROGMEM  = {
  0x00, 0xC0, 0xFF, 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0xFE, 0xFF, 0xFF, 0x17, 
  0xFC, 0xFF, 0xFF, 0xFF, 0x00, 0xE0, 0xFF, 0xFF, 0xFF, 0x3F, 0x00, 0x00, 
  0xFF, 0xFF, 0xFF, 0x14, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xF0, 0xFF, 0xFF, 
  0xFF, 0x7F, 0x00, 0x00, 0xFC, 0xFF, 0xFF, 0x14, 0xFD, 0xFF, 0xFF, 0xFF, 
  0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x8F, 0xFF, 0x10, 
  0x7E, 0xFF, 0xFF, 0xFF, 0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 
  0xFE, 0x8F, 0xFF, 0x90, 0x7F, 0xFF, 0xFF, 0xFF, 0x00, 0xFE, 0xFF, 0xFF, 
  0xFF, 0xFF, 0x03, 0x00, 0xFC, 0x0F, 0xFF, 0x90, 0xFF, 0xEE, 0xFF, 0xFF, 
  0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0xFF, 0x0F, 0xFF, 0x90, 
  0xFF, 0xCC, 0xFF, 0xFF, 0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x00, 
  0xFF, 0x0F, 0xFF, 0x92, 0xFF, 0xC0, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0x1F, 0x00, 0xFF, 0x1F, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 
  0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0x00, 0xFF, 0xFF, 0xFF, 0xFE, 
  0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 
  0xFC, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0x7F, 0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFE, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0x3F, 0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 
  0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0x00, 0xFC, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFE, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0x00, 
  0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0x3F, 0x00, 0xFC, 0xC7, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 
  0x00, 0xFF, 0xF7, 0xFF, 0xFF, 0xFF, 0x3F, 0x00, 0xFE, 0xE7, 0xFF, 0xFF, 
  0xFF, 0xFE, 0xFF, 0xFF, 0x00, 0xFF, 0xF7, 0xFF, 0xFF, 0xFF, 0x3F, 0x00, 
  0xFC, 0xE7, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0x00, 0xFF, 0xF7, 0xFF, 
  0xFF, 0xFF, 0x3F, 0x00, 0xFC, 0xE7, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 
  0x00, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0x1F, 0x00, 0xFE, 0xC7, 0xFF, 0xFF, 
  0xFF, 0xFE, 0xFF, 0xFF, 0x00, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0x00, 
  0xFE, 0xCF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0x00, 0x03, 0x07, 0xFF, 
  0xFF, 0xFF, 0x0F, 0x00, 0xFC, 0xEF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 
  0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x1F, 0x00, 0xFE, 0xEF, 0xFF, 0xFF, 
  0xFF, 0xFE, 0xFF, 0xFF, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x1F, 0x00, 
  0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xA6, 0xFF, 
  0xFF, 0xFF, 0x1F, 0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0x00, 0x06, 0xEE, 0xFF, 0xFF, 0x7F, 0x3F, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x0E, 0xEF, 0xFF, 0xFF, 0xFF, 0x1E, 0x80, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFE, 0xFF, 0xFF, 
  0xFF, 0xFF, 0x5E, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF, 
  0x00, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFD, 0xFF, 0xFF, 0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F, 0xFD, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF, 0x00, 0xFC, 0xFF, 0xFF, 
  0xFF, 0xFF, 0x5F, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF, 
  0x00, 0xF9, 0xFF, 0xFF, 0x7F, 0x80, 0xC6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFA, 0xFF, 0xFF, 0x7F, 0x80, 0xFF, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xF0, 0xFF, 0xFF, 
  0x6F, 0x80, 0xD1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0x00, 0xF0, 0xFF, 0xFF, 0x2F, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xF1, 0xFF, 0xFF, 0x27, 0x00, 0xFF, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xF1, 0xFF, 0xFF, 
  0x23, 0x00, 0xE0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0x00, 0xF2, 0xFF, 0xFF, 0x31, 0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFD, 0xFF, 0xFF, 0x00, 0xF2, 0xFF, 0xFF, 0x10, 0x00, 0x74, 0xF8, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF, 0x00, 0xF6, 0x7F, 0xFE, 
  0x10, 0x00, 0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0xFF, 0xFF, 
  0x00, 0xF6, 0x3F, 0x00, 0x10, 0x00, 0xF4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xB4, 0x0F, 0x00, 0x18, 0x00, 0xF0, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xB4, 0x07, 0x00, 
  0x08, 0x00, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0x00, 0xBC, 0x03, 0x00, 0x08, 0x00, 0xFD, 0xF3, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFB, 0xFF, 0xFF, 0x00, 0x38, 0x01, 0x00, 0x08, 0x00, 0xF9, 0xF9, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xFF, 0xFF, 0x00, 0x3C, 0x00, 0x00, 
  0x0C, 0x90, 0xFF, 0xF9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xFF, 0xFF, 
  0x00, 0x3C, 0x00, 0x00, 0x04, 0x00, 0xE3, 0x78, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFB, 0xFF, 0xFF, 0x00, 0x38, 0x00, 0x00, 0x06, 0x00, 0xC3, 0x31, 
  0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xFF, 0xFF, 0x00, 0x3C, 0x00, 0x00, 
  0x06, 0x80, 0xE3, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xFF, 0xFF, 
  0x7F, 0x1C, 0x00, 0x00, 0x03, 0x80, 0x60, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFB, 0xFF, 0xFF, 0xFF, 0x19, 0x00, 0x80, 0x01, 0x08, 0xF0, 0x00, 
  0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xFF, 0xFF, 0xFF, 0x1F, 0x00, 0xC0, 
  0x00, 0x08, 0xF0, 0x83, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0xFF, 0xFF, 
  0xFF, 0x1F, 0x00, 0xC0, 0x00, 0x28, 0xF8, 0xE3, 0xFF, 0xF7, 0xFF, 0xFF, 
  0xFF, 0xFB, 0xFF, 0xFF, 0xFF, 0x1F, 0x00, 0x60, 0x00, 0x08, 0xFC, 0xE6, 
  0xFF, 0xCD, 0xFF, 0xFF, 0xFF, 0xFB, 0xFF, 0xFF, 0xFF, 0x1B, 0x00, 0x30, 
  0x00, 0x04, 0x3C, 0xF8, 0xFF, 0xCF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0x79, 0x40, 0x18, 0x00, 0x1C, 0x9E, 0xFC, 0xFF, 0x03, 0xFF, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1C, 0x00, 0x0C, 0x00, 0x0C, 0x4E, 0xFE, 
  0xFF, 0x33, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x3C, 0x00, 0x06, 
  0x00, 0x0C, 0x77, 0xFF, 0xFF, 0x33, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0x7F, 0x7F, 0x00, 0x23, 0x00, 0xCE, 0xFF, 0xFF, 0xFF, 0x16, 0xF8, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x85, 0x01, 0x00, 0x1E, 0xFE, 0xFF, 
  0x7F, 0x10, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC7, 0x01, 
  0x80, 0x0F, 0xFF, 0xFF, 0x3F, 0x10, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0xFF, 0xFF, 0x67, 0x00, 0xD8, 0x1F, 0xFF, 0xFF, 0x7F, 0x10, 0xFE, 0xFF, 
  0xFF, 0xFF, 0xFF, 0xFF};

void setup() {
  u8g2.begin();
  u8g2.setCursor(0, u8cursor);
  u8g2.setFont(u8g2_font_pcsenior_8r);
  u8g2.setDisplayRotation(U8G2_R2); // U8G2_R0 No rotation, landscape
  cameraSetExposure = 5; // Default exposure
  EEPROM.begin(12);
  Serial.begin(115200);
  // Find out what are this PINS on ESP32 
  //Serial.print("MOSI:");Serial.println(MOSI);
  //Serial.print("MISO:");Serial.println(MISO);
  //Serial.print("SCK:");Serial.println(SCK);
  //Serial.print("SDA:");Serial.println(SDA);

  // Define outputs
  pinMode(CS, OUTPUT);
  pinMode(gpioCameraVcc, OUTPUT);
  pinMode(ledStatus, OUTPUT);
  digitalWrite(gpioCameraVcc, LOW); // Turn camera ON
  // Read memory struct from EEPROM
  EEPROM_readAnything(0, memory);

  // Read configuration from FS json
  if (SPIFFS.begin()) {
   if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", FILE_READ);
      if (configFile) {
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        //json.printTo(Serial);
        if (json.success()) {          
          strcpy(timelapse, json["timelapse"]);
          strcpy(upload_host, json["upload_host"]);
          strcpy(upload_path, json["upload_path"]);
          strcpy(slave_cam_ip, json["slave_cam_ip"]);
          strcpy(jpeg_size, json["jpeg_size"]);
          strcpy(camera_mosfet, json["camera_mosfet"]);
        } else {
          printMessage("ERR load config");
        }
        configFile.close();
      }
    }
  } else {
    printMessage("ERR FS failed");
    printMessage("FFS Formatted?");
  }
  // WiFI Manager menu options
  std::vector<const char *> menu = {"wifi", "sep", "param","sep", "info", "ota"};
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_html("<h2>Camera configuration</h2><p>After saving camera will restart and try to login to last WiFi connection</p>"); 
  WiFiManagerParameter param_timelapse("timelapse", "Timelapse in secs", timelapse,4);
  WiFiManagerParameter param_slave_cam_ip("slave_cam_ip", "Slave cam ip/ping", slave_cam_ip,16);
  WiFiManagerParameter param_upload_host("upload_host", "API host for upload", upload_host,120);
  WiFiManagerParameter param_upload_path("upload_path", "Path to API endoint", upload_path,240);
  WiFiManagerParameter param_camera_mosfet("camera_mosfet", "Camera on only when taking photo (1)", camera_mosfet, 1);
  WiFiManagerParameter param_jpeg_size("jpeg_size", "Select JPG Size: 640x480 1024x768 1280x1024 1600x1200 (2 & 5mp) / 2048x1536 2592x1944 (only 5mp)", jpeg_size, 10);
 
 //if (onlineMode) {
  //printMessage("> Camera is on ONLINE Mode");
  // This is triggered on next restart after click in RESET WIFI AND EDIT CONFIGURATION
  /* if (memory.resetWifiSettings) {
    wm.resetSettings();
  } */
  wm.setMenu(menu);
  // Add the defined parameters to wm
  wm.addParameter(&custom_html);
  wm.addParameter(&param_timelapse);
  wm.addParameter(&param_slave_cam_ip);
  wm.addParameter(&param_upload_host);
  wm.addParameter(&param_upload_path);
  wm.addParameter(&param_jpeg_size);
  wm.addParameter(&param_camera_mosfet);
  wm.setMinimumSignalQuality(20);
  // Callbacks configuration
  wm.setSaveParamsCallback(saveParamCallback);
  wm.setBreakAfterConfig(true); // Without this saveConfigCallback does not get fired
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setAPCallback(configModeCallback);
  wm.setDebugOutput(false);
  // If saveParamCallback is called then on next restart trigger config portal to update camera params
  if (memory.editSetup) {
    // Let's do this just one time: Restarting again should connect to previous WiFi
    memory.editSetup = false;
    EEPROM_writeAnything(0, memory);
    wm.startConfigPortal(configModeAP);
  } else {
    wm.autoConnect(configModeAP);
  }
 /* } else {
   printMessage("OFFLINE Mode");
 } */

  // Read updated parameters
  strcpy(timelapse, param_timelapse.getValue());
  strcpy(slave_cam_ip, param_slave_cam_ip.getValue());
  strcpy(upload_host, param_upload_host.getValue());
  strcpy(upload_path, param_upload_path.getValue());
  strcpy(jpeg_size, param_jpeg_size.getValue());
  strcpy(camera_mosfet, param_camera_mosfet.getValue());
  if (shouldSaveConfig) {
    printMessage("Save config.json", true, true);
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["timelapse"] = timelapse;
    json["slave_cam_ip"] = slave_cam_ip;
    json["upload_host"] = upload_host;
    json["upload_path"] = upload_path;
    json["jpeg_size"] = jpeg_size;
    json["camera_mosfet"] = camera_mosfet;
    Serial.println("timelapse:"+String(timelapse));
    Serial.println("slave_cam_ip:"+String(slave_cam_ip));
    Serial.println("upload_host:"+String(upload_host));
    Serial.println("upload_path:"+String(upload_path));
    Serial.println("jpeg_size:"+String(jpeg_size));
    Serial.println("camera_mosfet:"+String(camera_mosfet));
    File configFile = SPIFFS.open("/config.json", FILE_WRITE);
    if (!configFile) {
      printMessage("ERR config file");
    }
    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }

  // Button events
  buttonShutter.attachClick(shutterReleased);
  
  uint8_t vid, pid;
  uint8_t temp;
  // myCAM.write_reg uses Wire for I2C communication
  Wire.begin();
  SPI.begin();
  SPI.setFrequency(4000000); //4MHz

  if (String(jpeg_size) == "640x480") {
   jpeg_size_id = 1;
  }
  if (String(jpeg_size) == "1024x768") {
   jpeg_size_id = 2;
  }
  if (String(jpeg_size) == "1280x1024") {
   jpeg_size_id = 3;
  }
  if (String(jpeg_size) == "1600x1200") {
   jpeg_size_id = 4;
  } 
  if (String(jpeg_size) == "2048x1536") {
   jpeg_size_id = 5;
  } 
  if (String(jpeg_size) == "2592x1944") {
   jpeg_size_id = 6;
  }
  if (strcmp(camera_mosfet,"0")==0) {
    Serial.println(">>>camera_mosfet is 0: starting OV5642 on setup()");
    //Check if the ArduCAM SPI bus is OK
    myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
    temp = myCAM.read_reg(ARDUCHIP_TEST1);
    if (temp != 0x55) {
      printMessage("ERR SPI: Check");
      printMessage("ArduCam wiring");
      delay(10000);
      serverDeepSleep();
    }

    SPI.transfer(0x00);
    myCAM.clear_bit(6, GPIO_PWDN_MASK); //disable low power
    //Check if the camera module type is OV5642
    myCAM.wrSensorReg16_8(0xff, 0x01);
    myCAM.rdSensorReg16_8(12298, &vid);
    myCAM.rdSensorReg16_8(12299, &pid);

    if((vid != 0x56) || (pid != 0x42)) {
      printMessage("ERR conn OV5642");
      delay(10000);
      serverDeepSleep();
    } else {
      myCAM.set_format(JPEG);
      myCAM.InitCAM();
      // ARDUCHIP_TIM, VSYNC_LEVEL_MASK
      myCAM.write_reg(3, 2);   //VSYNC is active HIGH 
    }

  } else {
    digitalWrite(gpioCameraVcc, HIGH); // Turn off camera
  }
     printMessage("FS2 CAMERA READY", true, true);
     u8cursor = u8cursor+u8newline;
     printMessage("Res: "+ String(jpeg_size), true);
     printMessage(IpAddress2String(WiFi.localIP()));

  // Set up mDNS responder:
  if (onlineMode) {  //TODO Check if this onlineMode thing makes sense
    if (!MDNS.begin(localDomain)) {
      while(1) { 
        delay(500);
      }
    }
    MDNS.addService("http", "tcp", 80);
    
    // ROUTING
    defineServerRouting();
    server.onNotFound(handleWebServerRoot);
    server.begin();
  }

  }

String camCapture(ArduCAM myCAM) {
   // Check if available bytes in SPIFFS
  uint32_t bytesAvailableSpiffs = SPIFFS.totalBytes()-SPIFFS.usedBytes();
  uint32_t len  = myCAM.read_fifo_length();

  if (len*2 > bytesAvailableSpiffs && saveInSpiffs) {
    memory.photoCount = 1;
    printMessage("Count reset 1");
  }
  long full_length;
  
  if (len == 0) {
    message = "ERR read memory";
    printMessage(message);
    return message;
  }

  myCAM.CS_LOW();
  myCAM.set_fifo_burst();
  if (client.connect(upload_host, 80) || onlineMode) { 
    if (onlineMode) {
      while(client.available()) {
        String line = client.readStringUntil('\r');
       }  // Empty wifi receive bufffer
    }
  start_request = start_request + 
  "\n--"+boundary+"\n" + 
  "Content-Disposition: form-data; name=\"upload\"; filename=\"CAM.JPG\"\n" + 
  "Content-Transfer-Encoding: binary\n\n";
  
   full_length = start_request.length() + len + end_request.length();
   printMessage(String(full_length/1024)+ " Kb sent");
   printMessage(String(upload_host));
    client.println("POST "+String(upload_path)+" HTTP/1.1");
    client.println("Host: "+String(upload_host));
    client.println("Content-Type: multipart/form-data; boundary="+boundary);
    client.print("Content-Length: "); client.println(full_length);
    client.println();
    client.print(start_request);
  if (saveInSpiffs) {
    String filename = String(memory.photoCount)+".jpg";
     if (SPIFFS.exists("/"+filename)) {
        SPIFFS.remove("/"+filename);
        delay(1);
     }
    printMessage("Saving: "+filename);
    fsFile = SPIFFS.open("/"+filename, "w");
  }
  // Read image data from Arducam
  static uint8_t buffer[bufferSize] = {0xFF};
  _md5.begin();
  int loops = 1;

  while (len) {
      size_t will_copy = (len < bufferSize) ? len : bufferSize;
      
      SPI.transferBytes(&buffer[0], &buffer[0], will_copy);

      // Check that FF & D8 came as JPEG headers (ArduCAM/Arduino/issues/381)
      if ((loops == 1) && (buffer[0] != 255) && (buffer[1] = 216)) {
        client.stop();
        printMessage("JPEG corrupt", true);
        printMessage("Abort transfer", true);
        return "JPEG headers corrupt";
      }
      delay(0);
      _md5.add(buffer, will_copy);
      //We won't break the WiFi upload if client disconnects since this is also for SPIFFS upload
      if (client.connected()) {
         client.write(&buffer[0], will_copy);
      }
      if (fsFile) {
        fsFile.write(&buffer[0], will_copy);
      }
      len -= will_copy;
      delay(0);
      loops++;
  }
  if (fsFile) {
    fsFile.close();
    memory.photoCount++;
    EEPROM_writeAnything(0, memory);
  }
  client.println(end_request);
  myCAM.CS_HIGH(); 

  bool   skip_headers = true;
  String rx_line;
  String response;
  
  // Read all the lines of the reply from server and print them to Serial
    int timeout = millis() + 5000;
  while (client.available() == 0) {
    if (timeout - millis() < 0) {
      message = "Client timeout";
      printMessage(message);
      client.stop();
      return message;
    }
  }
  while(client.available()) {
    rx_line = client.readStringUntil('\r');
    if (rx_line.length() <= 1) { 
        skip_headers = false;
      }
      // Collect http response
     if (!skip_headers) {
        response += rx_line;
     }
  }
  _md5.calculate();
  camHash = _md5.toString(); // Used to compare hashes in serverCapture
  response.trim();
  client.stop();
  return response;
  } else {
    message = "ERROR: Could not connect to "+String(upload_host);
    printMessage("Conn failed to");
    printMessage(String(upload_host));
    return message;
  }
}

void serverCapture() {
  digitalWrite(ledStatus, HIGH);
  cameraInit();
  
  start_capture();
  printMessage("CAPTURING", true, true);
  u8cursor = u8cursor+u8newline;
  int total_time = millis();
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
   delay(0);
  } 

  String response = camCapture(myCAM);
  total_time = millis() - total_time;
  printMessage("Upload in "+String(total_time/1000)+ " s.");
  Serial.print("RENDER THUMB json bytes:"+String(response.length())+" ");
  cameraOff();

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(response);
   
  if (!json.success()) {
    printMessage("JSON parse fail");
    server.send(200, "text/html", "<div id='m'>JSON parse error. Debug:</div><br>"+response);
    delay(100);
    return;
  }
  
  //json.printTo(Serial); // Only for debugging purpouses, may kill everything
  char imageUrl[300];
  char hash[33];
  char thumbWidth[3];
  char thumbHeight[3];
  strcpy(imageUrl, json["url"]);
  strcpy(thumbWidth, json["thumb_width"]);
  strcpy(thumbHeight, json["thumb_height"]);
  strcpy(hash, json["hash"]);
  JsonArray& arr = json["xbm"];
  
  int c=0;
  const char* tempx;
  for (auto value : arr) {
    // Assign the json array to xtemp
    tempx = value.as<char*>();
   
    image[c] = strtol(tempx, NULL, 16);
    // Preview first 10 lines for debugging
    //if (c<10) { Serial.print("i:");Serial.print(image[c]);Serial.print(" "); }
    c++;      
  }
    Serial.println(" pixels loaded:"+String(c));
    
  digitalWrite(ledStatus, LOW);
  u8g2.setDrawColor(0);
  u8g2.clearBuffer();
  u8g2.drawXBM( 0, 0, atoi(thumbWidth), atoi(thumbHeight), (const uint8_t *)image);
  u8g2.sendBuffer();
  
  String hashCheck = "<label style='color:red'>Image upload corrupted</label>";
  char camHashChar[33];
  camHash.toCharArray(camHashChar,33);
  //strcpy(camHashChar, "12345678901234567890123456789012");//DEBUG: Make comparison fail
  
  Serial.println("cam HASH: " +String(camHashChar));
  Serial.println("api HASH: " +String(hash));
  
  if (strcmp(camHashChar, hash) == 0) {
      printMessage("OK");
      hashCheck = "<label style='color:green'>Image verified: "+camHash+"</label>";
  } else {
    printMessage("UP. CORRUPT"); // Repeat upload automatically
  }
  if (onlineMode) {
    server.send(200, "text/html", "<div id='m'><small>"+String(hashCheck)+"<br>"+String(imageUrl)+
              "</small><br><img src='"+String(imageUrl)+"' width='400'></div>"+ javascriptFadeMessage);
  }
}

/**
 * This is the home page and also the page that comes when a 404 is generated
 */
void handleWebServerRoot() {
  String fileName = "/ux.html";
  
  if (SPIFFS.exists(fileName)) {
    File file = SPIFFS.open(fileName, "r");
    server.streamFile(file, getContentType(fileName));
    file.close();
  } else {
    message = "ERROR: handleWebServerRoot could not read "+fileName;
    printMessage(message);
    server.send(200, "text/html", message);
    return;
  }
  
  server.send(200, "text/html");
}

void configModeCallback(WiFiManager *myWiFiManager) {
  digitalWrite(ledStatus, HIGH);
  message = "CAM can't get online. Entering config mode. Please connect to access point "+String(configModeAP);
  delay(500);
  printMessage("CAM offline",true,true);
  printMessage("connect to ");
  printMessage(String(configModeAP));
}

void saveConfigCallback() {
  memory.resetWifiSettings = false;
  EEPROM_writeAnything(0, memory);
  shouldSaveConfig = true;
  printMessage("Saving config");
}

void saveParamCallback(){
  shouldSaveConfig = true;
  delay(100);
  wm.stopConfigPortal();
  Serial.println("[CALLBACK] saveParamCallback fired -> should save config is TRUE");
}

void shutterPing() {
  // Attempt to read settings.slave_cam_ip and ping a second camera
  if (strlen(slave_cam_ip) == 0) return;
  printMessage("PING to\n"+String(slave_cam_ip));
  
  if (!client.connect(slave_cam_ip, 80)) {
    printMessage("Ping failed");
    return;
  }
    // This will send the request to the server
  client.print(String("GET ") + "/capture HTTP/1.1\r\n" +
               "Host: " + slave_cam_ip + "\r\n" + 
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 100) {
      printMessage("> Pinged target "+String(slave_cam_ip));
      client.stop();
      return;
    }
  }
}

void serverResetWifiSettings() {
    printMessage("> Resseting WiFi settings, please connect to "+String(configModeAP)+" and setup a new connection");
    Serial.println("resetWifiSettings flag is saved on EEPROM");
    memory.resetWifiSettings = true;
    memory.photoCount = 1;
    EEPROM_writeAnything(0, memory);
    server.send(200, "text/html", "<div id='m'><h5>Restarting please connect to "+String(configModeAP)+"</h5>WiFi credentials will be deleted and camera will start in configuration mode.</div>"+ javascriptFadeMessage);
    delay(500);
    ESP.restart();
}

void serverCameraParams() {
    printMessage("Restarting. Connect to "+String(configModeAP)+" and click SETUP to update camera configuration");
    memory.editSetup = true;
    EEPROM_writeAnything(0, memory);
    server.send(200, "text/html", "<div id='m'><h5>Restarting please connect to "+String(configModeAP)+"</h5>Edit camera configuration using <b>Setup</b></div>"+ javascriptFadeMessage);
    delay(500);
    ESP.restart();
}

// Button events
void shutterReleased() {
    serverCapture();
}

void serverDeepSleep() {
  printMessage("Deep sleep");
  esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);

  server.send(200, "text/html", "<div id='m'><h5>Entering deep sleep, server will not respond until restart</div>");
  delay(1000);
    // Send Oled to sleep
  u8x8_cad_StartTransfer(u8g2.getU8x8());
  u8x8_cad_SendCmd( u8g2.getU8x8(), 0x8d);
  u8x8_cad_SendArg( u8g2.getU8x8(), 0x10);
  u8x8_cad_EndTransfer(u8g2.getU8x8());
  esp_deep_sleep_start();
}

void cameraInit() {
  Serial.println("cameraInit() _exposure: "+String(cameraSetExposure));
  if (strcmp(camera_mosfet, "0") == 0) {
    // Set back the selected resolution
    myCAM.OV5642_set_JPEG_size(jpeg_size_id);
    // Set Exposure many times does not work and will make a corrupt and big JPG
    myCAM.OV5642_set_Exposure_level(cameraSetExposure);
    delay(150);
    return;
  }
  digitalWrite(gpioCameraVcc, LOW);    // Power camera ON
  myCAM.clear_bit(6, GPIO_PWDN_MASK);  // Disable low power
  myCAM.set_format(JPEG);
  myCAM.InitCAM();

  int waitMs = 700;
  Serial.println("cameraInit() waitMS: "+String(waitMs));
  delay(waitMs);                       // 750 base
  myCAM.write_reg(3, VSYNC_LEVEL_MASK);// VSYNC is active HIGH
  myCAM.OV5642_set_JPEG_size(jpeg_size_id);
  myCAM.OV5642_set_Exposure_level(cameraSetExposure);
  delay(200); // Without wating some time here set exposure does nothing
  // NOTE : In some OV5642 Models just doing a 200 Miliseconds total delay is enough
  //        in other models, with doing about 800 Miliseconds wait after camera turns on
  //        the picture will be black, or oversized, and not readable.
}

void cameraOff() {
  if (strcmp(camera_mosfet,"0")==0) return;
  digitalWrite(gpioCameraVcc, HIGH); // Power camera OFF
  Serial.println("cameraOff()");
}

void serverStopStream() {
    printMessage("STREAM stopped", true, true);
    cameraOff();
    delay(3);
    server.send(200, "text/html", "<div id='m'>Streaming stoped</div>"+ javascriptFadeMessage);
}

void serverStream() {
  cameraInit();
  printMessage("STREAMING");
  myCAM.OV5642_set_JPEG_size(1);

  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);
  int counter = 0;
  while (true) {
    counter++;
    // Use a handleClient only 1 every N times
    if (counter % 129 == 0) {
       server.handleClient();
       Serial.print(String(counter)+" % NN Matched handleClient()");
    }
    start_capture();
    while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
       // server.handleClient(); ?
       delay(0);
    }
    size_t len = myCAM.read_fifo_length();

    if (len == 0 ) //0 kb
    {
      Serial.println(F("Size is 0."));
      continue;
    }
    myCAM.CS_LOW();
    myCAM.set_fifo_burst();
    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n\r\n";
    server.sendContent(response);
    
    while ( len-- )
    {
      temp_last = temp;
      temp =  SPI.transfer(0x00);

      //Read JPEG data from FIFO
      if ( (temp == 0xD9) && (temp_last == 0xFF) ) //If find the end ,break while,
      {
        buffer[i++] = temp;  //save the last  0XD9
        //Write the remain bytes in the buffer
        myCAM.CS_HIGH();
        client.write(&buffer[0], i);
        is_header = false;
        i = 0;
      }
      if (is_header == true)
      {
        //Write image data to buffer if not full
        if (i < bufferSize)
          buffer[i++] = temp;
        else
        {
          //Write bufferSize bytes image data to file
          myCAM.CS_HIGH();
          client.write(&buffer[0], bufferSize);
          i = 0;
          buffer[i++] = temp;
          myCAM.CS_LOW();
          myCAM.set_fifo_burst();
        }
      }
      else if ((temp == 0xD8) & (temp_last == 0xFF))
      {
        is_header = true;
        buffer[i++] = temp_last;
        buffer[i++] = temp;
      }
    }
    if (!client.connected()) {
      client.stop(); is_header = false; break;
    }
  }
  cameraOff();
}

void loop() {
   if (onlineMode) { 
    server.handleClient(); 
   }
  buttonShutter.tick();
}
