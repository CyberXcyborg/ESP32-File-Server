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
int wsClientCount = 0; // Track connected WebSocket clients

// ============== API RATE LIMITER ==============
// Sliding window rate limiter per IP: max 30 requests per 10 seconds
struct RateLimit {
  IPAddress ip;
  unsigned long windowStart;
  int count;
};
#define MAX_RATE_ENTRIES 20
#define RATE_WINDOW_MS 10000UL
#define RATE_MAX_REQUESTS 30
RateLimit rateLimits[MAX_RATE_ENTRIES];
int rateLimitCount = 0;

bool checkRateLimit(IPAddress ip) {
  unsigned long now = millis();
  // Find or create entry
  for (int i = 0; i < rateLimitCount; i++) {
    if (rateLimits[i].ip == ip) {
      if (now - rateLimits[i].windowStart > RATE_WINDOW_MS) {
        rateLimits[i].windowStart = now;
        rateLimits[i].count = 1;
        return true;
      }
      rateLimits[i].count++;
      return rateLimits[i].count <= RATE_MAX_REQUESTS;
    }
  }
  // New entry
  if (rateLimitCount < MAX_RATE_ENTRIES) {
    rateLimits[rateLimitCount].ip = ip;
    rateLimits[rateLimitCount].windowStart = now;
    rateLimits[rateLimitCount].count = 1;
    rateLimitCount++;
  }
  return true;
}

bool wifiConnected = false;
bool accessPointMode = false;
String server_ip = "";
unsigned long lastWiFiCheck = 0;
bool sdOK = true;

// ============== SD HEALTH MONITORING ==============
unsigned long lastHealthCheck = 0;
uint32_t sectorErrors = 0;
uint32_t healthCheckInterval = 600000UL; // 10 minutes
uint32_t totalWriteOps = 0; // Track total write operations for wear estimation
uint32_t totalWriteBytes = 0; // Total bytes written (wear leveling hint)
// Predictive failure detection
uint32_t consecutiveErrors = 0; // Count of sequential check failures
uint32_t totalErrors = 0; // Lifetime error counter
unsigned long firstErrorTime = 0; // When errors started
uint8_t failureRisk = 0; // 0-100% estimated failure risk
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

// ============== BROADCAST STATS UPDATE ==============
void broadcastStatsUpdate() {
  DynamicJsonDocument doc(256);
  doc["event"] = "stats-update";
  doc["sd_free"] = (uint32_t)((SD.totalBytes() - SD.usedBytes()) / 1024);
  doc["sd_used"] = (uint32_t)(SD.usedBytes() / 1024);
  doc["sd_total"] = (uint32_t)(SD.totalBytes() / 1024);
  doc["write_ops"] = totalWriteOps;
  doc["write_mb"] = (uint32_t)(totalWriteBytes / 1048576UL);
  String msg;
  serializeJson(doc, msg);
  webSocket.broadcastTXT(msg);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] WS Disconnected\n", num);
      wsClientCount--;
      break;
    case WStype_CONNECTED:
      {
        wsClientCount++;
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
      consecutiveErrors++;
      totalErrors++;
      if (firstErrorTime == 0) firstErrorTime = millis();
      // Risk increases with consecutive errors and total error history
      failureRisk = min(100, (consecutiveErrors * 15) + min(40, totalErrors * 2));
      sdOK = (consecutiveErrors < 3); // Only mark failed after 3 consecutive misses
      Serial.println("SD health check FAILED (risk: " + String(failureRisk) + "%)");
      // Critical: if risk hits 100%, log and attempt graceful restart
      if (failureRisk >= 100) {
        Serial.println("CRITICAL: SD card failure imminent, rebooting...");
        delay(1000);
        ESP.restart();
      }
    } else {
      sdOK = true;
      if (consecutiveErrors > 0) consecutiveErrors = 0; // Reset on success
      // Decay risk slowly on successful checks
      if (failureRisk > 0 && (millis() - lastHealthCheck > healthCheckInterval * 2)) {
        failureRisk = max(0, (int)failureRisk - 5);
      }
    }
  }
}

