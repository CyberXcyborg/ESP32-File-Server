# ESP32 File Server

![License](https://img.shields.io/github/license/CyberXcyborg/ESP32-File-Server)
![Version](https://img.shields.io/badge/version-3.3.0-blue)

Turn your ESP32 into a powerful file server with a modern web interface.

## Features

### File Management
- Upload, download, create, rename, delete, move, copy files & folders
- Folder upload (webkitdirectory)
- Drag & drop with progress bar
- File preview (images, text, audio, video)
- Search & sort (name, size, date)
- File info panel
- File versioning (auto-backup)
- ZIP download (select multiple files)
- Share links (public URLs)

### System
- **OTA firmware update** — update from web UI, no USB needed
- **WiFi auto-reconnect** — reconnects automatically if connection drops
- **Remote reboot** — reboot device from web UI
- **Health check** — public endpoint for monitoring
- **SD card monitoring** — auto-detect failure, attempt reconnect
- **Settings page** — change WiFi, AP, FTP settings from web UI
- **Activity log** — track all operations with timestamp and user
- **Trash system** — soft delete with restore
- **Multi-user** — admin/user roles
- **PWA support** — install as app on phone
- **Dark mode** — toggle with localStorage
- **FTP server** — standard FTP access
- **WiFi + AP mode** — connect to network or create hotspot
- **Responsive UI** — works on desktop, tablet, phone
- **Fully offline** — no internet/cloud required

## Default Credentials

| Service | Username | Password |
|---------|----------|----------|
| Web | admin | admin123 |
| FTP | esp32 | esp32 |

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Delete | Delete selected files |
| Ctrl+A | Select all files |
| F2 | Rename selected file |
| Escape | Clear selection / close modals |

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | /health | Health check (public) |
| GET | /api/list?path=/ | List files |
| GET | /api/info?path=/file | File info |
| GET | /api/download?path=/file | Download file |
| DELETE | /api/delete?path=/file | Move to trash |
| POST | /api/create-dir?path=/dir | Create directory |
| POST | /api/rename?path=/old&name=new | Rename |
| POST | /api/move?path=/src&dest=/dst | Move file |
| POST | /api/copy?path=/src&dest=/dst | Copy file |
| POST | /api/upload | Upload file |
| POST | /api/share?path=/file | Create share link |
| POST | /api/zip | Download files as ZIP |
| GET | /api/changes?since=ts | Check for changes |
| POST | /api/ota-upload | OTA firmware update |
| POST | /api/reboot | Remote reboot |
| GET | /api/settings | Get settings |
| POST | /api/settings | Save settings |

## Hardware

- ESP32 development board
- Micro SD card (up to 32GB)
- SD card module

## Wiring

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
3. Install libraries: `WiFi`, `WebServer`, `SimpleFTPServer`, `ESPmDNS`, `SD`, `SPI`, `FS`, `ArduinoJson`
4. Update WiFi credentials in `config.h`
5. Upload to ESP32
6. Access at `http://[IP_ADDRESS]`

## OTA Updates

1. In Arduino IDE: Sketch → Export Compiled Binary
2. Open web UI → OTA tab
3. Select the `.bin` file and upload
4. Device reboots automatically

## License

MIT License
