# ESP32 File Server

![License](https://img.shields.io/github/license/CyberXcyborg/ESP32-File-Server)
![Version](https://img.shields.io/badge/version-2.0.0-blue)

Turn your ESP32 into a powerful file server with a modern web interface. Upload, download, preview, and manage files on an SD card — all from your browser.

## What's New in v2.0

- 🌙 **Dark mode** — toggle between light and dark themes (saved to localStorage)
- 📤 **Drag & drop upload** — drag files directly onto the page
- 👁️ **File preview** — view images, text, audio, and video in-browser
- ✏️ **Rename files/folders** — right-click or use the rename button
- 🔍 **Search** — filter files by name in the current directory
- 📊 **Sort** — by name, size, or date (ascending/descending)
- 🗑️ **Trash / Recycle bin** — soft delete with restore option
- 💾 **Storage dashboard** — see used/total space with progress bar
- 📱 **Improved mobile UI** — better responsive design
- 🏗️ **Modular code** — clean header-based architecture

## Features

- **Web File Manager** — Upload, download, create, rename, delete files & folders
- **File Preview** — Images, text, audio, video in-browser
- **User Authentication** — Admin and user roles, session management
- **Trash System** — Soft delete with restore and permanent delete
- **FTP Server** — Access files via standard FTP clients
- **WiFi or AP Mode** — Connect to your network or create a hotspot
- **Dark Mode** — Eye-friendly dark theme
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
file_ops.h             — File operations (SD card, trash, icons)
web_ui.h               — Web interface (HTML, CSS, JavaScript)
```

## License

MIT License — Free to use, modify, and share.