void sendSecurityHeaders() {
  webServer.sendHeader("X-Content-Type-Options", "nosniff");
  webServer.sendHeader("X-Frame-Options", "DENY");
  webServer.sendHeader("X-XSS-Protection", "1; mode=block");
  webServer.sendHeader("Referrer-Policy", "no-referrer");
  webServer.sendHeader("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; media-src 'self'; connect-src 'self' ws: wss:");
  webServer.sendHeader("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
  webServer.sendHeader("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
}

void sendError(int code, String msg) {
  sendSecurityHeaders();
  String json = "{"error":""+msg+"","code":"+String(code)+"}";
  webServer.send(code, "application/json", json);
}

// Rate-limited request wrapper
bool isRateLimited() {
  if (!checkRateLimit(webServer.client().remoteIP())) {
    webServer.send(429, "application/json", "{\"error\":\"Too many requests\",\"retry\":10}");
    return true;
  }
  return false;
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
  sendSecurityHeaders();
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
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin" || !checkCsrf(webServer)) { webServer.send(403); return; }
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
  String sortBy = webServer.hasArg("sort") ? webServer.arg("sort") : "name"; // name, size, date
  String sortOrder = webServer.hasArg("order") ? webServer.arg("order") : "asc"; // asc, desc
  DynamicJsonDocument doc(16384);
  doc["username"] = u; doc["userLevel"] = lvl;
  JsonObject st = doc.createNestedObject("storage");
  st["total"] = SD.totalBytes(); st["used"] = SD.usedBytes();
  st["free"] = SD.totalBytes() - SD.usedBytes();
  JsonArray arr = doc.createNestedArray("files");
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { webServer.send(400); return; }
  // Collect files into temp array for sorting
  struct FileEntry { String name; String path; bool isDir; uint64_t size; unsigned long mtime; };
  FileEntry entries[200];
  int count = 0;
  int fc = 0, dc = 0;
  File file;
  while (file = dir.openNextFile() && count < 200) {
    String fn = String(file.name());
    int sl = fn.lastIndexOf('/');
    String name = (sl >= 0) ? fn.substring(sl+1) : fn;
    bool isD = file.isDirectory();
    if (isD) dc++; else fc++;
    entries[count].name = name;
    entries[count].path = path + (path.endsWith("/")?"":"/") + name + (isD?"/":"");
    entries[count].isDir = isD;
    entries[count].size = isD ? 0 : file.size();
    entries[count].mtime = file.fileTime();
    count++;
    file.close();
  }
  dir.close();
  // Sort entries (dirs first, then by criteria)
  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
      bool swap = false;
      // Directories always first
      if (!entries[j].isDir && entries[j+1].isDir) swap = true;
      else if (entries[j].isDir == entries[j+1].isDir) {
        if (sortBy == "size") {
          swap = (sortOrder == "asc") ? (entries[j].size > entries[j+1].size) : (entries[j].size < entries[j+1].size);
        } else if (sortBy == "date") {
          swap = (sortOrder == "asc") ? (entries[j].mtime > entries[j+1].mtime) : (entries[j].mtime < entries[j+1].mtime);
        } else {
          // name sort (case-insensitive)
          String a = entries[j].name; a.toLowerCase();
          String b = entries[j+1].name; b.toLowerCase();
          swap = (sortOrder == "asc") ? (a > b) : (a < b);
        }
      }
      if (swap) { FileEntry tmp = entries[j]; entries[j] = entries[j+1]; entries[j+1] = tmp; }
    }
  }
  for (int i = 0; i < count; i++) {
    JsonObject f = arr.createNestedObject();
    f["name"] = entries[i].name;
    f["path"] = entries[i].path;
    f["type"] = entries[i].isDir ? "dir" : "file";
    f["size"] = entries[i].isDir ? 0 : (uint64_t)entries[i].size;
    f["icon"] = entries[i].isDir ? "📁" : getFileIcon(entries[i].name);
    f["previewable"] = isPreviewable(entries[i].name);
  }
  doc["fileCount"] = fc; doc["dirCount"] = dc;
  doc["sortBy"] = sortBy; doc["sortOrder"] = sortOrder;
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
  String ctype = getContentType(name);
  // ETag based on file size + modification time for caching
  unsigned long fsize = f.size();
  unsigned long fmtime = f.fileTime();
  String etag = "\"" + String(fsize) + "-" + String(fmtime) + "\"";
  webServer.sendHeader("ETag", etag);
  webServer.sendHeader("Cache-Control", "public, max-age=300");
  // If client sends matching If-None-Match, return 304
  if (webServer.hasHeader("If-None-Match") && webServer.header("If-None-Match") == etag) {
    f.close();
    webServer.send(304, "text/plain", "Not Modified");
    logActivity("download-304", path, u);
    return;
  }
  // Auto-detect gzip support from Accept-Encoding header
  bool clientWantsGzip = webServer.hasHeader("Accept-Encoding") && webServer.header("Accept-Encoding").indexOf("gzip") >= 0;
  bool canCompress = shouldCompress(name) && f.size() >= 10240 && f.size() <= 524288ULL;
  if (clientWantsGzip && canCompress) {
    // Serve compressed via gzip
    webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + ".gz\"");
    webServer.sendHeader("Content-Type", "application/gzip");
    webServer.sendHeader("Content-Encoding", "gzip");
    webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer.send(200, "application/gzip", "");
    uint8_t *srcBuf = new uint8_t[f.size()];
    if (srcBuf) {
      f.read(srcBuf, f.size());
      f.close();
      size_t compCap = (size_t)(f.size() + f.size()/100 + 1024);
      uint8_t *compBuf = new uint8_t[compCap];
      if (compBuf) {
        size_t compSize = gzipCompress(srcBuf, (size_t)f.size(), compBuf, compCap);
        if (compSize > 0) {
          webServer.sendContent((const char*)compBuf, compSize);
          logActivity("download-gzip-auto", path+" ("+String(compSize)+"/"+String(f.size())+"B)", u);
        } else {
          webServer.sendContent((const char*)srcBuf, f.size());
          logActivity("download-gzip-auto-failed", path, u);
        }
        delete[] compBuf;
      } else {
        webServer.sendContent((const char*)srcBuf, f.size());
      }
      delete[] srcBuf;
    } else {
      // OOM fallback - stream raw
      webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
      webServer.sendHeader("Content-Type", ctype);
      webServer.streamFile(f, ctype);
      f.close();
      logActivity("download", path, u);
      return;
    }
    webServer.sendContent("");
    return;
  }
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  webServer.sendHeader("Content-Type", ctype);
  webServer.streamFile(f, ctype);
  f.close();
  logActivity("download", path, u);
}

// ============== DOWNLOAD WITH GZIP ==============
// True on-the-fly gzip compression using miniz deflate
// Reads file in chunks, compresses, streams to client
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
  // Stream gzip-compressed file using true miniz deflate
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + ".gz\"");
  webServer.sendHeader("Content-Type", "application/gzip");
  webServer.sendHeader("Content-Encoding", "gzip");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "application/gzip", "");
  // Read entire file into memory for compression (up to 512KB to avoid OOM)
  // For larger files, fall back to uncompressed
  uint64_t fileSize = f.size();
  if (fileSize > 524288ULL) {
    // File too large for memory - stream uncompressed with .gz extension
    uint8_t buf[512];
    while (f.available()) {
      int n = f.read(buf, 512);
      webServer.sendContent((const char*)buf, n);
    }
    f.close();
    webServer.sendContent("");
    logActivity("download-gzip-skipped", path, u);
    return;
  }
  // Read file, compress, stream
  uint8_t *srcBuf = new uint8_t[fileSize];
  if (!srcBuf) {
    f.close();
    webServer.send(500, "text/plain", "Out of memory");
    return;
  }
  f.read(srcBuf, fileSize);
  f.close();
  // Allocate compression buffer (source + 1% overhead + 18 bytes header/trailer)
  size_t compCapacity = (size_t)(fileSize + fileSize / 100 + 1024);
  uint8_t *compBuf = new uint8_t[compCapacity];
  if (!compBuf) {
    delete[] srcBuf;
    webServer.send(500, "text/plain", "Out of memory");
    return;
  }
  size_t compSize = gzipCompress(srcBuf, (size_t)fileSize, compBuf, compCapacity);
  if (compSize > 0) {
    webServer.sendContent((const char*)compBuf, compSize);
    webServer.sendContent("");
    logActivity("download-gzip", path+" ("+String(compSize)+"/"+String(fileSize)+"B)", u);
  } else {
    // Compression failed - send raw
    webServer.sendContent((const char*)srcBuf, fileSize);
    webServer.sendContent("");
    logActivity("download-gzip-failed", path, u);
  }
  delete[] srcBuf;
  delete[] compBuf;
}

