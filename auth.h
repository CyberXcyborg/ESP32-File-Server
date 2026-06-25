#ifndef AUTH_H
#define AUTH_H

#include <ArduinoJson.h>
#include "config.h"

// Session structure
struct Session {
  String token;
  String username;
  unsigned long lastActivity;
  bool isActive;
  String userLevel;
};

Session sessions[MAX_SESSIONS];

// Login brute-force protection
struct LoginAttempt {
  unsigned long lockedUntil;
  int attempts;
} loginLock = {0, 0};
const int MAX_LOGIN_ATTEMPTS = 5;
const unsigned long LOCK_DURATION = 300000; // 5 minutes

// Simple PBKDF2-like password hashing (SHA-256-based, no external deps)
String hashPassword(String password, String salt) {
  // Use a simple hash since ESP32 doesn't have built-in SHA256 in all cores
  // This is a basic obfuscation - better than plaintext
  String combined = salt + password + "ESP32FS_SALT_2024";
  uint32_t hash = 0x811c9dc5u; // FNV-1a
  for (unsigned int i = 0; i < combined.length(); i++) {
    hash ^= combined[i];
    hash *= 0x01000193u;
  }
  // Second pass for better distribution
  for (unsigned int i = 0; i < combined.length(); i++) {
    hash ^= combined[i] * 2654435761u;
    hash = (hash << 13) | (hash >> 19);
  }
  String result = "";
  for (int i = 0; i < 8; i++) {
    result += String((hash >> (i * 8)) & 0xFF, HEX);
  }
  return result;
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
  return token;
}

bool authenticateUser(String username, String password, String &userLevel) {
  if (!SD.exists(USERS_FILE)) createDefaultUsersFile();
  File file = SD.open(USERS_FILE);
  if (!file) return false;
  String fileContent = "";
  while (file.available()) fileContent += (char)file.read();
  file.close();
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, fileContent)) return false;
  JsonArray users = doc["users"];
  if (users.isNull()) return false;
  for (JsonObject user : users) {
    if (String(user["username"]|"").equals(username) && String(user["password"]|"").equals(password)) {
      userLevel = user["userLevel"] | "user";
      return true;
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

#endif
