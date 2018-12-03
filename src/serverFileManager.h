// Return whether a file has delete permission
bool isServerDeleteable(String filename) {
  if (filename == "config.json"
    ||filename == "template.html"
    ||filename == "ux.html")
  {
    return false;
  } 
  return true;
}

// Return whether a file should be listed
bool isServerListable(char* filename) {
  int8_t len = strlen(filename);
  bool result;
  if (  strstr(strlwr(filename + (len - 4)), ".jpg")
     || strstr(strlwr(filename + (len - 5)), ".json")
    ) {
    result = true;
  } else {
    result = false;
  }
  return result;
}

// List files in SPIFFs (Note we use a {{moustache}} type template)
void serverListFiles() {
  
  String fileName = "/template.html";
  webTemplate = "";
  printMessage("Listing files");
  
  if (SPIFFS.exists(fileName)) {
    File file = SPIFFS.open(fileName, "r");
    while (file.available() != 0) {  
      webTemplate += file.readStringUntil('\n');  
    }
    file.close();
  } else {
    Serial.println("Could not read "+fileName+" from SPIFFS");
    server.send(200, "text/html", "Could not read "+fileName+" from SPIFFS");
    return;
  }
  
  String body = "<table class='table'>";
  body += "<tr><th>File</th><th>Size</th><th>Del</th></tr>";
  //'Dir' was not declared in this scope
//  Dir dir = SPIFFS.openDir("/");
    File root = SPIFFS.open("/");
    if(!root){
        Serial.println("- failed to open directory");
        return;
}
  String fileUnit;
  unsigned int fileSize;
  char fileChar[32];
  
    File file = root.openNextFile();
    
    while(file){
      String fileName = file.name();
      fileName.toCharArray(fileChar, 32);
      if (!isServerListable(fileChar)) {
        // Move the pointer to avoid endless loop
        file = root.openNextFile();
        continue;
      }
      
      if (file.size()<1024) {
          fileUnit = " bytes";
          fileSize = file.size();
          } else {
            fileUnit = " Kb";
            fileSize = file.size()/1024;
          }
      fileName.remove(0,1);
      body += "<tr><td><a href='/fs/download?f="+fileName+"'>";
      body += fileName+"</a></td>";
      body += "<td>"+ String(fileSize)+fileUnit +"</td>";
      body += "<td>";
      if (isServerDeleteable(fileName)) {
        body += "<a class='btn-sm btn-danger' href='/fs/delete?f="+fileName+"'>x</a>";
      }
      body += "</td>";
      body += "</tr>";
      printMessage(fileName+ " "+ String(fileSize)+fileUnit);
      file = root.openNextFile();
  }
    
  body += "</table>";
  body += "<br>Total KB: "+String(SPIFFS.totalBytes()/1024)+" Kb";
  body += "<br>Used KB: "+String(SPIFFS.usedBytes()/1024)+" Kb";
  body += "<br>Avail KB: <b>"+String((SPIFFS.totalBytes()-SPIFFS.usedBytes())/1024)+" Kb</b><br>";

  webTemplate.replace("{{localDomain}}", localDomain);
  webTemplate.replace("{{home}}", "Camera UI");
  webTemplate.replace("{{body}}", body);
  
  server.send(200, "text/html", webTemplate);
}

void serverDownloadFile() {
  if (server.args() > 0 ) { 
    if (server.hasArg("f")) {
      String filename = server.arg(0);
      File download = SPIFFS.open("/"+filename, "r");
      if (download) {
        server.sendHeader("Content-Type", "text/text");
        server.sendHeader("Content-Disposition", "attachment; filename="+filename);
        server.sendHeader("Connection", "close");
        server.streamFile(download, "application/octet-stream");
        download.close();
      } else {
        server.send(404, "text/html", "file: "+ filename +" not found.");
      }
    } else {
      server.send(404, "text/html", "f parameter not received by GET.");
    }
  } else {
    server.send(404, "text/html", "No server parameters received.");
  }
}

void serverDeleteFile() {
  if (server.args() > 0 ) { 
    if (server.hasArg("f")) {
      String filename = server.arg(0);
      if(isServerDeleteable(filename)) {
         SPIFFS.remove("/"+filename);
         printMessage("> Deleting "+ filename);
      }
      server.sendHeader("Location", "/fs/list", true);
      server.send (302, "text/plain", "");
    } else {
      server.send(404, "text/html", "f parameter not received by GET.");
    }
  }
}