// ============== DELETE ==============
void handleDelete() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  // Acquire lock to prevent concurrent deletion corruption
  if (!acquireFileLock(path, 2000)) { webServer.send(423, "text/plain", "File is locked"); return; }
  bool ok = moveToTrash(path);
  releaseFileLock(path);
  if (ok) { logActivity("delete", path, u); broadcastChange("delete", path); webServer.send(200, "text/plain", "Moved to trash"); }
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
  if (dot > 0) { base = newName.substring(0, dot); ext = newName.substring(dot); }
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
  if (moveFile(path, dp)) { logActivity("move", path+" -> "+dp, u); broadcastChange("move", dp); webServer.send(200, "text/plain", "OK"); }
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
  if (copyFile(path, dp)) { logActivity("copy", path+" -> "+dp, u); broadcastChange("copy", dp); webServer.send(200, "text/plain", "OK"); }
  else webServer.send(500, "text/plain", "Failed");
}

// ============== UPLOAD ==============
void handleUploadAuth() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  // Reuse existing session token for upload (no separate token needed)
  String tok;
  if (webServer.hasHeader("Authorization")) {
    String auth = webServer.header("Authorization");
    if (auth.startsWith("Bearer ")) tok = auth.substring(7);
  }
  if (tok.isEmpty() && webServer.hasHeader("Cookie")) {
    String cookies = webServer.header("Cookie");
    int pos = cookies.indexOf("session_token=");
    if (pos != -1) { pos += 14; int end = cookies.indexOf(";", pos); tok = (end != -1) ? cookies.substring(pos, end) : cookies.substring(pos); }
  }
  if (tok.isEmpty()) tok = generateSessionToken(); // Fallback: generate new
  webServer.send(200, "application/json", "{\"token\":\""+tok+"\"}");
}

void handleUpload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { webServer.send(503); return; }
  HTTPUpload& up = webServer.upload();
  static File uf;
  static String upp;
  } else if (up.status == UPLOAD_FILE_START) {
    // Check free space (need at least upload size + 10KB buffer)
    uint64_t free = SD.totalBytes() - SD.usedBytes();
    if (free < up.totalSize + 10240) {
      Serial.println("Upload rejected: not enough space");
      return; // Will timeout on client
    }
    // Quarantine: block dangerous file types
    if (!isUploadSafe(up.filename)) {
      Serial.println("Upload rejected: dangerous file type: " + up.filename);
      return;
    }
    String p = webServer.hasArg("path") ? webServer.arg("path") : "/";
    if (!p.endsWith("/")) p += "/";
    // Sanitize filename: strip path components, block traversal
    String safeName = up.filename;
    int sl = safeName.lastIndexOf('/');
    if (sl >= 0) safeName = safeName.substring(sl + 1);
    sl = safeName.lastIndexOf('\\');
    if (sl >= 0) safeName = safeName.substring(sl + 1);
    safeName.replace("..", "");
    if (safeName.length() == 0 || safeName == "." || safeName == "..") {
      Serial.println("Upload rejected: invalid filename");
      return;
    }
    upp = p + safeName;
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
    totalWriteOps++;
    totalWriteBytes += up.totalSize;
    logActivity("upload", upp+" ("+String(up.totalSize)+"B)", u);
    broadcastChange("upload", upp);
    // Store CRC32 sidecar for integrity verification
    storeFileCRC(upp);
    // Broadcast updated storage stats to all clients
    broadcastStatsUpdate();
    webServer.send(200, "application/json", "{\"ok\":true,\"path\":\""+upp+"\"}");
  }
}

// ============== SHARE ==============
void handleCreateShare() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
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
  if(isRateLimited()) return;
  sendSecurityHeaders();
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
// Serve video with proper range request support and SVG thumbnail placeholder
void handleVideoThumbnail() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  // For ESP32 we can't generate actual video thumbnails, but we:
  // 1. Serve an SVG placeholder with file info for thumbnail requests
  // 2. Support range requests for streaming playback
  File f = SD.open(path, FILE_READ);
  if (!f) { webServer.send(500); return; }
  String name = path.substring(path.lastIndexOf('/')+1);
  String ctype = getContentType(name);
  // ETag for caching based on file size + modification time
  unsigned long fsize = f.size();
  unsigned long fmtime = f.fileTime();
  String etag = "\"" + String(fsize) + "-" + String(fmtime) + "\"";
  webServer.sendHeader("ETag", etag);
  webServer.sendHeader("Cache-Control", "public, max-age=3600");
  // If client sends matching If-None-Match, return 304
  if (webServer.hasHeader("If-None-Match") && webServer.header("If-None-Match") == etag) {
    f.close();
    webServer.send(304, "text/plain", "Not Modified");
    return;
  }
  // Thumbnail request: return SVG placeholder with file info
  if (webServer.hasArg("thumb")) {
    unsigned long thumbSize = fsize;
    String thumbName = name;
    f.close();
    String svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 320 240'>";
    svg += "<defs><linearGradient id='g' x1='0' y1='0' x2='1' y2='1'>";
    svg += "<stop offset='0%' stop-color='#1a1a2e'/><stop offset='100%' stop-color='#16213e'/>";
    svg += "</linearGradient></defs>";
    svg += "<rect width='320' height='240' fill='url(#g)'/>";
    svg += "<rect x='120' y='80' width='80' height='80' rx='8' fill='#0f3460' stroke='#e94560' stroke-width='2'/>";
    svg += "<polygon points='145,95 145,145 185,120' fill='#e94560'/>";
    svg += "<text x='160' y='190' fill='#eee' text-anchor='middle' font-family='sans-serif' font-size='11'>";
    svg += thumbName.substring(0, 20) + (thumbName.length() > 20 ? "..." : "");
    svg += "</text>";
    svg += "<text x='160' y='210' fill='#aaa' text-anchor='middle' font-family='sans-serif' font-size='10'>";
    svg += getFileSize(thumbSize) + " · " + ctype;
    svg += "</text>";
    svg += "</svg>";
    webServer.send(200, "image/svg+xml", svg);
    logActivity("video-thumb", path, u);
    return;
  }
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

