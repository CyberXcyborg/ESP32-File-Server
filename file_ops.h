/usr/bin/bash: warning: setlocale: LC_ALL: cannot change locale (en_US.UTF-8): No such file or directory
#ifndef FILE_OPS_H
#define FILE_OPS_H

#include "config.h"

bool initSDCard() {
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) return false;
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) return false;
  return true;
}

String getFileSize(uint64_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1024 * 1024) return String(bytes / 1024.0, 1) + " KB";
  if (bytes < 1024 * 1024 * 1024) return String(bytes / (1024.0 * 1024.0), 1) + " MB";
  return String(bytes / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
}

uint64_t getDirSize(String path) {
  uint64_t total = 0;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return 0;
  File file;
  while (file = dir.openNextFile()) {
    if (file.isDirectory()) total += getDirSize(String(file.path()));
    else total += file.size();
    file.close();
  }
  dir.close();
  return total;
}

bool createDirRecursive(String path) {
  if (SD.exists(path)) return true;
  int slash = path.lastIndexOf('/');
  if (slash > 0) {
    String parent = path.substring(0, slash);
    if (!createDirRecursive(parent)) return false;
  }
  return SD.mkdir(path);
}

bool removeDir(String path) {
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return false;
  File file;
  while (file = dir.openNextFile()) {
    String fp = String(file.path());
    if (file.isDirectory()) removeDir(fp);
    else SD.remove(fp);
    file.close();
  }
  dir.close();
  return SD.rmdir(path);
}

bool moveToTrash(String path) {
  if (!SD.exists(TRASH_FOLDER)) {
    if (!createDirRecursive(TRASH_FOLDER)) return false;
  }
  String trashPath = TRASH_FOLDER + path;
  int slash = trashPath.lastIndexOf('/');
  if (slash > 0) createDirRecursive(trashPath.substring(0, slash));
  return SD.rename(path, trashPath);
}

bool restoreFromTrash(String trashPath) {
  String origPath = trashPath;
  origPath.replace(TRASH_FOLDER, "");
  int slash = origPath.lastIndexOf('/');
  if (slash > 0) createDirRecursive(origPath.substring(0, slash));
  return SD.rename(trashPath, origPath);
}

// ============== FILE MOVE/COPY ==============
bool moveFile(String src, String dst) {
  if (!SD.exists(src)) return false;
  int slash = dst.lastIndexOf('/');
  if (slash > 1) {
    String parent = dst.substring(0, slash);
    createDirRecursive(parent);
  }
  // Try rename first (same partition), fallback to copy+delete
  if (SD.rename(src, dst)) return true;
  // Copy + delete
  File in = SD.open(src, FILE_READ);
  if (!in) return false;
  File out = SD.open(dst, FILE_WRITE);
  if (!out) { in.close(); return false; }
  uint8_t buf[512];
  while (in.available()) {
    int n = in.read(buf, 512);
    out.write(buf, n);
  }
  in.close(); out.close();
  return SD.remove(src);
}

// ============== GZIP COMPRESSION ==============
// Compress file data using simple deflate (miniz or manual RLE for ESP32)
// For large files we use chunked transfer with compression hint
bool shouldCompress(String fileName) {
  String ext = fileName.substring(fileName.lastIndexOf('.') + 1);
  ext.toLowerCase();
  // Compress text-based files > 10KB for download
  return ext=="txt"||ext=="html"||ext=="htm"||ext=="css"||ext=="js"||ext=="json"||ext=="xml"||
         ext=="md"||ext=="csv"||ext=="log"||ext=="svg"||ext=="ini"||ext=="cfg";
}

String getContentType(String fileName) {
  String ext = fileName.substring(fileName.lastIndexOf('.') + 1);
  ext.toLowerCase();
  if (ext=="html"||ext=="htm") return "text/html";
  if (ext=="css") return "text/css";
  if (ext=="js") return "application/javascript";
  if (ext=="json") return "application/json";
  if (ext=="png") return "image/png";
  if (ext=="jpg"||ext=="jpeg") return "image/jpeg";
  if (ext=="gif") return "image/gif";
  if (ext=="svg") return "image/svg+xml";
  if (ext=="pdf") return "application/pdf";
  if (ext=="mp3") return "audio/mpeg";
  if (ext=="mp4") return "video/mp4";
  if (ext=="zip") return "application/zip";
  if (ext=="gz") return "application/gzip";
  return "application/octet-stream";
}

