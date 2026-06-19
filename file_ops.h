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
    String filePath = String(file.path());
    if (file.isDirectory()) removeDir(filePath);
    else SD.remove(filePath);
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

// ============== ACTIVITY LOG ==============
void logActivity(String action, String path, String username) {
  File logFile = SD.open(LOG_FILE, FILE_APPEND);
  if (!logFile) {
    logFile = SD.open(LOG_FILE, FILE_WRITE);
    if (!logFile) return;
  }
  // Simple CSV format: timestamp,username,action,path
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
  
  // Version path: .versions/original_path/timestamp_filename
  String verDir = VERSIONS_FOLDER + path;
  int slash = verDir.lastIndexOf('/');
  if (slash > 0) {
    String parent = verDir.substring(0, slash);
    createDirRecursive(parent);
  }
  
  // Add timestamp to filename to avoid conflicts
  int dot = verDir.lastIndexOf('.');
  String verPath;
  if (dot > slash) {
    verPath = verDir.substring(0, dot) + "_" + String(millis()) + verDir.substring(dot);
  } else {
    verPath = verDir + "_" + String(millis());
  }
  
  File src = SD.open(path, FILE_READ);
  if (!src) return false;
  File dst = SD.open(verPath, FILE_WRITE);
  if (!dst) { src.close(); return false; }
  
  while (src.available()) dst.write(src.read());
  src.close();
  dst.close();
  return true;
}

// ============== SHARING ==============
// Share tokens stored as JSON on SD: { "token": { "path": "/file.txt", "expires": 123 } }
bool createShare(String path, String &shareToken) {
  shareToken = "";
  const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 16; i++) shareToken += charset[random(0, 62)];
  
  DynamicJsonDocument doc(1024);
  // Read existing shares
  if (SD.exists(SHARES_FILE)) {
    File f = SD.open(SHARES_FILE, FILE_READ);
    if (f) {
      deserializeJson(doc, f);
      f.close();
    }
  }
  
  JsonObject share = doc.createNestedObject(shareToken);
  share["path"] = path;
  share["created"] = millis();
  share["downloads"] = 0;
  
  File f = SD.open(SHARES_FILE, FILE_WRITE);
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

bool getSharePath(String token, String &path) {
  if (!SD.exists(SHARES_FILE)) return false;
  File f = SD.open(SHARES_FILE, FILE_READ);
  if (!f) return false;
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, f);
  f.close();
  
  JsonObject share = doc[token];
  if (share.isNull()) return false;
  
  path = share["path"].as<String>();
  // Increment download count
  share["downloads"] = (share["downloads"] | 0) + 1;
  
  // Save incremented count
  File fw = SD.open(SHARES_FILE, FILE_WRITE);
  if (fw) { serializeJson(doc, fw); fw.close(); }
  
  return true;
}

// ============== ICONS & HELPERS ==============
String getFileIcon(String fileName) {
  String ext = fileName.substring(fileName.lastIndexOf('.') + 1);
  ext.toLowerCase();
  if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "bmp" || ext == "svg" || ext == "webp") return "🖼️";
  if (ext == "mp3" || ext == "wav" || ext == "ogg" || ext == "flac") return "🎵";
  if (ext == "mp4" || ext == "mov" || ext == "avi" || ext == "mkv") return "🎬";
  if (ext == "pdf") return "📑";
  if (ext == "doc" || ext == "docx" || ext == "txt" || ext == "md") return "📝";
  if (ext == "xls" || ext == "xlsx" || ext == "csv") return "📊";
  if (ext == "zip" || ext == "rar" || ext == "7z" || ext == "tar" || ext == "gz") return "📦";
  if (ext == "html" || ext == "htm" || ext == "css" || ext == "js" || ext == "json" || ext == "xml") return "🌐";
  if (ext == "py" || ext == "c" || ext == "cpp" || ext == "h" || ext == "java" || ext == "php") return "📟";
  return "📄";
}

bool isPreviewable(String fileName) {
  String ext = fileName.substring(fileName.lastIndexOf('.') + 1);
  ext.toLowerCase();
  return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "bmp" || ext == "svg" || ext == "webp" ||
         ext == "txt" || ext == "html" || ext == "htm" || ext == "css" || ext == "js" || ext == "json" || ext == "xml" ||
         ext == "md" || ext == "csv" || ext == "pdf" || ext == "mp3" || ext == "wav" || ext == "ogg";
}

#endif
