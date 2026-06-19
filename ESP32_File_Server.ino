/*
 * ESP32 File Server v3.0
 * (c) 2024 CyberXcyborg
 * 
 * v3.0 Changes:
 * - File move/copy between folders
 * - Folder upload support
 * - Large file chunked upload
 * - Settings page (WiFi config from web UI)
 * - Keyboard shortcuts (Del, Ctrl+A, F2, Esc)
 * - File info panel
 * - Mobile UI improvements
 * - Download selected as ZIP
 * - Activity logging
 * - File versioning
 * - Share links
 * - PWA support
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SimpleFTPServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <ArduinoJson.h>

#include "config.h"
#include "auth.h"
#include "file_ops.h"
#include "web_ui.h"

WebServer webServer(webServerPort);
FtpServer ftpSrv;

bool wifiConnected = false;
bool accessPointMode = false;
String server_ip = "";

// ============== WIFI ==============
bool connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  int attempts = 0;
  while (attempts < 3) {
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
      delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
      server_ip = WiFi.localIP().toString();
      wifiConnected = true;
      accessPointMode = false;
      if (MDNS.begin("esp32files")) {
        Serial.println("mDNS: http://esp32files.local");
        MDNS.addService("http", "tcp", webServerPort);
      }
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
  Serial.println("AP IP: " + server_ip);
  wifiConnected = false;
  accessPointMode = true;
  if (MDNS.begin("esp32files")) {
    Serial.println("mDNS: http://esp32files.local");
    MDNS.addService("http", "tcp", webServerPort);
  }
}

// ============== SERVER INFO ==============
void handleServerInfo() {
  String mode = accessPointMode ? "Access Point" : "WiFi Client";
  String json = "{\"ip\":\"" + server_ip + "\",\"port\":" + String(webServerPort);
  json += ",\"mode\":\"" + mode + "\"";
  json += ",\"hostname\":\"esp32files.local\"";
  json += ",\"version\":\"" + String(FIRMWARE_VERSION) + "\"}";
  webServer.send(200, "application/json", json);
}

// ============== API: LIST FILES ==============
void handleListFiles() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  String path = webServer.hasArg("path") ? webServer.arg("path") : "/";
  if (path.length() == 0) path = "/";

  DynamicJsonDocument doc(16384);
  doc["username"] = username;
  doc["userLevel"] = userLevel;

  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  JsonObject storage = doc.createNestedObject("storage");
  storage["total"] = total;
  storage["used"] = used;

  JsonArray arr = doc.createNestedArray("files");
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { webServer.send(400, "application/json", "{\"error\":\"Invalid path\"}"); return; }

  int fileCount = 0, dirCount = 0;
  File file;
  while (file = dir.openNextFile()) {
    String fname = String(file.name());
    int slash = fname.lastIndexOf('/');
    String name = (slash >= 0) ? fname.substring(slash + 1) : fname;
    bool isDir = file.isDirectory();
    if (isDir) dirCount++; else fileCount++;
    JsonObject f = arr.createNestedObject();
    f["name"] = name;
    f["path"] = path + (path.endsWith("/") ? "" : "/") + name + (isDir ? "/" : "");
    f["type"] = isDir ? "dir" : "file";
    f["size"] = isDir ? 0 : file.size();
    f["icon"] = isDir ? "📁" : getFileIcon(name);
    f["previewable"] = isPreviewable(name);
    file.close();
  }
  dir.close();
  doc["fileCount"] = fileCount;
  doc["dirCount"] = dirCount;

  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== API: FILE INFO ==============
void handleFileInfo() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  
  File f = SD.open(path);
  if (!f) { webServer.send(500); return; }
  
  DynamicJsonDocument doc(512);
  doc["name"] = path.substring(path.lastIndexOf('/') + 1);
  doc["path"] = path;
  doc["type"] = f.isDirectory() ? "dir" : "file";
  doc["size"] = f.isDirectory() ? 0 : f.size();
  doc["sizeFormatted"] = f.isDirectory() ? "-" : getFileSize(f.size());
  f.close();
  
  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== API: DOWNLOAD ==============
void handleDownload() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { webServer.send(500); return; }
  String name = path.substring(path.lastIndexOf('/') + 1);
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  webServer.streamFile(f, "application/octet-stream");
  f.close();
  logActivity("download", path, username);
}

// ============== API: DELETE ==============
void handleDelete() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  if (moveToTrash(path)) {
    logActivity("delete", path, username);
    webServer.send(200, "text/plain", "Moved to trash");
  } else {
    webServer.send(500, "text/plain", "Failed");
  }
}

// ============== API: CREATE DIR ==============
void handleCreateDir() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (createDirRecursive(path)) {
    logActivity("mkdir", path, username);
    webServer.send(200, "text/plain", "OK");
  } else {
    webServer.send(500, "text/plain", "Failed");
  }
}

// ============== API: RENAME (with versioning) ==============
void handleRename() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path") || !webServer.hasArg("name")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  String newName = webServer.arg("name");
  if (!SD.exists(path)) { webServer.send(404); return; }
  if (!SD.open(path).isDirectory()) createVersion(path);
  int slash = path.lastIndexOf('/');
  String parent = (slash > 0) ? path.substring(0, slash + 1) : "/";
  String newPath = parent + newName;
  if (SD.rename(path, newPath)) {
    logActivity("rename", path + " -> " + newPath, username);
    webServer.send(200, "text/plain", "OK");
  } else {
    webServer.send(500, "text/plain", "Failed");
  }
}

// ============== API: MOVE ==============
void handleMove() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path") || !webServer.hasArg("dest")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  String dest = webServer.arg("dest");
  if (!SD.exists(path)) { webServer.send(404); return; }
  
  // Build destination path
  String destPath;
  File destDir = SD.open(dest);
  if (destDir && destDir.isDirectory()) {
    // dest is a directory, move file into it
    String name = path.substring(path.lastIndexOf('/') + 1);
    destPath = dest + (dest.endsWith("/") ? "" : "/") + name;
    destDir.close();
  } else {
    destPath = dest;
  }
  
  if (moveFile(path, destPath)) {
    logActivity("move", path + " -> " + destPath, username);
    webServer.send(200, "text/plain", "OK");
  } else {
    webServer.send(500, "text/plain", "Failed");
  }
}

// ============== API: COPY ==============
void handleCopy() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path") || !webServer.hasArg("dest")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  String dest = webServer.arg("dest");
  if (!SD.exists(path)) { webServer.send(404); return; }
  
  String destPath;
  File destDir = SD.open(dest);
  if (destDir && destDir.isDirectory()) {
    String name = path.substring(path.lastIndexOf('/') + 1);
    destPath = dest + (dest.endsWith("/") ? "" : "/") + name;
    destDir.close();
  } else {
    destPath = dest;
  }
  
  if (copyFile(path, destPath)) {
    logActivity("copy", path + " -> " + destPath, username);
    webServer.send(200, "text/plain", "OK");
  } else {
    webServer.send(500, "text/plain", "Failed");
  }
}

// ============== API: UPLOAD ==============
void handleUploadAuth() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  String token = generateSessionToken();
  createSession(username + "_upload", userLevel);
  webServer.send(200, "application/json", "{\"token\":\"" + token + "\"}");
}

void handleUpload() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }

  HTTPUpload& upload = webServer.upload();
  static File uploadFile;
  static String uploadPath;

  if (upload.status == UPLOAD_FILE_START) {
    String path = webServer.hasArg("path") ? webServer.arg("path") : "/";
    if (!path.endsWith("/")) path += "/";
    uploadPath = path + upload.filename;
    if (SD.exists(uploadPath)) createVersion(uploadPath);
    else SD.remove(uploadPath);
    uploadFile = SD.open(uploadPath, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    logActivity("upload", uploadPath + " (" + String(upload.totalSize) + "B)", username);
  }
}

// ============== API: SHARE ==============
void handleCreateShare() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  String token;
  if (createShare(path, token)) {
    logActivity("share", path, username);
    String url = "http://" + server_ip + "/s/" + token;
    webServer.send(200, "application/json", "{\"url\":\"" + url + "\",\"token\":\"" + token + "\"}");
  } else {
    webServer.send(500, "text/plain", "Failed");
  }
}

void handleSharedFile() {
  String token = webServer.pathArg(0);
  String path;
  if (!getSharePath(token, path)) { webServer.send(404, "text/plain", "Link expired or invalid"); return; }
  if (!SD.exists(path)) { webServer.send(404, "text/plain", "File not found"); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { webServer.send(500); return; }
  String name = path.substring(path.lastIndexOf('/') + 1);
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  webServer.streamFile(f, "application/octet-stream");
  f.close();
}

// ============== API: SETTINGS ==============
void handleGetSettings() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel) || userLevel != "admin") { webServer.send(403); return; }
  DynamicJsonDocument doc(512);
  doc["wifi_ssid"] = String(ssid);
  doc["ap_ssid"] = String(ap_ssid);
  doc["ftp_user"] = String(ftp_user);
  doc["version"] = FIRMWARE_VERSION;
  doc["ip"] = server_ip;
  doc["mode"] = accessPointMode ? "AP" : "WiFi";
  doc["uptime"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  // Load saved settings if exist
  if (SD.exists(SETTINGS_FILE)) {
    File f = SD.open(SETTINGS_FILE, FILE_READ);
    if (f) {
      DynamicJsonDocument saved(512);
      deserializeJson(saved, f);
      f.close();
      if (saved["wifi_ssid"]) doc["saved_wifi_ssid"] = saved["wifi_ssid"].as<String>();
      if (saved["ap_ssid"]) doc["saved_ap_ssid"] = saved["ap_ssid"].as<String>();
    }
  }
  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleSaveSettings() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel) || userLevel != "admin") { webServer.send(403); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(512);
  deserializeJson(doc, webServer.arg("plain"));
  
  String wifiSsid = doc["wifi_ssid"] | String(ssid);
  String wifiPass = doc["wifi_pass"] | String(password);
  String apSsid = doc["ap_ssid"] | String(ap_ssid);
  String apPass = doc["ap_pass"] | String(ap_password);
  String ftpUser = doc["ftp_user"] | String(ftp_user);
  String ftpPass = doc["ftp_pass"] | String(ftp_password);
  
  if (saveSettings(wifiSsid, wifiPass, apSsid, apPass, ftpUser, ftpPass)) {
    logActivity("settings", "updated", username);
    webServer.send(200, "application/json", "{\"ok\":true,\"msg\":\"Settings saved. Reboot to apply WiFi changes.\"}");
  } else {
    webServer.send(500, "text/plain", "Failed to save");
  }
}

// ============== API: ACTIVITY LOG ==============
void handleGetLog() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel) || userLevel != "admin") { webServer.send(403); return; }
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("log");
  if (SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_READ);
    if (f) {
      String lines[50];
      int count = 0;
      String line = "";
      while (f.available()) {
        char c = f.read();
        if (c == '\n') { lines[count % 50] = line; count++; line = ""; }
        else line += c;
      }
      f.close();
      int start = count > 50 ? count - 50 : 0;
      for (int i = start; i < count; i++) {
        String l = lines[i % 50];
        int c1 = l.indexOf(','), c2 = l.indexOf(',', c1+1), c3 = l.indexOf(',', c2+1);
        if (c1 > 0 && c2 > 0 && c3 > 0) {
          JsonObject e = arr.createNestedObject();
          e["time"] = l.substring(0, c1).toInt();
          e["user"] = l.substring(c1+1, c2);
          e["action"] = l.substring(c2+1, c3);
          e["path"] = l.substring(c3+1);
        }
      }
    }
  }
  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== TRASH ==============
void handleTrashPage() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.sendHeader("Location", "/login"); webServer.send(302); return; }
  webServer.send(200, "text/html", String(index_html));
}

void handleTrashList() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("files");
  if (SD.exists(TRASH_FOLDER)) {
    File dir = SD.open(TRASH_FOLDER);
    if (dir) {
      File f;
      while (f = dir.openNextFile()) {
        String name = String(f.name());
        int slash = name.lastIndexOf();
        String shortName = (slash >= 0) ? name.substring(slash + 1) : name;
        JsonObject item = arr.createNestedObject();
        item["name"] = shortName;
        item["path"] = name;
        item["type"] = f.isDirectory() ? "dir" : "file";
        item["size"] = f.isDirectory() ? 0 : f.size();
        f.close();
      }
      dir.close();
    }
  }
  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleRestore() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (restoreFromTrash(path)) {
    logActivity("restore", path, username);
    webServer.send(200, "text/plain", "Restored");
  } else {
    webServer.send(500, "text/plain", "Failed");
  }
}

void handleEmptyTrash() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (webServer.hasArg("path")) {
    String path = webServer.arg("path");
    if (SD.exists(path)) {
      File f = SD.open(path);
      if (f.isDirectory()) removeDir(path);
      else SD.remove(path);
      f.close();
    }
  } else {
    if (SD.exists(TRASH_FOLDER)) { removeDir(TRASH_FOLDER); SD.mkdir(TRASH_FOLDER); }
  }
  logActivity("empty-trash", "all", username);
  webServer.send(200, "text/plain", "OK");
}

// ============== USER MANAGEMENT ==============
void handleGetUsers() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel) || userLevel != "admin") { webServer.send(403); return; }
  File f = SD.open(USERS_FILE);
  if (!f) { webServer.send(500); return; }
  String content = "";
  while (f.available()) content += (char)f.read();
  f.close();
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, content);
  JsonArray users = doc["users"];
  DynamicJsonDocument out(1024);
  JsonArray arr = out.createNestedArray("users");
  for (JsonObject u : users) {
    JsonObject o = arr.createNestedObject();
    o["username"] = u["username"];
    o["userLevel"] = u["userLevel"];
  }
  String result;
  serializeJson(out, result);
  webServer.send(200, "application/json", result);
}

void handleAddUser() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel) || userLevel != "admin") { webServer.send(403); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(256);
  deserializeJson(doc, webServer.arg("plain"));
  String newUser = doc["username"] | "";
  String newPass = doc["password"] | "";
  String newLevel = doc["userLevel"] | "user";
  if (newUser.length() == 0 || newPass.length() == 0) { webServer.send(400); return; }
  File f = SD.open(USERS_FILE);
  String content = "{\"users\":[]}"; if (f) { content = ""; while (f.available()) content += (char)f.read(); f.close(); }
  DynamicJsonDocument existing(1024);
  deserializeJson(existing, content);
  JsonArray users = existing["users"];
  JsonObject u = users.createNestedObject();
  u["username"] = newUser; u["password"] = newPass; u["userLevel"] = newLevel;
  f = SD.open(USERS_FILE, FILE_WRITE); if (!f) { webServer.send(500); return; }
  serializeJson(existing, f); f.close();
  logActivity("add-user", newUser, username);
  webServer.send(200, "text/plain", "OK");
}

void handleUpdateUser() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel) || userLevel != "admin") { webServer.send(403); return; }
  String path = webServer.pathArg(0);
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(256);
  deserializeJson(doc, webServer.arg("plain"));
  File f = SD.open(USERS_FILE);
  String content = "{\"users\":[]}"; if (f) { content = ""; while (f.available()) content += (char)f.read(); f.close(); }
  DynamicJsonDocument existing(1024);
  deserializeJson(existing, content);
  JsonArray users = existing["users"];
  for (JsonObject u : users) {
    if (String(u["username"]|"").equals(path)) {
      if (doc["password"] && strlen(doc["password"]) > 0) u["password"] = doc["password"];
      u["userLevel"] = doc["userLevel"] | u["userLevel"];
      break;
    }
  }
  f = SD.open(USERS_FILE, FILE_WRITE); if (!f) { webServer.send(500); return; }
  serializeJson(existing, f); f.close();
  webServer.send(200, "text/plain", "OK");
}

void handleDeleteUser() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel) || userLevel != "admin") { webServer.send(403); return; }
  String path = webServer.pathArg(0);
  File f = SD.open(USERS_FILE);
  String content = "{\"users\":[]}"; if (f) { content = ""; while (f.available()) content += (char)f.read(); f.close(); }
  DynamicJsonDocument existing(1024);
  deserializeJson(existing, content);
  JsonArray users = existing["users"];
  for (int i = 0; i < users.size(); i++) {
    if (String(users[i]["username"]|"").equals(path)) { users.remove(i); break; }
  }
  f = SD.open(USERS_FILE, FILE_WRITE); if (!f) { webServer.send(500); return; }
  serializeJson(existing, f); f.close();
  logActivity("delete-user", path, username);
  webServer.send(200, "text/plain", "OK");
}

void handleUserManagementPage() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.sendHeader("Location", "/login"); webServer.send(302); return; }
  if (userLevel != "admin") { webServer.sendHeader("Location", "/"); webServer.send(302); return; }
  webServer.send(200, "text/html", String(index_html));
}

// ============== LOGIN / LOGOUT ==============
void handleLogin() {
  String errorMessage = "";
  if (webServer.method() == HTTP_POST) {
    String username = webServer.arg("username");
    String password = webServer.arg("password");
    String userLevel;
    if (authenticateUser(username, password, userLevel)) {
      String token = createSession(username, userLevel);
      webServer.sendHeader("Set-Cookie", "session_token=" + token + "; Path=/; Max-Age=1800; SameSite=Strict");
      String redirectUrl = webServer.hasArg("redirect") ? webServer.arg("redirect") : "/";
      webServer.sendHeader("Location", redirectUrl + "?token=" + token, true);
      webServer.send(302, "text/plain", "");
      return;
    } else {
      errorMessage = "Invalid username or password";
    }
  }
  String info = accessPointMode ? "AP: " + String(ap_ssid) : "IP: " + server_ip;
  String html = String(login_html);
  html.replace("%ERROR%", errorMessage);
  html.replace("%INFO%", info);
  webServer.send(200, "text/html", html);
}

void handleLogout() {
  String username, userLevel;
  if (isAuthenticated(webServer, username, userLevel)) {
    String token;
    if (webServer.hasHeader("Authorization")) token = webServer.header("Authorization").substring(7);
    else if (webServer.hasArg("token")) token = webServer.arg("token");
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (sessions[i].isActive && sessions[i].token == token) { sessions[i].isActive = false; break; }
    }
  }
  webServer.sendHeader("Set-Cookie", "session_token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
  webServer.sendHeader("Location", "/login");
  webServer.send(302);
}

void handleRoot() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.sendHeader("Location", "/login"); webServer.send(302); return;
  }
  webServer.send(200, "text/html", String(index_html));
}

// ============== PWA MANIFEST ==============
void handleManifest() {
  String json = "{\"name\":\"ESP32 File Server\",\"short_name\":\"ESP32-FS\",\"start_url\":\"/\",\"display\":\"standalone\",\"background_color\":\"#0984e3\",\"theme_color\":\"#0984e3\",\"icons\":[]}";
  webServer.send(200, "application/manifest+json", json);
}

// ============== SETUP / LOOP ==============
void setup() {
  Serial.begin(115200);
  Serial.println("\n\nESP32 File Server v" + String(FIRMWARE_VERSION));

  if (!initSDCard()) {
    Serial.println("SD Card failed!");
  } else {
    Serial.println("SD Card OK");
  }

  loadSettings();
  setupAuthentication();
  connectToWiFi();

  // Web routes
  webServer.on("/", handleRoot);
  webServer.on("/login", handleLogin);
  webServer.on("/logout", handleLogout);
  webServer.on("/server-info", handleServerInfo);
  webServer.on("/users", handleUserManagementPage);
  webServer.on("/trash", handleTrashPage);
  webServer.on("/manifest.json", handleManifest);
  webServer.on("/s/", HTTP_GET, handleSharedFile);

  // API routes
  webServer.on("/api/list", HTTP_GET, handleListFiles);
  webServer.on("/api/info", HTTP_GET, handleFileInfo);
  webServer.on("/api/download", HTTP_GET, handleDownload);
  webServer.on("/api/delete", HTTP_DELETE, handleDelete);
  webServer.on("/api/create-dir", HTTP_POST, handleCreateDir);
  webServer.on("/api/rename", HTTP_POST, handleRename);
  webServer.on("/api/move", HTTP_POST, handleMove);
  webServer.on("/api/copy", HTTP_POST, handleCopy);
  webServer.on("/api/upload-auth", HTTP_GET, handleUploadAuth);
  webServer.on("/api/upload", HTTP_POST, []() { webServer.send(200); }, handleUpload);
  webServer.on("/api/share", HTTP_POST, handleCreateShare);
  webServer.on("/api/trash", HTTP_GET, handleTrashList);
  webServer.on("/api/restore", HTTP_GET, handleRestore);
  webServer.on("/api/empty-trash", HTTP_GET, handleEmptyTrash);
  webServer.on("/api/log", HTTP_GET, handleGetLog);
  webServer.on("/api/settings", HTTP_GET, handleGetSettings);
  webServer.on("/api/settings", HTTP_POST, handleSaveSettings);
  webServer.on("/api/users", HTTP_GET, handleGetUsers);
  webServer.on("/api/users", HTTP_POST, handleAddUser);
  webServer.on("/api/users/", HTTP_PUT, handleUpdateUser);
  webServer.on("/api/users/", HTTP_DELETE, handleDeleteUser);

  webServer.begin();
  Serial.println("Web server started on port " + String(webServerPort));

  ftpSrv.begin(ftp_user, ftp_password);
  Serial.println("FTP server started");
}

void loop() {
  webServer.handleClient();
  ftpSrv.handleFTP();
}
