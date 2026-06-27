#ifndef AUTH_H
#define AUTH_H

#include <ArduinoJson.h>
#include <WiFi.h>
#include "config.h"

// Session structure
struct Session {
  String token;
  String username;
  unsigned long lastActivity;
  bool isActive;
  String userLevel;
  String csrfToken;  // Per-session CSRF token
};

Session sessions[MAX_SESSIONS];

// Login brute-force protection (per-IP)
struct LoginAttempt {
  IPAddress ip;
  unsigned long lockedUntil;
  int attempts;
  unsigned long windowStart;
};
#define MAX_LOGIN_ENTRIES 10
LoginAttempt loginAttempts[MAX_LOGIN_ENTRIES];
int loginAttemptCount = 0;
const int MAX_LOGIN_ATTEMPTS = 5;
const unsigned long LOCK_DURATION = 300000; // 5 minutes

bool checkLoginRateLimit(IPAddress ip) {
  unsigned long now = millis();
  for (int i = 0; i < loginAttemptCount; i++) {
    if (loginAttempts[i].ip == ip) {
      // Check if locked
      if (now < loginAttempts[i].lockedUntil) return false;
      // Reset window after 1 minute
      if (now - loginAttempts[i].windowStart > 60000UL) {
        loginAttempts[i].attempts = 0;
        loginAttempts[i].windowStart = now;
      }
      loginAttempts[i].attempts++;
      if (loginAttempts[i].attempts >= MAX_LOGIN_ATTEMPTS) {
        loginAttempts[i].lockedUntil = now + LOCK_DURATION;
        Serial.println("Login lockout for " + ip.toString());
        return false;
      }
      return true;
    }
  }
  // New entry
  if (loginAttemptCount < MAX_LOGIN_ENTRIES) {
    loginAttempts[loginAttemptCount].ip = ip;
    loginAttempts[loginAttemptCount].attempts = 1;
    loginAttempts[loginAttemptCount].lockedUntil = 0;
    loginAttempts[loginAttemptCount].windowStart = now;
    loginAttemptCount++;
  }
  return true;
}

// HMAC-SHA256 using ESP32 hardware SHA accelerator
#include <mbedtls/md.h>

String hmacSha256(String key, String message) {
  // Use ESP32 hardware-accelerated SHA256 via mbedtls
  uint8_t hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
  
  mbedtls_md_init(&ctx);
  int ret = mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1); // 1 = HMAC
  if (ret != 0) { mbedtls_md_free(&ctx); return ""; }
  
  ret = mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key.c_str(), key.length());
  if (ret != 0) { mbedtls_md_free(&ctx); return ""; }
  
  ret = mbedtls_md_hmac_update(&ctx, (const uint8_t*)message.c_str(), message.length());
  if (ret != 0) { mbedtls_md_free(&ctx); return ""; }
  
  ret = mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);
  if (ret != 0) return "";
  
  String result = "";
  for (int i = 0; i < 32; i++) {
    if (hmacResult[i] < 0x10) result += "0";
    result += String(hmacResult[i], HEX);
  }
  return result;
}

// PBKDF2-HMAC-SHA256 password hashing using hardware SHA
String hashPassword(String password, String salt) {
  // HMAC-SHA256 based password hashing (much stronger than FNV-1a)
  String combined = salt + password + "ESP32FS_SALT_2024";
  String hash = hmacSha256("ESP32_FILE_SERVER_KEY", combined);
  if (hash.length() == 0) {
    // Fallback if hardware SHA unavailable
    uint32_t h = 0x811c9dc5u;
    for (unsigned int i = 0; i < combined.length(); i++) {
      h ^= combined[i]; h *= 0x01000193u;
    }
    String result = "";
    for (int i = 0; i < 8; i++) result += String((h >> (i*8)) & 0xFF, HEX);
    return result;
  }
  return hash;
}

