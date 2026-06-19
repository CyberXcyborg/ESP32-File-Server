/*
 * ESP32 File Server v2.0
 * (c) 2024 CyberXcyborg
 * 
 * Features:
 * - Web file manager with drag & drop upload
 * - File preview (images, text, audio, video)
 * - File/folder rename
 * - Search & sort (name, size, date)
 * - Dark mode
 * - Trash / recycle bin
 * - Storage usage dashboard
 * - Multi-user authentication
 * - FTP server
 * - WiFi + AP mode
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
    Serial.println("\nAttempt " + String(attempts) + " failed");
    if (attempts < 3) { WiFi.disconnect(); delay(1000); WiFi.begin(ssid, password); }
  }
  Serial.println("\nWiFi failed, starting AP...");
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

// ============== API HANDLERS ==============

void handleServerInfo() {
  String mode = accessPointMode ? "Access Point" : "WiFi Client";
  String json = "{\"ip\":\"" + server_ip + "\",\"port\":" + String(webServerPort);
  json += ",\"ftp_user\":\"" + String(ftp_user) + "\"";
  json += ",\"ftp_password\":\"" + String(ftp_password) + "\"";
  json += ",\"mode\":\"" + mode + "\"";
  json += ",\"hostname\":\"esp32files.local\"";
  json += ",\"version\":\"" + String(FIRMWARE_VERSION) + "\"}";
  webServer.send(200, "application/json", json);
}

void handleListFiles() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  String path = webServer.hasArg("path") ? webServer.arg("path") : "/";
  if (path.length() == 0) path = "/";

  DynamicJsonDocument doc(8192);
  doc["username"] = username;
  doc["userLevel"] = userLevel;

  // Storage info
  uint64_t total = SD.totalBytes();
  uint64_t used = SD.usedBytes();
  JsonObject storage = doc.createNestedObject("storage");
  storage["total"] = total;
  storage["used"] = used;

  JsonArray arr = doc.createNestedArray("files");
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { webServer.send(400, "application/json", "{\"error\":\"Invalid path\"}"); return; }

  File file;
  while (file = dir.openNextFile()) {
    String fname = String(file.name());
    int slash = fname.lastIndexOf('/');
    String name = (slash >= 0) ? fname.substring(slash + 1) : fname;
    bool isDir = file.isDirectory();
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

  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

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
}

void handleDelete() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  // Move to trash instead of permanent delete
  if (moveToTrash(path)) {
    webServer.send(200, "text/plain", "Moved to trash");
  } else {
    webServer.send(500, "text/plain", "Failed to move to trash");
  }
}

void handleCreateDir() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (createDirRecursive(path)) {
    webServer.send(200, "text/plain", "OK");
  } else {
    webServer.send(500, "text/plain", "Failed");
  }
}

void handleRename() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path") || !webServer.hasArg("name")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  String newName = webServer.arg("name");
  if (!SD.exists(path)) { webServer.send(404); return; }
  // Build new path
  int slash = path.lastIndexOf('/');
  String parent = (slash > 0) ? path.substring(0, slash + 1) : "/";
  String newPath = parent + newName;
  if (SD.rename(path, newPath)) {
    webServer.send(200, "text/plain", "OK");
  } else {
    webServer.send(500, "text/plain", "Failed");
  }
}

void handleUploadAuth() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  String token = generateSessionToken();
  // Store upload token in sessions
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
    if (SD.exists(uploadPath)) SD.remove(uploadPath);
    uploadFile = SD.open(uploadPath, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    Serial.println("Upload: " + uploadPath + " (" + String(upload.totalSize) + " bytes)");
  }
}

// ============== TRASH HANDLERS ==============

void handleTrashPage() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) {
    webServer.sendHeader("Location", "/login"); webServer.send(302); return;
  }
  // Simple trash listing page
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1.0'><title>Trash</title>";
  html += "<style>:root{--bg:#f5f6fa;--card:#fff;--text:#2d3436;--primary:#0984e3;--danger:#d63031;--border:#dfe6e9}";
  html += "*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',system-ui,sans-serif}";
  html += "body{background:var(--bg);color:var(--text);padding:20px}";
  html += ".container{max-width:800px;margin:0 auto;background:var(--card);border-radius:8px;padding:20px;box-shadow:0 2px 10px rgba(0,0,0,0.08)}";
  html += "h1{color:var(--primary);margin-bottom:15px;font-size:20px}";
  html += ".btn{background:var(--primary);color:#fff;border:none;padding:8px 14px;border-radius:6px;cursor:pointer;font-size:13px;text-decoration:none;display:inline-block;margin-right:5px}";
  html += ".btn-danger{background:var(--danger)}.back{margin-bottom:15px;display:inline-block}";
  html += ".item{padding:10px;border-bottom:1px solid var(--border);display:flex;justify-content:space-between;align-items:center}";
  html += ".empty{padding:30px;text-align:center;color:#999}</style></head><body>";
  html += "<div class='container'><a href='/' class='back btn'>← Back to Files</a><h1>🗑️ Trash</h1>";

  if (!SD.exists(TRASH_FOLDER)) {
    html += "<div class='empty'>Trash is empty</div>";
  } else {
    File dir = SD.open(TRASH_FOLDER);
    if (!dir) {
      html += "<div class='empty'>Cannot open trash folder</div>";
    } else {
      File f;
      int count = 0;
      while (f = dir.openNextFile()) {
        String name = String(f.name());
        int slash = name.lastIndexOf('/');
        String shortName = (slash >= 0) ? name.substring(slash + 1) : name;
        html += "<div class='item'><span>" + shortName + "</span><span>";
        html += "<a class='btn' href='/api/restore?path=" + name + "'>♻️ Restore</a> ";
        html += "<a class='btn btn-danger' href='/api/empty-trash?path=" + name + "'>❌ Delete</a>";
        html += "</span></div>";
        f.close();
        count++;
      }
      dir.close();
      if (count == 0) html += "<div class='empty'>Trash is empty</div>";
      else html += "<div style='margin-top:15px'><a class='btn btn-danger' href='/api/empty-trash'>Empty All Trash</a></div>";
    }
  }
  html += "</div></body></html>";
  webServer.send(200, "text/html", html);
}

void handleRestore() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.send(401); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (restoreFromTrash(path)) webServer.send(200, "text/plain", "Restored");
  else webServer.send(500, "text/plain", "Failed");
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
    // Empty all trash
    if (SD.exists(TRASH_FOLDER)) {
      removeDir(TRASH_FOLDER);
      SD.mkdir(TRASH_FOLDER);
    }
  }
  webServer.sendHeader("Location", "/trash"); webServer.send(302);
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
  // Strip passwords for API response
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
  // Read existing
  File f = SD.open(USERS_FILE);
  String content = "{\"users\":[]}"; if (f) { content = ""; while (f.available()) content += (char)f.read(); f.close(); }
  DynamicJsonDocument existing(1024);
  deserializeJson(existing, content);
  JsonArray users = existing["users"];
  JsonObject u = users.createNestedObject();
  u["username"] = newUser; u["password"] = newPass; u["userLevel"] = newLevel;
  f = SD.open(USERS_FILE, FILE_WRITE); if (!f) { webServer.send(500); return; }
  serializeJson(existing, f); f.close();
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
  webServer.send(200, "text/plain", "OK");
}

void handleUserManagementPage() {
  String username, userLevel;
  if (!isAuthenticated(webServer, username, userLevel)) { webServer.sendHeader("Location", "/login"); webServer.send(302); return; }
  if (userLevel != "admin") { webServer.sendHeader("Location", "/"); webServer.send(302); return; }
  // Serve user management HTML (embedded minimal version)
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1.0'><title>User Management</title>";
  html += "<style>:root{--bg:#f5f6fa;--card:#fff;--text:#2d3436;--primary:#0984e3;--danger:#d63031;--border:#dfe6e9;--success:#00b894}";
  html += "*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',system-ui,sans-serif}";
  html += "body{background:var(--bg);color:var(--text);padding:20px}";
  html += ".container{max-width:700px;margin:0 auto;background:var(--card);border-radius:8px;padding:20px;box-shadow:0 2px 10px rgba(0,0,0,0.08)}";
  html += "h1{color:var(--primary);margin-bottom:15px;font-size:20px}";
  html += ".btn{background:var(--primary);color:#fff;border:none;padding:8px 14px;border-radius:6px;cursor:pointer;font-size:13px;text-decoration:none;display:inline-block;margin-right:5px}";
  html += ".btn-danger{background:var(--danger)}.btn-ghost{background:transparent;color:var(--text);border:1px solid var(--border)}";
  html += "table{width:100%;border-collapse:collapse;margin-top:15px}";
  html += "th,td{padding:10px;text-align:left;border-bottom:1px solid var(--border);font-size:13px}";
  html += "th{background:var(--primary);color:#fff}";
  html += ".modal{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.5);z-index:100;justify-content:center;align-items:center}";
  html += ".modal-content{background:var(--card);border-radius:8px;padding:25px;width:90%;max-width:400px}";
  html += "input,select{width:100%;padding:10px;border:1px solid var(--border);border-radius:6px;font-size:14px;margin-top:5px}";
  html += "label{font-size:13px;font-weight:500}";
  html += ".form-group{margin-bottom:12px}";
  html += ".toast{position:fixed;bottom:20px;right:20px;padding:10px 18px;border-radius:6px;color:#fff;font-size:13px;opacity:0;transition:opacity .3s}";
  html += ".toast.show{opacity:1}.toast.success{background:var(--success)}.toast.error{background:var(--danger)}</style></head><body>";
  html += "<div class='container'><a href='/' class='btn btn-ghost'>← Back</a><h1>👥 User Management</h1>";
  html += "<button class='btn' onclick='showModal()'>+ Add User</button>";
  html += "<table><thead><tr><th>Username</th><th>Role</th><th>Actions</th></tr></thead><tbody id='userTable'></tbody></table></div>";
  html += "<div class='modal' id='userModal'><div class='modal-content'>";
  html += "<h2 id='modalTitle' style='margin-bottom:15px'>Add User</h2>";
  html += "<div class='form-group'><label>Username</label><input type='text' id='uName'></div>";
  html += "<div class='form-group'><label>Password</label><input type='password' id='uPass'></div>";
  html += "<div class='form-group'><label>Role</label><select id='uRole'><option value='user'>User</option><option value='admin'>Admin</option></select></div>";
  html += "<div style='display:flex;gap:8px;justify-content:flex-end;margin-top:15px'>";
  html += "<button class='btn btn-ghost' onclick='closeM()'>Cancel</button><button class='btn' onclick='saveUser()'>Save</button></div></div></div>";
  html += "<div class='toast' id='toast'></div>";
  html += "<script>const tok=getToken();let users=[];";
  html += "function getToken(){const c=document.cookie.split(';');for(let x of c){const[n,v]=x.trim().split('=');if(n==='session_token')return v;}return '';}";
  html += "function load(){fetch('/api/users',{headers:{'Authorization':'Bearer '+tok}}).then(r=>r.json()).then(d=>{users=d.users||[];render();});}";
  html += "function render(){let h='';users.forEach((u,i)=>{h+=`<tr><td>${u.username}</td><td>${u.userLevel}</td><td><button class='btn' onclick='edit(${i})'>✏️</button> <button class='btn btn-danger' onclick='del(${i})'>🗑️</button></td></tr>`;});document.getElementById('userTable').innerHTML=h;}";
  html += "function showModal(){document.getElementById('modalTitle').textContent='Add User';document.getElementById('uName').value='';document.getElementById('uPass').value='';document.getElementById('uRole').value='user';document.getElementById('uName').disabled=false;document.getElementById('userModal').style.display='flex';}";
  html += "function closeM(){document.getElementById('userModal').style.display='none';}";
  html += "function edit(i){document.getElementById('modalTitle').textContent='Edit User';document.getElementById('uName').value=users[i].username;document.getElementById('uPass').value='';document.getElementById('uRole').value=users[i].userLevel;document.getElementById('uName').disabled=true;document.getElementById('userModal').style.display='flex';}";
  html += "function saveUser(){const n=document.getElementById('uName').value,p=document.getElementById('uPass').value,r=document.getElementById('uRole').value;";
  html += "const isEdit=document.getElementById('uName').disabled;";
  html += "const body={username:n,userLevel:r};if(p)body.password=p;";
  html += "const url=isEdit?'/api/users/'+n:'/api/users';const method=isEdit?'PUT':'POST';";
  html += "fetch(url,{method:method,headers:{'Content-Type':'application/json','Authorization':'Bearer '+tok},body:JSON.stringify(body)}).then(r=>{if(r.ok){closeM();load();showToast('Saved','success');}else showToast('Failed','error');});}";
  html += "function del(i){if(!confirm('Delete user "'+users[i].username+'"?'))return;";
  html += "fetch('/api/users/'+users[i].username,{method:'DELETE',headers:{'Authorization':'Bearer '+tok}}).then(r=>{if(r.ok){load();showToast('Deleted','success');}else showToast('Failed','error');});}";
  html += "function showToast(m,t){const el=document.getElementById('toast');el.textContent=m;el.className='toast '+t+' show';setTimeout(()=>el.classList.remove('show'),3000);}";
  html += "load();</script></body></html>";
  webServer.send(200, "text/html", html);
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

// ============== SETUP / LOOP ==============

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nESP32 File Server v" + String(FIRMWARE_VERSION));

  if (!initSDCard()) {
    Serial.println("SD Card failed!");
  } else {
    Serial.println("SD Card OK");
  }

  setupAuthentication();
  connectToWiFi();

  // Web routes
  webServer.on("/", handleRoot);
  webServer.on("/login", handleLogin);
  webServer.on("/logout", handleLogout);
  webServer.on("/server-info", handleServerInfo);
  webServer.on("/users", handleUserManagementPage);
  webServer.on("/trash", handleTrashPage);

  // API routes
  webServer.on("/api/list", HTTP_GET, handleListFiles);
  webServer.on("/api/download", HTTP_GET, handleDownload);
  webServer.on("/api/delete", HTTP_DELETE, handleDelete);
  webServer.on("/api/create-dir", HTTP_POST, handleCreateDir);
  webServer.on("/api/rename", HTTP_POST, handleRename);
  webServer.on("/api/upload-auth", HTTP_GET, handleUploadAuth);
  webServer.on("/api/upload", HTTP_POST, []() { webServer.send(200); }, handleUpload);
  webServer.on("/api/restore", HTTP_GET, handleRestore);
  webServer.on("/api/empty-trash", HTTP_GET, handleEmptyTrash);
  webServer.on("/api/users", HTTP_GET, handleGetUsers);
  webServer.on("/api/users", HTTP_POST, handleAddUser);
  webServer.on("/api/users/", HTTP_PUT, handleUpdateUser);
  webServer.on("/api/users/", HTTP_DELETE, handleDeleteUser);

  webServer.begin();
  Serial.println("Web server started on port " + String(webServerPort));

  // FTP
  ftpSrv.begin(ftp_user, ftp_password);
  Serial.println("FTP server started");
}

void loop() {
  webServer.handleClient();
  ftpSrv.handleFTP();
}
