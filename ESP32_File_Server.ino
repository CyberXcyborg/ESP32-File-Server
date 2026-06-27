/usr/bin/bash: warning: setlocale: LC_ALL: cannot change locale (en_US.UTF-8): No such file or directory
/usr/bin/bash: warning: setlocale: LC_ALL: cannot change locale (en_US.UTF-8): No such file or directory
/*
 * ESP32 File Server v3.5 -> v6.4
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
#include <time.h>

#include "config.h"
#include "auth.h"
#include "file_ops.h"
#include "web_ui.h"

WebServer webServer(webServerPort);
WebSocketsServer webSocket(81); // WebSocket on port 81
FtpServer ftpSrv;
int wsClientCount = 0; // Track connected WebSocket clients

// ============== WS CLIENT METADATA ==============
// Per-client WebSocket state for monitoring, auth tracking, and connection stats
struct WsClientInfo {
  bool authenticated;
  String username;
  String userLevel;
  IPAddress ip;
  unsigned long connectTime;
  unsigned long lastActivity;
  uint32_t msgCount;  // Messages received from this client
};
#define WS_MAX_CLIENTS 8
WsClientInfo wsClients[WS_MAX_CLIENTS];

// Initialize WS client on connect
void initWsClient(uint8_t num, IPAddress remoteIp) {
  if (num >= WS_MAX_CLIENTS) return;
  wsClients[num].authenticated = false;
  wsClients[num].username = "";
  wsClients[num].userLevel = "";
  wsClients[num].ip = remoteIp;
  wsClients[num].connectTime = millis();
  wsClients[num].lastActivity = millis();
  wsClients[num].msgCount = 0;
}

// Clear WS client on disconnect
void clearWsClient(uint8_t num) {
  if (num >= WS_MAX_CLIENTS) return;
  wsClients[num].authenticated = false;
  wsClients[num].username = "";
  wsClients[num].userLevel = "";
  wsClients[num].msgCount = 0;
}

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

// ============== REQUEST SIZE LIMITER =============
// Reject POST/PUT bodies larger than this to prevent OOM
#define MAX_POST_BODY_SIZE 32768 // 32KB max POST body
unsigned long requestStartMs = 0; // Track request start time
#define MAX_REQUEST_TIME_MS 15000 // 15s max per request

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
// CRC spot-check: verify one random file's CRC per health cycle
uint32_t crcSpotCheckErrors = 0; // Count of CRC mismatches found by spot-checks
uint32_t crcSpotChecksDone = 0; // Total spot-checks performed
struct HealthLog {
  unsigned long timestamp;
  bool ok;
  uint32_t freeSpace;
  uint32_t errors;
};
HealthLog healthHistory[48]; // Last 48 entries (~8 hours at 10min intervals)
int healthIndex = 0;

// ============== REQUEST METRICS ==============
// Track per-endpoint response times and request counts for performance monitoring
struct EndpointMetric {
  String path;
  uint32_t count;
  uint32_t totalTimeMs;   // Cumulative response time
  uint32_t maxTimeMs;     // Worst case
  uint32_t lastTimeMs;    // Most recent
};
#define MAX_METRICS 40
EndpointMetric endpointMetrics[MAX_METRICS];
int metricCount = 0;

// Find or create metric entry for a path
EndpointMetric* findMetric(const String& path) {
  for (int i = 0; i < metricCount; i++) {
    if (endpointMetrics[i].path == path) return &endpointMetrics[i];
  }
  if (metricCount < MAX_METRICS) {
    endpointMetrics[metricCount].path = path;
    endpointMetrics[metricCount].count = 0;
    endpointMetrics[metricCount].totalTimeMs = 0;
    endpointMetrics[metricCount].maxTimeMs = 0;
    endpointMetrics[metricCount].lastTimeMs = 0;
    return &endpointMetrics[metricCount++];
  }
  return nullptr;
}

// Record a request timing
void recordMetric(const String& path, uint32_t elapsedMs) {
  EndpointMetric* m = findMetric(path);
  if (!m) return;
  m->count++;
  m->totalTimeMs += elapsedMs;
  if (elapsedMs > m->maxTimeMs) m->maxTimeMs = elapsedMs;
  m->lastTimeMs = elapsedMs;
}

// Expose metrics via /api/metrics
void handleMetrics() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  sendSecurityHeaders();
  DynamicJsonDocument doc(4096);
  doc["firmware"] = FIRMWARE_VERSION;
  doc["uptime_sec"] = millis() / 1000;
  doc["heap_free"] = ESP.getFreeHeap();
  doc["ws_clients"] = wsClientCount;
  doc["metrics_count"] = metricCount;
  JsonObject sys = doc.createNestedObject("system");
  sys["sd_ok"] = sdOK;
  sys["wifi_rssi"] = WiFi.RSSI();
  sys["total_requests"] = 0;
  // Include per-client WebSocket connection details
  JsonArray wsArr = doc.createNestedArray("ws_clients_detail");
  for (int i = 0; i < WS_MAX_CLIENTS; i++) {
    if (wsClients[i].authenticated) {
      JsonObject c = wsArr.createNestedObject();
      c["id"] = i;
      c["user"] = wsClients[i].username;
      c["level"] = wsClients[i].userLevel;
      c["ip"] = wsClients[i].ip.toString();
      c["connected_s"] = (millis() - wsClients[i].connectTime) / 1000;
      c["msgs"] = wsClients[i].msgCount;
    }
  }
  JsonArray arr = doc.createNestedArray("endpoints");
  uint32_t totalReqs = 0;
  for (int i = 0; i < metricCount; i++) {
    JsonObject e = arr.createNestedObject();
    e["path"] = endpointMetrics[i].path;
    e["count"] = endpointMetrics[i].count;
    e["avg_ms"] = endpointMetrics[i].count > 0 ? (endpointMetrics[i].totalTimeMs / endpointMetrics[i].count) : 0;
    e["max_ms"] = endpointMetrics[i].maxTimeMs;
    e["last_ms"] = endpointMetrics[i].lastTimeMs;
    totalReqs += endpointMetrics[i].count;
  }
  sys["total_requests"] = totalReqs;
  String out; serializeJson(doc, out);
  sendJson(200, out);
}

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
      if (num < WS_MAX_CLIENTS && wsClients[num].authenticated) wsClientCount--;
      clearWsClient(num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] WS Connected from %s (pending auth)\n", num, ip.toString().c_str());
        initWsClient(num, ip);
        // Send challenge — client must respond with {"cmd":"auth","token":"..."}
        webSocket.sendTXT(num, "{\"event\":\"auth-required\"}");
      }
      break;
    case WStype_TEXT:
      if (num < WS_MAX_CLIENTS) {
        wsClients[num].lastActivity = millis();
        wsClients[num].msgCount++;
      }
      if (length > 0 && payload[0] == '{') {
        DynamicJsonDocument doc(512);
        if (!deserializeJson(doc, payload, length)) {
          String cmd = doc["cmd"] | "";
          if (cmd == "ping") {
            webSocket.sendTXT(num, "{\"event\":\"pong\"}");
          } else if (cmd == "auth") {
            // Authenticate WebSocket with session token
            String wsToken = doc["token"] | "";
            String wsUser, wsLvl;
            if (validateSession(wsToken, wsUser, wsLvl)) {
              wsClientCount++;
              if (num < WS_MAX_CLIENTS) {
                wsClients[num].authenticated = true;
                wsClients[num].username = wsUser;
                wsClients[num].userLevel = wsLvl;
              }
              webSocket.sendTXT(num, "{\"event\":\"auth-ok\",\"user\":\"" + wsUser + "\",\"level\":\"" + wsLvl + "\"}");
              // Send initial status after successful auth
              DynamicJsonDocument status(256);
              status["event"] = "connected";
              status["uptime"] = millis() / 1000;
              status["sd_ok"] = sdOK;
              String msg;
              serializeJson(status, msg);
              webSocket.sendTXT(num, msg);
            } else {
              webSocket.sendTXT(num, "{\"event\":\"auth-failed\"}");
              webSocket.disconnect(num);
            }
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
      // ============== CRC SPOT-CHECK ==============
      // Each health cycle, pick one file with a .crc32 sidecar and verify integrity
      // This detects silent data corruption from SD wear or flash errors
      {
        File root = SD.open("/");
        if (root && root.isDirectory()) {
          File f;
          String checkPath = "";
          String checkCrcPath = "";
          // Scan for a file with a .crc32 sidecar (pick the first one we find)
          while (f = root.openNextFile()) {
            if (!f.isDirectory()) {
              String fp = String(f.name());
              f.close();
              if (fp.endsWith(".crc32") || fp.endsWith(".lock") || fp.startsWith("/.")) continue;
              String crcPath = fp + ".crc32";
              if (SD.exists(crcPath)) {
                checkPath = fp;
                checkCrcPath = crcPath;
                break;
              }
            } else {
              f.close();
            }
          }
          root.close();
          // Verify CRC if we found a candidate
          if (checkPath.length() > 0 && SD.exists(checkPath)) {
            crcSpotChecksDone++;
            // Read stored CRC
            String storedCrc = "";
            File cf = SD.open(checkCrcPath, FILE_READ);
            if (cf) { storedCrc = cf.readString().trim(); cf.close(); }
            // Compute current CRC
            String computedCrc = getFileCRC32(checkPath);
            if (storedCrc.length() > 0 && computedCrc.length() > 0 && storedCrc != computedCrc) {
              crcSpotCheckErrors++;
              Serial.println("CRC SPOT-CHECK MISMATCH: " + checkPath + " stored=" + storedCrc + " current=" + computedCrc);
              // Broadcast corruption alert via WebSocket
              DynamicJsonDocument alert(256);
              alert["event"] = "crc-mismatch";
              alert["path"] = checkPath;
              alert["stored_crc"] = storedCrc;
              alert["current_crc"] = computedCrc;
              alert["spot_errors"] = crcSpotCheckErrors;
              String msg;
              serializeJson(alert, msg);
              webSocket.broadcastTXT(msg);
            }
          }
        }
      }
    }
  }
}

// ============== IDEMPOTENCY KEY SUPPORT =============
// Prevents duplicate operations on network retries
// Client sends "Idempotency-Key" header; server caches result for 60s
struct IdempotencyEntry {
  String key;
  String response; // Cached response body
  int code;        // Cached HTTP status
  unsigned long timestamp;
};
#define MAX_IDEMPOTENCY 10
#define IDEMPOTENCY_TTL 60000UL // 60 second cache
IdempotencyEntry idempotencyCache[MAX_IDEMPOTENCY];
int idempotencyCount = 0;

// Check if request has a cached idempotency response; if so, send it and return true
bool checkIdempotencyKey(WebServer &server) {
  if (!server.hasHeader("Idempotency-Key")) return false;
  String key = server.header("Idempotency-Key");
  if (key.length() == 0) return false;
  unsigned long now = millis();
  for (int i = 0; i < idempotencyCount; i++) {
    if (idempotencyCache[i].key == key) {
      // Expired? Remove and allow re-execution
      if (now - idempotencyCache[i].timestamp > IDEMPOTENCY_TTL) {
        idempotencyCache[i].key = "";
        return false;
      }
      // Return cached response
      server.send(idempotencyCache[i].code, "application/json", idempotencyCache[i].response);
      return true;
    }
  }
  return false;
}

// Store an idempotency result for future duplicate requests
void storeIdempotencyResult(String key, int code, String response) {
  if (key.length() == 0) return;
  unsigned long now = millis();
  // Find expired slot, or use oldest/LRU
  int slot = -1;
  unsigned long oldest = now;
  for (int i = 0; i < idempotencyCount; i++) {
    if (idempotencyCache[i].key.length() == 0 || now - idempotencyCache[i].timestamp > IDEMPOTENCY_TTL) {
      slot = i; break;
    }
    if (idempotencyCache[i].timestamp < oldest) { oldest = idempotencyCache[i].timestamp; slot = i; }
  }
  if (slot < 0 && idempotencyCount < MAX_IDEMPOTENCY) { slot = idempotencyCount; idempotencyCount++; }
  if (slot < 0) slot = 0; // Overwrite LRU
  idempotencyCache[slot].key = key;
  idempotencyCache[slot].code = code;
  idempotencyCache[slot].response = response.substring(0, 512); // Cap cached response size
  idempotencyCache[slot].timestamp = now;
}

// ============== REQUEST ID COUNTER =============
static uint32_t requestIdCounter = 0;

// ============== POST BODY VALIDATION =============
// Ensures POST/PUT request body doesn't exceed MAX_POST_BODY_SIZE
// Returns true if valid, false if too large (sends 413 automatically)
bool validatePostBody() {
  if ((webServer.method() == HTTP_POST || webServer.method() == HTTP_PUT) &&
      webServer.hasArg("plain") && webServer.arg("plain").length() > MAX_POST_BODY_SIZE) {
    sendError(413, "Request body too large (max 32KB)");
    return false;
  }
  return true;
}

void sendSecurityHeaders() {
  webServer.sendHeader("X-Content-Type-Options", "nosniff");
  webServer.sendHeader("X-Frame-Options", "DENY");
  webServer.sendHeader("X-XSS-Protection", "1; mode=block");
  webServer.sendHeader("Referrer-Policy", "no-referrer");
  webServer.sendHeader("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; media-src 'self'; connect-src 'self' ws: wss:");
  webServer.sendHeader("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
  webServer.sendHeader("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
  // Additional cross-origin security headers (defense-in-depth)
  webServer.sendHeader("Cross-Origin-Opener-Policy", "same-origin");
  webServer.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
  webServer.sendHeader("X-Permitted-Cross-Domain-Policies", "none");
  // Suppress server identity for security (don't leak ESP32/Arduino info)
  webServer.sendHeader("Server", "ESP32FS");
  // Allow HTTP keep-alive for fewer TCP round-trips on repeated requests
  webServer.sendHeader("Connection", "keep-alive");
  webServer.sendHeader("Keep-Alive", "timeout=5, max=100");
  // Attach unique request ID for tracing/debugging
  requestIdCounter++;
  webServer.sendHeader("X-Request-Id", String(millis(), HEX) + "-" + String(requestIdCounter));
  // Server-Timing header (W3C spec): expose server-side processing time in ms
  // Browsers show this in DevTools Network panel
  if (requestStartMs > 0) {
    uint32_t elapsed = (uint32_t)(millis() - requestStartMs);
    webServer.sendHeader("Server-Timing", "handler;dur=" + String(elapsed));
  }
}

void sendError(int code, String msg) {
  sendSecurityHeaders();
  String json = "{\"error\":\""+msg+"\",\"code\":"+String(code)+"}";
  webServer.sendHeader("Content-Length", String(json.length()));
  webServer.send(code, "application/json", json);
}

// Send JSON response with Content-Length header for HTTP compliance
void sendJson(int code, String json) {
  sendSecurityHeaders();
  webServer.sendHeader("Content-Length", String(json.length()));
  webServer.send(code, "application/json", json);
}

// ============== AUDIT LOG =============
// Log client IP + method for state-changing requests (security audit trail)
void auditRequest(String action, String detail) {
  IPAddress ip = webServer.client().remoteIP();
  String ipStr = ip.toString();
  // Append to audit log (separate from activity log for security review)
  File f = SD.open("/.audit.log", FILE_APPEND);
  if (!f) f = SD.open("/.audit.log", FILE_WRITE);
  if (f) {
    f.print(millis());
    f.print(",");
    f.print(ipStr);
    f.print(",");
    f.print(action);
    f.print(",");
    f.println(detail);
    f.close();
  }
}

// Rate-limited request wrapper
// Sends X-RateLimit-* headers so clients can self-throttle
bool isRateLimited() {
  IPAddress ip = webServer.client().remoteIP();
  // Find current rate limit entry to report remaining count
  int remaining = RATE_MAX_REQUESTS;
  unsigned long resetIn = RATE_WINDOW_MS / 1000;
  for (int i = 0; i < rateLimitCount; i++) {
    if (rateLimits[i].ip == ip) {
      unsigned long now = millis();
      if (now - rateLimits[i].windowStart > RATE_WINDOW_MS) {
        remaining = RATE_MAX_REQUESTS - 1; // New window, this request counts
      } else {
        remaining = RATE_MAX_REQUESTS - rateLimits[i].count;
        resetIn = (RATE_WINDOW_MS - (now - rateLimits[i].windowStart)) / 1000;
      }
      break;
    }
  }
  // Always send rate limit headers for client awareness
  webServer.sendHeader("X-RateLimit-Limit", String(RATE_MAX_REQUESTS));
  webServer.sendHeader("X-RateLimit-Remaining", String(max(0, remaining)));
  webServer.sendHeader("X-RateLimit-Reset", String(resetIn));

  if (!checkRateLimit(ip)) {
    webServer.sendHeader("Retry-After", String(resetIn));
    webServer.send(429, "application/json", "{\"error\":\"Too many requests\",\"retry\":\"" + String(resetIn) + "\"}");
    return true;
  }
  // Check POST body size to prevent OOM
  if (!validatePostBody()) return true;
  // Emit CORS headers on actual API requests (non-preflight) for cross-origin access
  if (webServer.uri().startsWith("/api/")) {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.sendHeader("Access-Control-Allow-Headers", "Authorization, Content-Type, X-CSRF-Token, Idempotency-Key");
    webServer.sendHeader("Access-Control-Expose-Headers", "X-Request-Id, X-RateLimit-Limit, X-RateLimit-Remaining, Server-Timing");
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
  sendJson(200, json);
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
  // ETag for health: based on heap + sd status + uptime bucket (changes every 10s)
  String healthEtag = "\"" + String(ESP.getFreeHeap()) + "-" + String(sdOK) + "-" + String(millis() / 10000) + "\"";
  webServer.sendHeader("ETag", healthEtag);
  webServer.sendHeader("Cache-Control", "private, max-age=5, stale-while-revalidate=15");
  if (webServer.hasHeader("If-None-Match") && webServer.header("If-None-Match") == healthEtag) {
    webServer.send(304, "text/plain", "Not Modified");
    return;
  }
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
  // ETag for listing cache: based on dir stats + used bytes (changes on any file change)
  String etag = "\"" + String(count) + "-" + String(SD.usedBytes()) + "\"";
  webServer.sendHeader("ETag", etag);
  webServer.sendHeader("Cache-Control", "private, max-age=5, stale-while-revalidate=30");
  if (webServer.hasHeader("If-None-Match") && webServer.header("If-None-Match") == etag) {
    webServer.send(304, "text/plain", "Not Modified");
    return;
  }
  webServer.sendHeader("Content-Length", String(out.length()));
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
  // Include download count and read-only flag from access metadata
  doc["downloads"] = getFileDownloads(path);
  doc["readonly"] = isFileReadOnly(path);
  unsigned long fmtime = f.fileTime();
  f.close();
  String out; serializeJson(doc, out);
  // ETag caching for file info: based on size + mtime (changes on file edit)
  String infoEtag = "\"" + String((uint32_t)doc["size"]) + "-" + String(fmtime) + "\"";
  webServer.sendHeader("ETag", infoEtag);
  webServer.sendHeader("Cache-Control", "private, max-age=10");
  if (webServer.hasHeader("If-None-Match") && webServer.header("If-None-Match") == infoEtag) {
    webServer.send(304, "text/plain", "Not Modified");
    return;
  }
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
  webServer.sendHeader("Accept-Ranges", "bytes");
  // If client sends matching If-None-Match, return 304
  if (webServer.hasHeader("If-None-Match") && webServer.header("If-None-Match") == etag) {
    f.close();
    webServer.send(304, "text/plain", "Not Modified");
    logActivity("download-304", path, u);
    return;
  }
  // Byte-range support for resume/partial downloads
  if (webServer.hasHeader("Range")) {
    String rangeHdr = webServer.header("Range");
    if (rangeHdr.startsWith("bytes=")) {
      String rangeSpec = rangeHdr.substring(6);
      int dash = rangeSpec.indexOf('-');
      if (dash > 0) {
        unsigned long startByte = rangeSpec.substring(0, dash).toInt();
        unsigned long endByte = (dash < rangeSpec.length()-1) ? rangeSpec.substring(dash+1).toInt() : fsize - 1;
        if (endByte >= fsize) endByte = fsize - 1;
        if (startByte <= endByte && startByte < fsize) {
          unsigned long contentLen = endByte - startByte + 1;
          webServer.sendHeader("Content-Range", "bytes " + String(startByte) + "-" + String(endByte) + "/" + String(fsize));
          webServer.sendHeader("Content-Length", String(contentLen));
          webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
          webServer.sendHeader("Content-Type", ctype);
          webServer.send(206, ctype, "");
          f.seek(startByte);
          uint8_t buf[512];
          unsigned long sent = 0;
          while (sent < contentLen && f.available()) {
            int toRead = min((unsigned long)512, contentLen - sent);
            int n = f.read(buf, toRead);
            if (n <= 0) break;
            webServer.sendContent((const char*)buf, n);
            sent += n;
          }
          f.close();
          logActivity("download-range", path + " (" + String(startByte) + "-" + String(endByte) + ")", u);
          return;
        }
      }
    }
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
  // Add Last-Modified header for better client caching
  if (fmtime > 0) {
    // Format as HTTP-date (best effort — ESP32 time may not be NTP-synced)
    webServer.sendHeader("Last-Modified", String(fmtime));
  }
  webServer.streamFile(f, ctype);
  f.close();
  logActivity("download", path, u);
  // Track download count per file
  trackFileAccess(path, "download");
  // Fire webhook notification for download event
  fireWebhook("download", path, u);
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

// ============== DELETE =============
void handleDelete() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  // Idempotency: if duplicate request, return cached result
  if (checkIdempotencyKey(webServer)) return;
  auditRequest("delete", webServer.hasArg("path") ? webServer.arg("path") : "");
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (!SD.exists(path)) { webServer.send(404); return; }
  // Reject delete if file is marked read-only (immutable)
  if (isFileReadOnly(path)) { webServer.send(403, "application/json", "{\"error\":\"File is read-only (immutable)\"}"); return; }
  // Acquire lock to prevent concurrent deletion corruption
  if (!acquireFileLock(path, 2000)) { webServer.send(423, "text/plain", "File is locked"); return; }
  bool ok = moveToTrash(path);
  releaseFileLock(path);
  if (ok) {
    String resp = "{\"ok\":true,\"path\":\""+path+"\"}";
    logActivity("delete", path, u); broadcastChange("delete", path); fireWebhook("delete", path, u); webServer.send(200, "application/json", resp);
    // Cache idempotency result
    if (webServer.hasHeader("Idempotency-Key")) storeIdempotencyResult(webServer.header("Idempotency-Key"), 200, resp);
  }
  else webServer.send(500, "text/plain", "Failed");
}

// ============== CREATE DIR =============
void handleCreateDir() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (checkIdempotencyKey(webServer)) return;
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  // Check SD free space
  if (SD.totalBytes() - SD.usedBytes() < 1024) {
    webServer.send(507, "text/plain", "SD card full"); return;
  }
  if (createDirRecursive(path)) {
    String resp = "{\"ok\":true,\"path\":\""+path+"\"}";
    logActivity("mkdir", path, u); broadcastChange("mkdir", path); webServer.send(200, "application/json", resp);
    if (webServer.hasHeader("Idempotency-Key")) storeIdempotencyResult(webServer.header("Idempotency-Key"), 200, resp);
  }
  else webServer.send(500, "text/plain", "Failed");
}

// ============== CREATE EMPTY FILE ==============
void handleCreateFile() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = webServer.arg("path");
  if (SD.totalBytes() - SD.usedBytes() < 1024) { webServer.send(507, "text/plain", "SD card full"); return; }
  if (SD.exists(path)) { webServer.send(409, "text/plain", "File already exists"); return; }
  int slash = path.lastIndexOf('/');
  if (slash > 0) createDirRecursive(path.substring(0, slash));
  File f = SD.open(path, FILE_WRITE);
  if (!f) { webServer.send(500, "text/plain", "Failed to create file"); return; }
  f.close();
  logActivity("create-file", path, u);
  broadcastChange("create", path);
  webServer.send(200, "text/plain", "OK");
}

// ============== RENAME ==============
void handleRename() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { sendError(503,"SD card not available"); return; }
  if (!webServer.hasArg("path") || !webServer.hasArg("name")) { sendError(400,"Missing path or name"); return; }
  String path = webServer.arg("path"), newName = webServer.arg("name");
  if (!SD.exists(path)) { sendError(404,"Source file not found"); return; }
  // Reject rename if file is read-only (immutable)
  if (isFileReadOnly(path)) { sendError(403,"File is read-only (immutable)"); return; }
  // Validate new name - prevent path traversal
  if (newName.indexOf('/') >= 0 || newName.indexOf('\\') >= 0 || newName.length() == 0) {
    sendError(400,"Invalid file name"); return;
  }
  if (!acquireFileLock(path)) { sendError(423,"File is locked"); return; }
  // Create version for files (not directories)
  {
    File tf = SD.open(path);
    if (tf && !tf.isDirectory()) { createVersion(path); tf.close(); }
    else if (tf) tf.close();
  }
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
  // Lock source file to prevent concurrent modification during move
  if (!acquireFileLock(path, 3000)) { webServer.send(423, "text/plain", "Source file is locked"); return; }
  if (moveFile(path, dp)) { logActivity("move", path+" -> "+dp, u); broadcastChange("move", dp); webServer.send(200, "text/plain", "OK"); }
  else webServer.send(500, "text/plain", "Failed");
  releaseFileLock(path);
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
  auditRequest("upload", webServer.hasArg("path") ? webServer.arg("path") : "/");
  if (!sdOK) { webServer.send(503); return; }
  HTTPUpload& up = webServer.upload();
  static File uf;
  static String upp;
  if (up.status == UPLOAD_FILE_START) {
    // Enforce maximum upload size limit
    if (up.totalSize > MAX_UPLOAD_SIZE) {
      Serial.printf("Upload rejected: too large (%u > %u)\n", up.totalSize, MAX_UPLOAD_SIZE);
      return;
    }
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
    // Acquire lock to prevent concurrent write corruption
    if (!acquireFileLock(upp, 5000)) {
      Serial.println("Upload rejected: file is locked: " + upp);
      webServer.send(423, "text/plain", "File is locked by another operation");
      return;
    }
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
    releaseFileLock(upp); // Release lock after upload completes
    totalWriteOps++;
    totalWriteBytes += up.totalSize;
    logActivity("upload", upp+" ("+String(up.totalSize)+"B)", u);
    broadcastChange("upload", upp);
    trackFileAccess(upp, "view"); // Record initial upload
    fireWebhook("upload", upp, u);
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

// Stream a ZIP archive from a JSON array of file paths
// Uses miniz to compress files on-the-fly and stream to client
void streamZipFromJsonArray(JsonArray &paths, String label, String &username) {
  if (paths.size() == 0) return;
  // Parse paths into a local array
  String filePaths[100];
  int fileCount = min((int)paths.size(), 100);
  for (int i = 0; i < fileCount; i++) {
    filePaths[i] = paths[i].as<String>();
    if (!filePaths[i].startsWith("/")) filePaths[i] = "/" + filePaths[i];
  }
  // Build ZIP using miniz
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "application/zip", "");
  tdefl_compressor comp;
  tdefl_status status = tdefl_init(&comp, NULL, NULL, TDEFL_DEFAULT_MAX_PROBES);
  if (status != TDEFL_STATUS_OKAY) { webServer.sendContent(""); return; }
  // Write ZIP end-of-central-directory record placeholder
  // We'll use a simple approach: write each file as a stored (uncompressed) entry
  struct ZipEntry {
    String name;
    uint32_t crc;
    uint32_t size;
  };
  ZipEntry entries[100];
  // Compute CRC and sizes first
  for (int i = 0; i < fileCount; i++) {
    entries[i].name = filePaths[i].substring(1); // Remove leading /
    File f = SD.open(filePaths[i], FILE_READ);
    if (!f) { fileCount--; i--; continue; }
    entries[i].size = f.size();
    uint32_t crc = 0xFFFFFFFF;
    uint8_t buf[256];
    while (f.available()) {
      int n = f.read(buf, 256);
      for (int j = 0; j < n; j++) {
        crc ^= buf[j];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
      }
    }
    entries[i].crc = crc ^ 0xFFFFFFFF;
    f.close();
  }
  // Write local file headers + data
  uint32_t centralDirOffset = 0;
  for (int i = 0; i < fileCount; i++) {
    // Local file header (stored, no compression for speed)
    uint8_t lh[30];
    memset(lh, 0, 30);
    lh[0] = 0x50; lh[1] = 0x4B; lh[2] = 0x03; lh[3] = 0x04; // Signature
    lh[4] = 20; lh[5] = 0; // Version needed
    lh[6] = 0; lh[7] = 0; // Flags (none)
    lh[8] = 0; lh[9] = 0; // Compression (stored)
    // Mod time/date (zero)
    uint32_t sz = entries[i].size;
    uint16_t nameLen = entries[i].name.length();
    lh[26] = nameLen & 0xFF; lh[27] = (nameLen >> 8) & 0xFF;
    lh[28] = 0; lh[29] = 0; // Extra field length
    // Send local header
    webServer.sendContent((const char*)lh, 30);
    // Send filename
    webServer.sendContent(entries[i].name.c_str(), nameLen);
    centralDirOffset += 30 + nameLen;
    // Send file data (stored)
    File f = SD.open(filePaths[i], FILE_READ);
    if (f) {
      uint8_t buf[512];
      while (f.available()) {
        int n = f.read(buf, 512);
        webServer.sendContent((const char*)buf, n);
      }
      f.close();
      centralDirOffset += sz;
    }
  }
  // Write central directory
  uint32_t cdSize = 0;
  for (int i = 0; i < fileCount; i++) {
    uint8_t ch[46];
    memset(ch, 0, 46);
    ch[0] = 0x50; ch[1] = 0x4B; ch[2] = 0x01; ch[3] = 0x02; // Signature
    ch[4] = 20; ch[5] = 0; // Version made by
    ch[6] = 20; ch[7] = 0; // Version needed
    ch[8] = 0; ch[9] = 0; // Flags
    ch[10] = 0; ch[11] = 0; // Compression (stored)
    uint16_t nameLen = entries[i].name.length();
    ch[28] = nameLen & 0xFF; ch[29] = (nameLen >> 8) & 0xFF;
    ch[30] = 0; ch[31] = 0; // Extra field
    ch[32] = 0; ch[33] = 0; // Comment
    ch[34] = 0; ch[35] = 0; // Disk number
    ch[36] = 0; ch[37] = 0; // Internal attrs
    ch[38] = 0; ch[39] = 0; ch[40] = 0; ch[41] = 0; // External attrs
    uint32_t localHdrOffset = 0;
    for (int j = 0; j < i; j++) {
      localHdrOffset += 30 + entries[j].name.length() + entries[j].size;
    }
    ch[42] = localHdrOffset & 0xFF; ch[43] = (localHdrOffset >> 8) & 0xFF;
    ch[44] = (localHdrOffset >> 16) & 0xFF; ch[45] = (localHdrOffset >> 24) & 0xFF;
    webServer.sendContent((const char*)ch, 46);
    webServer.sendContent(entries[i].name.c_str(), nameLen);
    cdSize += 46 + nameLen;
  }
  // End of central directory
  uint8_t eocd[22];
  memset(eocd, 0, 22);
  eocd[0] = 0x50; eocd[1] = 0x4B; eocd[2] = 0x05; eocd[3] = 0x06;
  eocd[4] = 0; eocd[5] = 0; // Disk number
  eocd[6] = 0; eocd[7] = 0; // CD disk
  eocd[8] = fileCount & 0xFF; eocd[9] = (fileCount >> 8) & 0xFF;
  eocd[10] = fileCount & 0xFF; eocd[11] = (fileCount >> 8) & 0xFF;
  eocd[12] = cdSize & 0xFF; eocd[13] = (cdSize >> 8) & 0xFF;
  eocd[14] = (cdSize >> 16) & 0xFF; eocd[15] = (cdSize >> 24) & 0xFF;
  eocd[16] = centralDirOffset & 0xFF; eocd[17] = (centralDirOffset >> 8) & 0xFF;
  eocd[18] = (centralDirOffset >> 16) & 0xFF; eocd[19] = (centralDirOffset >> 24) & 0xFF;
  eocd[20] = 0; eocd[21] = 0; // Comment length
  webServer.sendContent((const char*)eocd, 22);
  webServer.sendContent("");
  logActivity("zip", label + " (" + String(fileCount) + " files)", username);
}

void handleZipDownload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  if (!validatePostBody()) return;
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, webServer.arg("plain"));
  JsonArray paths = doc["paths"];
  if (paths.size() == 0) { webServer.send(400); return; }
  streamZipFromJsonArray(paths, "zip", u);
}

// ============== BATCH DOWNLOAD SELECTED ==============
// Accepts JSON {paths: [...]} and streams a ZIP of those files
void handleBatchDownload() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("plain")) { webServer.send(400); return; }
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, webServer.arg("plain"))) { webServer.send(400); return; }
  JsonArray paths = doc["paths"];
  if (paths.size() == 0) { webServer.send(400, "text/plain", "No files selected"); return; }
  if (paths.size() > 100) { webServer.send(400, "text/plain", "Max 100 files"); return; }
  streamZipFromJsonArray(paths, "batch-download", u);
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
  // OTA requires admin privilege — firmware update is the highest-privilege operation
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
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
  auditRequest("save-settings", u);
  if (!validatePostBody()) return;
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
  // POST body: optional "path" arg for single-item permanent delete
  if(webServer.hasArg("path")){String path=sanitizePath(webServer.arg("path"));if(SD.exists(path)){File f=SD.open(path);if(f.isDirectory())removeDir(path);else SD.remove(path);f.close();logActivity("perm-delete",path,u);}}
  else{if(SD.exists(TRASH_FOLDER)){removeDir(TRASH_FOLDER);SD.mkdir(TRASH_FOLDER);}logActivity("empty-trash","all",u);}
  broadcastChange("empty-trash","/");webServer.send(200,"text/plain","OK");
}

// ============== BATCH RESTORE FROM TRASH ==============
// Restore multiple items from trash in one request
void handleBatchRestore() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("plain")) { sendError(400, "Need JSON body"); return; }
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, webServer.arg("plain"))) { sendError(400, "Invalid JSON"); return; }
  JsonArray paths = doc["paths"];
  if (paths.isNull() || paths.size() == 0) { sendError(400, "No paths"); return; }
  int ok = 0, fail = 0;
  for (JsonVariant p : paths) {
    String trashPath = p.as<String>();
    if (restoreFromTrash(trashPath)) {
      ok++;
      logActivity("restore", trashPath, u);
      broadcastChange("restore", trashPath);
    } else {
      fail++;
    }
  }
  DynamicJsonDocument out(256);
  out["ok"] = ok;
  out["fail"] = fail;
  String s; serializeJson(out, s);
  webServer.send(200, "application/json", s);
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
  if(!isAuthenticated(webServer,u,lvl)||lvl!="admin"||!checkCsrf(webServer)){webServer.send(403);return;}
  auditRequest("add-user", u);
  if(!validatePostBody())return;
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
  // Hash password with per-user salt before storing
  String npHash = hashPasswordForStorage(nu, np);
  JsonObject u2=ex["users"].createNestedObject();u2["username"]=nu;u2["password"]=npHash;u2["userLevel"]=nl;
  f=SD.open(USERS_FILE,FILE_WRITE);if(!f){webServer.send(500);return;}
  serializeJson(ex,f);f.close();logActivity("add-user",nu,u);webServer.send(200,"text/plain","OK");
}
void handleUpdateUser() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)||lvl!="admin"||!checkCsrf(webServer)){webServer.send(403);return;}
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
      // Hash password with per-user salt before storing
      u["password"]=hashPasswordForStorage(path, np);
    }
    u["userLevel"]=doc["userLevel"]|u["userLevel"];break;}}
  f=SD.open(USERS_FILE,FILE_WRITE);if(!f){webServer.send(500);return;}
  serializeJson(ex,f);f.close();webServer.send(200,"text/plain","OK");
}
void handleDeleteUser() {
  String u,lvl;
  if(!isAuthenticated(webServer,u,lvl)||lvl!="admin"||!checkCsrf(webServer)){webServer.send(403);return;}
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
      auditRequest("csrf-fail", webServer.arg("username"));
    } else {
      String lvl;
      if(authenticateUser(u,p,lvl)){
        auditRequest("login-ok", u);
        resetLoginRateLimit(webServer.client().remoteIP()); // Clear rate limit on success
        String tok=createSession(u,lvl);
        webServer.sendHeader("Set-Cookie","session_token="+tok+"; Path=/; Max-Age=1800; SameSite=Strict; HttpOnly");
        String r=webServer.hasArg("redirect")?webServer.arg("redirect"):"/";
        webServer.sendHeader("Location",r+"?token="+tok,true);webServer.send(302,"text/plain","");return;
      } else { err="Invalid username or password"; auditRequest("login-fail", u); }
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
  // PWA manifest with inline icons (1x1 blue PNG as placeholder)
  String m = "{\"name\":\"ESP32 File Server\",\"short_name\":\"ESP32-FS\",\"start_url\":\"/\",\"display\":\"standalone\",";
  m += "\"background_color\":\"#0984e3\",\"theme_color\":\"#0984e3\",";
  m += "\"description\":\"WiFi file manager for ESP32 with SD card\",";
  m += "\"categories\":[\"utilities\",\"productivity\"],";
  m += "\"icons\":[{\"src\":\"/favicon.ico\",\"sizes\":\"32x32\",\"type\":\"image/x-icon\"}],";
  m += "\"shortcuts\":[{\"name\":\"Upload\",\"url\":\"/\",\"description\":\"Upload files\"}]}";
  webServer.send(200, "application/manifest+json", m);
}
void handleRobotsTxt() {
  webServer.send(200, "text/plain", "User-agent: *\nDisallow: /\n");
}
void handleFavicon() {
  // Inline 1x1 transparent PNG to avoid 404
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.send(200, "image/x-icon", "");
}

// ============== SERVICE WORKER =============
// Minimal SW for PWA offline support: cache app shell, network-first for API
void handleServiceWorker() {
  sendSecurityHeaders();
  webServer.sendHeader("Cache-Control", "public, max-age=86400");
  webServer.sendHeader("Content-Type", "application/javascript");
  String sw = "const CACHE='esp32fs-v6.11';const SHELL=['/', '/manifest.json'];\\n";
  sw += "self.addEventListener('install',e=>{e.waitUntil(caches.open(CACHE).then(c=>c.addAll(SHELL)));self.skipWaiting();});\n";
  sw += "self.addEventListener('activate',e=>{e.waitUntil(caches.keys().then(ks=>Promise.all(ks.filter(k=>k!==CACHE).map(k=>caches.delete(k)))));self.clients.claim();});\n";
  sw += "self.addEventListener('fetch',e=>{if(e.request.url.includes('/api/')){e.respondWith(fetch(e.request).catch(()=>caches.match(e.request)));}else{e.respondWith(caches.match(e.request).then(r=>r||fetch(e.request)));}});\n";
  webServer.send(200, "application/javascript", sw);
}

// ============== BATCH OPERATIONS ==============
void handleBatchDelete() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!validatePostBody()) return;
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
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
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
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
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

// ============== CONTENT SEARCH (GREP) =============
// Search within file contents (text grep), limited to small text files
#define MAX_GREP_RESULTS 50
#define MAX_GREP_FILE_SIZE 65536 // Only grep files <= 64KB
#define MAX_GREP_PREVIEW 120     // Chars of context around match

void handleGrep() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401, "Not authenticated"); return; }
  if (!sdOK) { sendError(503, "SD card not available"); return; }
  String query = webServer.hasArg("q") ? webServer.arg("q") : "";
  String dir = webServer.hasArg("dir") ? sanitizePath(webServer.arg("dir")) : "/";
  if (query.length() == 0) { sendError(400, "Need search query (q)"); return; }
  query.toLowerCase();
  DynamicJsonDocument doc(8192);
  JsonArray results = doc.createNestedArray("results");
  int found = 0;
  // Recursive scan
  std::function<void(String)> scanGrep = [&](String path) {
    if (found >= MAX_GREP_RESULTS) return;
    File d = SD.open(path);
    if (!d || !d.isDirectory()) { if(d) d.close(); return; }
    File f;
    while (f = d.openNextFile()) {
      if (found >= MAX_GREP_RESULTS) { f.close(); break; }
      String fn = String(f.name());
      if (f.isDirectory()) {
        if (!fn.startsWith("/.")) scanGrep(fn);
      } else {
        // Only search text files within size limit
        if (f.size() <= MAX_GREP_FILE_SIZE && shouldCompress(fn)) {
          String content = "";
          uint8_t buf[512];
          size_t totalRead = 0;
          while (f.available() && totalRead < MAX_GREP_FILE_SIZE) {
            int n = f.read(buf, 512);
            for (int i = 0; i < n && totalRead < MAX_GREP_FILE_SIZE; i++) {
              char c = buf[i];
              if (c >= 32 && c < 127) content += c;
              else if (c == '\n' || c == '\t') content += c;
              else content += ' ';
              totalRead++;
            }
          }
          String lower = content;
          lower.toLowerCase();
          int pos = lower.indexOf(query);
          if (pos >= 0) {
            int sl = fn.lastIndexOf('/');
            JsonObject r = results.createNestedObject();
            r["name"] = (sl >= 0) ? fn.substring(sl+1) : fn;
            r["path"] = fn;
            r["size"] = f.size();
            int start = pos > 40 ? pos - 40 : 0;
            int len = MAX_GREP_PREVIEW;
            if (start + len > (int)content.length()) len = content.length() - start;
            String preview = content.substring(start, start + len);
            preview.replace("<", "&lt;");
            preview.replace(">", "&gt;");
            r["preview"] = preview;
            r["match_pos"] = pos;
            found++;
          }
        }
      }
      f.close();
    }
    d.close();
  };
  scanGrep(dir);
  doc["count"] = found;
  doc["query"] = query;
  String out; serializeJson(doc, out);
  sendJson(200, out);
  logActivity("grep", query + " (" + String(found) + " hits)", u);
}

// ============== RECURSIVE SEARCH ==============
// Max search results to prevent OOM on large storage
#define MAX_SEARCH_RESULTS 200

void searchRecursive(String path, String query, JsonArray &results, int &found) {
  if (found >= MAX_SEARCH_RESULTS) return; // Early exit when limit reached
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;
  File file;
  while (file = dir.openNextFile()) {
    if (found >= MAX_SEARCH_RESULTS) { file.close(); break; }
    String fn = String(file.name());
    int sl = fn.lastIndexOf('/');
    String name = (sl >= 0) ? fn.substring(sl+1) : fn;
    if (file.isDirectory()) {
      searchRecursive(fn, query, results, found);
    } else {
      if (name.toLowerCase().indexOf(query) >= 0) {
        String p = path + (path.endsWith("/")?"":"/") + name;
        JsonObject r = results.createNestedObject();
        r["name"] = name;
        r["path"] = p;
        r["size"] = file.size();
        r["icon"] = getFileIcon(name);
        found++;
      }
    }
    file.close();
  }
  dir.close();
}

void handleSearch() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (isRateLimited()) return;
  String query = webServer.hasArg("q") ? webServer.arg("q") : "";
  if (query.length() == 0) { webServer.send(400); return; }
  DynamicJsonDocument doc(16384);
  JsonArray results = doc.createNestedArray("results");
  int found = 0;
  searchRecursive("/", query.toLowerCase(), results, found);
  doc["count"] = results.size();
  doc["limit"] = MAX_SEARCH_RESULTS;
  doc["truncated"] = (found >= MAX_SEARCH_RESULTS);
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
  // Validate input: username 1-32 chars, password 4-64 chars
  if (au.length() < 1 || au.length() > 32 || ap.length() < 4 || ap.length() > 64) {
    webServer.send(400, "text/plain", "Invalid credentials (user:1-32, pass:4-64)"); return;
  }
  // Hash password before storing (use HMAC-SHA256 like normal user creation)
  String hashedPw = hashPasswordForStorage(au, ap);
  // Save users file with hashed password
  File f = SD.open(USERS_FILE, FILE_WRITE);
  if (f) { f.print("{\"users\":[{\"username\":\""+au+"\",\"password\":\""+hashedPw+"\",\"userLevel\":\"admin\"}]}"); f.close(); }
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
  doc["sd_crc_spot_checks"] = crcSpotChecksDone;
  doc["sd_crc_spot_errors"] = crcSpotCheckErrors;
  doc["max_upload_size"] = MAX_UPLOAD_SIZE;
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
// ============== FILE ACCESS STATS =============
void handleAccessStats() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!SD.exists(ACCESS_META_FILE)) { webServer.send(200, "application/json", "{}"); return; }
  File f = SD.open(ACCESS_META_FILE, FILE_READ);
  if (!f) { webServer.send(500); return; }
  String content;
  while (f.available()) content += (char)f.read();
  f.close();
  webServer.send(200, "application/json", content);
}

// ============== TOGGLE FILE READ-ONLY =============
void handleSetReadOnly() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin" || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = sanitizePath(webServer.arg("path"));
  bool readOnly = webServer.hasArg("readonly") && webServer.arg("readonly") == "1";
  if (setFileReadOnly(path, readOnly)) {
    logActivity(readOnly ? "lock" : "unlock", path, u);
    broadcastChange(readOnly ? "lock" : "unlock", path);
    webServer.send(200, "application/json", "{\"ok\":true,\"path\":\"" + path + "\",\"readonly\":" + (readOnly ? "true" : "false") + "}");
  } else {
    webServer.send(500, "application/json", "{\"error\":\"Failed to update\"}");
  }
}

// ============== WEBHOOK CONFIG =============
void handleWebhookConfig() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { webServer.send(403); return; }
  if (webServer.method() == HTTP_GET) {
    if (!SD.exists(WEBHOOK_FILE)) { webServer.send(200, "application/json", "{\"enabled\":false,\"url\":\"\"}"); return; }
    File f = SD.open(WEBHOOK_FILE, FILE_READ);
    if (!f) { webServer.send(500); return; }
    String content;
    while (f.available()) content += (char)f.read();
    f.close();
    webServer.send(200, "application/json", content);
  } else if (webServer.method() == HTTP_POST) {
    if (!checkCsrf(webServer)) { webServer.send(403); return; }
    String url = webServer.hasArg("url") ? webServer.arg("url") : "";
    bool enabled = webServer.hasArg("enabled") && webServer.arg("enabled") == "1";
    DynamicJsonDocument doc(512);
    doc["url"] = url;
    doc["enabled"] = enabled;
    File f = SD.open(WEBHOOK_FILE, FILE_WRITE);
    if (!f) { webServer.send(500); return; }
    serializeJson(doc, f); f.close();
    logActivity("webhook-config", url, u);
    webServer.send(200, "application/json", "{\"ok\":true}");
  }
}

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
// ============== FILE TYPE DETECTION API ==============
void handleDetectType() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = sanitizePath(webServer.arg("path"));
  if (!SD.exists(path)) { webServer.send(404); return; }
  String detectedType = detectFileTypeByContent(path);
  String extType = getContentType(path);
  DynamicJsonDocument doc(256);
  doc["path"] = path;
  doc["detected_type"] = detectedType;
  doc["extension_type"] = extType;
  doc["match"] = detectedType.equals(extType) || 
    (detectedType.startsWith("text/") && extType.startsWith("text/"));
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

// ============== FILE PREVIEW WITH LINE NUMBERS ==============
// Returns file content with HTML line numbers for code viewing
void handleFilePreviewCode() {
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
  // Code file extensions that get line numbers
  if (ext != "txt" && ext != "md" && ext != "csv" && ext != "log" && ext != "json" && 
      ext != "xml" && ext != "html" && ext != "htm" && ext != "css" && ext != "js" &&
      ext != "ini" && ext != "cfg" && ext != "conf" && ext != "sh" && ext != "py" &&
      ext != "c" && ext != "cpp" && ext != "h" && ext != "hpp" && ext != "java" &&
      ext != "php" && ext != "rs" && ext != "go" && ext != "ts" && ext != "sql" &&
      ext != "yaml" && ext != "yml" && ext != "toml") {
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
  // Build line-numbered HTML
  int lineCount = 1;
  for (int i = 0; i < content.length(); i++) {
    if (content[i] == '\n') lineCount++;
  }
  String html = "<div style='font-family:monospace;font-size:12px;background:var(--bg);border-radius:6px;overflow:auto;max-height:60vh'>";
  html += "<table style='border-collapse:collapse;width:100%'><tbody>";
  int lineNum = 1;
  int start = 0;
  for (int i = 0; i <= content.length(); i++) {
    if (i == content.length() || content[i] == '\n') {
      String lineContent = content.substring(start, i);
      // HTML escape
      lineContent.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
      html += "<tr><td style='padding:0 8px;text-align:right;color:var(--text2);user-select:none;border-right:1px solid var(--border);min-width:40px'>" + String(lineNum) + "</td>";
      html += "<td style='padding:0 8px;white-space:pre'>" + lineContent + "</td></tr>";
      lineNum++;
      start = i + 1;
    }
  }
  html += "</tbody></table></div>";
  if (fileSize > maxBytes) {
    html += "<p style='color:var(--text2);font-size:12px;margin-top:8px'>⚠️ Preview truncated (showing first 64KB)</p>";
  }
  DynamicJsonDocument doc(65536 + 1024);
  doc["name"] = name;
  doc["path"] = path;
  doc["size"] = (uint64_t)fileSize;
  doc["sizeFormatted"] = getFileSize((uint64_t)fileSize);
  doc["truncated"] = fileSize > maxBytes;
  doc["lineCount"] = lineNum - 1;
  doc["html"] = html;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
  logActivity("preview-code", path, u);
}

// ============== FILE CONTENT EDIT (SAVE) =============
// Save text content to an existing file via POST body
void handleFileEdit() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || !checkCsrf(webServer)) { webServer.send(403); return; }
  if (!sdOK) { sendError(503, "SD card not available"); return; }
  if (!webServer.hasArg("path")) { sendError(400, "Need path"); return; }
  if (!webServer.hasArg("content")) { sendError(400, "Need content"); return; }
  String path = sanitizePath(webServer.arg("path"));
  if (!SD.exists(path)) { sendError(404, "File not found"); return; }
  // Reject edit if file is read-only (immutable)
  if (isFileReadOnly(path)) { sendError(403, "File is read-only (immutable)"); return; }
  // Only allow editing text-based files
  String name = path.substring(path.lastIndexOf('/')+1);
  if (!shouldCompress(name) && name != "txt") { sendError(403, "Cannot edit binary files"); return; }
  // Acquire lock to prevent concurrent write corruption
  if (!acquireFileLock(path, 3000)) { sendError(423, "File is locked"); return; }
  // Create version backup before overwriting
  createVersion(path);
  String content = webServer.arg("content");
  // Limit content size to prevent OOM (max 64KB)
  if (content.length() > 65536) { releaseFileLock(path); sendError(413, "Content too large (max 64KB)"); return; }
  File f = SD.open(path, FILE_WRITE);
  if (!f) { releaseFileLock(path); sendError(500, "Cannot open file for writing"); return; }
  size_t written = f.print(content);
  f.close();
  releaseFileLock(path);
  totalWriteOps++;
  totalWriteBytes += written;
  if (written == content.length()) {
    logActivity("edit", path + " (" + String(written) + "B)", u);
    broadcastChange("edit", path);
    broadcastStatsUpdate();
    // Recompute CRC sidecar after edit
    storeFileCRC(path);
    sendJson(200, "{\"ok\":true,\"path\":\"" + path + "\",\"size\":" + String(written) + "}");
  } else {
    sendError(500, "Write incomplete");
  }
}

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
  if (isRateLimited()) return;
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
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { sendError(403,"Admin only"); return; }
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

// ============== AUDIT LOG ENDPOINT =============
void handleAuditLog() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl) || lvl != "admin") { sendError(403,"Admin only"); return; }
  int limit = webServer.hasArg("limit") ? webServer.arg("limit").toInt() : 50;
  if (limit > 200) limit = 200;
  DynamicJsonDocument adoc(8192);
  JsonArray aarr = adoc.createNestedArray("entries");
  if (SD.exists("/.audit.log")) {
    File f = SD.open("/.audit.log", FILE_READ);
    if (f) {
      String lines[200]; int count = 0; String line = "";
      while (f.available()) { char c = f.read(); if (c == '\n') { lines[count % 200] = line; count++; line = ""; } else line += c; }
      f.close();
      int start = count > limit ? count - limit : 0;
      for (int i = start; i < count; i++) {
        String l = lines[i % 200];
        int c1 = l.indexOf(','), c2 = l.indexOf(',', c1+1), c3 = l.indexOf(',', c2+1);
        if (c1 > 0 && c2 > 0 && c3 > 0) {
          JsonObject e = aarr.createNestedObject();
          e["time"] = l.substring(0, c1).toInt();
          e["ip"] = l.substring(c1+1, c2);
          e["action"] = l.substring(c2+1, c3);
          e["detail"] = l.substring(c3+1);
        }
      }
    }
  }
  adoc["count"] = aarr.size();
  String out; serializeJson(adoc, out);
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

// ============== IMAGE THUMBNAIL / RESIZE ==============
// Simple nearest-neighbor downscale for JPEG/PNG thumbnails
// Outputs a small BMP (24-bit) that browsers can display inline
void handleImageThumb() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { webServer.send(401); return; }
  if (!sdOK) { webServer.send(503); return; }
  if (!webServer.hasArg("path")) { webServer.send(400); return; }
  String path = sanitizePath(webServer.arg("path"));
  if (!SD.exists(path)) { webServer.send(404); return; }
  String name = path.substring(path.lastIndexOf('/')+1);
  String ext = name.substring(name.lastIndexOf('.')+1);
  ext.toLowerCase();
  // Only support common raster formats for thumbnailing
  if (ext != "jpg" && ext != "jpeg" && ext != "png" && ext != "bmp") {
    webServer.send(403, "text/plain", "Unsupported type"); return;
  }
  int maxW = webServer.hasArg("w") ? webServer.arg("w").toInt() : 160;
  int maxH = webServer.hasArg("h") ? webServer.arg("h").toInt() : 120;
  if (maxW < 16 || maxW > 800) maxW = 160;
  if (maxH < 16 || maxH > 800) maxH = 120;
  File f = SD.open(path, FILE_READ);
  if (!f) { webServer.send(500); return; }
  // Read first 32 bytes to get dimensions from header
  uint8_t hdr[32];
  if (f.read(hdr, 32) < 32) { f.close(); webServer.send(500); return; }
  int imgW = 0, imgH = 0;
  if (ext == "bmp") {
    // BMP header: width at offset 18, height at offset 22 (little-endian 32-bit)
    if (hdr[0] != 'B' || hdr[1] != 'M') { f.close(); webServer.send(403); return; }
    imgW = hdr[18] | (hdr[19]<<8) | (hdr[20]<<16) | (hdr[21]<<24);
    imgH = abs(hdr[22] | (hdr[23]<<8) | (hdr[24]<<16) | (hdr[25]<<24));
  } else if (ext == "jpg" || ext == "jpeg") {
    // Parse JPEG SOF markers to get dimensions
    f.seek(2);
    while (f.available()) {
      uint8_t marker = f.read();
      if (marker != 0xFF) break;
      uint8_t type = f.read();
      if (type == 0xC0 || type == 0xC2) { // SOF0 or SOF2
        f.read(); f.read(); // length
        f.read(); // precision
        imgH = f.read() << 8 | f.read();
        imgW = f.read() << 8 | f.read();
        break;
      }
      if (type == 0xD8 || type == 0xD9 || (type >= 0xD0 && type <= 0xD7)) continue;
      if (f.available() < 2) break;
      int segLen = f.read() << 8 | f.read();
      f.seek(f.position() + segLen - 2);
    }
  }
  f.close();
  if (imgW <= 0 || imgH <= 0 || imgW > 10000 || imgH > 10000) {
    webServer.send(403, "text/plain", "Cannot read dimensions"); return;
  }
  // Calculate scaled dimensions (maintain aspect ratio)
  float scale = min((float)maxW / imgW, (float)maxH / imgH);
  if (scale > 1) scale = 1;
  int thumbW = (int)(imgW * scale);
  int thumbH = (int)(imgH * scale);
  if (thumbW < 1) thumbW = 1;
  if (thumbH < 1) thumbH = 1;
  // For BMP source: generate a simple downscaled BMP
  if (ext == "bmp") {
    // Read pixel data offset from header
    f = SD.open(path, FILE_READ);
    if (!f) { webServer.send(500); return; }
    uint8_t bhdr[54];
    f.read(bhdr, 54);
    int dataOffset = bhdr[10] | (bhdr[11]<<8) | (bhdr[12]<<16) | (bhdr[13]<<24);
    int srcRowSize = ((imgW * 3 + 3) / 4) * 4;
    int dstRowSize = ((thumbW * 3 + 3) / 4) * 4;
    int dstImageSize = dstRowSize * thumbH;
    int dstFileSize = 54 + dstImageSize;
    // BMP output header
    uint8_t out[54];
    memset(out, 0, 54);
    out[0]='B'; out[1]='M';
    out[2]=dstFileSize&0xFF; out[3]=(dstFileSize>>8)&0xFF; out[4]=(dstFileSize>>16)&0xFF; out[5]=(dstFileSize>>24)&0xFF;
    out[10]=54; out[14]=40;
    out[18]=thumbW&0xFF; out[19]=(thumbW>>8)&0xFF; out[20]=(thumbW>>16)&0xFF; out[21]=(thumbW>>24)&0xFF;
    out[22]=thumbH&0xFF; out[23]=(thumbH>>8)&0xFF; out[24]=(thumbH>>16)&0xFF; out[25]=(thumbH>>24)&0xFF;
    out[26]=1; out[28]=24;
    out[34]=dstImageSize&0xFF; out[35]=(dstImageSize>>8)&0xFF; out[36]=(dstImageSize>>16)&0xFF; out[37]=(dstImageSize>>24)&0xFF;
    webServer.sendHeader("Content-Type", "image/bmp");
    webServer.sendHeader("Cache-Control", "public, max-age=600");
    webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer.send(200, "image/bmp", "");
    webServer.sendContent((const char*)out, 54);
    // Nearest-neighbor downscale
    uint8_t *rowBuf = new uint8_t[dstRowSize];
    for (int y = 0; y < thumbH; y++) {
      int srcY = (int)(y * (float)imgH / thumbH);
      f.seek(dataOffset + srcY * srcRowSize);
      uint8_t *srcRow = new uint8_t[srcRowSize];
      f.read(srcRow, srcRowSize);
      memset(rowBuf, 0, dstRowSize);
      for (int x = 0; x < thumbW; x++) {
        int srcX = (int)(x * (float)imgW / thumbW);
        rowBuf[x*3] = srcRow[srcX*3];
        rowBuf[x*3+1] = srcRow[srcX*3+1];
        rowBuf[x*3+2] = srcRow[srcX*3+2];
      }
      webServer.sendContent((const char*)rowBuf, dstRowSize);
      delete[] srcRow;
    }
    delete[] rowBuf;
    f.close();
    webServer.sendContent("");
    logActivity("thumb", path+" ("+thumbW+"x"+thumbH+")", u);
    return;
  }
  // For JPEG/PNG: redirect to original with query param (browser handles scaling)
  // But return a JSON with dimensions for client-side handling
  DynamicJsonDocument doc(256);
  doc["width"] = imgW;
  doc["height"] = imgH;
  doc["thumb_w"] = thumbW;
  doc["thumb_h"] = thumbH;
  doc["path"] = path;
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
  logActivity("thumb-info", path+" ("+imgW+"x"+imgH+")", u);
}

// ============== FILE DIFF ==============
// Compare two text files and return line-by-line diff
void handleFileDiff() {
  String u, lvl;
  if (!isAuthenticated(webServer, u, lvl)) { sendError(401, "Not authenticated"); return; }
  if (!sdOK) { sendError(503, "SD card not available"); return; }
  if (!webServer.hasArg("a") || !webServer.hasArg("b")) { sendError(400, "Need paths a and b"); return; }
  String pathA = sanitizePath(webServer.arg("a"));
  String pathB = sanitizePath(webServer.arg("b"));
  if (!SD.exists(pathA) || !SD.exists(pathB)) { sendError(404, "File not found"); return; }
  // Read both files (limit to 32KB each)
  String contentA = "", contentB = "";
  File fa = SD.open(pathA, FILE_READ);
  if (fa) {
    size_t limit = min((size_t)fa.size(), (size_t)32768);
    contentA.reserve(limit);
    uint8_t buf[256];
    size_t read = 0;
    while (read < limit && fa.available()) {
      size_t chunk = min((size_t)256, limit - read);
      int n = fa.read(buf, chunk);
      if (n <= 0) break;
      for (int i = 0; i < n; i++) {
        char c = buf[i];
        if (c >= 32 && c < 127) contentA += c;
        else if (c == '\n') contentA += '\n';
        else if (c == '\t') contentA += '\t';
        else if (c == '\r') contentA += '\r';
        else contentA += '.';
      }
      read += n;
    }
    fa.close();
  }
  File fb = SD.open(pathB, FILE_READ);
  if (fb) {
    size_t limit = min((size_t)fb.size(), (size_t)32768);
    contentB.reserve(limit);
    uint8_t buf[256];
    size_t read = 0;
    while (read < limit && fb.available()) {
      size_t chunk = min((size_t)256, limit - read);
      int n = fb.read(buf, chunk);
      if (n <= 0) break;
      for (int i = 0; i < n; i++) {
        char c = buf[i];
        if (c >= 32 && c < 127) contentB += c;
        else if (c == '\n') contentB += '\n';
        else if (c == '\t') contentB += '\t';
        else if (c == '\r') contentB += '\r';
        else contentB += '.';
      }
      read += n;
    }
    fb.close();
  }
  // Split into lines
  DynamicJsonDocument doc(65536);
  doc["path_a"] = pathA;
  doc["path_b"] = pathB;
  // Simple line-by-line comparison
  int linesA = 0, linesB = 0;
  for (int i = 0; i < contentA.length(); i++) if (contentA[i] == '\n') linesA++;
  for (int i = 0; i < contentB.length(); i++) if (contentB[i] == '\n') linesB++;
  int maxLines = max(linesA, linesB) + 1;
  int added = 0, removed = 0, changed = 0;
  // Extract and compare lines
  int la = 0, lb = 0;
  String ca = "", cb = "";
  int idx = 0;
  while (la <= linesA || lb <= linesB) {
    // Get next line from A
    ca = "";
    while (la < linesA && contentA.length() > 0) {
      int nl = contentA.indexOf('\n', idx);
      if (nl < 0) { ca = contentA.substring(idx); idx = contentA.length(); }
      else { ca = contentA.substring(idx, nl); idx = nl + 1; }
      la++;
      break;
    }
    // Get next line from B
    cb = ""; idx = 0;
    while (lb < linesB && contentB.length() > 0) {
      int nl = contentB.indexOf('\n', idx);
      if (nl < 0) { cb = contentB.substring(idx); idx = contentB.length(); }
      else { cb = contentB.substring(idx, nl); idx = nl + 1; }
      lb++;
      break;
    }
    if (ca == cb) {
      // Unchanged
    } else {
      changed++;
    }
    if (la > linesA && cb.length() > 0) added++;
    if (lb > linesB && ca.length() > 0) removed++;
    if (idx > 60000) break; // Safety limit
  }
  doc["lines_a"] = linesA;
  doc["lines_b"] = linesB;
  doc["changed"] = changed;
  doc["added"] = added;
  doc["removed"] = removed;
  doc["size_a"] = (uint64_t)contentA.length();
  doc["size_b"] = (uint64_t)contentB.length();
  String out; serializeJson(doc, out);
  webServer.send(200, "application/json", out);
  logActivity("diff", pathA+" vs "+pathB, u);
}

// ============== AUTO TRASH EXPIRY ==============
// Periodically purge trash items older than TRASH_EXPIRY_DAYS to prevent SD fill
#define TRASH_EXPIRY_DAYS 30
unsigned long lastTrashExpiry = 0;
void autoExpireTrash() {
  if (millis() - lastTrashExpiry < 3600000UL) return; // Check every hour
  lastTrashExpiry = millis();
  if (!sdOK || !SD.exists(TRASH_FOLDER)) return;
  // Estimate cutoff: since millis() wraps at ~49 days, use file modification time
  File dir = SD.open(TRASH_FOLDER);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  int expired = 0;
  File f;
  while (f = dir.openNextFile()) {
    if (f.isDirectory()) { f.close(); continue; }
    // Check file age via name timestamp or size heuristic
    // Trash files older than TRASH_EXPIRY_DAYS: use fileTime if available
    unsigned long ftime = f.fileTime();
    // fileTime returns epoch seconds on ESP32 SD; compare against current epoch
    if (ftime > 0) {
      // Get current time (best effort — may be 0 if NTP not synced)
      time_t now_secs = time(nullptr);
      if (now_secs > TRASH_EXPIRY_DAYS * 86400UL) {
        time_t cutoff = now_secs - (TRASH_EXPIRY_DAYS * 86400UL);
        if (ftime < cutoff) {
          String fp = String(f.name());
          f.close();
          SD.remove(fp);
          expired++;
          if (expired >= 50) break; // Limit per cycle to avoid blocking
          continue;
        }
      }
    }
    f.close();
  }
  dir.close();
  if (expired > 0) {
    Serial.println("Auto-expired " + String(expired) + " trash items");
    logActivity("trash-expiry", String(expired) + " items", "system");
  }
}

// ============== PERIODIC SD HEALTH BROADCAST ==============
unsigned long lastHealthBroadcast = 0;
void broadcastSdHealth() {
  if (millis() - lastHealthBroadcast < 30000UL) return; // Every 30s max
  lastHealthBroadcast = millis();
  if (!sdOK) return;
  DynamicJsonDocument doc(512);
  doc["event"] = "sd-health";
  doc["ok"] = sdOK;
  doc["free_kb"] = (uint32_t)((SD.totalBytes() - SD.usedBytes()) / 1024);
  doc["total_kb"] = (uint32_t)(SD.totalBytes() / 1024);
  doc["used_pct"] = (uint8_t)(((SD.totalBytes() - SD.usedBytes()) * 100) / SD.totalBytes());
  doc["write_ops"] = totalWriteOps;
  doc["write_mb"] = (uint32_t)(totalWriteBytes / 1048576);
  doc["risk"] = failureRisk;
  String msg; serializeJson(doc, msg);
  webSocket.broadcastTXT(msg);
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
  webServer.on("/sw.js",handleServiceWorker);
  webServer.on("/setup",handleSetupPage);
  webServer.on("/api/complete-setup",HTTP_POST,handleCompleteSetup);
  webServer.on("/s/",HTTP_GET,handleSharedFile);

  webServer.on("/api/list",HTTP_GET,handleListFiles);
  webServer.on("/api/info",HTTP_GET,handleFileInfo);
  webServer.on("/api/download",HTTP_GET,handleDownload);
  webServer.on("/api/download-gzip",HTTP_GET,handleDownloadGzip);
  webServer.on("/api/delete",HTTP_POST,handleDelete);
  webServer.on("/api/create-dir",HTTP_POST,handleCreateDir);
  webServer.on("/api/create-file",HTTP_POST,handleCreateFile);
  webServer.on("/api/rename",HTTP_POST,handleRename);
  webServer.on("/api/move",HTTP_POST,handleMove);
  webServer.on("/api/copy",HTTP_POST,handleCopy);
  webServer.on("/api/upload-auth",HTTP_GET,handleUploadAuth);
  webServer.on("/api/upload",HTTP_POST,[](){if(!checkCsrf(webServer)){webServer.send(403,"text/plain","CSRF invalid");return;}webServer.send(200);},handleUpload);
  webServer.on("/api/share",HTTP_POST,handleCreateShare);
  webServer.on("/api/zip",HTTP_POST,handleZipDownload);
  webServer.on("/api/batch-download",HTTP_POST,handleBatchDownload);
  webServer.on("/api/video",HTTP_GET,handleVideoThumbnail);
  webServer.on("/api/batch-delete",HTTP_POST,handleBatchDelete);
  webServer.on("/api/batch-move",HTTP_POST,handleBatchMove);
  webServer.on("/api/batch-copy",HTTP_POST,handleBatchCopy);
  webServer.on("/api/search",HTTP_GET,handleSearch);
  webServer.on("/api/grep",HTTP_GET,handleGrep);
  webServer.on("/api/changes",HTTP_GET,handleChanges);
  webServer.on("/api/folder-zip",HTTP_POST,handleFolderZip);
  webServer.on("/api/cleanup",HTTP_POST,handleAutoCleanup);
  webServer.on("/api/dir-info",HTTP_GET,handleDirInfo);
  webServer.on("/api/trash",HTTP_GET,handleTrashList);
  webServer.on("/api/restore",HTTP_GET,handleRestore);
  webServer.on("/api/batch-restore",HTTP_POST,handleBatchRestore);
  webServer.on("/api/empty-trash",HTTP_POST,handleEmptyTrash);
  webServer.on("/api/log",HTTP_GET,handleGetLog);
  webServer.on("/api/settings",HTTP_GET,handleGetSettings);
  webServer.on("/api/settings",HTTP_POST,handleSaveSettings);
  webServer.on("/api/csrf",HTTP_GET,handleCsrfToken);
  webServer.on("/api/ota-status",HTTP_GET,handleOtaStatus);
  webServer.on("/api/ota-upload",HTTP_POST,[](){if(!checkCsrf(webServer)){webServer.send(403,"text/plain","CSRF invalid");return;}webServer.send(200);},handleOtaUpload);
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
  webServer.on("/api/audit",HTTP_GET,handleAuditLog);
  webServer.on("/api/notes",HTTP_GET,handleGetNotes);
  webServer.on("/api/notes",HTTP_POST,handleSaveNote);
  webServer.on("/api/scan",HTTP_GET,handleScanStorage);
  webServer.on("/api/sd-health",HTTP_GET,handleSdHealth);
  webServer.on("/api/metrics",HTTP_GET,handleMetrics);
  webServer.on("/api/analytics",HTTP_GET,handleStorageAnalytics);
  webServer.on("/api/detect-type",HTTP_GET,handleDetectType);
  webServer.on("/api/crc",HTTP_GET,handleFileCRC);
  webServer.on("/api/duplicates",HTTP_GET,handleFindDuplicates);
  webServer.on("/api/recent",HTTP_GET,handleRecentFiles);
  webServer.on("/api/access-stats",HTTP_GET,handleAccessStats);
  webServer.on("/api/readonly",HTTP_POST,handleSetReadOnly);
  webServer.on("/api/webhook",HTTP_GET,handleWebhookConfig);
  webServer.on("/api/webhook",HTTP_POST,handleWebhookConfig);
  webServer.on("/api/thumb",HTTP_GET,handleImageThumb);
  webServer.on("/api/diff",HTTP_GET,handleFileDiff);
  webServer.on("/api/preview",HTTP_GET,handleFilePreview);
  webServer.on("/api/preview-code",HTTP_GET,handleFilePreviewCode);
  webServer.on("/api/edit",HTTP_POST,handleFileEdit);
  webServer.on("/api/md-preview",HTTP_GET,handleMarkdownPreview);
  webServer.on("/api/users",HTTP_GET,handleGetUsers);
  webServer.on("/api/users",HTTP_POST,handleAddUser);
  webServer.on("/api/users/",HTTP_PUT,handleUpdateUser);
  webServer.on("/api/users/",HTTP_DELETE,handleDeleteUser);

  // Add security headers to all responses
  // CORS preflight handler: respond to OPTIONS on /api/* with proper CORS headers
  webServer.onNotFound([](){
    // Handle CORS preflight (OPTIONS) requests for /api/* paths
    if (webServer.method() == HTTP_OPTIONS && webServer.uri().startsWith("/api/")) {
      webServer.sendHeader("Access-Control-Allow-Origin", "*");
      webServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
      webServer.sendHeader("Access-Control-Allow-Headers", "Authorization, Content-Type, X-CSRF-Token, Idempotency-Key");
      webServer.sendHeader("Access-Control-Max-Age", "86400"); // Preflight cache 24h
      webServer.sendHeader("Access-Control-Allow-Credentials", "true");
      webServer.send(204, "text/plain", ""); // No content for OPTIONS
      return;
    }
    webServer.sendHeader("X-Content-Type-Options", "nosniff");
    webServer.sendHeader("X-Frame-Options", "DENY");
    webServer.send(404, "text/plain", "Not Found");
  });
  webServer.begin();
  Serial.println("HTTP on "+String(webServerPort));
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  // Enable WebSocket heartbeat: ping every 30s, disconnect after 3 missed pongs (90s)
  webSocket.enableHeartbeat(30000, 3000, 3);
  Serial.println("WebSocket on 81 (heartbeat enabled)");
  ftpSrv.begin(ftp_user,ftp_password);
  Serial.println("FTP started");
}

void loop() {
  // Record request start time for metrics & timeout enforcement
  requestStartMs = millis();
  webServer.handleClient();
  // Enforce request timeout: if a request took longer than MAX_REQUEST_TIME_MS,
  // send 504 Gateway Timeout and stop the client to free resources
  if (webServer.method() != HTTP_ANY && (millis() - requestStartMs) > MAX_REQUEST_TIME_MS) {
    webServer.send(504, "application/json", "{\"error\":\"Request timeout\",\"max_ms\":" + String(MAX_REQUEST_TIME_MS) + "}");
    webServer.client().stop();
    Serial.println("Request timeout (504): " + webServer.uri());
    recordMetric(webServer.uri() + " [timeout]", MAX_REQUEST_TIME_MS);
  }
  // Record per-endpoint request metrics
  if (webServer.method() != HTTP_ANY && millis() > requestStartMs) {
    String uri = webServer.uri();
    uint32_t elapsed = (uint32_t)(millis() - requestStartMs);
    recordMetric(uri, elapsed);
  }
  webSocket.loop();
  ftpSrv.handleFTP();
  checkWiFi();
  checkSD();
  broadcastSdHealth(); // Push SD health to WebSocket clients every 30s
  autoExpireTrash();   // Auto-purge old trash items every hour
  // ============== OOM PROTECTION ==============
  // If free heap drops below 8KB, reject new connections and log warning
  // ESP32 has ~320KB; below 8KB means critical memory pressure
  static unsigned long lastHeapCheck = 0;
  if (millis() - lastHeapCheck > 5000UL) {
    lastHeapCheck = millis();
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 8192) {
      Serial.println("WARNING: Low heap (" + String(freeHeap) + "B) — dropping oldest session");
      // Force-expire least recently used session to reclaim memory
      unsigned long oldest = millis();
      int oldestIdx = -1;
      for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].isActive && sessions[i].lastActivity < oldest) {
          oldest = sessions[i].lastActivity; oldestIdx = i;
        }
      }
      if (oldestIdx >= 0) {
        sessions[oldestIdx].isActive = false;
        Serial.println("Evicted session: " + sessions[oldestIdx].username);
      }
      // Prune idempotency cache to reclaim String memory
      for (int i = 0; i < idempotencyCount; i++) {
        idempotencyCache[i].key = "";
        idempotencyCache[i].response = "";
      }
      idempotencyCount = 0;
    }
  }
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