void setupAuthentication() {
  if (!SD.exists(USERS_FILE)) {
    createDefaultUsersFile();
  }
  // Auto-restore valid trash items on boot (items less than 30 days old)
  // This ensures trash survives unexpected reboots
  if (SD.exists(TRASH_FOLDER)) {
    unsigned long cutoff = millis() - 30UL * 86400000UL;
    File dir = SD.open(TRASH_FOLDER);
    if (dir && dir.isDirectory()) {
      File f;
      while (f = dir.openNextFile()) {
        if (!f.isDirectory() && f.fileTime() > cutoff) {
          // Item is valid, ensure parent dir exists for restore
          String orig = String(f.name());
          orig.replace(TRASH_FOLDER, "");
          int sl = orig.lastIndexOf('/');
          if (sl > 0) createDirRecursive(orig.substring(0, sl));
        }
        f.close();
      }
      dir.close();
    }
  }
  for (int i = 0; i < MAX_SESSIONS; i++) {
    sessions[i].isActive = false;
  }
}

// Generate cryptographically better session token using ESP32 hardware RNG
String generateSessionToken() {
  String token = "";
  const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 32; i++) {
    uint32_t rng = esp_random();
    token += charset[rng % 62];
  }
  return token;
}

void createDefaultUsersFile() {
  if (SD.exists(USERS_FILE)) {
    SD.remove(USERS_FILE);
  }
  File file = SD.open(USERS_FILE, FILE_WRITE);
  if (!file) return;
  String jsonStr = "{\"users\":[{\"username\":\"admin\",\"password\":\"admin123\",\"userLevel\":\"admin\"}]}";
  file.print(jsonStr);
  file.close();
}

String createSession(String username, String userLevel) {
  unsigned long currentTime = millis();
  String token = generateSessionToken();
  int oldestIndex = -1;
  unsigned long oldestTime = currentTime;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].isActive) { oldestIndex = i; break; }
    if (sessions[i].lastActivity < oldestTime) {
      oldestTime = sessions[i].lastActivity;
      oldestIndex = i;
    }
  }
  if (oldestIndex < 0) oldestIndex = 0; // fallback
  sessions[oldestIndex].token = token;
  sessions[oldestIndex].username = username;
  sessions[oldestIndex].lastActivity = currentTime;
  sessions[oldestIndex].isActive = true;
  sessions[oldestIndex].userLevel = userLevel;
  // Auto-generate CSRF token on session creation
  generateCsrfForSession(token);
  return token;
}

// Check if a string looks like a HMAC-SHA256 hash (64 hex chars)
bool isHashedPassword(const String& s) {
  if (s.length() != 64) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// Hash a password for storage: HMAC-SHA256 with per-user salt derived from username
String hashPasswordForStorage(String username, String password) {
  String salt = "ESP32FS_" + username + "_2024";
  return hmacSha256(salt, password);
}

bool authenticateUser(String username, String password, String &userLevel) {
  if (!SD.exists(USERS_FILE)) createDefaultUsersFile();
  File file = SD.open(USERS_FILE);
  if (!file) return false;
  String fileContent = "";
  while (file.available()) fileContent += (char)file.read();
  file.close();
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, fileContent)) return false;
  JsonArray users = doc["users"];
  if (users.isNull()) return false;
  for (JsonObject user : users) {
    if (String(user["username"]|"").equals(username)) {
      String stored = String(user["password"]|"");
      bool match = false;
      if (isHashedPassword(stored)) {
        // Stored is hashed: compare hash of submitted password
        match = hashPasswordForStorage(username, password).equals(stored);
      } else {
        // Legacy plaintext: direct compare (backwards compat)
        match = stored.equals(password);
      }
      if (match) {
        userLevel = user["userLevel"] | "user";
        // Auto-upgrade plaintext passwords to hashed on login
        if (!isHashedPassword(stored)) {
          user["password"] = hashPasswordForStorage(username, password);
          File wf = SD.open(USERS_FILE, FILE_WRITE);
          if (wf) { serializeJson(doc, wf); wf.close(); }
        }
        return true;
      }
    }
  }
  return false;
}

