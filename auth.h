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

void setupAuthentication() {
  if (!SD.exists(USERS_FILE)) {
    createDefaultUsersFile();
  }
  for (int i = 0; i < MAX_SESSIONS; i++) {
    sessions[i].isActive = false;
  }
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

String generateSessionToken() {
  String token = "";
  const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 32; i++) {
    token += charset[random(0, 62)];
  }
  return token;
}

String createSession(String username, String userLevel) {
  unsigned long currentTime = millis();
  String token = generateSessionToken();
  int oldestIndex = 0;
  unsigned long oldestTime = currentTime;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].isActive) { oldestIndex = i; break; }
    if (sessions[i].lastActivity < oldestTime) {
      oldestTime = sessions[i].lastActivity;
      oldestIndex = i;
    }
  }
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
