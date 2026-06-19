#ifndef CONFIG_H
#define CONFIG_H

// ============== CONFIGURATION ==============
// WiFi credentials
const char* ssid = "TEST";
const char* password = "TEST";

// Access Point settings (used if WiFi connection fails)
const char* ap_ssid = "ESP32-FileServer";
const char* ap_password = "fileserver123";

// Web server port
const uint16_t webServerPort = 80;

// FTP credentials
const char* ftp_user = "esp32";
const char* ftp_password = "esp32";

// SD card configuration
#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18

// Authentication settings
#define MAX_SESSIONS 5
#define SESSION_TIMEOUT 1800000  // 30 minutes
#define USERS_FILE "/users.json"
#define TRASH_FOLDER "/.trash"
#define LOG_FILE "/.activity.log"
#define SHARES_FILE "/.shares.json"
#define VERSIONS_FOLDER "/.versions"
#define SETTINGS_FILE "/.settings.json"
#define DEFAULT_ADMIN_USER "admin"
#define DEFAULT_ADMIN_PASS "admin123"

// Version
#define FIRMWARE_VERSION "3.0.0"

#endif