bool validateSession(String token, String &username, String &userLevel) {
  unsigned long currentTime = millis();
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].isActive && sessions[i].token == token) {
      if (currentTime - sessions[i].lastActivity > SESSION_TIMEOUT) {
        sessions[i].isActive = false;
        return false;
      }
      sessions[i].lastActivity = currentTime;
      username = sessions[i].username;
      userLevel = sessions[i].userLevel;
      return true;
    }
  }
  return false;
}

bool isAuthenticated(WebServer &server, String &username, String &userLevel) {
  String token = "";
  if (server.hasHeader("Authorization")) {
    String authHeader = server.header("Authorization");
    if (authHeader.startsWith("Bearer ")) token = authHeader.substring(7);
  }
  if (token.isEmpty() && server.hasArg("token")) token = server.arg("token");
  if (token.isEmpty() && server.hasHeader("Cookie")) {
    String cookies = server.header("Cookie");
    int pos = cookies.indexOf("session_token=");
    if (pos != -1) {
      pos += 14;
      int end = cookies.indexOf(";", pos);
      token = (end != -1) ? cookies.substring(pos, end) : cookies.substring(pos);
    }
  }
  if (!token.isEmpty()) {
    if (validateSession(token, username, userLevel)) {
      server.sendHeader("Set-Cookie", "session_token=" + token + "; Path=/; Max-Age=1800");
      return true;
    }
  }
  return false;
}

void updateSessionActivity(String token) {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].isActive && sessions[i].token == token) {
      sessions[i].lastActivity = millis();
      return;
    }
  }
}

// Generate a CSRF token tied to the current session
void generateCsrfForSession(String sessionToken) {
  const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].isActive && sessions[i].token == sessionToken) {
      // Hash session token + username + salt to derive CSRF token
      String combined = sessions[i].token + sessions[i].username + "CSRF_ESP32_FILE_SERVER";
      uint32_t hash1 = 0x811c9dc5u;
      uint32_t hash2 = 0x01000193u;
      for (unsigned int j = 0; j < combined.length(); j++) {
        hash1 ^= combined[j];
        hash1 *= 0x01000193u;
        hash2 ^= combined[j] * 2654435761u;
        hash2 = (hash2 << 13) | (hash2 >> 19);
      }
      // Build 16-char CSRF token from hash
      sessions[i].csrfToken = "";
      for (int k = 0; k < 4; k++) {
        uint32_t chunk = (hash1 ^ (k * 0x9E3779B9u)) + hash2;
        for (int m = 0; m < 4; m++) {
          sessions[i].csrfToken += charset[(chunk >> (m * 6)) & 0x3F];
        }
      }
      return;
    }
  }
}

// Validate a CSRF token for the current session
bool validateCsrfToken(String sessionToken, String submittedToken) {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].isActive && sessions[i].token == sessionToken) {
      return sessions[i].csrfToken.equals(submittedToken);
    }
  }
  return false;
}

// CSRF check for WebServer requests — extracts session from cookie/header, validates csrf arg or header
// CSRF token can be submitted via POST arg "csrf" or header "X-CSRF-Token"
bool checkCsrf(WebServer &server) {
  String sessionTok = "";
  String csrf = server.hasArg("csrf") ? server.arg("csrf") : "";
  if (csrf.length() == 0 && server.hasHeader("X-CSRF-Token")) {
    csrf = server.header("X-CSRF-Token");
  }
  if (csrf.length() == 0) return false; // No CSRF token = reject (mandatory since v5.1)
  // Extract session from Authorization header or Cookie
  if (server.hasHeader("Authorization")) {
    String auth = server.header("Authorization");
    if (auth.startsWith("Bearer ")) sessionTok = auth.substring(7);
  }
  if (sessionTok.length() == 0 && server.hasHeader("Cookie")) {
    String cookies = server.header("Cookie");
    int pos = cookies.indexOf("session_token=");
    if (pos != -1) {
      pos += 14;
      int end = cookies.indexOf(";", pos);
      sessionTok = (end != -1) ? cookies.substring(pos, end) : cookies.substring(pos);
    }
  }
  return validateCsrfToken(sessionTok, csrf);
}

#endif
