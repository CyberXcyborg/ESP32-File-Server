# ESP32 File Server

![License](https://img.shields.io/github/license/CyberXcyborg/ESP32-File-Server)
![Version](https://img.shields.io/badge/version-3.0.0-blue)

Turn your ESP32 into a powerful file server with a modern web interface. Upload, download, preview, and manage files on an SD card — all from your browser.

## What's New in v3.0

- 📦 **File Move/Copy** — Move or copy files between folders with a folder tree picker
- 📁 **Folder Upload** — Upload entire folders (webkitdirectory support)
- ⚙️ **Settings Page** — Change WiFi, AP, and FTP settings from the web UI (no reflash needed)
- ⌨️ **Keyboard Shortcuts** — Delete, Ctrl+A (select all), F2 (rename), Escape (clear)
- ℹ️ **File Info Panel** — Click any file to see details (size, type, path)
- 📋 **Activity Log** — Track all file operations with timestamp and user (admin)
- 🔄 **File Versioning** — Auto-backup before overwrite or rename
- 🔗 **Share Links** — Generate public URLs for files (no auth needed)
- 📱 **PWA Support** — Install as app on phone home screen
- 🖱️ **Context Menu** — Right-click files for quick actions
- 📊 **System Info** — View version, IP, mode, uptime, free memory

## Features

- **Web File Manager** — Upload, download, create, rename, delete, move, copy files & folders
- **File Preview** — Images, text, audio, video in-browser
- **Drag & Drop Upload** — Multi-file and folder upload with progress bar
- **User Authentication** — Admin and user roles, session management
- **Trash System** — Soft delete with restore and permanent delete
- **File Sharing** — Generate public share links
- **Activity Log** — Track who did what and when (admin)
- **File Versioning** — Automatic backups before overwrite/rename
- **Dark Mode** — Toggle with localStorage persistence
- **FTP Server** — Access files via standard FTP clients
- **WiFi or AP Mode** — Connect to your network or create a hotspot
- **Search & Sort** — Filter by name, sort by name/size/date
- **Responsive UI** — Works on desktop, tablet, and phone
- **Fully Offline** — No internet/cloud required

## Default Credentials

| Service | Username | Password |
|---------|----------|----------|
| Web | admin | admin123 |
| FTP | esp32 | esp32 |

## Hardware Requirements

- ESP32 development board
- Micro SD card (up to 32GB recommended)
- SD card module (or built-in SD slot)

## Wiring (Default Pins)

| SD Card | ESP32 |
|---------|-------|
| CS | GPIO 5 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| SCK | GPIO 18 |
| VCC | 3.3V |
| GND | GND |

## Installation

1. Clone this repository
2. Open `ESP32_File_Server.ino` in Arduino IDE
3. Install required libraries: `WiFi`, `WebServer`, `SimpleFTPServer`, `ESPmDNS`, `SD`, `SPI`, `FS`, `ArduinoJson`
4. Update WiFi credentials in `config.h`
5. Upload to your ESP32
6. Open Serial Monitor at 115200 baud to see the IP address
7. Access the web interface at `http://[IP_ADDRESS]`

## Project Structure

```
ESP32_File_Server.ino  — Main application (setup, loop, web handlers)
config.h               — WiFi, SD, and system configuration
auth.h                 — User authentication and session management
file_ops.h             — File operations (SD card, trash, versions, shares, logging)
web_ui.h               — Web interface (HTML, CSS, JavaScript)
```

## Keyboard Shortcut

| Key | Action |
|-----|--------|
| Delete | Delete selected files |
| Ctrl+A | Select all files |
| F2 | Rename selected file |
| Escape | Clear selection / close modals |

## License

MIT License — Free to use, modify, and share.
