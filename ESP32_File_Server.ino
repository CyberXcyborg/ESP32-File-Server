/*
 * ESP32 File Server v3.2
 * (c) 2024 CyberXcyborg
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SimpleFTPServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <Update.h>

#include "config.h"
#include "auth.h"
#include "file_ops.h"
#include "web_ui.h"

WebServer webServer(webServerPort);
FtpServer ftpSrv;

bool wifiConnected = false;
bool accessPointMode = false;
String server_ip = "";
unsigned long lastWiFiCheck = 0;

bool connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting...");
  int attempts = 0;
  while (attempts < 3) {
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) { delay(500); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nIP: " + WiFi.localIP().toString());
      server_ip = WiFi.localIP().toString();
      wifiConnected = true; accessPointMode = false;
      if (MDNS.begin("esp32files")) { MDNS.addService("http", "tcp", webServerPort); }
      return true;
    }
    attempts++;
    if (attempts < 3) { WiFi.disconnect(); delay(1000); WiFi.begin(ssid, password); }
  }
  setupAccessPoint();
  return false;
}

void setupAccessPoint() {
  WiFi.disconnect(); delay(1000);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  server_ip = WiFi.softAPIP().toString();
  Serial.println("AP: " + server_ip);
  wifiConnected = false; accessPointMode = true;
  if (MDNS.begin("esp32files")) { MDNS.addService("http", "tcp", webServerPort); }
}

void checkWiFi() {
  if (millis() - lastWiFiCheck < 30000) return; // check every 30s
  lastWiFiCheck = millis();
  if (!accessPointMode && WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) { delay(500); attempts++; }
    if (WiFi.status() == WL_CONNECTED) {
      server_ip = WiFi.localIP().toString();
      wifiConnected = true;
      Serial.println("Reconnected: " + server_ip);
    }
  }
}

void handleServerInfo() {
  String m = accessPointMode ? "AP" : "WiFi";
  String json = "{\"ip\":\""+server_ip+"\",\"mode\":\""+m+"\",\"version\":\""+String(FIRMWARE_VERSION)+"\"";
  json += ",\"uptime\":"+String(millis()/1000);
  json += ",\"heap\":"+String(ESP.getFreeHeap());
  json += ",\"rssi\":"+String(WiFi.RSSI());
  json += "}";
  webServer.send(200,"application/json",json);
}

void handleListFiles() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  String path = webServer.hasArg("path") ? webServer.arg("path") : "/";
  DynamicJsonDocument doc(16384);
  doc["username"] = u; doc["userLevel"] = lvl;
  JsonObject st = doc.createNestedObject("storage");
  st["total"] = SD.totalBytes(); st["used"] = SD.usedBytes();
  JsonArray arr = doc.createNestedArray("files");
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { webServer.send(400); return; }
  int fc = 0, dc = 0;
  File file;
  while (file = dir.openNextFile()) {
    String fn = String(file.name());
    int sl = fn.lastIndexOf('/');
    String name = (sl >= 0) ? fn.substring(sl+1) : fn;
    bool isD = file.isDirectory();
    if (isD) dc++; else fc++;
    JsonObject f = arr.createNestedObject();
    f["name"] = name;
    f["path"] = path + (path.endsWith("/")?"":"/") + name + (isD?"/":"");
    f["type"] = isD ? "dir" : "file";
    f["size"] = isD ? 0 : file.size();
    f["icon"] = isD ? "📁" : getFileIcon(name);
    f["previewable"] = isPreviewable(name);
    file.close();
  }
  dir.close();
  doc["fileCount"] = fc; doc["dirCount"] = dc;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleFileInfo() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  File f = SD.open(path);
  if (!f) { webServer.send(500); return; }
  DynamicJsonDocument doc(512);
  doc["name"] = path.substring(path.lastIndexOf('/')+1);
  doc["path"] = path;
  doc["type"] = f.isDirectory() ? "dir" : "file";
  doc["size"] = f.isDirectory() ? 0 : f.size();
  doc["sizeFormatted"] = f.isDirectory() ? "-" : getFileSize(f.size());
  f.close();
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleDownload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { webServer.send(500); return; }
  String name = path.substring(path.lastIndexOf('/')+1);
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  webServer.streamFile(f, "application/octet-stream");
  f.close();
  logActivity("download", path, u);
}

void handleDelete() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  if (moveToTrash(path)) { logActivity("delete", path, u); webServer.send(200, "text/plain", "Moved to trash"); }
  else webServer.send(500, "text/plain", "Failed");
}

void handleCreateDir() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (createDirRecursive(path)) { logActivity("mkdir", path, u); webServer.send(200, "text/plain", "OK"); }
  else webServer.send(500, "text/plain", "Failed");
}

void handleRename() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("path") || !webServer.hasArg("name")) { webServer.send(400); return; }
  String path = webServer.arg("path"), newName = webServer.arg("name");
  if (!SD.exists(path)) { webServer.send(404); return; }
  if (!SD.open(path).isDirectory()) createVersion(path);
  int sl = path.lastIndexOf('/');
  String parent = (sl > 0) ? path.substring(0, sl+1) : "/";
  String np = parent + newName;
  if (SD.rename(path, np)) { logActivity("rename", path+" -> "+np, u); webServer.send(200, "text/plain", "OK"); }
  else webServer.send(500, "text/plain", "Failed");
}

void handleMove() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("path") || !webServer.hasArg("dest")) { webServer.send(400); return; }
  String path = webServer.arg("path"), dest = webServer.arg("dest");
  if (!SD.exists(path)) { webServer.send(404); return; }
  String dp;
  File dd = SD.open(dest);
  if (dd && dd.isDirectory()) { String n = path.substring(path.lastIndexOf('/')+1); dp = dest + (dest.endsWith("/")?"":"/") + n; dd.close(); }
  else dp = dest;
  if (moveFile(path, dp)) { logActivity("move", path+" -> "+dp, u); webServer.send(200, "text/plain", "OK"); }
  else webServer.send(500, "text/plain", "Failed");
}

void handleCopy() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("path") || !webServer.hasArg("dest")) { webServer.send(400); return; }
  String path = webServer.arg("path"), dest = webServer.arg("dest");
  if (!SD.exists(path)) { webServer.send(404); return; }
  String dp;
  File dd = SD.open(dest);
  if (dd && dd.isDirectory()) { String n = path.substring(path.lastIndexOf('/')+1); dp = dest + (dest.endsWith("/")?"":"/") + n; dd.close(); }
  else dp = dest;
  if (copyFile(path, dp)) { logActivity("copy", path+" -> "+dp, u); webServer.send(200, "text/plain", "OK"); }
  else webServer.send(500, "text/plain", "Failed");
}

void handleUploadAuth() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  String tok = generateSessionToken();
  createSession(u+"_upload", lvl);
  webServer.send(200, "application/json", "{\"token\":\""+tok+"\"}");
}

void handleUpload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  HTTPUpload& up = webServer.upload();
  static File uf;
  static String upp;
  if (up.status == UPLOAD_FILE_START) {
    String p = webServer.hasArg("path") ? webServer.arg("path") : "/";
    if (!p.endsWith("/")) p += "/";
    upp = p + up.filename;
    if (SD.exists(upp)) createVersion(upp);
    else SD.remove(upp);
    uf = SD.open(upp, FILE_WRITE);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uf) uf.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (uf) uf.close();
    logActivity("upload", upp+" ("+String(up.totalSize)+"B)", u);
  }
}

void handleCreateShare() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  String tok;
  if (createShare(path, tok)) {
    logActivity("share", path, u);
    String url = "http://"+server_ip+"/s/"+tok;
    webServer.send(200, "application/json", "{\"url\":\""+url+"\",\"token\":\""+tok+"\"}");
  } else webServer.send(500, "text/plain", "Failed");
}

void handleSharedFile() {
  String tok = webServer.pathArg(0);
  String path;
  if (!getSharePath(tok, path)) { webServer.send(404, "text/plain", "Invalid link"); return; }
  if (!SD.exists(path)) { webServer.send(404, "text/plain", "Not found"); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { webServer.send(500); return; }
  String name = path.substring(path.lastIndexOf('/')+1);
  webServer.sendHeader("Content-Disposition", "attachment; filename=\""+name+"\"");
  webServer.streamFile(f, "application/octet-stream");
  f.close();
}

uint32_t crc32c(uint32_t crc, uint8_t val) {
  crc ^= val;
  for(int i=0;i<8;i++) crc = (crc>>1)^(0x82F63B78&(-(crc&1)));
  return crc;
}

void handleZipDownload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, webServer.arg("plain"));
  JsonArray paths = doc["paths"];
  if (paths.size() == 0) { webServer.send(400); return; }
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "application/zip", "");
  uint16_t fc = 0;
  for (const char* ps : paths) {
    String p = ps;
    if (!SD.exists(p)) continue;
    File f = SD.open(p, FILE_READ);
    if (!f || f.isDirectory()) { if(f) f.close(); continue; }
    String name = p.substring(p.lastIndexOf('/')+1);
    uint32_t sz = f.size();
    uint32_t crc = 0; uint8_t buf[256];
    while (f.available()) { int n=f.read(buf,256); for(int i=0;i<n;i++) crc=crc32c(crc,buf[i]); }
    f.seek(0);
    uint8_t lh[30] = {0x50,0x4b,0x03,0x04,20,0,20,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    lh[26]=name.length()&0xFF; lh[27]=(name.length()>>8)&0xFF;
    webServer.sendContent((char*)lh,30);
    webServer.sendContent(name);
    while(f.available()){int n=f.read(buf,256);webServer.sendContent((char*)buf,n);}
    f.close();
    uint8_t cdh[46]={0x50,0x4b,0x01,0x02,20,0,20,0,20,0,0,0,0,0,0,0,0,0,0,0,0,0,(uint8_t)(crc&0xFF),(uint8_t)((crc>>8)&0xFF),(uint8_t)((crc>>16)&0xFF),(uint8_t)((crc>>24)&0xFF),(uint8_t)(sz&0xFF),(uint8_t)((sz>>8)&0xFF),(uint8_t)((sz>>16)&0xFF),(uint8_t)((sz>>24)&0xFF),(uint8_t)(sz&0xFF),(uint8_t)((sz>>8)&0xFF),(uint8_t)((sz>>16)&0xFF),(uint8_t)((sz>>24)&0xFF),0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    cdh[28]=name.length()&0xFF; cdh[29]=(name.length()>>8)&0xFF;
    webServer.sendContent((char*)cdh,46);
    webServer.sendContent(name);
    fc++;
  }
  uint8_t eocd[22]={0x50,0x4b,0x05,0x06,0,0,0,0,(uint8_t)(fc&0xFF),(uint8_t)((fc>>8)&0xFF),(uint8_t)(fc&0xFF),(uint8_t)((fc>>8)&0xFF),0,0,0,0,0,0,0,0,0,0};
  webServer.sendContent((char*)eocd,22);
  logActivity("zip",String(fc)+" files",u);
}

void handleChanges() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  unsigned long since = webServer.hasArg("since") ? webServer.arg("since").toInt() : 0;
  DynamicJsonDocument doc(256);
  doc["time"] = millis();
  doc["total"] = SD.totalBytes();
  doc["used"] = SD.usedBytes();
  bool changed = false;
  if (SD.exists(LOG_FILE) && since > 0) {
    File f = SD.open(LOG_FILE, FILE_READ);
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        int c1 = line.indexOf(',');
        if (c1 > 0 && (unsigned long)line.substring(0,c1).toInt() > since) { changed = true; break; }
      }
      f.close();
    }
  }
  doc["changed"] = changed;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== OTA UPDATE ==============
void handleOtaPage() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  webServer.send(200, "text/html", String(index_html));
}

void handleOtaUpload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  HTTPUpload& up = webServer.upload();
  static bool updateStarted = false;
  static size_t updateSize = 0;
  
  if (up.status == UPLOAD_FILE_START) {
    updateStarted = false;
    updateSize = 0;
    Serial.println("OTA: " + String(up.filename) + " size: " + String(up.totalSize));
    if (!Update.begin(up.totalSize, U_FLASH)) {
      Serial.println("OTA begin failed: " + String(Update.getError()));
    } else {
      updateStarted = true;
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (updateStarted) {
      size_t written = Update.write(up.buf, up.currentSize);
      if (written != up.currentSize) {
        Serial.println("OTA write failed");
        updateStarted = false;
      }
      updateSize += written;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (updateStarted && Update.end(true)) {
      Serial.println("OTA success: " + String(updateSize) + " bytes");
      logActivity("ota", "firmware updated "+String(updateSize)+"B", u);
      webServer.send(200, "text/plain", "OK: Firmware updated. Rebooting...");
      delay(1000);
      ESP.restart();
    } else {
      Serial.println("OTA end failed: " + String(Update.getError()));
      webServer.send(500, "text/plain", "OTA failed: " + String(Update.getError()));
    }
  }
}

void handleOtaStatus() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  DynamicJsonDocument doc(256);
  doc["version"] = FIRMWARE_VERSION;
  doc["sketch_size"] = ESP.getSketchSize();
  doc["free_space"] = ESP.getFreeSketchSpace();
  doc["heap"] = ESP.getFreeHeap();
  doc["chip"] = ESP.getChipModel();
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== SETTINGS ==============
void handleGetSettings() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  DynamicJsonDocument doc(512);
  doc["wifi_ssid"] = String(ssid);
  doc["ap_ssid"] = String(ap_ssid);
  doc["ftp_user"] = String(ftp_user);
  doc["version"] = FIRMWARE_VERSION;
  doc["ip"] = server_ip;
  doc["mode"] = accessPointMode ? "AP" : "WiFi";
  doc["uptime"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();
  if (SD.exists(SETTINGS_FILE)) {
    File f = SD.open(SETTINGS_FILE, FILE_READ);
    if (f) {
      DynamicJsonDocument s(512);
      deserializeJson(s, f); f.close();
      if (s["wifi_ssid"]) doc["saved_wifi_ssid"] = s["wifi_ssid"].as<String>();
      if (s["ap_ssid"]) doc["saved_ap_ssid"] = s["ap_ssid"].as<String>();
    }
  }
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleSaveSettings() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(512);
  deserializeJson(doc, webServer.arg("plain"));
  String ws = doc["wifi_ssid"] | String(ssid);
  String wp = doc["wifi_pass"] | String(password);
  String as_ = doc["ap_ssid"] | String(ap_ssid);
  String ap = doc["ap_pass"] | String(ap_password);
  String fu = doc["ftp_user"] | String(ftp_user);
  String fp = doc["ftp_pass"] | String(ftp_password);
  if (saveSettings(ws, wp, as_, ap, fu, fp)) {
    logActivity("settings", "updated", u);
    webServer.send(200, "application/json", "{\"ok\":true,\"msg\":\"Saved. Reboot to apply WiFi.\"}");
  } else webServer.send(500, "text/plain", "Failed");
}

// ============== ACTIVITY LOG ==============
void handleGetLog() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("log");
  if (SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_READ);
    if (f) {
      String lines[50]; int count = 0; String line = "";
      while (f.available()) {
        char c = f.read();
        if (c == '\n') { lines[count % 50] = line; count++; line = ""; }
        else line += c;
      }
      f.close();
      int start = count > 50 ? count - 50 : 0;
      for (int i = start; i < count; i++) {
        String l = lines[i % 50];
        int c1=l.indexOf(','), c2=l.indexOf(',',c1+1), c3=l.indexOf(',',c2+1);
        if (c1>0 && c2>0 && c3>0) {
          JsonObject e = arr.createNestedObject();
          e["time"] = l.substring(0,c1).toInt();
          e["user"] = l.substring(c1+1,c2);
          e["action"] = l.substring(c2+1,c3);
          e["path"] = l.substring(c3+1);
        }
      }
    }
  }
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== TRASH ==============
void handleTrashPage() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.sendHeader("Location","/login"); webServer.send(302); return; }
  webServer.send(200, "text/html", String(index_html));
}

void handleTrashList() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("files");
  if (SD.exists(TRASH_FOLDER)) {
    File dir = SD.open(TRASH_FOLDER);
    if (dir) {
      File f;
      while (f = dir.openNextFile()) {
        String name = String(f.name());
        int sl = name.lastIndexOf('/');
        String sn = (sl >= 0) ? name.substring(sl+1) : name;
        JsonObject it = arr.createNestedObject();
        it["name"] = sn; it["path"] = name;
        it["type"] = f.isDirectory() ? "dir" : "file";
        it["size"] = f.isDirectory() ? 0 : f.size();
        f.close();
      }
      dir.close();
    }
  }
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleRestore() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (restoreFromTrash(path)) { logActivity("restore", path, u); webServer.send(200, "text/plain", "Restored"); }
  else webServer.send(500, "text/plain", "Failed");
}

void handleEmptyTrash() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (webServer.hasArg("path")) {
    String path = webServer.arg("path");
    if (SD.exists(path)) { File f=SD.open(path); if(f.isDirectory()) removeDir(path); else SD.remove(path); f.close(); }
  } else { if (SD.exists(TRASH_FOLDER)) { removeDir(TRASH_FOLDER); SD.mkdir(TRASH_FOLDER); } }
  logActivity("empty-trash", "all", u);
  webServer.send(200, "text/plain", "OK");
}

// ============== USER MANAGEMENT ==============
void handleGetUsers() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  File f = SD.open(USERS_FILE);
  if (!f) { webServer.send(500); return; }
  String content = ""; while (f.available()) content += (char)f.read(); f.close();
  DynamicJsonDocument doc(1024); deserializeJson(doc, content);
  DynamicJsonDocument out(1024);
  JsonArray arr = out.createNestedArray("users");
  for (JsonObject u : doc["users"]) { JsonObject o = arr.createNestedObject(); o["username"]=u["username"]; o["userLevel"]=u["userLevel"]; }
  String r; serializeJson(out, r); webServer.send(200, "application/json", r);
}

void handleAddUser() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(256); deserializeJson(doc, webServer.arg("plain"));
  String nu = doc["username"] | "", np = doc["password"] | "", nl = doc["userLevel"] | "user";
  if (nu.length()==0 || np.length()==0) { webServer.send(400); return; }
  File f = SD.open(USERS_FILE);
  String c = "{\"users\":[]}"; if (f) { c=""; while(f.available()) c+=(char)f.read(); f.close(); }
  DynamicJsonDocument ex(1024); deserializeJson(ex, c);
  JsonObject u2 = ex["users"].createNestedObject();
  u2["username"]=nu; u2["password"]=np; u2["userLevel"]=nl;
  f = SD.open(USERS_FILE, FILE_WRITE); if (!f) { webServer.send(500); return; }
  serializeJson(ex, f); f.close();
  logActivity("add-user", nu, u); webServer.send(200, "text/plain", "OK");
}

void handleUpdateUser() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  String path = webServer.pathArg(0);
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(256); deserializeJson(doc, webServer.arg("plain"));
  File f = SD.open(USERS_FILE);
  String c = "{\"users\":[]}"; if (f) { c=""; while(f.available()) c+=(char)f.read(); f.close(); }
  DynamicJsonDocument ex(1024); deserializeJson(ex, c);
  for (JsonObject u : ex["users"]) {
    if (String(u["username"]|"").equals(path)) {
      if (doc["password"] && strlen(doc["password"])>0) u["password"]=doc["password"];
      u["userLevel"]=doc["userLevel"]|u["userLevel"]; break;
    }
  }
  f = SD.open(USERS_FILE, FILE_WRITE); if (!f) { webServer.send(500); return; }
  serializeJson(ex, f); f.close(); webServer.send(200, "text/plain", "OK");
}

void handleDeleteUser() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  String path = webServer.pathArg(0);
  File f = SD.open(USERS_FILE);
  String c = "{\"users\":[]}"; if (f) { c=""; while(f.available()) c+=(char)f.read(); f.close(); }
  DynamicJsonDocument ex(1024); deserializeJson(ex, c);
  for (int i=0; i<ex["users"].size(); i++) {
    if (String(ex["users"][i]["username"]|"").equals(path)) { ex["users"].remove(i); break; }
  }
  f = SD.open(USERS_FILE, FILE_WRITE); if (!f) { webServer.send(500); return; }
  serializeJson(ex, f); f.close();
  logActivity("delete-user", path, u); webServer.send(200, "text/plain", "OK");
}

void handleUserManagementPage() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.sendHeader("Location","/login"); webServer.send(302); return; }
  if (lvl != "admin") { webServer.sendHeader("Location","/"); webServer.send(302); return; }
  webServer.send(200, "text/html", String(index_html));
}

// ============== LOGIN / LOGOUT ==============
void handleLogin() {
  String err = "";
  if (webServer.method() == HTTP_POST) {
    String u = webServer.arg("username"), p = webServer.arg("password");
    String lvl;
    if (authenticateUser(u, p, lvl)) {
      String tok = createSession(u, lvl);
      webServer.sendHeader("Set-Cookie","session_token="+tok+"; Path=/; Max-Age=1800; SameSite=Strict");
      String r = webServer.hasArg("redirect") ? webServer.arg("redirect") : "/";
      webServer.sendHeader("Location", r+"?token="+tok, true);
      webServer.send(302, "text/plain", ""); return;
    } else err = "Invalid username or password";
  }
  String info = accessPointMode ? "AP: "+String(ap_ssid) : "IP: "+server_ip;
  String h = String(login_html);
  h.replace("%ERROR%", err); h.replace("%INFO%", info);
  webServer.send(200, "text/html", h);
}

void handleLogout() {
  String u, lvl;
  if (isAuthenticated(webServer, u, lvl)) {
    String tok;
    if (webServer.hasHeader("Authorization")) tok = webServer.header("Authorization").substring(7);
    else if (webServer.hasArg("token")) tok = webServer.arg("token");
    for (int i=0; i<MAX_SESSIONS; i++) { if(sessions[i].isActive && sessions[i].token==tok){sessions[i].isActive=false;break;} }
  }
  webServer.sendHeader("Set-Cookie","session_token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
  webServer.sendHeader("Location","/login"); webServer.send(302);
}

void handleRoot() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.sendHeader("Location","/login"); webServer.send(302); return; }
  webServer.send(200, "text/html", String(index_html));
}

void handleManifest() {
  webServer.send(200,"application/manifest+json","{\"name\":\"ESP32 File Server\",\"short_name\":\"ESP32-FS\",\"start_url\":\"/\",\"display\":\"standalone\",\"background_color\":\"#0984e3\",\"theme_color\":\"#0984e3\",\"icons\":[]}");
}

// ============== SETUP / LOOP ==============
void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 File Server v" + String(FIRMWARE_VERSION));
  if (!initSDCard()) Serial.println("SD Card failed!");
  else Serial.println("SD OK");
  loadSettings();
  setupAuthentication();
  connectToWiFi();

  webServer.on("/", handleRoot);
  webServer.on("/login", handleLogin);
  webServer.on("/logout", handleLogout);
  webServer.on("/server-info", handleServerInfo);
  webServer.on("/users", handleUserManagementPage);
  webServer.on("/trash", handleTrashPage);
  webServer.on("/ota", handleOtaPage);
  webServer.on("/manifest.json", handleManifest);
  webServer.on("/s/", HTTP_GET, handleSharedFile);

  webServer.on("/api/list", HTTP_GET, handleListFiles);
  webServer.on("/api/info", HTTP_GET, handleFileInfo);
  webServer.on("/api/download", HTTP_GET, handleDownload);
  webServer.on("/api/delete", HTTP_DELETE, handleDelete);
  webServer.on("/api/create-dir", HTTP_POST, handleCreateDir);
  webServer.on("/api/rename", HTTP_POST, handleRename);
  webServer.on("/api/move", HTTP_POST, handleMove);
  webServer.on("/api/copy", HTTP_POST, handleCopy);
  webServer.on("/api/upload-auth", HTTP_GET, handleUploadAuth);
  webServer.on("/api/upload", HTTP_POST, [](){webServer.send(200);}, handleUpload);
  webServer.on("/api/share", HTTP_POST, handleCreateShare);
  webServer.on("/api/zip", HTTP_POST, handleZipDownload);
  webServer.on("/api/changes", HTTP_GET, handleChanges);
  webServer.on("/api/trash", HTTP_GET, handleTrashList);
  webServer.on("/api/restore", HTTP_GET, handleRestore);
  webServer.on("/api/empty-trash", HTTP_GET, handleEmptyTrash);
  webServer.on("/api/log", HTTP_GET, handleGetLog);
  webServer.on("/api/settings", HTTP_GET, handleGetSettings);
  webServer.on("/api/settings", HTTP_POST, handleSaveSettings);
  webServer.on("/api/ota-status", HTTP_GET, handleOtaStatus);
  webServer.on("/api/ota-upload", HTTP_POST, [](){webServer.send(200);}, handleOtaUpload);
  webServer.on("/api/users", HTTP_GET, handleGetUsers);
  webServer.on("/api/users", HTTP_POST, handleAddUser);
  webServer.on("/api/users/", HTTP_PUT, handleUpdateUser);
  webServer.on("/api/users/", HTTP_DELETE, handleDeleteUser);

  webServer.begin();
  Serial.println("HTTP started on " + String(webServerPort));
  ftpSrv.begin(ftp_user, ftp_password);
  Serial.println("FTP started");
}

void loop() {
  webServer.handleClient();
  ftpSrv.handleFTP();
  checkWiFi();
}
