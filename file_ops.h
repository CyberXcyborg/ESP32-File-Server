/usr/bin/bash: warning: setlocale: LC_ALL: cannot change locale (en_US.UTF-8): No such file or directory
#ifndef FILE_OPS_H
#define FILE_OPS_H

#include "config.h"
#include <WiFiClient.h>
#include <ArduinoJson.h>

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
// True gzip compression using miniz deflate for ESP32
// Compresses text-based files on-the-fly during download
#include <miniz.h>
#include <miniz_zip.h>

// ============== UPLOAD QUARANTINE ==============
// Block dangerous file types from being uploaded to protect the ESP32
bool isUploadSafe(String fileName) {
  String ext = fileName.substring(fileName.lastIndexOf('.') + 1);
  ext.toLowerCase();
  // Block potentially dangerous executables and scripts
  if (ext == "exe" || ext == "bat" || ext == "cmd" || ext == "com" ||
      ext == "scr" || ext == "pif" || ext == "vbs" || ext == "vbe" ||
      ext == "wsf" || ext == "wsh" || ext == "msi" || ext == "msp" ||
      ext == "js" || ext == "jsx" || ext == "hta" || ext == "cpl" ||
      ext == "inf" || ext == "ins" || ext == "isp" || ext == "jse" ||
      ext == "lnk" || ext == "reg" || ext == "rgs" || ext == "sct" ||
      ext == "shb" || ext == "shs" || ext == "ws" || ext == "wsc" ||
      ext == "ps1" || ext == "ps1xml" || ext == "ps2" || ext == "ps2xml" ||
      ext == "psc1" || ext == "psc2") {
    return false;
  }
  return true;
}

bool shouldCompress(String fileName) {
  String ext = fileName.substring(fileName.lastIndexOf('.') + 1);
  ext.toLowerCase();
  // Compress text-based files > 10KB for download
  return ext=="txt"||ext=="html"||ext=="htm"||ext=="css"||ext=="js"||ext=="json"||ext=="xml"||
         ext=="md"||ext=="csv"||ext=="log"||ext=="svg"||ext=="ini"||ext=="cfg"||
         ext=="sh"||ext=="py"||ext=="c"||ext=="cpp"||ext=="h"||ext=="php";
}

// Compress data buffer into gzip format using miniz deflate
// Returns compressed size (0 on failure)
size_t gzipCompress(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstCapacity) {
  // tdefl compression with gzip headers
  tdefl_compressor comp;
  size_t compLen = 0;
  
  // Initialize compressor with default compression level
  tdefl_status status = tdefl_init(&comp, NULL, NULL, TDEFL_DEFAULT_MAX_PROBES);
  if (status != TDEFL_STATUS_OKAY) return 0;
  
  // Write gzip header manually (10 bytes)
  if (dstCapacity < 18) return 18; // Need at least header + trailer
  dst[0] = 0x1F; dst[1] = 0x8B; // Magic
  dst[2] = 8; // Deflate method
  dst[3] = 0; // Flags
  dst[4] = dst[5] = dst[6] = dst[7] = 0; // MTIME = 0
  dst[8] = 0; // XFL
  dst[9] = 255; // OS (unknown)
  compLen = 10;
  
  // Compress data
  size_t inBytes = srcLen;
  const uint8_t *inBuf = src;
  size_t outCapacity = dstCapacity - compLen - 8; // Reserve 8 for trailer
  
  status = tdefl_compress(&comp, inBuf, &inBytes, dst + compLen, &outCapacity, TDEFL_FINISH);
  if (status != TDEFL_STATUS_DONE) return 0;
  compLen += outCapacity;
  
  // Write gzip trailer (CRC32 + original size)
  uint32_t crc = mz_crc32(MZ_CRC32_INIT, src, srcLen);
  dst[compLen++] = (uint8_t)(crc & 0xFF);
  dst[compLen++] = (uint8_t)((crc >> 8) & 0xFF);
  dst[compLen++] = (uint8_t)((crc >> 16) & 0xFF);
  dst[compLen++] = (uint8_t)((crc >> 24) & 0xFF);
  dst[compLen++] = (uint8_t)(srcLen & 0xFF);
  dst[compLen++] = (uint8_t)((srcLen >> 8) & 0xFF);
  dst[compLen++] = (uint8_t)((srcLen >> 16) & 0xFF);
  dst[compLen++] = (uint8_t)((srcLen >> 24) & 0xFF);
  
  return compLen;
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
  if (ext=="webp") return "image/webp";
  if (ext=="ico") return "image/x-icon";
  if (ext=="pdf") return "application/pdf";
  if (ext=="mp3") return "audio/mpeg";
  if (ext=="wav") return "audio/wav";
  if (ext=="ogg") return "audio/ogg";
  if (ext=="mp4") return "video/mp4";
  if (ext=="webm") return "video/webm";
  if (ext=="mkv") return "video/x-matroska";
  if (ext=="avi") return "video/x-msvideo";
  if (ext=="zip") return "application/zip";
  if (ext=="gz") return "application/gzip";
  if (ext=="woff2") return "font/woff2";
  if (ext=="woff") return "font/woff";
  if (ext=="ttf") return "font/ttf";
  if (ext=="otf") return "font/otf";
  if (ext=="md"||ext=="markdown") return "text/markdown";
  if (ext=="xml") return "application/xml";
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

// Recursively copy a directory tree (src -> dst)
bool copyDir(String src, String dst) {
  if (!SD.exists(src)) return false;
  createDirRecursive(dst);
  File dir = SD.open(src);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return false; }
  File f;
  while (f = dir.openNextFile()) {
    String name = String(f.name());
    // Extract just the filename from full path
    int li = name.lastIndexOf('/');
    if (li >= 0) name = name.substring(li + 1);
    String srcPath = src + (src.endsWith("/") ? "" : "/") + name;
    String dstPath = dst + (dst.endsWith("/") ? "" : "/") + name;
    if (f.isDirectory()) {
      f.close();
      if (!copyDir(srcPath, dstPath)) return false;
    } else {
      f.close();
      if (!copyFile(srcPath, dstPath)) return false;
    }
  }
  dir.close();
  return true;
}