// ============== CSRF TOKEN ENDPOINT ==============
void handleCsrfToken() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  // Extract session token to find CSRF
  String tok;
  if (webServer.hasHeader("Authorization")) {
    String auth = webServer.header("Authorization");
    if (auth.startsWith("Bearer ")) tok = auth.substring(7);
  }
  if (tok.isEmpty() && webServer.hasHeader("Cookie")) {
    String cookies = webServer.header("Cookie");
    int pos = cookies.indexOf("session_token=");
    if (pos != -1) { pos += 14; int end = cookies.indexOf(";", pos); tok = (end != -1) ? cookies.substring(pos, end) : cookies.substring(pos); }
  }
  String csrf = "";
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].isActive && sessions[i].token == tok) { csrf = sessions[i].csrfToken; break; }
  }
  DynamicJsonDocument doc(128);
  doc["csrf"] = csrf;
  String out;
  serializeJson(doc, out);
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
  if(!isAuthenticated(webServer,u,lvl)||!checkCsrf(webServer)){webServer.send(403);return;}
  if(!webServer.hasArg("path")){sendError(400,"Bad request");return;}
  String path=webServer.arg("path");
  if(restoreFromTrash(path)){
    logActivity("restore",path,u);
    broadcastChange("restore", path);
    webServer.send(200,"text/plain","Restored");
  }
  else webServer.send(500,"text/plain","Failed");
}
void handleEmptyTrash() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)||!checkCsrf(webServer)){webServer.send(403);return;}
  if(webServer.hasArg("path")){String path=webServer.arg("path");if(SD.exists(path)){File f=SD.open(path);if(f.isDirectory())removeDir(path);else SD.remove(path);f.close();}}
  else{if(SD.exists(TRASH_FOLDER)){removeDir(TRASH_FOLDER);SD.mkdir(TRASH_FOLDER);}}
  logActivity("empty-trash","all",u);broadcastChange("empty-trash","/");webServer.send(200,"text/plain","OK");
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
  // Password strength: min 6 chars, must contain letter and digit
  if(np.length()<6){sendError(400,"Password too short (min 6)");return;}
  bool hasLetter=false,hasDigit=false;
  for(unsigned int i=0;i<np.length();i++){if(isalpha(np[i]))hasLetter=true;if(isdigit(np[i]))hasDigit=true;}
  if(!hasLetter||!hasDigit){sendError(400,"Password must contain letters and digits");return;}
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
  for(JsonObject u:ex["users"]){if(String(u["username"]|"").equals(path)){
    if(doc["password"]&&strlen(doc["password"])>0){
      String np=doc["password"];
      if(np.length()<6){sendError(400,"Password too short (min 6)");return;}
      bool hasLetter=false,hasDigit=false;
      for(unsigned int i=0;i<np.length();i++){if(isalpha(np[i]))hasLetter=true;if(isdigit(np[i]))hasDigit=true;}
      if(!hasLetter||!hasDigit){sendError(400,"Password must contain letters and digits");return;}
      u["password"]=np;
    }
    u["userLevel"]=doc["userLevel"]|u["userLevel"];break;}}
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
  if(isRateLimited()) return;
  // Per-IP brute-force protection
  if(!checkLoginRateLimit(webServer.client().remoteIP())){
    webServer.send(429,"text/html","<html><body style='font-family:sans-serif;padding:40px'><h1>🔒 Too many login attempts</h1><p>Your IP has been temporarily locked for 5 minutes due to too many failed login attempts.</p></body></html>");
    return;
  }
  String err="";
  if(webServer.method()==HTTP_POST){
    String u=webServer.arg("username"),p=webServer.arg("password");
    // Validate login CSRF token (submitted in form body must match cookie)
    String submittedCsrf=webServer.hasArg("csrf")?webServer.arg("csrf"):"";
    String cookieCsrf="";
    if(webServer.hasHeader("Cookie")){
      String cookies=webServer.header("Cookie");
      int pos=cookies.indexOf("login_csrf=");
      if(pos!=-1){pos+=10;int end=cookies.indexOf(";",pos);cookieCsrf=(end!=-1)?cookies.substring(pos,end):cookies.substring(pos);}
    }
    if(submittedCsrf.length()==0 || cookieCsrf.length()==0 || submittedCsrf!=cookieCsrf){
      err="Security token mismatch";
    } else {
      String lvl;
      if(authenticateUser(u,p,lvl)){
        String tok=createSession(u,lvl);
        webServer.sendHeader("Set-Cookie","session_token="+tok+"; Path=/; Max-Age=1800; SameSite=Strict; HttpOnly");
        String r=webServer.hasArg("redirect")?webServer.arg("redirect"):"/";
        webServer.sendHeader("Location",r+"?token="+tok,true);webServer.send(302,"text/plain","");return;
      } else err="Invalid username or password";
    }
  }
  String info=accessPointMode?"AP: "+String(ap_ssid):"IP: "+server_ip;
  // Generate a pre-login CSRF token (sessionless)
  String preLoginCsrf="";
  const char cs[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  for(int i=0;i<16;i++) preLoginCsrf+=cs[esp_random()%62];
  String h=String(login_html);h.replace("%ERROR%",err);h.replace("%INFO%",info);h.replace("%CSRF%",preLoginCsrf);
  // Store for validation (associate with client via cookie)
  webServer.sendHeader("Set-Cookie","login_csrf="+preLoginCsrf+"; Path=/login; SameSite=Strict; HttpOnly");
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
  if(isRateLimited()) return;
  webServer.sendHeader("Cache-Control","no-cache");
  webServer.send(200,"text/html",String(index_html));
}
void handleManifest() {
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send(200, "application/manifest+json", "{\"name\":\"ESP32 File Server\",\"short_name\":\"ESP32-FS\",\"start_url\":\"/\",\"display\":\"standalone\",\"background_color\":\"#0984e3\",\"theme_color\":\"#0984e3\",\"icons\":[]}");
}
void handleRobotsTxt() {
  webServer.send(200, "text/plain", "User-agent: *\nDisallow: /\n");
}
void handleFavicon() {
  // Inline 1x1 transparent PNG to avoid 404
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send(200, "image/x-icon", "");
}

// ============== BATCH OPERATIONS ==============
void handleBatchDelete() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, webServer.arg("plain"));
  JsonArray paths = doc["paths"];
  int ok = 0, fail = 0;
  for (const char* ps : paths) {
    String p = ps;
    if (!SD.exists(p)) { fail++; continue; }
    if (!acquireFileLock(p, 2000)) { fail++; continue; }
    if (moveToTrash(p)) { ok++; logActivity("batch-delete", p, u); broadcastChange("delete", p); }
    else fail++;
    releaseFileLock(p);
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
    if (moveFile(p, dp)) { ok++; logActivity("batch-move", p+" -> "+dp, u); broadcastChange("move", dp); }
    else fail++;
  }
  webServer.send(200, "application/json", "{\"ok\":"+String(ok)+",\"fail\":"+String(fail)+"}");
}

