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
    if (file.isDirectory()) {
      total += getDirSize(String(file.path()));
    } else {
      total += file.size();
    }
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
    if (file.isDirectory()) {
      removeDir(filePath);
    } else {
      SD.remove(filePath);
    }
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
  if (slash > 0) {
    String parent = trashPath.substring(0, slash);
    createDirRecursive(parent);
  }
  return SD.rename(path, trashPath);
}

bool restoreFromTrash(String trashPath) {
  String origPath = trashPath;
  origPath.replace(TRASH_FOLDER, "");
  int slash = origPath.lastIndexOf('/');
  if (slash > 0) {
    String parent = origPath.substring(0, slash);
    createDirRecursive(parent);
  }
  return SD.rename(trashPath, origPath);
}

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

String getPreviewType(String fileName) {
  String ext = fileName.substring(fileName.lastIndexOf('.') + 1);
  ext.toLowerCase();
  if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "bmp" || ext == "svg" || ext == "webp") return "image";
  if (ext == "mp3" || ext == "wav" || ext == "ogg") return "audio";
  if (ext == "mp4" || ext == "mov" || ext == "avi") return "video";
  if (ext == "txt" || ext == "html" || ext == "htm" || ext == "css" || ext == "js" || ext == "json" || ext == "xml" || ext == "md" || ext == "csv") return "text";
  return "none";
}

#endif
