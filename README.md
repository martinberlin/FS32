# FS32
## ESP32 WiFi powered digital Camera

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

## Companion projects 
If you are interested in controlling addressable Leds with ESP32 and launching animations with short UDP commands check out our project [Remora](https://github.com/martinberlin/Remora)