// ============== BATCH COPY ==============
void handleBatchCopy() {
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
    if (copyFile(p, dp)) { ok++; logActivity("batch-copy", p+" -> "+dp, u); broadcastChange("copy", dp); }
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
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin" || !checkCsrf(webServer)) { webServer.send(403); return; }
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

// ============== SPACE USAGE BREAKDOWN ==============
// Returns per-subfolder disk usage for analytics dashboard
void handleSpaceUsage() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  String path = webServer.hasArg("path") ? webServer.arg("path") : "/";
  DynamicJsonDocument doc(2048);
  doc["total"] = (uint32_t)(SD.totalBytes() / 1024);
  doc["used"] = (uint32_t)(SD.usedBytes() / 1024);
  doc["free"] = (uint32_t)((SD.totalBytes() - SD.usedBytes()) / 1024);
  JsonArray arr = doc.createNestedArray("breakdown");
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { webServer.send(400, "text/plain", "Invalid dir"); return; }
  File file;
  while (file = dir.openNextFile()) {
    String fn = String(file.name());
    int sl = fn.lastIndexOf('/');
    String name = (sl >= 0) ? fn.substring(sl+1) : fn;
    if (file.isDirectory()) {
      uint64_t sz = getDirSize(fn);
      JsonObject entry = arr.createNestedObject();
      entry["name"] = name;
      entry["size_kb"] = (uint32_t)(sz / 1024);
      entry["size_fmt"] = getFileSize(sz);
      entry["is_dir"] = true;
    } else {
      JsonObject entry = arr.createNestedObject();
      entry["name"] = name;
      entry["size_kb"] = (uint32_t)(file.size() / 1024);
      entry["size_fmt"] = getFileSize(file.size());
      entry["is_dir"] = false;
    }
    file.close();
  }
  dir.close();
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
  doc["ws_clients"] = wsClientCount;
  // Add SD health stats
  doc["sd_sector_errors"] = sectorErrors;
  doc["sd_health_interval"] = healthCheckInterval / 1000;
  doc["sd_wear_pct"] = totalWriteOps > 0 ? min(100, (int)(totalWriteOps / 10000UL)) : 0;
  doc["sd_write_mb"] = (uint32_t)(totalWriteBytes / 1048576UL);
  doc["sd_failure_risk"] = failureRisk;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== STORE CRC AFTER UPLOAD ==============
void storeFileCRC(String path) {
  // Compute and save CRC32 to a sidecar file for later integrity verification
  String crc = getFileCRC32(path);
  if (crc.length() == 0) return;
  String crcPath = path + ".crc32";
  File f = SD.open(crcPath, FILE_WRITE);
  if (f) { f.print(crc); f.close(); }
}

// ============== FILE CRC32 INTEGRITY ==============
void handleFileCRC() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = sanitizePath(webServer.arg("path"));
  if (!SD.exists(path)) { webServer.send(404); return; }
  // Check for stored CRC file first
  String crcPath = path + ".crc32";
  String crcStored = "";
  if (SD.exists(crcPath)) {
    File cf = SD.open(crcPath, FILE_READ);
    if (cf) { crcStored = cf.readString().trim(); cf.close(); }
  }
  String crcComputed = getFileCRC32(path);
  DynamicJsonDocument doc(512);
  doc["path"] = path;
  File sf = SD.open(path, FILE_READ);
  doc["size"] = sf ? sf.size() : 0;
  if (sf) sf.close();
  doc["crc32"] = crcComputed;
  doc["stored_crc32"] = crcStored.length() > 0 ? crcStored : (char*)nullptr;
  doc["match"] = crcStored.length() == 0 || crcStored == crcComputed;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== DUPLICATE FILE FINDER ==============
void handleFindDuplicates() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  if (!sdOK) { sendError(503,"SD card not available"); return; }
  // Scan for duplicates: group by size first, then CRC
  // Limit scan to avoid memory issues - max 500 files
  struct FileInfo { String path; uint32_t size; };
  FileInfo files[500];
  int fileCount = 0;
  // Collect all files (one level deep from each dir)
  File dir = SD.open("/");
  if (!dir) { sendError(500,"Cannot open root"); return; }
  File file;
  while (file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      if (fileCount < 500) {
        files[fileCount].path = String(file.name());
        files[fileCount].size = file.size();
        fileCount++;
      }
    } else {
      String fn = String(file.name());
      if (fn.startsWith("/.")) { file.close(); continue; }
      File sub = SD.open(fn);
      if (sub) {
        File sf;
        while (sf = sub.openNextFile()) {
          if (!sf.isDirectory() && fileCount < 500) {
            files[fileCount].path = String(sf.name());
            files[fileCount].size = sf.size();
            fileCount++;
          }
          sf.close();
        }
        sub.close();
      }
    }
    file.close();
  }
  dir.close();
  // Find duplicates: same size files
  DynamicJsonDocument doc(8192);
  JsonArray groups = doc.createNestedArray("duplicates");
  int dupGroups = 0;
  for (int i = 0; i < fileCount && dupGroups < 20; i++) {
    if (files[i].size == 0) continue;
    // Check if any other file has same size
    bool found = false;
    for (int j = i + 1; j < fileCount; j++) {
      if (files[j].size == files[i].size) {
        if (!found) {
          // First match - verify with CRC
          String crc1 = getFileCRC32(files[i].path);
          String crc2 = getFileCRC32(files[j].path);
          if (crc1.length() > 0 && crc1 == crc2) {
            found = true;
            JsonObject g = groups.createNestedObject();
            g["size"] = files[i].size;
            g["size_formatted"] = getFileSize((uint64_t)files[i].size);
            g["crc32"] = crc1;
            JsonArray paths = g.createNestedArray("files");
            paths.add(files[i].path);
            paths.add(files[j].path);
            dupGroups++;
          }
        } else {
          // Additional duplicate - check CRC matches the group
          String crc = getFileCRC32(files[j].path);
          JsonObject g = groups[dupGroups - 1];
          if (crc == g["crc32"]) {
            g["files"].add(files[j].path);
          }
        }
      }
    }
    // Mark matched files to avoid re-processing
    if (found) {
      for (int j = i + 1; j < fileCount; j++) {
        if (files[j].size == files[i].size) files[j].size = 0;
      }
    }
  }
  doc["groups"] = dupGroups;
  doc["files_scanned"] = fileCount;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== STORAGE ANALYTICS ==============