bool copyFile(String src, String dst) {
  if (!SD.exists(src)) return false;
  int slash = dst.lastIndexOf('/');
  if (slash > 1) {
    String parent = dst.substring(0, slash);
    createDirRecursive(parent);
  }
  File in = SD.open(src, FILE_READ);
  if (!in) return false;
  File out = SD.open(dst, FILE_WRITE);
  if (!out) { in.close(); return false; }
  uint8_t buf[512];
  while (in.available()) {
    int n = in.read(buf, 512);
    out.write(buf, n);
  }
  in.close(); out.close();
  return true;
}

// ============== ACTIVITY LOG ==============
void logActivity(String action, String path, String username) {
  // Truncate log to last 1000 entries to prevent SD fill
  const int MAX_LOG_ENTRIES = 1000;
  const int KEEP_ENTRIES = 500;
  if (SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_READ);
    if (f) {
      // Count lines
      int lines = 0;
      while (f.available()) { if (f.read() == '\n') lines++; }
      f.close();
      if (lines > MAX_LOG_ENTRIES) {
        // Keep only last KEEP_ENTRIES
        File src = SD.open(LOG_FILE, FILE_READ);
        File dst = SD.open(LOG_FILE ".tmp", FILE_WRITE);
        if (src && dst) {
          int skipped = lines - KEEP_ENTRIES;
          int current = 0;
          bool inPartial = true;
          while (src.available()) {
            char c = src.read();
            if (inPartial && c == '\n') {
              current++;
              if (current >= skipped) inPartial = false;
            }
            if (!inPartial) dst.write(c);
          }
          src.close(); dst.close();
          SD.remove(LOG_FILE);
          SD.rename(LOG_FILE ".tmp", LOG_FILE);
        } else {
          if (src) src.close();
          if (dst) dst.close();
        }
      }
    }
  }
  File logFile = SD.open(LOG_FILE, FILE_APPEND);
  if (!logFile) {
    logFile = SD.open(LOG_FILE, FILE_WRITE);
    if (!logFile) return;
  }
  logFile.print(millis());
  logFile.print(",");
  logFile.print(username);
  logFile.print(",");
  logFile.print(action);
  logFile.print(",");
  logFile.println(path);
  logFile.close();
}

// ============== FILE VERSIONING ==============
bool createVersion(String path) {
  if (!SD.exists(path)) return false;
  if (!SD.exists(VERSIONS_FOLDER)) createDirRecursive(VERSIONS_FOLDER);
  File f = SD.open(path);
  if (!f) return false;
  bool isDir = f.isDirectory();
  f.close();
  if (isDir) return false;
  String verDir = VERSIONS_FOLDER + path;
  int slash = verDir.lastIndexOf('/');
  if (slash > 0) createDirRecursive(verDir.substring(0, slash));
  int dot = verDir.lastIndexOf('.');
  String verPath;
  if (dot > slash) verPath = verDir.substring(0, dot) + "_" + String(millis()) + verDir.substring(dot);
  else verPath = verDir + "_" + String(millis());
  File src = SD.open(path, FILE_READ);
  if (!src) return false;
  File dst = SD.open(verPath, FILE_WRITE);
  if (!dst) { src.close(); return false; }
  uint8_t buf[512];
  while (src.available()) { int n = src.read(buf, 512); dst.write(buf, n); }
  src.close(); dst.close();
  return true;
}

// ============== SHARING ==============
bool createShare(String path, String &shareToken) {
  shareToken = "";
  const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 16; i++) shareToken += charset[random(0, 62)];
  DynamicJsonDocument doc(1024);
  if (SD.exists(SHARES_FILE)) {
    File f = SD.open(SHARES_FILE, FILE_READ);
    if (f) { deserializeJson(doc, f); f.close(); }
  }
  JsonObject share = doc.createNestedObject(shareToken);
  share["path"] = path;
  share["created"] = millis();
  share["downloads"] = 0;
  File f = SD.open(SHARES_FILE, FILE_WRITE);
  if (!f) return false;
  serializeJson(doc, f); f.close();
  return true;
}

