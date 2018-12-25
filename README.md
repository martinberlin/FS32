# FS32
## ESP32 WiFi powered digital Camera

**Branch: tft-7735** is an experimental version that uses a Color TFT display to render a JPG thumbnail that comes as a response from the Upload endpoint. 
Note: As a fist approach I connected this SPI display to share same MOSI, MISO and CLK with Arducam. So unless the camera SPI is using another interface, like HSPI, is not possible to send something to the display in the moment we are reading the cameras memory. I didn't find the time yet to soldier and try to instance the SPI interface in HSPI but it would be great to have them working independantly from each other. 
Example instancing 2 SPI buses: https://github.com/espressif/arduino-esp32/blob/master/libraries/SPI/examples/SPI_Multiple_Buses/SPI_Multiple_Buses.ino
I hit the limit at least using this ESP32 (Wemos Lolin32) of DynamicJson buffer. Any JSON bigger than 8 Kb delivers a parser error, so in this cases no thumbnail is displayed (Although picture upload may be successful)

### The FS32 is a digital Polaroid that uploads the photo instantly to the cloud
This project serves as a base to explore Arducam possibilities in combination with a WiFI ESP32 "System on a Chip" to create a simple WiFi point-and-shoot digital camera.

FS32 is a derivative from [FS2 Project](https://github.com/martinberlin/FS2) with the intention to make the most minimal version and feature set that is easier to handle and removing any other functionality. 
To the minimun means respecting this basic guidelines:

1. Only one camera is supported (OV5642)
2. No external events more than a simple Shutter button to ground that will make only one thing: To take a picture
3. Only Exposure control via Web Interface
4. SPIFFS should work (And selectable if we want to enable it on the Web UI)

As 5. we would like to additionally provide an upload chunker interface that will upload the files each 20Kb, sending an md5(hash) along with the upload POST, and the API receiver will return a 200 status header if the resulting hash matches or a 404 if not. That way we could implement a safe upload where if a part does not match due WiFi issues, will at least attempt to send N times more, only this affected part. This is the more complicated one since means completely refactoring not only the client firmware, but also the API upload endpoint, and make the described checksum system to allow progressive checks of the upload. Aborting the WiFi upload should not cut the SPIFFS save, with the objective to have the photo always saved somewhere.

And that's all. It will be kept to this mini set of 5 Points. All the rest should be implemented by the users in forks from this. 

### Requirements 

1. An ESP32 board (Heltec, Wemos Lolin 32, any other)
2. An OV5642 ArduCam
3. A simple button that will connect a GPIO to GND