// ============== RECENT FILES ==============
void handleRecentFiles() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  if (!sdOK) { sendError(503,"SD card not available"); return; }
  int maxCount = webServer.hasArg("limit") ? webServer.arg("limit").toInt() : 20;
  if (maxCount > 100) maxCount = 100;
  // Collect files with modification time
  struct FInfo { String path; unsigned long mtime; uint64_t size; };
  FInfo *files = new FInfo[200];
  int fileCount = 0;
  // Recursive scan limited to 200 files for memory safety
  std::function<void(String)> scanDir = [&](String path) {
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) return;
    File file;
    while (file = dir.openNextFile()) {
      if (fileCount < 200) {
        if (!file.isDirectory()) {
          files[fileCount].path = String(file.name());
          files[fileCount].mtime = file.fileTime();
          files[fileCount].size = file.size();
          fileCount++;
        } else {
          String fn = String(file.name());
          if (!fn.startsWith("/.")) scanDir(fn);
        }
      }
      file.close();
    }
    dir.close();
  };
  scanDir("/");
  // Simple bubble sort by mtime descending
  for (int i = 0; i < fileCount - 1; i++) {
    for (int j = 0; j < fileCount - i - 1; j++) {
      if (files[j].mtime < files[j+1].mtime) {
        FInfo tmp = files[j]; files[j] = files[j+1]; files[j+1] = tmp;
      }
    }
  }
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("recent");
  int count = fileCount < maxCount ? fileCount : maxCount;
  for (int i = 0; i < count; i++) {
    JsonObject f = arr.createNestedObject();
    String name = files[i].path;
    int sl = name.lastIndexOf('/');
    f["name"] = (sl >= 0) ? name.substring(sl+1) : name;
    f["path"] = files[i].path;
    f["size"] = (uint64_t)files[i].size;
    f["sizeFormatted"] = getFileSize((uint64_t)files[i].size);
    f["modified"] = files[i].mtime;
    f["icon"] = getFileIcon(name);
  }
  doc["total"] = fileCount;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
  delete[] files;
}

// ============== SIMPLE MARKDOWN TO HTML ==============
// Minimal markdown rendering for .md preview (headers, bold, italic, code, links, lists)
String simpleMarkdown(String md) {
  String html = "";
  html.reserve(md.length() + 256);
  bool inCodeBlock = false;
  bool inList = false;
  int len = md.length();
  for (int i = 0; i < len; i++) {
    // Code block ```
    if (md[i] == '`' && i+2 < len && md[i+1] == '`' && md[i+2] == '`') {
      if (inCodeBlock) { html += "</code></pre>"; inCodeBlock = false; }
      else { html += "<pre><code>"; inCodeBlock = true; }
      i += 2;
      continue;
    }
    if (inCodeBlock) {
      if (md[i] == '<') html += "&lt;";
      else if (md[i] == '>') html += "&gt;";
      else html += md[i];
      continue;
    }
    // Skip list markers
    if (md[i] == '-' || md[i] == '*') {
      if (i == 0 || md[i-1] == '\n') {
        if (!inList) { html += "<ul>"; inList = true; }
        html += "<li>";
        i++; // skip space
        continue;
      }
    }
    if (inList && md[i] == '\n') {
      // Check if next line is also a list item
      if (i+1 < len && md[i+1] != '-' && md[i+1] != '*') {
        html += "</ul>"; inList = false;
      }
    }
    // Bold **text**
    if (md[i] == '*' && i+1 < len && md[i+1] == '*') {
      html += "<strong>";
      i += 2;
      while (i+1 < len && !(md[i] == '*' && md[i+1] == '*')) html += md[i++];
      html += "</strong>";
      i++; // skip closing *
      continue;
    }
    // Italic *text*
    if (md[i] == '*') {
      html += "<em>";
      i++;
      while (i < len && md[i] != '*') html += md[i++];
      html += "</em>";
      continue;
    }
    // Inline code
    if (md[i] == '`') {
      html += "<code>";
      i++;
      while (i < len && md[i] != '`') {
        if (md[i] == '<') html += "&lt;";
        else if (md[i] == '>') html += "&gt;";
        else html += md[i++];
      }
      html += "</code>";
      continue;
    }
    // Links [text](url)
    if (md[i] == '[') {
      int close = md.indexOf(']', i);
      if (close > 0 && close+1 < len && md[close+1] == '(') {
        int paren = md.indexOf(')', close+2);
        if (paren > 0) {
          String text = md.substring(i+1, close);
          String url = md.substring(close+2, paren);
          html += "<a href=\"" + url + "\" target=\"_blank\">" + text + "</a>";
          i = paren;
          continue;
        }
      }
    }
    // Escape HTML
    if (md[i] == '<') html += "&lt;";
    else if (md[i] == '>') html += "&gt;";
    else html += md[i];
  }
  if (inList) html += "</ul>";
  if (inCodeBlock) html += "</code></pre>";
  return html;
}

// ============== MARKDOWN PREVIEW ENDPOINT ==============
void handleMarkdownPreview() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = sanitizePath(webServer.arg("path"));
  if (!SD.exists(path)) { webServer.send(404); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { webServer.send(500); return; }
  String name = path.substring(path.lastIndexOf('/')+1);
  String ext = name.substring(name.lastIndexOf('.')+1);
  ext.toLowerCase();
  if (ext != "md") { f.close(); webServer.send(400, "text/plain", "Not a markdown file"); return; }
  // Limit to 64KB
  size_t toRead = f.size() < 65536 ? f.size() : 65536;
  String content = "";
  content.reserve(toRead);
  uint8_t buf[512];
  size_t read = 0;
  while (read < toRead && f.available()) {
    size_t chunk = toRead - read;
    if (chunk > 512) chunk = 512;
    int n = f.read(buf, chunk);
    if (n <= 0) break;
    for (int i = 0; i < n; i++) {
      char c = buf[i];
      if (c >= 32 && c < 127) content += c;
      else if (c == '\n') content += '\n';
      else if (c == '\t') content += '\t';
      else if (c == '\r') content += '\r';
      else content += '.';
    }
    read += n;
  }
  f.close();
  String html = simpleMarkdown(content);
  String out = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  out += "<style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;max-width:800px;margin:20px auto;padding:0 16px;line-height:1.6;color:#333}";
  out += "pre{background:#f4f4f4;padding:12px;border-radius:6px;overflow-x:auto}code{background:#f0f0f0;padding:2px 6px;border-radius:3px;font-size:0.9em}";
  out += "a{color:#0984e3;text-decoration:none}a:hover{text-decoration:underline}";
  out += "h1,h2,h3{color:#2d3436}ul{padding-left:24px}</style></head><body>";
  out += html;
  out += "</body></html>";
  webServer.send(200, "text/html", out);
  logActivity("md-preview", path, u);
}