bool getSharePath(String token, String &path) {
  if (!SD.exists(SHARES_FILE)) return false;
  File f = SD.open(SHARES_FILE, FILE_READ);
  if (!f) return false;
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, f); f.close();
  JsonObject share = doc[token];
  if (share.isNull()) return false;
  path = share["path"].as<String>();
  share["downloads"] = (share["downloads"] | 0) + 1;
  File fw = SD.open(SHARES_FILE, FILE_WRITE);
  if (fw) { serializeJson(doc, fw); fw.close(); }
  return true;
}

// ============== SETTINGS ==============
void loadSettings() {
  if (!SD.exists(SETTINGS_FILE)) return;
  File f = SD.open(SETTINGS_FILE, FILE_READ);
  if (!f) return;
  DynamicJsonDocument doc(512);
  deserializeJson(doc, f); f.close();
  // Apply web port if saved and valid
  uint16_t savedPort = doc["web_port"] | 0;
  if (savedPort >= 80 && savedPort <= 65535) {
    webServerPort = savedPort;
  }
}

bool saveSettings(String wifiSsid, String wifiPass, String apSsid, String apPass, String ftpUser, String ftpPass) {
  DynamicJsonDocument doc(512);
  doc["wifi_ssid"] = wifiSsid;
  doc["wifi_pass"] = wifiPass;
  doc["ap_ssid"] = apSsid;
  doc["ap_pass"] = apPass;
  doc["ftp_user"] = ftpUser;
  doc["ftp_pass"] = ftpPass;
  doc["web_port"] = webServerPort;
  File f = SD.open(SETTINGS_FILE, FILE_WRITE);
  if (!f) return false;
  serializeJson(doc, f); f.close();
  return true;
}

// ============== FILE LOCKING ==============
bool acquireFileLock(String path, unsigned long timeoutMs = 5000) {
  String lockPath = path + ".lock";
  unsigned long start = millis();
  while (SD.exists(lockPath)) {
    // Check if lock is stale (>30s old)
    File lockFile = SD.open(lockPath, FILE_READ);
    if (lockFile) {
      unsigned long lockTime = lockFile.read() | (lockFile.read() << 8) | (lockFile.read() << 16) | (lockFile.read() << 24);
      lockFile.close();
      if (millis() - lockTime > 30000UL) {
        SD.remove(lockPath);
        break;
      }
    }
    if (millis() - start > timeoutMs) return false;
    delay(10);
  }
  File lockFile = SD.open(lockPath, FILE_WRITE);
  if (!lockFile) return false;
  unsigned long now = millis();
  lockFile.write((uint8_t)(now & 0xFF));
  lockFile.write((uint8_t)((now >> 8) & 0xFF));
  lockFile.write((uint8_t)((now >> 16) & 0xFF));
  lockFile.write((uint8_t)((now >> 24) & 0xFF));
  lockFile.close();
  return true;
}

void releaseFileLock(String path) {
  String lockPath = path + ".lock";
  if (SD.exists(lockPath)) SD.remove(lockPath);
}

// ============== ICONS & HELPERS ==============
String getFileIcon(String fileName) {
  String ext = fileName.substring(fileName.lastIndexOf('.') + 1);
  ext.toLowerCase();
  if (ext=="jpg"||ext=="jpeg"||ext=="png"||ext=="gif"||ext=="bmp"||ext=="svg"||ext=="webp") return "🖼️";
  if (ext=="mp3"||ext=="wav"||ext=="ogg"||ext=="flac") return "🎵";
  if (ext=="mp4"||ext=="mov"||ext=="avi"||ext=="mkv") return "🎬";
  if (ext=="pdf") return "📑";
  if (ext=="doc"||ext=="docx"||ext=="txt"||ext=="md") return "📝";
  if (ext=="xls"||ext=="xlsx"||ext=="csv") return "📊";
  if (ext=="zip"||ext=="rar"||ext=="7z"||ext=="tar"||ext=="gz") return "📦";
  if (ext=="html"||ext=="htm"||ext=="css"||ext=="js"||ext=="json"||ext=="xml") return "🌐";
  if (ext=="py"||ext=="c"||ext=="cpp"||ext=="h"||ext=="java"||ext=="php") return "📟";
  return "📄";
}

bool isPreviewable(String fileName) {
  String ext = fileName.substring(fileName.lastIndexOf('.') + 1);
  ext.toLowerCase();
  return ext=="jpg"||ext=="jpeg"||ext=="png"||ext=="gif"||ext=="bmp"||ext=="svg"||ext=="webp"||
         ext=="txt"||ext=="html"||ext=="htm"||ext=="css"||ext=="js"||ext=="json"||ext=="xml"||
         ext=="md"||ext=="csv"||ext=="pdf"||ext=="mp3"||ext=="wav"||ext=="ogg";
}

