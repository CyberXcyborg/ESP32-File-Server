#ifndef CONFIG_H
#define CONFIG_H

const char* ssid = "TEST";
const char* password = "TEST";
const char* ap_ssid = "ESP32-FileServer";
const char* ap_password = "fileserver123";
uint16_t webServerPort = 80; // Runtime configurable, default 80
const uint16_t DEFAULT_WEB_PORT = 80;
const char* ftp_user = "esp32";
const char* ftp_password = "esp32";

#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18

#define MAX_SESSIONS 5
#define SESSION_TIMEOUT 1800000
#define USERS_FILE "/users.json"
#define TRASH_FOLDER "/.trash"
#define LOG_FILE "/.activity.log"
#define SHARES_FILE "/.shares.json"
#define VERSIONS_FOLDER "/.versions"
#define SETTINGS_FILE "/.settings.json"
#define ACCESS_META_FILE "/.access.json"
#define WEBHOOK_FILE "/.webhook.json"
#define DEFAULT_ADMIN_USER "admin"
#define DEFAULT_ADMIN_PASS "admin123"

#define FIRMWARE_VERSION "6.12.0"

// Maximum single upload size (16 MB — adjust for your SD card capacity)
#define MAX_UPLOAD_SIZE (16UL * 1024UL * 1024UL)

#endif