// ============== FILE PREVIEW ==============
void handleFilePreview() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = sanitizePath(webServer.arg("path"));
  if (!SD.exists(path)) { webServer.send(404); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { webServer.send(500); return; }
  String name = path.substring(path.lastIndexOf('/')+1);
  String ext = name.substring(name.lastIndexOf('.')+1);
  ext.toLowerCase();
  // Only allow preview for safe text types
  if (ext != "txt" && ext != "md" && ext != "csv" && ext != "log" && ext != "json" && 
      ext != "xml" && ext != "html" && ext != "htm" && ext != "css" && ext != "js" &&
      ext != "ini" && ext != "cfg" && ext != "conf" && ext != "sh" && ext != "py" &&
      ext != "c" && ext != "cpp" && ext != "h") {
    webServer.send(403, "application/json", "{\"error\":\"Preview not allowed for this type\"}");
    f.close();
    return;
  }
  // Limit preview to 64KB
  size_t maxBytes = 65536;
  size_t fileSize = f.size();
  size_t toRead = fileSize < maxBytes ? fileSize : maxBytes;
  String content = "";
  content.reserve(toRead);
  uint8_t buf[512];
  size_t read = 0;
  while (read < toRead && f.available()) {
    size_t chunk = toRead - read;
    if (chunk > 512) chunk = 512;
    int n = f.read(buf, chunk);
    if (n <= 0) break;
    // Sanitize non-printable chars for JSON safety
    for (int i = 0; i < n; i++) {
      char c = buf[i];
      if (c >= 32 && c < 127) content += c;
      else if (c == '\n') content += '\n';
      else if (c == '\t') content += '\t';
      else if (c == '\r') content += '\r';
      else content += '.';
    }
    read += n;
  }
  f.close();
  DynamicJsonDocument doc(65536 + 512);
  doc["name"] = name;
  doc["path"] = path;
  doc["size"] = (uint64_t)fileSize;
  doc["sizeFormatted"] = getFileSize((uint64_t)fileSize);
  doc["truncated"] = fileSize > maxBytes;
  doc["content"] = content;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
  logActivity("preview", path, u);
}