// Count all files recursively (for stats/progress)
uint32_t countFilesRecursive(String path) {
  uint32_t count = 0;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
  File f;
  while (f = dir.openNextFile()) {
    if (f.isDirectory()) { f.close(); count += countFilesRecursive(String(f.name())); }
    else { f.close(); count++; }
  }
  dir.close();
  return count;
}

// Count all directories recursively
uint32_t countDirsRecursive(String path) {
  uint32_t count = 0;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
  File f;
  while (f = dir.openNextFile()) {
    if (f.isDirectory()) { f.close(); count += 1 + countDirsRecursive(String(f.name())); }
    else f.close();
  }
  dir.close();
  return count;
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
  // Broadcast log event to authenticated admin WebSocket clients
  broadcastLogEvent(action, path, username);
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
  // Enforce 24h share expiry (86400000 ms) to prevent stale links
  unsigned long created = share["created"] | 0UL;
  if (created > 0 && (millis() - created) > 86400000UL) {
    // Expired — remove from shares file and reject
    doc.remove(token);
    File fw = SD.open(SHARES_FILE, FILE_WRITE);
    if (fw) { serializeJson(doc, fw); fw.close(); }
    return false;
  }
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

// ============== MAGIC BYTE FILE TYPE DETECTION ==============
// Detect file type by content (first bytes) for more reliable identification
String detectFileTypeByContent(String path) {
  File f = SD.open(path, FILE_READ);
  if (!f || f.size() < 4) { if (f) f.close(); return "unknown"; }
  uint8_t header[16];
  int n = f.read(header, 16);
  f.close();
  if (n < 4) return "unknown";
  
  // Images
  if (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) return "image/jpeg";
  if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47) return "image/png";
  if (header[0] == 0x47 && header[1] == 0x49 && header[2] == 0x46 && header[3] == 0x38) return "image/gif";
  if (header[0] == 0x42 && header[1] == 0x4D) return "image/bmp";
  if (n >= 12 && header[0] == 0x52 && header[1] == 0x49 && header[2] == 0x46 && header[3] == 0x46 &&
      header[8] == 0x57 && header[9] == 0x45 && header[10] == 0x42 && header[11] == 0x50) return "image/webp";
  
  // Video
  if (n >= 12 && header[4] == 0x66 && header[5] == 0x74 && header[6] == 0x79 && header[7] == 0x70) {
    if (header[8] == 0x6D && header[9] == 0x70 && header[10] == 0x34) return "video/mp4";
    if (header[8] == 0x61 && header[9] == 0x76 && header[10] == 0x69) return "video/avi";
    if (header[8] == 0x4D && header[9] == 0x34 && header[10] == 0x56) return "video/mp4";
  }
  if (header[0] == 0x1A && header[1] == 0x45 && header[2] == 0xDF && header[3] == 0xA3) return "video/webm";
  
  // Audio
  if (header[0] == 0x49 && header[1] == 0x44 && header[2] == 0x33) return "audio/mpeg";
  if (n >= 12 && header[0] == 0x52 && header[1] == 0x49 && header[2] == 0x46 && header[3] == 0x46 &&
      header[8] == 0x57 && header[9] == 0x41 && header[10] == 0x56 && header[11] == 0x45) return "audio/wav";
  if (header[0] == 0x4F && header[1] == 0x67 && header[2] == 0x67 && header[3] == 0x53) return "audio/ogg";
  
  // Archives
  if (header[0] == 0x50 && header[1] == 0x4B && header[2] == 0x03 && header[3] == 0x04) return "application/zip";
  if (header[0] == 0x1F && header[1] == 0x8B) return "application/gzip";
  if (header[0] == 0x52 && header[1] == 0x61 && header[2] == 0x72 && header[3] == 0x21) return "application/rar";
  if (header[0] == 0x37 && header[1] == 0x7A && header[2] == 0xBC && header[3] == 0xAF) return "application/7z";
  
  // PDF
  if (header[0] == 0x25 && header[1] == 0x50 && header[2] == 0x44 && header[3] == 0x46) return "application/pdf";
  
  // Executables
  if (header[0] == 0x4D && header[1] == 0x5A) return "application/x-exe";  // PE/EXE
  if (header[0] == 0x7F && header[1] == 0x45 && header[2] == 0x4C && header[3] == 0x46) return "application/x-elf"; // ELF
  
  // Text-based detection (check if all bytes are printable/whitespace)
  bool isText = true;
  for (int i = 0; i < n && i < 512; i++) {
    if (header[i] < 32 && header[i] != '\n' && header[i] != '\r' && header[i] != '\t') {
      isText = false; break;
    }
  }
  if (isText) return "text/plain";
  
  return "application/octet-stream";
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

// ============== FILE ACCESS TRACKING =============
// Increment download counter for a file path in .access.json
void trackFileAccess(String path, String action) {
  // Load existing access metadata
  DynamicJsonDocument doc(4096);
  if (SD.exists(ACCESS_META_FILE)) {
    File f = SD.open(ACCESS_META_FILE, FILE_READ);
    if (f) { deserializeJson(doc, f); f.close(); }
  }
  String key = path;
  // JSON keys can't start with /, so prefix with _
  if (key.startsWith("/")) key = "_" + key.substring(1);
  JsonObject entry = doc[key];
  if (entry.isNull()) {
    entry = doc.createNestedObject(key);
    entry["downloads"] = 0;
    entry["views"] = 0;
    entry["lastAccess"] = 0;
  }
  if (action == "download") entry["downloads"] = (entry["downloads"] | 0) + 1;
  else if (action == "view") entry["views"] = (entry["views"] | 0) + 1;
  entry["lastAccess"] = millis();
  // Persist (limit file size by pruning entries >200)
  if (doc.size() > 200) {
    // Remove oldest entries
    JsonObject obj = doc.as<JsonObject>();
    unsigned long oldestTime = millis();
    String oldestKey = "";
    for (JsonPair kv : obj) {
      unsigned long t = kv.value()["lastAccess"] | 0;
      if (t < oldestTime) { oldestTime = t; oldestKey = kv.key().c_str(); }
    }
    if (oldestKey.length() > 0) doc.remove(oldestKey);
  }
  File wf = SD.open(ACCESS_META_FILE, FILE_WRITE);
  if (wf) { serializeJson(doc, wf); wf.close(); }
}

// Get download count for a file
int getFileDownloads(String path) {
  if (!SD.exists(ACCESS_META_FILE)) return 0;
  File f = SD.open(ACCESS_META_FILE, FILE_READ);
  if (!f) return 0;
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, f); f.close();
  String key = path;
  if (key.startsWith("/")) key = "_" + key.substring(1);
  return doc[key]["downloads"] | 0;
}