// ============== COLLECT FILES RECURSIVELY ==============
void collectFiles(String path, JsonArray &files) {
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;
  File file;
  while (file = dir.openNextFile()) {
    String fn = String(file.name());
    int sl = fn.lastIndexOf('/');
    String name = (sl >= 0) ? fn.substring(sl+1) : fn;
    if (!file.isDirectory()) {
      String p = path + (path.endsWith("/")?"":"/") + name;
      JsonObject f = files.createNestedObject();
      f["name"] = name;
      f["path"] = p;
      f["size"] = file.size();
    } else {
      collectFiles(fn, files);
    }
    file.close();
  }
  dir.close();
}

// ============== AUTO-CLEANUP TRASH ==============
int autoCleanTrash(int maxAgeDays) {
  if (!SD.exists(TRASH_FOLDER)) return 0;
  unsigned long cutoff = millis() - ((unsigned long)maxAgeDays * 86400000UL);
  int cleaned = 0;
  File dir = SD.open(TRASH_FOLDER);
  if (!dir || !dir.isDirectory()) return 0;
  File file;
  while (file = dir.openNextFile()) {
    String fn = String(file.name());
    if (file.isDirectory()) {
      // Check if old
      if (file.fileTime() < cutoff) {
        removeDir(fn);
        cleaned++;
      }
    } else {
      if (file.fileTime() < cutoff) {
        SD.remove(fn);
        cleaned++;
      }
    }
    file.close();
  }
  dir.close();
  return cleaned;
}

// ============== COUNT FILES IN DIR ==============
int countFiles(String path) {
  int count = 0;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return 0;
  File file;
  while (file = dir.openNextFile()) {
    if (!file.isDirectory()) count++;
    else count += countFiles(String(file.path()));
    file.close();
  }
  dir.close();
  return count;
}

int countDirs(String path) {
  int count = 0;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return 0;
  File file;
  while (file = dir.openNextFile()) {
    if (file.isDirectory()) {
      count++;
      count += countDirs(String(file.path()));
    }
    file.close();
  }
  dir.close();
  return count;
}

// ============== COLLECT FILES RECURSIVELY ==============
void collectFiles(String path, JsonArray &files) {
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;
  File file;
  while (file = dir.openNextFile()) {
    String fn = String(file.name());
    int sl = fn.lastIndexOf('/');
    String name = (sl >= 0) ? fn.substring(sl+1) : fn;
    if (!file.isDirectory()) {
      String p = path + (path.endsWith("/")?"":"/") + name;
      JsonObject f = files.createNestedObject();
      f["name"] = name;
      f["path"] = p;
      f["size"] = file.size();
    } else {
      collectFiles(fn, files);
    }
    file.close();
  }
  dir.close();
}

// ============== AUTO-CLEANUP TRASH ==============
int autoCleanTrash(int maxAgeDays) {
  if (!SD.exists(TRASH_FOLDER)) return 0;
  unsigned long cutoff = millis() - ((unsigned long)maxAgeDays * 86400000UL);
  int cleaned = 0;
  File dir = SD.open(TRASH_FOLDER);
  if (!dir || !dir.isDirectory()) return 0;
  File file;
  while (file = dir.openNextFile()) {
    String fn = String(file.name());
    if (file.isDirectory()) {
      if (file.fileTime() < cutoff) { removeDir(fn); cleaned++; }
    } else {
      if (file.fileTime() < cutoff) { SD.remove(fn); cleaned++; }
    }
    file.close();
  }
  dir.close();
  return cleaned;
}

// ============== COUNT FILES & DIRS IN DIR ==============
int countFiles(String path) {
  int count = 0;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return 0;
  File file;
  while (file = dir.openNextFile()) {
    if (!file.isDirectory()) count++;
    else count += countFiles(String(file.path()));
    file.close();
  }
  dir.close();
  return count;
}

int countDirs(String path) {
  int count = 0;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return 0;
  File file;
  while (file = dir.openNextFile()) {
    if (file.isDirectory()) { count++; count += countDirs(String(file.path())); }
    file.close();
  }
  dir.close();
  return count;
}

// Aliases for backward compatibility
#define countFilesInDir countFiles
#define countDirsInDir countDirs

#endif