// ============== STORAGE ANALYTICS ==============
void handleStorageAnalytics() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401,"Not authenticated"); return; }
  if (!sdOK) { sendError(503,"SD card not available"); return; }
  DynamicJsonDocument doc(8192);
  // File type distribution
  uint32_t imgCount=0, vidCount=0, audCount=0, docCount=0, arcCount=0, codeCount=0, otherCount=0;
  uint64_t imgSize=0, vidSize=0, audSize=0, docSize=0, arcSize=0, codeSize=0, otherSize=0;
  uint32_t totalFiles=0; uint64_t totalSize=0;
  // Recursive scan from root
  File dir = SD.open("/");
  if (!dir || !dir.isDirectory()) { sendError(500,"Cannot open root"); return; }
  File file;
  while (file = dir.openNextFile()) {
    if (file.isDirectory()) {
      // Skip hidden/system dirs
      String fn = String(file.name());
      if (fn.startsWith("/.")) { file.close(); continue; }
      // Count files in subdir (one level)
      File sub = SD.open(fn);
      if (sub) {
        File sf;
        while (sf = sub.openNextFile()) {
          if (!sf.isDirectory()) {
            totalFiles++; totalSize += sf.size();
            String name = String(sf.name());
            int dot = name.lastIndexOf('.');
            String ext = (dot >= 0) ? name.substring(dot+1).toLowerCase() : "";
            if (ext=="jpg"||ext=="jpeg"||ext=="png"||ext=="gif"||ext=="bmp"||ext=="svg"||ext=="webp") { imgCount++; imgSize+=sf.size(); }
            else if (ext=="mp4"||ext=="avi"||ext=="mov"||ext=="mkv"||ext=="webm") { vidCount++; vidSize+=sf.size(); }
            else if (ext=="mp3"||ext=="wav"||ext=="ogg"||ext=="flac"||ext=="aac") { audCount++; audSize+=sf.size(); }
            else if (ext=="pdf"||ext=="doc"||ext=="docx"||ext=="txt"||ext=="md"||ext=="rtf") { docCount++; docSize+=sf.size(); }
            else if (ext=="zip"||ext=="rar"||ext=="7z"||ext=="tar"||ext=="gz") { arcCount++; arcSize+=sf.size(); }
            else if (ext=="c"||ext=="cpp"||ext=="h"||ext=="py"||ext=="js"||ext=="html"||ext=="css"||ext=="json"||ext=="xml") { codeCount++; codeSize+=sf.size(); }
            else { otherCount++; otherSize+=sf.size(); }
          }
          sf.close();
        }
        sub.close();
      }
    } else {
      totalFiles++; totalSize += file.size();
      String name = String(file.name());
      int dot = name.lastIndexOf('.');
      String ext = (dot >= 0) ? name.substring(dot+1).toLowerCase() : "";
      if (ext=="jpg"||ext=="jpeg"||ext=="png"||ext=="gif"||ext=="bmp"||ext=="svg"||ext=="webp") { imgCount++; imgSize+=file.size(); }
      else if (ext=="mp4"||ext=="avi"||ext=="mov"||ext=="mkv"||ext=="webm") { vidCount++; vidSize+=file.size(); }
      else if (ext=="mp3"||ext=="wav"||ext=="ogg"||ext=="flac"||ext=="aac") { audCount++; audSize+=file.size(); }
      else if (ext=="pdf"||ext=="doc"||ext=="docx"||ext=="txt"||ext=="md"||ext=="rtf") { docCount++; docSize+=file.size(); }
      else if (ext=="zip"||ext=="rar"||ext=="7z"||ext=="tar"||ext=="gz") { arcCount++; arcSize+=file.size(); }
      else if (ext=="c"||ext=="cpp"||ext=="h"||ext=="py"||ext=="js"||ext=="html"||ext=="css"||ext=="json"||ext=="xml") { codeCount++; codeSize+=file.size(); }
      else { otherCount++; otherSize+=file.size(); }
    }
    file.close();
  }
  dir.close();
  doc["total_files"] = totalFiles;
  doc["total_size"] = (uint64_t)totalSize;
  doc["total_size_formatted"] = getFileSize((uint64_t)totalSize);
  JsonObject breakdown = doc.createNestedObject("breakdown");
  JsonObject cat;
  cat = breakdown.createNestedObject("images"); cat["count"]=imgCount; cat["size"]=(uint64_t)imgSize; cat["size_fmt"]=getFileSize((uint64_t)imgSize);
  cat = breakdown.createNestedObject("video"); cat["count"]=vidCount; cat["size"]=(uint64_t)vidSize; cat["size_fmt"]=getFileSize((uint64_t)vidSize);
  cat = breakdown.createNestedObject("audio"); cat["count"]=audCount; cat["size"]=(uint64_t)audSize; cat["size_fmt"]=getFileSize((uint64_t)audSize);
  cat = breakdown.createNestedObject("documents"); cat["count"]=docCount; cat["size"]=(uint64_t)docSize; cat["size_fmt"]=getFileSize((uint64_t)docSize);
  cat = breakdown.createNestedObject("archives"); cat["count"]=arcCount; cat["size"]=(uint64_t)arcSize; cat["size_fmt"]=getFileSize((uint64_t)arcSize);
  cat = breakdown.createNestedObject("code"); cat["count"]=codeCount; cat["size"]=(uint64_t)codeSize; cat["size_fmt"]=getFileSize((uint64_t)codeSize);
  cat = breakdown.createNestedObject("other"); cat["count"]=otherCount; cat["size"]=(uint64_t)otherSize; cat["size_fmt"]=getFileSize((uint64_t)otherSize);
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
  // Wear leveling stats
  doc["total_write_ops"] = totalWriteOps;
  doc["total_write_mb"] = (uint32_t)(totalWriteBytes / 1048576UL);
  doc["wear_percent"] = totalWriteOps > 0 ? min(100, (int)(totalWriteOps / 10000UL)) : 0; // Rough estimate: 10K ops = 1% wear
  // Predictive failure detection
  doc["failure_risk"] = failureRisk;
  doc["consecutive_errors"] = consecutiveErrors;
  doc["total_errors"] = totalErrors;
  doc["health_trend"] = consecutiveErrors > 0 ? "degrading" : (failureRisk > 50 ? "warning" : "healthy");
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
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
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
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
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
  webServer.on("/robots.txt",handleRobotsTxt);
  webServer.on("/favicon.ico",handleFavicon);
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
  webServer.on("/api/upload",HTTP_POST,[](){if(!checkCsrf(webServer)){webServer.send(403,"text/plain","CSRF invalid");return;}webServer.send(200);},handleUpload);
  webServer.on("/api/share",HTTP_POST,handleCreateShare);
  webServer.on("/api/zip",HTTP_POST,handleZipDownload);
  webServer.on("/api/video",HTTP_GET,handleVideoThumbnail);
  webServer.on("/api/batch-delete",HTTP_POST,handleBatchDelete);
  webServer.on("/api/batch-move",HTTP_POST,handleBatchMove);
  webServer.on("/api/batch-copy",HTTP_POST,handleBatchCopy);
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
  webServer.on("/api/csrf",HTTP_GET,handleCsrfToken);
  webServer.on("/api/ota-status",HTTP_GET,handleOtaStatus);
  webServer.on("/api/ota-upload",HTTP_POST,[](){webServer.send(200);},handleOtaUpload);
  webServer.on("/api/reboot",HTTP_POST,handleReboot);
  webServer.on("/api/export-log",HTTP_GET,handleExportLog);
  webServer.on("/api/stats",HTTP_GET,handleStats);
  webServer.on("/api/list-filtered",HTTP_GET,handleListFilesFiltered);
  webServer.on("/api/storage",HTTP_GET,handleStorageBreakdown);
  webServer.on("/api/space-usage",HTTP_GET,handleSpaceUsage);
  webServer.on("/api/favorites",HTTP_GET,handleGetFavorites);
  webServer.on("/api/favorites/toggle",HTTP_POST,handleToggleFavorite);
  webServer.on("/api/sessions",HTTP_GET,handleListSessions);
  webServer.on("/api/sessions/kill",HTTP_POST,handleKillSession);
  webServer.on("/api/errors",HTTP_GET,handleErrorLogs);
  webServer.on("/api/notes",HTTP_GET,handleGetNotes);
  webServer.on("/api/notes",HTTP_POST,handleSaveNote);
  webServer.on("/api/scan",HTTP_GET,handleScanStorage);
  webServer.on("/api/sd-health",HTTP_GET,handleSdHealth);
  webServer.on("/api/analytics",HTTP_GET,handleStorageAnalytics);
  webServer.on("/api/crc",HTTP_GET,handleFileCRC);
  webServer.on("/api/duplicates",HTTP_GET,handleFindDuplicates);
  webServer.on("/api/recent",HTTP_GET,handleRecentFiles);
  webServer.on("/api/preview",HTTP_GET,handleFilePreview);
  webServer.on("/api/md-preview",HTTP_GET,handleMarkdownPreview);
  webServer.on("/api/users",HTTP_GET,handleGetUsers);
  webServer.on("/api/users",HTTP_POST,handleAddUser);
  webServer.on("/api/users/",HTTP_PUT,handleUpdateUser);
  webServer.on("/api/users/",HTTP_DELETE,handleDeleteUser);

  // Add security headers to all responses
  webServer.onNotFound([](){
    webServer.sendHeader("X-Content-Type-Options", "nosniff");
    webServer.sendHeader("X-Frame-Options", "DENY");
    webServer.send(404, "text/plain", "Not Found");
  });
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
  // Periodic session cleanup (every 5 minutes)
  static unsigned long lastSessionCleanup = 0;
  if (millis() - lastSessionCleanup > 300000UL) {
    lastSessionCleanup = millis();
    unsigned long now = millis();
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (sessions[i].isActive && (now - sessions[i].lastActivity > SESSION_TIMEOUT)) {
        sessions[i].isActive = false;
        Serial.println("Session expired: " + sessions[i].username);
      }
    }
  }
}