// ============== PER-FILE READ-ONLY PROTECTION =============
// Check if a file is marked read-only (immutable) in .access.json
bool isFileReadOnly(String path) {
  if (!SD.exists(ACCESS_META_FILE)) return false;
  File f = SD.open(ACCESS_META_FILE, FILE_READ);
  if (!f) return false;
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, f); f.close();
  String key = path;
  if (key.startsWith("/")) key = "_" + path.substring(1);
  return doc[key]["readonly"] | false;
}

// Set or clear read-only flag on a file
bool setFileReadOnly(String path, bool readOnly) {
  DynamicJsonDocument doc(4096);
  if (SD.exists(ACCESS_META_FILE)) {
    File f = SD.open(ACCESS_META_FILE, FILE_READ);
    if (f) { deserializeJson(doc, f); f.close(); }
  }
  String key = path;
  if (key.startsWith("/")) key = "_" + path.substring(1);
  if (doc[key].isNull()) doc.createNestedObject(key);
  doc[key]["readonly"] = readOnly;
  File wf = SD.open(ACCESS_META_FILE, FILE_WRITE);
  if (!wf) return false;
  serializeJson(doc, wf); wf.close();
  return true;
}

// ============== HTTP WEBHOOK NOTIFICATIONS =============
// Send event to external webhook URL configured in .webhook.json
// Non-blocking: fires and forgets to avoid blocking the main loop
void fireWebhook(String event, String path, String username) {
  if (!SD.exists(WEBHOOK_FILE)) return;
  File f = SD.open(WEBHOOK_FILE, FILE_READ);
  if (!f) return;
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  String url = doc["url"] | "";
  if (url.length() == 0) return;
  bool enabled = doc["enabled"] | true;
  if (!enabled) return;
  // Build JSON payload
  DynamicJsonDocument payload(512);
  payload["event"] = event;
  payload["path"] = path;
  payload["user"] = username;
  payload["time"] = millis();
  payload["device"] = "esp32-file-server";
  String body;
  serializeJson(payload, body);
  // Fire HTTP POST via WiFiClient (non-blocking best-effort)
  WiFiClient wc;
  // Parse host and path from URL
  String host = url;
  String webPath = "/";
  if (host.startsWith("http://")) host = host.substring(7);
  else if (host.startsWith("https://")) host = host.substring(8); // no TLS on ESP32 client, best-effort
  int slashPos = host.indexOf('/');
  if (slashPos > 0) { webPath = host.substring(slashPos); host = host.substring(0, slashPos); }
  int port = 80;
  int colonPos = host.indexOf(':');
  if (colonPos > 0) { port = host.substring(colonPos + 1).toInt(); host = host.substring(0, colonPos); }
  if (wc.connect(host.c_str(), port, 2000)) { // 2s connect timeout
    wc.println("POST " + webPath + " HTTP/1.1");
    wc.println("Host: " + host);
    wc.println("Content-Type: application/json");
    wc.println("Content-Length: " + String(body.length()));
    wc.println("Connection: close");
    wc.println();
    wc.print(body);
    wc.stop();
  }
}

