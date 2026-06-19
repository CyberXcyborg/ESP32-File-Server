# ESP32 File Server

![License](https://img.shields.io/github/license/CyberXcyborg/ESP32-File-Server)
![Version](https://img.shields.io/badge/version-2.1.0-blue)

Turn your ESP32 into a powerful file server with a modern web interface. Upload, download, preview, and manage files on an SD card — all from your browser.

## What's New in v2.1

- 📋 **Activity log** — tracks all file operations (upload, delete, rename, download, share) with timestamp and user
- 🔄 **File versioning** — auto-creates backup before overwrite or rename
- 🔗 **Share links** — generate public URLs for files (no auth needed)
- 📱 **PWA support** — install as app on phone home screen
- 🖱️ **Context menu** — right-click files for quick actions
- 📑 **Tabbed UI** — Files / Trash / Users / Activity views
- 🗑️ **Improved trash** — integrated into main UI with restore & permanent delete
- 📊 **File/folder count** — shows counts in storage info

## Features

- **Web File Manager** — Upload, download, create, rename, delete files & folders
- **File Preview** — Images, text, audio, video in-browser
- **Drag & Drop Upload** — Multi-file with progress bar
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

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/list?path=/` | List files in directory |
| GET | `/api/download?path=/file.txt` | Download file |
| DELETE | `/api/delete?path=/file.txt` | Move file to trash |
| POST | `/api/create-dir?path=/newdir` | Create directory |
| POST | `/api/rename?path=/old&name=new` | Rename file/folder |
| POST | `/api/upload` | Upload file (multipart) |
| POST | `/api/share?path=/file.txt` | Create share link |
| GET | `/api/trash` | List trash contents |
| GET | `/api/restore?path=...` | Restore from trash |
| GET | `/api/empty-trash` | Empty trash (permanent) |
| GET | `/api/log` | Activity log (admin) |
| GET | `/api/users` | List users (admin) |
| POST | `/api/users` | Add user (admin) |
| PUT | `/api/users/name` | Update user (admin) |
| DELETE | `/api/users/name` | Delete user (admin) |
| GET | `/s/TOKEN` | Access shared file (public) |

## License

MIT License — Free to use, modify, and share.
