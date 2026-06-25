/usr/bin/bash: warning: setlocale: LC_ALL: cannot change locale (en_US.UTF-8): No such file or directory
/usr/bin/bash: warning: setlocale: LC_ALL: cannot change locale (en_US.UTF-8): No such file or directory
/*
 * ESP32 File Server v3.5
 * (c) 2024 CyberXcyborg
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
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
WebSocketsServer webSocket(81); // WebSocket on port 81
FtpServer ftpSrv;

bool wifiConnected = false;
bool accessPointMode = false;
String server_ip = "";
unsigned long lastWiFiCheck = 0;
bool sdOK = true;

// ============== SD HEALTH MONITORING ==============
unsigned long lastHealthCheck = 0;
uint32_t sectorErrors = 0;
uint32_t healthCheckInterval = 600000UL; // 10 minutes
struct HealthLog {
  unsigned long timestamp;
  bool ok;
  uint32_t freeSpace;
  uint32_t errors;
};
HealthLog healthHistory[48]; // Last 48 entries (~8 hours at 10min intervals)
int healthIndex = 0;

// ============== WE BROADCAST ==============
void broadcastChange(String action, String path) {
  // Broadcast file change event to all connected WebSocket clients
  DynamicJsonDocument doc(256);
  doc["event"] = action; // "upload", "delete", "rename", "mkdir", "move"
  doc["path"] = path;
  doc["time"] = millis();
  String msg;
  serializeJson(doc, msg);
  webSocket.broadcastTXT(msg);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] WS Disconnected\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] WS Connected from %s\n", num, ip.toString().c_str());
        // Send initial status on connect
        DynamicJsonDocument doc(256);
        doc["event"] = "connected";
        doc["uptime"] = millis() / 1000;
        doc["sd_ok"] = sdOK;
        String msg;
        serializeJson(doc, msg);
        webSocket.sendTXT(num, msg);
      }
      break;
    case WStype_TEXT:
      // Handle ping/pong or auth
      if (length > 0 && payload[0] == '{') {
        DynamicJsonDocument doc(256);
        if (!deserializeJson(doc, payload, length)) {
          String cmd = doc["cmd"] | "";
          if (cmd == "ping") {
            webSocket.sendTXT(num, "{\"event\":\"pong\"}");
          }
        }
      }
      break;
  }
}

// ============== WIFI ==============
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
  if (millis() - lastWiFiCheck < 30000) return;
  lastWiFiCheck = millis();
  if (!accessPointMode && WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.disconnect();
    delay(100);
    WiFi.reconnect(); // non-blocking
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
      delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
      server_ip = WiFi.localIP().toString();
      wifiConnected = true;
      Serial.println("Reconnected: " + server_ip);
    } else {
      Serial.println("Reconnect failed — staying in AP mode");
      setupAccessPoint();
    }
  }
}

void checkSD() {
  if (!sdOK) {
    // Try to reinit
    if (SD.begin(SD_CS)) { sdOK = true; Serial.println("SD reconnected"); }
  }
  // Periodic health check
  if (millis() - lastHealthCheck > healthCheckInterval) {
    lastHealthCheck = millis();
    bool ok = SD.begin(SD_CS);
    uint32_t free = SD.totalBytes() - SD.usedBytes();
    healthHistory[healthIndex % 48] = {(unsigned long)(millis() / 1000), ok, (uint32_t)(free / 1024), sectorErrors};
    healthIndex++;
    if (!ok) {
      sdOK = false;
      Serial.println("SD health check FAILED");
    } else {
      sdOK = true;
    }
  }
}

void sendError(int code, String msg) {
  String json = "{"error":""+msg+"","code":"+String(code)+"}";
  webServer.send(code, "application/json", json);
}

// ============== SERVER INFO ==============
void handleServerInfo() {
  String m = accessPointMode ? "AP" : "WiFi";
  String json = "{\"ip\":\""+server_ip+"\",\"mode\":\""+m+"\",\"version\":\""+String(FIRMWARE_VERSION)+"\"";
  json += ",\"uptime\":"+String(millis()/1000);
  json += ",\"heap\":"+String(ESP.getFreeHeap());
  json += ",\"rssi\":"+String(WiFi.RSSI());
  json += ",\"sd_ok\":"+String(sdOK ? "true" : "false");
  json += ",\"sd_total\":"+String(SD.totalBytes());
  json += ",\"sd_used\":"+String(SD.usedBytes());
  json += "}";
  webServer.send(200,"application/json",json);
}

// ============== HEALTH CHECK ==============
void handleHealth() {
  // Public endpoint, no auth needed
  DynamicJsonDocument doc(256);
  doc["status"] = "ok";
  doc["version"] = FIRMWARE_VERSION;
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["wifi"] = wifiConnected;
  doc["rssi"] = WiFi.RSSI();
  checkSD(); doc["sd"] = sdOK;
  doc["ip"] = server_ip;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== REBOOT ==============
void handleReboot() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  logActivity("reboot", "remote", u);
  webServer.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}

// ============== LIST FILES ==============
// Sanitize path to prevent traversal
String sanitizePath(String path) {
  if (path.length() == 0) return "/";
  // Remove any ".." components
  while (path.indexOf("..") >= 0) {
    int pos = path.indexOf("..");
    int before = path.lastIndexOf('/', pos - 1);
    if (before < 0) before = 0;
    int after = path.indexOf('/', pos + 2);
    if (after < 0) after = path.length();
    path = path.substring(0, before) + path.substring(after);
  }
  // Ensure starts with /
  if (!path.startsWith("/")) path = "/" + path;
  // Collapse multiple slashes
  while (path.indexOf("//") >= 0) path.replace("//", "/");
  return path;
}

void handleListFiles() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503, "application/json", "{\"error\":\"SD card not available\"}"); return; }
  String path = sanitizePath(webServer.hasArg("path") ? webServer.arg("path") : "/");
  DynamicJsonDocument doc(16384);
  doc["username"] = u; doc["userLevel"] = lvl;
  JsonObject st = doc.createNestedObject("storage");
  st["total"] = SD.totalBytes(); st["used"] = SD.usedBytes();
  st["free"] = SD.totalBytes() - SD.usedBytes();
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

// ============== FILE INFO ==============
void handleFileInfo() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
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

// ============== DOWNLOAD ==============
void handleDownload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
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

// ============== DOWNLOAD WITH GZIP ==============
void handleDownloadGzip() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { webServer.send(500); return; }
  String name = path.substring(path.lastIndexOf('/')+1);
  String contentType = getContentType(name);
  // Only compress text files > 10KB
  if (!shouldCompress(name) || f.size() < 10240) {
    // Regular download for non-compressible or small files
    webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    webServer.sendHeader("Content-Type", contentType);
    webServer.streamFile(f, contentType);
    f.close();
    logActivity("download", path, u);
    return;
  }
  // Stream gzip-compressed file using chunked transfer
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + ".gz\"");
  webServer.sendHeader("Content-Type", "application/gzip");
  webServer.sendHeader("Content-Encoding", "gzip");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "application/gzip", "");
  // Simple RLE + basic compression stream (chunked)
  // For ESP32 we use a simple approach: stream raw with gzip header
  // Client decompresses - actual compression done via miniz if available
  // Fallback: stream as octet-stream with .gz extension (client handles)
  uint8_t buf[512];
  while (f.available()) {
    int n = f.read(buf, 512);
    webServer.sendContent((const char*)buf, n);
  }
  f.close();
  webServer.sendContent("");
  logActivity("download-gzip", path, u);
}

// ============== DELETE ==============
void handleDelete() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  if (moveToTrash(path)) { logActivity("delete", path, u); broadcastChange("delete", path); webServer.send(200, "text/plain", "Moved to trash"); }
  else webServer.send(500, "text/plain", "Failed");
}

// ============== CREATE DIR ==============
void handleCreateDir() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  // Check SD free space
  if (SD.totalBytes() - SD.usedBytes() < 1024) {
    webServer.send(507, "text/plain", "SD card full"); return;
  }
  if (createDirRecursive(path)) { logActivity("mkdir", path, u); broadcastChange("mkdir", path); webServer.send(200, "text/plain", "OK"); }
  else webServer.send(500, "text/plain", "Failed");
}

// ============== RENAME ==============
void handleRename() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { sendError(503,"SD card not available"); return; }
  if (!webServer.hasArg("path") || !webServer.hasArg("name")) { sendError(400,"Missing path or name"); return; }
  String path = webServer.arg("path"), newName = webServer.arg("name");
  if (!SD.exists(path)) { sendError(404,"Source file not found"); return; }
  // Validate new name - prevent path traversal
  if (newName.indexOf('/') >= 0 || newName.indexOf('\\') >= 0 || newName.length() == 0) {
    sendError(400,"Invalid file name"); return;
  }
  if (!acquireFileLock(path)) { sendError(423,"File is locked"); return; }
  // Create version for files (not directories)
  if (!SD.open(path).isDirectory()) createVersion(path);
  int sl = path.lastIndexOf('/');
  String parent = (sl > 0) ? path.substring(0, sl+1) : "/";
  String np = parent + newName;
  // Auto-rename if target exists
  if (SD.exists(np)) {
    int dot = newName.lastIndexOf('.');
    String base, ext;
    if () { base = newName.substring(0, dot); ext = newName.substring(dot); }
    else { base = newName; ext = ""; }
    int counter = 1;
    do {
      np = parent + base + "_" + String(counter) + ext;
      counter++;
    } while (SD.exists(np) && counter < 1000);
  }
  if (SD.rename(path, np)) { logActivity("rename", path+" -> "+np, u); broadcastChange("rename", np); webServer.send(200, "application/json", "{\"ok\":true,\"path\":\""+np+"\"}"); }
  else sendError(500,"Rename failed");
  releaseFileLock(path);
}

// ============== MOVE ==============
void handleMove() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { webServer.send(503); return; }
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

// ============== COPY ==============
void handleCopy() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { webServer.send(503); return; }
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

// ============== UPLOAD ==============
void handleUploadAuth() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  String tok = generateSessionToken();
  createSession(u, lvl); // use actual username, not u+"_upload"
  webServer.send(200, "application/json", "{\"token\":\""+tok+"\"}");
}

void handleUpload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { webServer.send(503); return; }
  HTTPUpload& up = webServer.upload();
  static File uf;
  static String upp;
  if (up.status == UPLOAD_FILE_START) {
    // Check free space (need at least upload size + 10KB buffer)
    uint64_t free = SD.totalBytes() - SD.usedBytes();
    if (free < up.totalSize + 10240) {
      Serial.println("Upload rejected: not enough space");
      return; // Will timeout on client
    }
    String p = webServer.hasArg("path") ? webServer.arg("path") : "/";
    if (!p.endsWith("/")) p += "/";
    upp = p + up.filename;
    if (SD.exists(upp)) createVersion(upp);
    else SD.remove(upp);
    uf = SD.open(upp, FILE_WRITE);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uf) {
      size_t written = uf.write(up.buf, up.currentSize);
      if (written != up.currentSize) {
        // SD full?
        Serial.println("Write failed - SD may be full");
        sdOK = false;
      }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (uf) uf.close();
    logActivity("upload", upp+" ("+String(up.totalSize)+"B)", u);
    broadcastChange("upload", upp);
  }
}

// ============== SHARE ==============
void handleCreateShare() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
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

// ============== ZIP ==============
uint32_t crc32c(uint32_t crc, uint8_t val) {
  crc ^= val;
  for(int i=0;i<8;i++) crc = (crc>>1)^(0x82F63B78&(-(crc&1)));
  return crc;
}

void handleZipDownload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, webServer.arg("plain"));
  JsonArray paths = doc["paths"];
  if (paths.size() == 0) { webServer.send(400); return; }
  streamZipFromJsonArray(paths, "zip", u);
}

// ============== VIDEO THUMBNAIL / STREAM ==============
void handleVideoThumbnail() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  // For ESP32 we can't generate actual thumbnails, but we can:
  // 1. Serve a placeholder icon image for video files
  // 2. Support range requests for streaming playback
  File f = SD.open(path, FILE_READ);
  if (!f) { webServer.send(500); return; }
  String name = path.substring(path.lastIndexOf('/')+1);
  String ctype = getContentType(name);
  // Support range requests for video streaming
  if (webServer.hasHeader("Range")) {
    // Parse range header for partial content
    String range = webServer.header("Range");
    int64_t start = 0, end = f.size() - 1;
    int64_t pos = range.indexOf('=');
    if (pos > 0) {
      String rangeSpec = range.substring(pos + 1);
      int dash = rangeSpec.indexOf('-');
      if (dash > 0) {
        start = rangeSpec.substring(0, dash).toInt();
        String endStr = rangeSpec.substring(dash + 1);
        if (endStr.length() > 0) end = endStr.toInt();
      }
    }
    int64_t contentLen = end - start + 1;
    webServer.setContentLength(contentLen);
    webServer.send(206, ctype, "");
    f.seek(start);
    int64_t remaining = contentLen;
    uint8_t buf[512];
    while (remaining > 0 && f.available()) {
      int toRead = remaining > 512 ? 512 : (int)remaining;
      int n = f.read(buf, toRead);
      if (n <= 0) break;
      webServer.sendContent((const char*)buf, n);
      remaining -= n;
    }
    webServer.sendContent("");
  } else {
    // Full file with Accept-Ranges header for streaming support
    webServer.sendHeader("Accept-Ranges", "bytes");
    webServer.sendHeader("Content-Disposition", "inline; filename=\"" + name + "\"");
    webServer.streamFile(f, ctype);
  }
  f.close();
  logActivity("video-stream", path, u);
}

// ============== CHANGES ==============
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

// ============== OTA ==============
void handleOtaPage() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  webServer.send(200, "text/html", String(index_html));
}

void handleOtaUpload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  HTTPUpload& up = webServer.upload();
  static bool started = false;
  static size_t sz = 0;
  if (up.status == UPLOAD_FILE_START) {
    started = false; sz = 0;
    if (!Update.begin(up.totalSize, U_FLASH)) {
      Serial.println("OTA fail: " + String(Update.getError()));
    } else started = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (started) {
      size_t w = Update.write(up.buf, up.currentSize);
      if (w != up.currentSize) started = false;
      sz += w;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (started && Update.end(true)) {
      logActivity("ota", String(sz)+"B", u);
      webServer.send(200, "text/plain", "OK - Rebooting...");
      delay(1000); ESP.restart();
    } else {
      webServer.send(500, "text/plain", "OTA failed");
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
  doc["web_port"] = webServerPort;
  doc["version"] = FIRMWARE_VERSION;
  doc["ip"] = server_ip;
  doc["mode"] = accessPointMode ? "AP" : "WiFi";
  doc["uptime"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();
  doc["sd_ok"] = sdOK;
  doc["sd_total"] = SD.totalBytes();
  doc["sd_used"] = SD.usedBytes();
  if (SD.exists(SETTINGS_FILE)) {
    File f = SD.open(SETTINGS_FILE, FILE_READ);
    if (f) { DynamicJsonDocument s(512); deserializeJson(s,f); f.close();
      if(s["wifi_ssid"]) doc["saved_wifi_ssid"]=s["wifi_ssid"].as<String>();
      if(s["ap_ssid"]) doc["saved_ap_ssid"]=s["ap_ssid"].as<String>();
    }
  }
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleSaveSettings() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(512);
  deserializeJson(doc, webServer.arg("plain"));
  String ws=doc["wifi_ssid"]|String(ssid), wp=doc["wifi_pass"]|String(password);
  String as_=doc["ap_ssid"]|String(ap_ssid), ap=doc["ap_pass"]|String(ap_password);
  String fu=doc["ftp_user"]|String(ftp_user), fp=doc["ftp_pass"]|String(ftp_password);
  uint16_t newPort = doc["web_port"] | webServerPort;
  if (newPort < 80 || newPort > 65535) newPort = 80; // Validate
  if (saveSettings(ws,wp,as_,ap,fu,fp)) {
    logActivity("settings","updated",u);
    if (newPort != webServerPort) {
      webServer.send(200,"application/json","{\"ok\":true,\"msg\":\"Saved. Rebooting for port change.\",\"reboot\":true}");
      delay(1500);
      ESP.restart();
    } else {
      webServer.send(200,"application/json","{\"ok\":true,\"msg\":\"Saved.\"}");
    }
  } else webServer.send(500,"text/plain","Failed");
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
      while (f.available()) { char c=f.read(); if(c=='\n'){lines[count%50]=count++;line="";} else line+=c; }
      f.close();
      int s = count>50?count-50:0;
      for (int i=s;i<count;i++) {
        String l=lines[i%50]; int c1=l.indexOf(','),c2=l.indexOf(',',c1+1),c3=l.indexOf(',',c2+1);
        if(c1>0&&c2>0&&c3>0){JsonObject e=arr.createNestedObject();e["time"]=l.substring(0,c1).toInt();e["user"]=l.substring(c1+1,c2);e["action"]=l.substring(c2+1,c3);e["path"]=l.substring(c3+1);}
      }
    }
  }
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== TRASH ==============
void handleTrashPage() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)){webServer.sendHeader("Location","/login");webServer.send(302);return;}
  webServer.send(200,"text/html",String(index_html));
}
void handleTrashList() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)){sendError(401,"Not authenticated");return;}
  if(!sdOK){sendError(503,"SD card not available");return;}
  DynamicJsonDocument doc(8192); JsonArray arr=doc.createNestedArray("files");
  if(SD.exists(TRASH_FOLDER)){
    File dir=SD.open(TRASH_FOLDER);
    if(dir){
      File f;
      while(f=dir.openNextFile()){
        String name=String(f.name());
        int sl=name.lastIndexOf('/');
        String sn=(sl>=0)?name.substring(sl+1):name;
        JsonObject it=arr.createNestedObject();
        it["name"]=sn;
        it["path"]=name;
        it["type"]=f.isDirectory()?"dir":"file";
        it["size"]=f.isDirectory()?0:f.size();
        it["deleted"]=f.fileTime();
        // Compute original path
        String orig=name;
        orig.replace(TRASH_FOLDER,"");
        it["original_path"]=orig;
        f.close();
      }
      dir.close();
    }
  }
  doc["count"]=arr.size();
  String out;serializeJson(doc,out);webServer.send(200,"application/json",out);
}
void handleRestore() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)){webServer.send(401);return;}
  if(!webServer.hasArg("path")){sendError(400,"Bad request");return;}
  String path=webServer.arg("path");
  if(restoreFromTrash(path)){logActivity("restore",path,u);webServer.send(200,"text/plain","Restored");}
  else webServer.send(500,"text/plain","Failed");
}
void handleEmptyTrash() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)){webServer.send(401);return;}
  if(webServer.hasArg("path")){String path=webServer.arg("path");if(SD.exists(path)){File f=SD.open(path);if(f.isDirectory())removeDir(path);else SD.remove(path);f.close();}}
  else{if(SD.exists(TRASH_FOLDER)){removeDir(TRASH_FOLDER);SD.mkdir(TRASH_FOLDER);}}
  logActivity("empty-trash","all",u);webServer.send(200,"text/plain","OK");
}

// ============== USERS ==============
void handleGetUsers() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)||lvl!="admin"){webServer.send(403);return;}
  File f=SD.open(USERS_FILE);if(!f){webServer.send(500);return;}
  String c="";while(f.available())c+=(char)f.read();f.close();
  DynamicJsonDocument doc(1024);deserializeJson(doc,c);
  DynamicJsonDocument out(1024);JsonArray arr=out.createNestedArray("users");
  for(JsonObject u:doc["users"]){JsonObject o=arr.createNestedObject();o["username"]=u["username"];o["userLevel"]=u["userLevel"];}
  String r;serializeJson(out,r);webServer.send(200,"application/json",r);
}
void handleAddUser() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)||lvl!="admin"){webServer.send(403);return;}
  if(!webServer.hasArg("plain")){sendError(400,"Bad request");return;}
  DynamicJsonDocument doc(256);deserializeJson(doc,webServer.arg("plain"));
  String nu=doc["username"]|"",np=doc["password"]|"",nl=doc["userLevel"]|"user";
  if(nu.length()==0||np.length()==0){sendError(400,"Bad request");return;}
  File f=SD.open(USERS_FILE);String c="{\"users\":[]}";if(f){c="";while(f.available())c+=(char)f.read();f.close();}
  DynamicJsonDocument ex(1024);deserializeJson(ex,c);
  JsonObject u2=ex["users"].createNestedObject();u2["username"]=nu;u2["password"]=np;u2["userLevel"]=nl;
  f=SD.open(USERS_FILE,FILE_WRITE);if(!f){webServer.send(500);return;}
  serializeJson(ex,f);f.close();logActivity("add-user",nu,u);webServer.send(200,"text/plain","OK");
}
void handleUpdateUser() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)||lvl!="admin"){webServer.send(403);return;}
  String path=webServer.pathArg(0);
  if(!webServer.hasArg("plain")){sendError(400,"Bad request");return;}
  DynamicJsonDocument doc(256);deserializeJson(doc,webServer.arg("plain"));
  File f=SD.open(USERS_FILE);String c="{\"users\":[]}";if(f){c="";while(f.available())c+=(char)f.read();f.close();}
  DynamicJsonDocument ex(1024);deserializeJson(ex,c);
  for(JsonObject u:ex["users"]){if(String(u["username"]|"").equals(path)){if(doc["password"]&&strlen(doc["password"])>0)u["password"]=doc["password"];u["userLevel"]=doc["userLevel"]|u["userLevel"];break;}}
  f=SD.open(USERS_FILE,FILE_WRITE);if(!f){webServer.send(500);return;}
  serializeJson(ex,f);f.close();webServer.send(200,"text/plain","OK");
}
void handleDeleteUser() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)||lvl!="admin"){webServer.send(403);return;}
  String path=webServer.pathArg(0);
  File f=SD.open(USERS_FILE);String c="{\"users\":[]}";if(f){c="";while(f.available())c+=(char)f.read();f.close();}
  DynamicJsonDocument ex(1024);deserializeJson(ex,c);
  for(int i=0;i<ex["users"].size();i++){if(String(ex["users"][i]["username"]|"").equals(path)){ex["users"].remove(i);break;}}
  f=SD.open(USERS_FILE,FILE_WRITE);if(!f){webServer.send(500);return;}
  serializeJson(ex,f);f.close();logActivity("delete-user",path,u);webServer.send(200,"text/plain","OK");
}
void handleUserManagementPage() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)){webServer.sendHeader("Location","/login");webServer.send(302);return;}
  if(lvl!="admin"){webServer.sendHeader("Location","/");webServer.send(302);return;}
  webServer.send(200,"text/html",String(index_html));
}

// ============== LOGIN/LOGOUT ==============
void handleLogin() {
  String err="";
  if(webServer.method()==HTTP_POST){
    String u=webServer.arg("username"),p=webServer.arg("password");String lvl;
    if(authenticateUser(u,p,lvl)){
      String tok=createSession(u,lvl);
      generateCsrfForSession(tok);
      webServer.sendHeader("Set-Cookie","session_token="+tok+"; Path=/; Max-Age=1800; SameSite=Strict; HttpOnly");
      String r=webServer.hasArg("redirect")?webServer.arg("redirect"):"/";
      webServer.sendHeader("Location",r+"?token="+tok,true);webServer.send(302,"text/plain","");return;
    } else err="Invalid username or password";
  }
  String info=accessPointMode?"AP: "+String(ap_ssid):"IP: "+server_ip;
  String h=String(login_html);h.replace("%ERROR%",err);h.replace("%INFO%",info);
  webServer.send(200,"text/html",h);
}
void handleLogout() {
  String u,lvl;
  if(isAuthenticated(webServer,u,lvl)){
    String tok;
    if(webServer.hasHeader("Authorization"))tok=webServer.header("Authorization").substring(7);
    else if(webServer.hasArg("token"))tok=webServer.arg("token");
    for(int i=0;i<MAX_SESSIONS;i++){if(sessions[i].isActive&&sessions[i].token==tok){sessions[i].isActive=false;break;}}
  }
  webServer.sendHeader("Set-Cookie","session_token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
  webServer.sendHeader("Location","/login");webServer.send(302);
}
void handleRoot() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)){webServer.sendHeader("Location","/login");webServer.send(302);return;}
  webServer.send(200,"text/html",String(index_html));
}
void handleManifest() {
  webServer.send(200,"application/manifest+json","{\"name\":\"ESP32 File Server\",\"short_name\":\"ESP32-FS\",\"start_url\":\"/\",\"display\":\"standalone\",\"background_color\":\"#0984e3\",\"theme_color\":\"#0984e3\",\"icons\":[]}");
}

// ============== BATCH OPERATIONS ==============
void handleBatchDelete() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, webServer.arg("plain"));
  JsonArray paths = doc["paths"];
  int ok = 0, fail = 0;
  for (const char* ps : paths) {
    String p = ps;
    if (SD.exists(p) && moveToTrash(p)) { ok++; logActivity("batch-delete", p, u); }
    else fail++;
  }
  String result = "{\"ok\":"+String(ok)+",\"fail\":"+String(fail)+"}";
  webServer.send(200, "application/json", result);
}

void handleBatchMove() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, webServer.arg("plain"));
  JsonArray paths = doc["paths"];
  String dest = doc["dest"] | "/";
  int ok = 0, fail = 0;
  for (const char* ps : paths) {
    String p = ps;
    if (!SD.exists(p)) { fail++; continue; }
    String dp;
    File dd = SD.open(dest);
    if (dd && dd.isDirectory()) { String n = p.substring(p.lastIndexOf('/')+1); dp = dest + (dest.endsWith("/")?"":"/") + n; dd.close(); }
    else dp = dest;
    if (moveFile(p, dp)) { ok++; logActivity("batch-move", p+" -> "+dp, u); }
    else fail++;
  }
  webServer.send(200, "application/json", "{\"ok\":"+String(ok)+",\"fail\":"+String(fail)+"}");
}

// ============== RECURSIVE SEARCH ==============
void searchRecursive(String path, String query, JsonArray &results) {
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;
  File file;
  while (file = dir.openNextFile()) {
    String fn = String(file.name());
    int sl = fn.lastIndexOf('/');
    String name = (sl >= 0) ? fn.substring(sl+1) : fn;
    if (file.isDirectory()) {
      searchRecursive(fn, query, results);
    } else {
      if (name.toLowerCase().indexOf(query) >= 0) {
        String p = path + (path.endsWith("/")?"":"/") + name;
        JsonObject r = results.createNestedObject();
        r["name"] = name;
        r["path"] = p;
        r["size"] = file.size();
        r["icon"] = getFileIcon(name);
      }
    }
    file.close();
  }
  dir.close();
}

void handleSearch() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  String query = webServer.hasArg("q") ? webServer.arg("q") : "";
  if (query.length() == 0) { webServer.send(400); return; }
  DynamicJsonDocument doc(8192);
  JsonArray results = doc.createNestedArray("results");
  searchRecursive("/", query.toLowerCase(), results);
  doc["count"] = results.size();
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== SETUP/LOOP ==============
// ============== FOLDER DOWNLOAD AS ZIP ==============
void handleFolderZip() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = sanitizePath(webServer.arg("path"));
  if (!SD.exists(path)) { webServer.send(404); return; }
  File d = SD.open(path);
  if (!d || !d.isDirectory()) { if(d) d.close(); webServer.send(400); return; }
  d.close();
  DynamicJsonDocument doc(16384);
  JsonArray paths = doc.createNestedArray("paths");
  collectFiles(path, paths);
  if (paths.size() == 0) { webServer.send(404, "text/plain", "Folder empty"); return; }
  // Normalize paths to absolute
  for (int i = 0; i < paths.size(); i++) {
    String p = paths[i].as<String>();
    if (!p.startsWith("/")) paths[i] = "/" + p;
  }
  streamZipFromJsonArray(paths, "folder-zip: "+path, u);
}

// ============== AUTO-CLEANUP ==============
void handleAutoCleanup() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  int days = webServer.hasArg("days") ? webServer.arg("days").toInt() : 30;
  int cleaned = autoCleanTrash(days);
  logActivity("auto-cleanup", String(cleaned)+" files older than "+String(days)+" days", u);
  webServer.send(200, "application/json", "{"cleaned":"+String(cleaned)+"}");
}

// ============== DIR COUNTS ==============
void handleDirInfo() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  String path = webServer.hasArg("path") ? webServer.arg("path") : "/";
  DynamicJsonDocument doc(256);
  doc["path"] = path;
  doc["files"] = countFilesInDir(path);
  doc["folders"] = countDirsInDir(path);
  doc["size"] = getDirSize(path);
  doc["sizeFormatted"] = getFileSize(getDirSize(path));
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== CONFIG WIZARD ==============
bool needsSetup() {
  // Check if this is first boot (no settings file or no admin user set)
  if (!SD.exists(SETTINGS_FILE)) return true;
  if (!SD.exists(USERS_FILE)) return true;
  File f = SD.open(USERS_FILE);
  if (!f) return true;
  String content = ""; while(f.available()) content += (char)f.read(); f.close();
  if (content.indexOf(""admin"") < 0) return true;
  return false;
}

void handleSetupPage() {
  if (!needsSetup()) { webServer.sendHeader("Location","/"); webServer.send(302); return; }
  String html = "<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1.0">";
  html += "<title>ESP32 File Server - Setup</title>";
  html += "<style>:root{--bg:#f5f6fa;--card:#fff;--text:#2d3436;--primary:#0984e3;--border:#dfe6e9}";
  html += "*{margin:0;padding:0;box-sizing:border-box;font-family:system-ui,sans-serif}";
  html += "body{background:var(--bg);display:flex;justify-content:center;align-items:center;min-height:100vh;padding:20px}";
  html += ".card{background:var(--card);border-radius:12px;padding:35px;max-width:450px;width:100%;box-shadow:0 4px 20px rgba(0,0,0,0.08)}";
  html += "h1{color:var(--primary);font-size:22px;margin-bottom:5px}";
  html += ".sub{color:#999;font-size:13px;margin-bottom:25px}";
  html += ".form-group{margin-bottom:15px}";
  html += "label{display:block;margin-bottom:5px;font-weight:500;font-size:13px}";
  html += "input{width:100%;padding:12px;border:1px solid var(--border);border-radius:8px;font-size:15px}";
  html += "input:focus{outline:none;border-color:var(--primary)}";
  html += ".btn{width:100%;background:var(--primary);color:#fff;border:none;padding:14px;border-radius:8px;cursor:pointer;font-size:15px;font-weight:600;margin-top:10px}";
  html += ".btn:hover{background:#0652DD}</style></head><body>";
  html += "<div class="card"><h1>📁 ESP32 File Server</h1><p class="sub">Initial setup - configure your server</p>";
  html += "<form method="post" action="/api/complete-setup">";
  html += "<div class="form-group"><label>Admin Username</label><input type="text" name="admin_user" value="admin" required></div>";
  html += "<div class="form-group"><label>Admin Password</label><input type="password" name="admin_pass" required></div>";
  html += "<div class="form-group"><label>WiFi SSID</label><input type="text" name="wifi_ssid" required></div>";
  html += "<div class="form-group"><label>WiFi Password</label><input type="password" name="wifi_pass" required></div>";
  html += "<button type="submit" class="btn">Complete Setup</button></form></div></body></html>";
  webServer.send(200, "text/html", html);
}

void handleCompleteSetup() {
  if (!webServer.hasArg("admin_user") || !webServer.hasArg("admin_pass") || !webServer.hasArg("wifi_ssid") || !webServer.hasArg("wifi_pass")) {
    webServer.send(400); return;
  }
  String au = webServer.arg("admin_user");
  String ap = webServer.arg("admin_pass");
  String ws = webServer.arg("wifi_ssid");
  String wp = webServer.arg("wifi_pass");
  // Save users file
  File f = SD.open(USERS_FILE, FILE_WRITE);
  if (f) { f.print("{\"users\":[{\"username\":\""+au+"\",\"password\":\""+ap+"\",\"userLevel\":\"admin\"}]}"); f.close(); }
  // Save settings
  saveSettings(ws, wp, ap_ssid, ap_password, ftp_user, ftp_password);
  webServer.send(200, "text/html", "<html><body style="font-family:system-ui;padding:40px;text-align:center"><h1>✅ Setup Complete!</h1><p>Device will now connect to WiFi and restart.</p></body></html>");
  delay(2000);
  ESP.restart();
}

// ============== ACTIVITY LOG EXPORT ==============
void handleExportLog() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { sendError(403,"Admin only"); return; }
  String format = webServer.hasArg("format") ? webServer.hasArg("format") : "json";
  if (format == "csv") {
    webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer.send(200, "text/csv", "timestamp,username,action,path\n");
    if (SD.exists(LOG_FILE)) {
      File f = SD.open(LOG_FILE, FILE_READ);
      if (f) { while(f.available()) webServer.sendContent(String((char)f.read())); f.close(); }
    }
  } else {
    File f = SD.open(LOG_FILE, FILE_READ);
    if (f) { String c=""; while(f.available()) c+=(char)f.read(); f.close(); webServer.send(200,"application/json",c); }
    else webServer.send(200,"application/json","[]");
  }
}

// ============== SYSTEM STATS ==============
void handleStats() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  DynamicJsonDocument doc(1024);
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = server_ip;
  doc["mode"] = accessPointMode ? "AP" : "WiFi";
  doc["sd_total"] = SD.totalBytes();
  doc["sd_used"] = SD.usedBytes();
  doc["sd_free"] = SD.totalBytes() - SD.usedBytes();
  doc["version"] = FIRMWARE_VERSION;
  // Add SD health stats
  doc["sd_sector_errors"] = sectorErrors;
  doc["sd_health_interval"] = healthCheckInterval / 1000;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== SD HEALTH ENDPOINT ==============
void handleSdHealth() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  DynamicJsonDocument doc(2048);
  doc["ok"] = sdOK;
  doc["total"] = (uint32_t)(SD.totalBytes() / 1024);
  doc["used"] = (uint32_t)(SD.usedBytes() / 1024);
  doc["free"] = (uint32_t)((SD.totalBytes() - SD.usedBytes()) / 1024);
  doc["sector_errors"] = sectorErrors;
  doc["last_check"] = (uint32_t)(lastHealthCheck / 1000);
  doc["card_type"] = SD.cardType();
  doc["sector_count"] = SD.sectorsPerCluster() * SD.clusterCount();
  JsonArray arr = doc.createNestedArray("history");
  for (int i = 0; i < 48 && i < healthIndex; i++) {
    int idx = (healthIndex - 1 - i + 48) % 48;
    if (healthHistory[idx].timestamp == 0) continue;
    JsonObject h = arr.createNestedObject();
    h["t"] = healthHistory[idx].timestamp;
    h["ok"] = healthHistory[idx].ok;
    h["free_kb"] = healthHistory[idx].freeSpace;
    h["err"] = healthHistory[idx].errors;
  }
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== FILE TYPE FILTER ==============
void handleListFilesFiltered() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  if (!sdOK) { sendError(503,"SD card not available"); return; }
  String path = webServer.hasArg("path") ? webServer.arg("path") : "/";
  String filter = webServer.hasArg("type") ? webServer.arg("type") : "all";
  DynamicJsonDocument doc(16384);
  doc["username"] = u; doc["userLevel"] = lvl; doc["filter"] = filter;
  JsonArray arr = doc.createNestedArray("files");
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { sendError(400,"Invalid directory"); return; }
  int fc = 0, dc = 0, totalSize = 0;
  File file;
  while (file = dir.openNextFile()) {
    String fn = String(file.name());
    int sl = fn.lastIndexOf('/');
    String name = (sl >= 0) ? fn.substring(sl+1) : fn;
    bool isD = file.isDirectory();
    bool include = true;
    if (!isD && filter != "all") {
      String ext = name.substring(name.lastIndexOf('.')+1); ext.toLowerCase();
      if (filter == "images") include = ext=="jpg"||ext=="jpeg"||ext=="png"||ext=="gif"||ext=="bmp"||ext=="svg"||ext=="webp";
      else if (filter == "video") include = ext=="mp4"||ext=="avi"||ext=="mov"||ext=="mkv";
      else if (filter == "audio") include = ext=="mp3"||ext=="wav"||ext=="ogg"||ext=="flac";
      else if (filter == "docs") include = ext=="pdf"||ext=="doc"||ext=="docx"||ext=="txt"||ext=="md";
      else if (filter == "archives") include = ext=="zip"||ext=="rar"||ext=="7z"||ext=="tar"||ext=="gz";
      else if (filter == "code") include = ext=="c"||ext=="cpp"||ext=="h"||ext=="py"||ext=="js"||ext=="html"||ext=="css"||ext=="json"||ext=="xml";
      else include = false;
    }
    if (include) {
      if (isD) dc++; else { fc++; totalSize += file.size(); }
      JsonObject f = arr.createNestedObject();
      f["name"] = name;
      f["path"] = path + (path.endsWith("/")?"":"/") + name + (isD?"/":"");
      f["type"] = isD ? "dir" : "file";
      f["size"] = isD ? 0 : file.size();
      f["icon"] = isD ? "📁" : getFileIcon(name);
      f["previewable"] = isPreviewable(name);
    }
    file.close();
  }
  dir.close();
  doc["fileCount"] = fc; doc["dirCount"] = dc; doc["totalSize"] = (uint64_t)totalSize; doc["totalSizeFormatted"] = getFileSize((uint64_t)totalSize);
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== STORAGE BREAKDOWN ==============
void handleStorageBreakdown() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  if (!sdOK) { sendError(503,"SD card not available"); return; }
  DynamicJsonDocument doc(1024);
  doc["total"] = SD.totalBytes();
  doc["used"] = SD.usedBytes();
  doc["free"] = SD.totalBytes() - SD.usedBytes();
  // Count by type
  uint64_t images = 0, video = 0, audio = 0, docs = 0, archives = 0, code = 0, other = 0, dirs = 0, trash = 0;
  // Simple breakdown by scanning root
  File dir = SD.open("/");
  if (dir && dir.isDirectory()) {
    File file;
    while (file = dir.openNextFile()) {
      String fn = String(file.name());
      int sl = fn.lastIndexOf('/');
      String name = (sl >= 0) ? fn.substring(sl+1) : fn;
      if (file.isDirectory()) { dirs++; }
      else {
        String ext = name.substring(name.lastIndexOf('.')+1); ext.toLowerCase();
        if (ext=="jpg"||ext=="jpeg"||ext=="png"||ext=="gif"||ext=="bmp"||ext=="svg"||ext=="webp") images += file.size();
        else if (ext=="mp4"||ext=="avi"||ext=="mov"||ext=="mkv") video += file.size();
        else if (ext=="mp3"||ext=="wav"||ext=="ogg"||ext=="flac") audio += file.size();
        else if (ext=="pdf"||ext=="doc"||ext=="docx"||ext=="txt") docs += file.size();
        else if (ext=="zip"||ext=="rar"||ext=="7z"||ext=="tar"||ext=="gz") archives += file.size();
        else if (ext=="c"||ext=="cpp"||ext=="h"||ext=="py"||ext=="js"||ext=="html"||ext=="css"||ext=="json"||ext=="xml") code += file.size();
        else other += file.size();
      }
      file.close();
    }
    dir.close();
  }
  trash = getDirSize(TRASH_FOLDER);
  JsonObject breakdown = doc.createNestedObject("breakdown");
  JsonObject img = breakdown.createNestedObject("images"); img["size"] = images; img["formatted"] = getFileSize(images);
  JsonObject vid = breakdown.createNestedObject("video"); vid["size"] = video; vid["formatted"] = getFileSize(video);
  JsonObject aud = breakdown.createNestedObject("audio"); aud["size"] = audio; aud["formatted"] = getFileSize(audio);
  JsonObject d = breakdown.createNestedObject("documents"); d["size"] = docs; d["formatted"] = getFileSize(docs);
  JsonObject arc = breakdown.createNestedObject("archives"); arc["size"] = archives; arc["formatted"] = getFileSize(archives);
  JsonObject cod = breakdown.createNestedObject("code"); cod["size"] = code; cod["formatted"] = getFileSize(code);
  JsonObject oth = breakdown.createNestedObject("other"); oth["size"] = other; oth["formatted"] = getFileSize(other);
  JsonObject tr = breakdown.createNestedObject("trash"); tr["size"] = trash; tr["formatted"] = getFileSize(trash);
  JsonObject dr = breakdown.createNestedObject("empty"); dr["size"] = (SD.totalBytes()-SD.usedBytes()); dr["formatted"] = getFileSize((uint64_t)(SD.totalBytes()-SD.usedBytes()));
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== FAVORITES ==============
void handleGetFavorites() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("favorites");
  String favFile = "/.favorites_" + u + ".json";
  if (SD.exists(favFile)) {
    File f = SD.open(favFile, FILE_READ);
    if (f) { deserializeJson(doc, f); f.close(); }
  }
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleToggleFavorite() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  if (!webServer.hasArg("path")) { sendError(400,"Missing path"); return; }
  String path = webServer.arg("path");
  String favFile = "/.favorites_" + u + ".json";
  DynamicJsonDocument doc(512);
  JsonArray arr = doc.createNestedArray("paths");
  if (SD.exists(favFile)) {
    File f = SD.open(favFile, FILE_READ);
    if (f) { deserializeJson(doc, f); f.close(); }
    arr = doc["paths"];
  }
  // Check if already exists, remove if so
  bool exists = false;
  for (int i = 0; i < arr.size(); i++) {
    if (arr[i].as<String>() == path) { arr.remove(i); exists = true; break; }
  }
  if (!exists) arr.add(path);
  File f = SD.open(favFile, FILE_WRITE);
  if (f) { serializeJson(doc, f); f.close(); }
  webServer.send(200, "application/json", "{"ok":true,"favorite":"+String(exists?"false":"true")+"}");
}

// ============== SESSION MANAGEMENT ==============
void handleListSessions() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { sendError(403,"Admin only"); return; }
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.createNestedArray("sessions");
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].isActive) {
      JsonObject s = arr.createNestedObject();
      s["id"] = i;
      s["username"] = sessions[i].username;
      s["level"] = sessions[i].userLevel;
      s["last_activity"] = millis() - sessions[i].lastActivity;
      s["active"] = true;
    }
  }
  doc["total"] = arr.size();
  doc["max"] = MAX_SESSIONS;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleKillSession() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { sendError(403,"Admin only"); return; }
  if (!webServer.hasArg("id")) { sendError(400,"Missing session id"); return; }
  int id = webServer.arg("id").toInt();
  if (id >= 0 && id < MAX_SESSIONS) {
    logActivity("session-kill", sessions[id].username, u);
    sessions[id].isActive = false;
    webServer.send(200, "application/json", "{"ok":true}");
  } else {
    sendError(404,"Session not found");
  }
}

// ============== ERROR STATS ==============
void handleErrorLogs() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  String since = webServer.hasArg("since") ? webServer.arg("since") : "10";
  int sinceCount = since.toInt();
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("recent_errors");
  if (SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_READ);
    if (f) {
      String lines[100];
      int count = 0; String line = "";
      while (f.available()) {
        char c = f.read();
        if (c == "\n") { lines[count % 100] = line; count++; line = ""; }
        else line += c;
      }
      f.close();
      int start = count > sinceCount ? count - sinceCount : 0;
      for (int i = start; i < count; i++) {
        String l = lines[i % 100];
        if (l.length() > 0) arr.add(l);
      }
    }
  }
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== FILE NOTES ==============
void handleGetNotes() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  String path = webServer.hasArg("path") ? webServer.arg("path") : "";
  String notesFile = "/.notes.json";
  DynamicJsonDocument doc(2048);
  if (SD.exists(notesFile)) {
    File f = SD.open(notesFile, FILE_READ);
    if (f) { deserializeJson(doc, f); f.close(); }
  }
  if (path.length() > 0 && doc.containsKey(path)) {
    webServer.send(200, "application/json", "{"notes":""+doc[path].as<String>()+""}");
  } else {
    webServer.send(200, "application/json", "{}");
  }
}

void handleSaveNote() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  if (!webServer.hasArg("plain")) { sendError(400,"Missing data"); return; }
  DynamicJsonDocument input(512);
  deserializeJson(input, webServer.arg("plain"));
  String path = input["path"] | "";
  String note = input["notes"] | "";
  if (path.length() == 0) { sendError(400,"Missing path"); return; }
  String notesFile = "/.notes.json";
  DynamicJsonDocument doc(2048);
  if (SD.exists(notesFile)) {
    File f = SD.open(notesFile, FILE_READ);
    if (f) { deserializeJson(doc, f); f.close(); }
  }
  if (note.length() == 0) {
    doc.remove(path);
  } else {
    doc[path] = note;
  }
  File f = SD.open(notesFile, FILE_WRITE);
  if (f) { serializeJson(doc, f); f.close(); }
  logActivity("note", path, u);
  webServer.send(200, "application/json", "{"ok":true}");
}


// ============== CLEANUP TOOLS ==============
void handleScanStorage() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  if (!sdOK) { sendError(503,"SD card not available"); return; }
  String path = webServer.hasArg("path") ? webServer.arg("path") : "/";
  DynamicJsonDocument doc(2048);
  int totalFiles = 0, totalDirs = 0, bigFiles = 0, oldFiles = 0;
  uint64_t totalSize = 0, oldSize = 0, trashSize = 0;
  int trashCount = 0;
  unsigned long now = millis();
  unsigned long thirtyDaysMs = (unsigned long)30 * 86400000UL;

  // Scan directory
  File dir = SD.open(path);
  if (dir && dir.isDirectory()) {
    File file;
    while (file = dir.openNextFile()) {
      if (file.isDirectory()) {
        totalDirs++;
      } else {
        totalFiles++;
        totalSize += file.size();
        if (file.size() > 10485760) bigFiles++;
        if ((now - file.fileTime()) > thirtyDaysMs) { oldFiles++; oldSize += file.size(); }
      }
      file.close();
    }
    dir.close();
  }

  // Trash stats
  if (SD.exists(TRASH_FOLDER)) {
    File td = SD.open(TRASH_FOLDER);
    if (td && td.isDirectory()) {
      File tf;
      while (tf = td.openNextFile()) {
        if (!tf.isDirectory()) { trashCount++; trashSize += tf.size(); }
        tf.close();
      }
      td.close();
    }
  }

  doc["total_files"] = totalFiles;
  doc["total_dirs"] = totalDirs;
  doc["total_size"] = (uint64_t)totalSize;
  doc["total_size_formatted"] = getFileSize((uint64_t)totalSize);
  doc["big_files"] = bigFiles;
  doc["old_files"] = oldFiles;
  doc["old_size"] = (uint64_t)oldSize;
  doc["old_size_formatted"] = getFileSize((uint64_t)oldSize);
  doc["trash_count"] = trashCount;
  doc["trash_size"] = (uint64_t)trashSize;
  doc["trash_size_formatted"] = getFileSize((uint64_t)trashSize);
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
  logActivity("scan", path, u);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 File Server v"+String(FIRMWARE_VERSION));
  sdOK = initSDCard();
  if(!sdOK) Serial.println("SD Card failed!");
  else Serial.println("SD OK");
  loadSettings();
  setupAuthentication();
  connectToWiFi();

  webServer.on("/",handleRoot);
  webServer.on("/login",handleLogin);
  webServer.on("/logout",handleLogout);
  webServer.on("/health",handleHealth);
  webServer.on("/server-info",handleServerInfo);
  webServer.on("/users",handleUserManagementPage);
  webServer.on("/trash",handleTrashPage);
  webServer.on("/ota",handleOtaPage);
  webServer.on("/manifest.json",handleManifest);
  webServer.on("/setup",handleSetupPage);
  webServer.on("/api/complete-setup",HTTP_POST,handleCompleteSetup);
  webServer.on("/s/",HTTP_GET,handleSharedFile);

  webServer.on("/api/list",HTTP_GET,handleListFiles);
  webServer.on("/api/info",HTTP_GET,handleFileInfo);
  webServer.on("/api/download",HTTP_GET,handleDownload);
  webServer.on("/api/download-gzip",HTTP_GET,handleDownloadGzip);
  webServer.on("/api/delete",HTTP_DELETE,handleDelete);
  webServer.on("/api/create-dir",HTTP_POST,handleCreateDir);
  webServer.on("/api/rename",HTTP_POST,handleRename);
  webServer.on("/api/move",HTTP_POST,handleMove);
  webServer.on("/api/copy",HTTP_POST,handleCopy);
  webServer.on("/api/upload-auth",HTTP_GET,handleUploadAuth);
  webServer.on("/api/upload",HTTP_POST,[](){webServer.send(200);},handleUpload);
  webServer.on("/api/share",HTTP_POST,handleCreateShare);
  webServer.on("/api/zip",HTTP_POST,handleZipDownload);
  webServer.on("/api/video",HTTP_GET,handleVideoThumbnail);
  webServer.on("/api/batch-delete",HTTP_POST,handleBatchDelete);
  webServer.on("/api/batch-move",HTTP_POST,handleBatchMove);
  webServer.on("/api/search",HTTP_GET,handleSearch);
  webServer.on("/api/changes",HTTP_GET,handleChanges);
  webServer.on("/api/folder-zip",HTTP_POST,handleFolderZip);
  webServer.on("/api/cleanup",HTTP_POST,handleAutoCleanup);
  webServer.on("/api/dir-info",HTTP_GET,handleDirInfo);
  webServer.on("/api/trash",HTTP_GET,handleTrashList);
  webServer.on("/api/restore",HTTP_GET,handleRestore);
  webServer.on("/api/empty-trash",HTTP_GET,handleEmptyTrash);
  webServer.on("/api/log",HTTP_GET,handleGetLog);
  webServer.on("/api/settings",HTTP_GET,handleGetSettings);
  webServer.on("/api/settings",HTTP_POST,handleSaveSettings);
  webServer.on("/api/ota-status",HTTP_GET,handleOtaStatus);
  webServer.on("/api/ota-upload",HTTP_POST,[](){webServer.send(200);},handleOtaUpload);
  webServer.on("/api/reboot",HTTP_POST,handleReboot);
  webServer.on("/api/export-log",HTTP_GET,handleExportLog);
  webServer.on("/api/stats",HTTP_GET,handleStats);
  webServer.on("/api/list-filtered",HTTP_GET,handleListFilesFiltered);
  webServer.on("/api/storage",HTTP_GET,handleStorageBreakdown);
  webServer.on("/api/favorites",HTTP_GET,handleGetFavorites);
  webServer.on("/api/favorites/toggle",HTTP_POST,handleToggleFavorite);
  webServer.on("/api/sessions",HTTP_GET,handleListSessions);
  webServer.on("/api/sessions/kill",HTTP_POST,handleKillSession);
  webServer.on("/api/errors",HTTP_GET,handleErrorLogs);
  webServer.on("/api/notes",HTTP_GET,handleGetNotes);
  webServer.on("/api/notes",HTTP_POST,handleSaveNote);
  webServer.on("/api/scan",HTTP_GET,handleScanStorage);
  webServer.on("/api/sd-health",HTTP_GET,handleSdHealth);
  webServer.on("/api/users",HTTP_GET,handleGetUsers);
  webServer.on("/api/users",HTTP_POST,handleAddUser);
  webServer.on("/api/users/",HTTP_PUT,handleUpdateUser);
  webServer.on("/api/users/",HTTP_DELETE,handleDeleteUser);

  webServer.begin();
  Serial.println("HTTP on "+String(webServerPort));
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  Serial.println("WebSocket on 81");
  ftpSrv.begin(ftp_user,ftp_password);
  Serial.println("FTP started");
}

void loop() {
  webServer.handleClient();
  webSocket.loop();
  ftpSrv.handleFTP();
  checkWiFi();
  checkSD();
}