// ============== WEAR LEVELING TRACKING ==============
// Track write operations per path to estimate SD wear (helps identify hot files)
#define WEAR_FILE "/.wear.json"
#define WEAR_MAX_ENTRIES 100
struct WearEntry { String path; uint32_t writeCount; uint32_t lastWrite; };
WearEntry wearEntries[WEAR_MAX_ENTRIES];
int wearCount = 0;

void trackWriteActivity(String path) {
  // Limit tracking to first 100 most-written files
  for (int i = 0; i < wearCount; i++) {
    if (wearEntries[i].path == path) {
      wearEntries[i].writeCount++;
      wearEntries[i].lastWrite = millis() / 1000;
      return;
    }
  }
  if (wearCount < WEAR_MAX_ENTRIES) {
    wearEntries[wearCount].path = path;
    wearEntries[wearCount].writeCount = 1;
    wearEntries[wearCount].lastWrite = millis() / 1000;
    wearCount++;
  }
}

// Get wear count for a file (for UI indicator)
uint32_t getWearCount(String path) {
  for (int i = 0; i < wearCount; i++) {
    if (wearEntries[i].path == path) return wearEntries[i].writeCount;
  }
  return 0;
}

// ============== FILE EXISTENCE HELPER ==============
// Fast check if a file exists and is not a directory (avoids open+isDirectory overhead)
bool isRegularFile(String path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  bool regular = !f.isDirectory();
  f.close();
  return regular;
}

// ============== CRC32 INTEGRITY CHECK ==============
// Compute CRC32 for a file on SD card (for integrity verification)
uint32_t computeFileCRC32(String path) {
  if (!SD.exists(path)) return 0;
  File f = SD.open(path, FILE_READ);
  if (!f) return 0;
  uint32_t crc = 0xFFFFFFFF;
  uint8_t buf[512];
  while (f.available()) {
    int n = f.read(buf, 512);
    for (int i = 0; i < n; i++) {
      crc ^= buf[i];
      for (int j = 0; j < 8; j++) {
        crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
      }
    }
  }
  f.close();
  return crc ^ 0xFFFFFFFF;
}

// Verify file integrity by computing CRC32 of a file
// Returns hex string of CRC32 for client-side verification
String getFileCRC32(String path) {
  uint32_t crc = computeFileCRC32(path);
  String result = "";
  for (int i = 3; i >= 0; i--) {
    uint8_t byte = (crc >> (i * 8)) & 0xFF;
    if (byte < 0x10) result += "0";
    result += String(byte, HEX);
  }
  return result;
}

// Forward declaration — defined in main .ino
void broadcastLogEvent(String action, String path, String username);

#endif
