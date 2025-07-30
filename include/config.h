#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Camera Configuration (ESP32-S3 CAM pins)
// ⚠ WARNING: These pins may not match your specific Freenove ESP32-S3 board!
// 📖 Common Freenove ESP32-S3 Camera Pin Configurations:
//
// OPTION 1 - Freenove ESP32-S3 WROOM CAM (most common):
// PWDN: -1 (not used), RESET: -1 (not used)
// XCLK: 4, SIOD: 18, SIOC: 23
// Y9: 36, Y8: 37, Y7: 38, Y6: 39, Y5: 35, Y4: 14, Y3: 13, Y2: 21
// VSYNC: 5, HREF: 27, PCLK: 25
//
// OPTION 2 - Alternative Freenove Pin Layout:
// XCLK: 10, SIOD: 40, SIOC: 39
// Y9: 48, Y8: 11, Y7: 12, Y6: 14, Y5: 16, Y4: 18, Y3: 17, Y2: 15
// VSYNC: 38, HREF: 47, PCLK: 13
//
// 🔧 If system hangs during camera init, try the alternative pin layout!

// CORRECT pins for Freenove ESP32-S3-WROOM CAM Board (verified working configuration)
#define PWDN_GPIO_NUM     -1  // Power down pin (not used)
#define RESET_GPIO_NUM    -1  // Reset pin (not used)
#define XCLK_GPIO_NUM     15  // External clock
#define SIOD_GPIO_NUM     4   // SDA (I2C data)
#define SIOC_GPIO_NUM     5   // SCL (I2C clock)
#define Y9_GPIO_NUM       16  // D9
#define Y8_GPIO_NUM       17  // D8
#define Y7_GPIO_NUM       18  // D7
#define Y6_GPIO_NUM       12  // D6
#define Y5_GPIO_NUM       10  // D5
#define Y4_GPIO_NUM       8   // D4
#define Y3_GPIO_NUM       9   // D3
#define Y2_GPIO_NUM       11  // D2
#define VSYNC_GPIO_NUM    6   // V_SYNC
#define HREF_GPIO_NUM     7   // H_REF
#define PCLK_GPIO_NUM     13  // PCLK

// SD Card Configuration (SD MMC) - Correct pins for Freenove ESP32-S3-WROOM CAM
// These pins are hardwired on the board - do not change!
#define SD_MMC_CMD        38  // CMD pin (hardwired)
#define SD_MMC_CLK        39  // CLK pin (hardwired) 
#define SD_MMC_D0         40  // D0 pin (hardwired)

// Camera settings - optimized for 8MB PSRAM
#define CAMERA_FRAME_SIZE FRAMESIZE_UXGA  // 1600x1200 - maximum resolution
#define CAMERA_JPEG_QUALITY 8  // 0-63, lower means higher quality (8 = high quality)

// SD Card settings
#define PHOTOS_DIR "/photos"
#define MAX_PHOTOS 100  // Keep only the latest 100 photos

// WiFi Configuration
extern const char* AP_SSID;
extern const char* AP_PASSWORD;

// Web Server Configuration
#define HTTP_PORT 80
#define DNS_PORT 53

// mDNS Configuration
extern const char* MDNS_NAME;

// Configuration structure
struct WiFiConfig {
  String ssid;
  String password;
  bool isValid() const {
    return ssid.length() > 0 && password.length() > 0;
  }
};

// Camera status structure
struct CameraStatus {
  bool initialized;
  bool sdCardReady;
  String currentPhoto;
  int photoCount;
  unsigned long lastPhotoTime;
  
  CameraStatus() : initialized(false), sdCardReady(false), currentPhoto(""), photoCount(0), lastPhotoTime(0) {}
};

#endif 