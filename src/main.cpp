#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "esp_camera.h"
#include "SD_MMC.h"
#include "FS.h"

// ===================
// FREENOVE ESP32-S3-WROOM CAM Pin Configuration 
// Based on ESP32S3_EYE model from official Freenove documentation
// ===================

// Pin definition for ESP32S3_EYE model (matching Freenove configuration)
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5

#define Y9_GPIO_NUM       16  // D9
#define Y8_GPIO_NUM       17  // D8
#define Y7_GPIO_NUM       18  // D7
#define Y6_GPIO_NUM       12  // D6
#define Y5_GPIO_NUM       10  // D5
#define Y4_GPIO_NUM        8  // D4
#define Y3_GPIO_NUM        9  // D3
#define Y2_GPIO_NUM       11  // D2
#define VSYNC_GPIO_NUM     6  // V_SYNC
#define HREF_GPIO_NUM      7  // H_REF
#define PCLK_GPIO_NUM     13  // PCLK

// WiFi AP Configuration
const char* AP_SSID = "ESP32-S3-Camera-Setup";
const char* AP_PASSWORD = "camera12345";

AsyncWebServer server(80);

bool cameraReady = false;
bool sdCardReady = false;
String sdCardInfo = "Not initialized";

// Photo capture variables
unsigned long lastPhotoTime = 0;
unsigned long photoCount = 0;
String lastPhotoFilename = "";
const unsigned long PHOTO_INTERVAL = 1000; // 1 second

bool initCamera() {
  Serial.println("📷 Initializing camera with OFFICIAL Freenove ESP32-S3-EYE model...");
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;  // Use Freenove's official frequency
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  // High-resolution settings with PSRAM optimization
  if(psramFound()){
    // FRAMESIZE_QQVGA    // 160x120   - Tiny
    // FRAMESIZE_QCIF     // 176x144   - Very small  
    // FRAMESIZE_HQVGA    // 240x176   - Small
    // FRAMESIZE_QVGA     // 320x240   - Small
    // FRAMESIZE_CIF      // 400x296   - Medium
    // FRAMESIZE_HVGA     // 480x320   - Medium
    // FRAMESIZE_VGA      // 640x480   - Standard
    // FRAMESIZE_SVGA     // 800x600   - Current fallback
    // FRAMESIZE_XGA      // 1024x768  - Current PSRAM setting
    // FRAMESIZE_HD       // 1280x720  - HD (larger files!)
    // FRAMESIZE_SXGA     // 1280x1024 - High (larger files!)
    // FRAMESIZE_UXGA     // 1600x1200 - Ultra High (HUGE files!)
    config.frame_size = FRAMESIZE_XGA;    // 1024x768 - MUCH higher than 800x600!
    config.jpeg_quality = 6;              // Higher quality (was 10)
    config.fb_count = 2;                  // Double buffering
    config.grab_mode = CAMERA_GRAB_LATEST;
    Serial.println("🎯 PSRAM detected: Using HIGH RESOLUTION 1024x768, Quality 6");
  } else {
    // Fallback for no PSRAM - still better than before
    config.frame_size = FRAMESIZE_SVGA;   // 800x600 (was HVGA 480x320)
    config.jpeg_quality = 8;              // Better quality
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    Serial.println("⚠️ No PSRAM: Using STANDARD RESOLUTION 800x600, Quality 8");
  }
  
  Serial.printf("📍 Using ESP32S3_EYE model with camera_pins.h definitions\n");
  Serial.printf("📍 Clock: XCLK=%d, I2C: SIOD=%d, SIOC=%d\n", XCLK_GPIO_NUM, SIOD_GPIO_NUM, SIOC_GPIO_NUM);
  Serial.printf("📍 Data: Y9=%d, Y8=%d, Y7=%d, Y6=%d\n", Y9_GPIO_NUM, Y8_GPIO_NUM, Y7_GPIO_NUM, Y6_GPIO_NUM);
  Serial.printf("📍 Sync: VSYNC=%d, HREF=%d, PCLK=%d\n", VSYNC_GPIO_NUM, HREF_GPIO_NUM, PCLK_GPIO_NUM);
  Serial.printf("⚠️  Camera init may take 5-10 seconds...\n");
  Serial.flush();
  
  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t * s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  s->set_vflip(s, 1); // flip it back
  s->set_brightness(s, 1); // up the brightness just a bit
  s->set_saturation(s, -1); // lower the saturation
  
  Serial.println("✅ Camera initialized successfully!");
  return true;
}

bool capturePhoto() {
  if (!cameraReady || !sdCardReady) {
    return false;
  }
  
  // Take picture with camera
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Camera capture failed");
    return false;
  }
  
  // Generate filename with timestamp (using millis for uniqueness)
  photoCount++;
  unsigned long timestamp = millis() / 1000; // seconds since boot
  char filename[50];
  snprintf(filename, sizeof(filename), "/photos/photo_%lu_%04lu.jpg", timestamp, photoCount);
  
  // Save to SD card
  File file = SD_MMC.open(filename, FILE_WRITE);
  if (!file) {
    Serial.printf("❌ Failed to open file: %s\n", filename);
    esp_camera_fb_return(fb);
    return false;
  }
  
  // Write image data
  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);
  
  // Update last photo info
  lastPhotoFilename = String(filename);
  
  Serial.printf("📸 Photo saved: %s (Size: %zu bytes)\n", filename, fb->len);
  return true;
}

bool initSDCard() {
  Serial.println("💾 Initializing SD card for ESP32-S3-WROOM CAM...");
  
  // ⚠️ CRITICAL QUESTION: Does your Freenove ESP32-S3-WROOM CAM have SD card slot?
  Serial.println("🔍 HARDWARE CHECK:");
  Serial.println("   ❓ Does your Freenove ESP32-S3-WROOM CAM board have a microSD card slot?");
  Serial.println("   ❓ Is there a microSD card physically inserted?");
  Serial.println("   ❓ Is the SD card ≤32GB and FAT32 formatted?");
  
  // ESP32-S3 standard SD_MMC pins (from ESP32-S3 Technical Reference Manual)
  #define SD_MMC_CMD 38  // CMD pin for ESP32-S3 (hardwired)
  #define SD_MMC_CLK 39  // CLK pin for ESP32-S3 (hardwired) 
  #define SD_MMC_D0  40  // D0 pin for ESP32-S3 (hardwired)
  
  Serial.printf("🔌 ESP32-S3 SD_MMC pins - CMD:%d, CLK:%d, D0:%d\n", 
                SD_MMC_CMD, SD_MMC_CLK, SD_MMC_D0);
  
  // CRITICAL: Set the SD MMC pins BEFORE begin()
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  
  Serial.println("📋 Attempting SD_MMC mount...");
  // Try without formatting first (safer)
  if (!SD_MMC.begin("/sdcard", true, false)) {
    Serial.println("❌ First mount attempt failed, trying with format...");
    // Try with format_if_mount_failed = true
    if (!SD_MMC.begin("/sdcard", true, true)) {
      Serial.println("❌ Card Mount Failed COMPLETELY");
      Serial.println("💡 HARDWARE TROUBLESHOOTING:");
      Serial.println("   🔍 1. Verify your board HAS a microSD slot (not all ESP32-S3 boards do!)");
      Serial.println("   🔍 2. Check SD card is properly seated in slot");
      Serial.println("   🔍 3. Try a known-good SD card (≤32GB, FAT32)");
      Serial.println("   🔍 4. Check if SD slot has physical switch/jumper to enable");
      Serial.println("   🔍 5. Measure 3.3V power on SD slot with multimeter");
      Serial.println("   📖 6. Consult your board's manual for SD card requirements");
      sdCardInfo = "Hardware/Mount Failed";
      return false;
    }
  }
  
  // Check card presence - Freenove method
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("❌ No SD_MMC card attached");
    sdCardInfo = "No card attached";
    return false;
  }
  
  // Print card type - Freenove method
  Serial.print("💾 SD_MMC Card Type: ");
  String cardTypeStr;
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
    cardTypeStr = "MMC";
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
    cardTypeStr = "SDSC";
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
    cardTypeStr = "SDHC";
  } else {
    Serial.println("UNKNOWN");
    cardTypeStr = "UNKNOWN";
  }
  
  // Get card size - Freenove method
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("💾 SD_MMC Card Size: %lluMB\n", cardSize);
  
  // Print storage statistics - Freenove method
  Serial.printf("📊 Total space: %lluMB\n", SD_MMC.totalBytes() / (1024 * 1024));
  Serial.printf("📊 Used space: %lluMB\n", SD_MMC.usedBytes() / (1024 * 1024));
  uint64_t freeBytes = (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024);
  Serial.printf("📊 Free space: %lluMB\n", freeBytes);
  
  // Create photos directory using SD_MMC
  if (!SD_MMC.exists("/photos")) {
    if (SD_MMC.mkdir("/photos")) {
      Serial.println("📁 Created /photos directory");
    } else {
      Serial.println("❌ Failed to create /photos directory");
    }
  } else {
    Serial.println("📁 /photos directory already exists");
  }
  
  // Test file operations - Freenove style
  const char* testPath = "/hello.txt";
  Serial.println("🧪 Testing SD card write capability...");
  
  File file = SD_MMC.open(testPath, FILE_WRITE);
  if (file) {
    file.println("Hello from Freenove ESP32-S3-WROOM CAM!");
    file.println("SD_MMC test successful");
    file.close();
    Serial.println("✅ SD card write test successful");
    
    // Read it back to verify
    file = SD_MMC.open(testPath);
    if (file) {
      Serial.print("📄 Test file content: ");
      while (file.available()) {
        Serial.write(file.read());
      }
      file.close();
      
      // Clean up test file
      SD_MMC.remove(testPath);
    }
  } else {
    Serial.println("❌ SD card write test failed - card may be read-only");
    sdCardInfo = "Write test failed";
    return false;
  }
  
  sdCardInfo = cardTypeStr + " " + String(cardSize) + "MB (" + String(freeBytes) + "MB free)";
  Serial.println("✅ SD card initialized successfully using Freenove method!");
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n=== ESP32-S3 CLEAN TEST ===");
  Serial.println("🚀 Step 1: ESP32-S3 started!");
  Serial.printf("📊 Chip: %s, Rev: %d, CPU: %dMHz\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
  Serial.printf("💾 Free Heap: %d bytes\n", ESP.getFreeHeap());
  
  // Check PSRAM
  Serial.println("🧠 Step 2: Checking PSRAM...");
  if (psramFound()) {
    Serial.printf("✅ PSRAM: %.1f MB detected\n", ESP.getPsramSize() / (1024.0 * 1024.0));
  } else {
    Serial.println("❌ PSRAM: Not detected");
  }
  
  // Test WiFi AP
  Serial.println("📡 Step 3: Starting WiFi Access Point...");
  WiFi.mode(WIFI_AP);
  bool wifiOK = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (wifiOK) {
    Serial.printf("✅ WiFi AP: %s\n", AP_SSID);
    Serial.printf("📱 IP Address: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("🔑 Password: %s\n", AP_PASSWORD);
  } else {
    Serial.println("❌ WiFi AP failed");
    return;
  }
  
  // Step 4: Start Web Server
  Serial.println("🌐 Step 4: Starting web server...");
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32-S3 Camera System</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta http-equiv="refresh" content="5">
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f0f0; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }
        h1 { color: #333; text-align: center; }
        .status { background: #e8f5e8; padding: 15px; border-radius: 5px; margin: 20px 0; }
        .info { background: #e8f0ff; padding: 15px; border-radius: 5px; margin: 20px 0; }
        .photo-section { background: #fff3e0; padding: 20px; border-radius: 5px; margin: 20px 0; text-align: center; }
        .latest-photo { max-width: 100%; height: auto; border: 3px solid #ddd; border-radius: 10px; }
        .gallery-link { display: inline-block; padding: 10px 20px; background: #2196F3; color: white; text-decoration: none; border-radius: 5px; margin: 10px; }
        .gallery-link:hover { background: #1976D2; }
        .clear-btn { display: inline-block; padding: 10px 20px; background: #f44336; color: white; text-decoration: none; border-radius: 5px; margin: 10px; }
        .clear-btn:hover { background: #d32f2f; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📷 ESP32-S3 Camera System</h1>
        <div class="status">
            <h3>✅ System Status: ONLINE</h3>
            <p><strong>Board:</strong> Freenove ESP32-S3-WROOM CAM</p>
            <p><strong>Network:</strong> )" + String(AP_SSID) + R"(</p>
            <p><strong>IP Address:</strong> )" + WiFi.softAPIP().toString() + R"(</p>
        </div>
        <div class="photo-section">
            <h3>📸 Latest Photo</h3>
            <p><strong>Photos Captured:</strong> )" + String(photoCount) + R"(</p>
            <p><strong>Capture Rate:</strong> Every 1 second</p>)" + 
            (lastPhotoFilename.length() > 0 && cameraReady && sdCardReady ? 
            R"(<p><strong>Latest:</strong> )" + lastPhotoFilename.substring(lastPhotoFilename.lastIndexOf('/') + 1) + R"(</p>
            <img src=")" + lastPhotoFilename + R"(" class="latest-photo" alt="Latest Photo"><br>)" : 
            R"(<p><strong>Status:</strong> No photos captured yet</p>)") + R"(
            <a href="/gallery" class="gallery-link">View Photo Gallery</a>
            <br>
            <a href="/clear-photos" class="clear-btn">Clear All Photos</a>
        </div>
        <div class="info">
            <h3>Camera Status</h3>
            <p><strong>Camera Module:</strong> )" + (cameraReady ? "Ready" : "Not Ready") + R"(</p>
            <p><strong>Resolution:</strong> )" + (cameraReady ? (psramFound() ? "1024x768 (XGA)" : "800x600 (SVGA)") : "Not Available") + R"(</p>
            <p><strong>Quality:</strong> )" + (cameraReady ? (psramFound() ? "High (JPEG 6)" : "Standard (JPEG 8)") : "Not Available") + R"(</p>
        </div>
        <div class="info">
            <h3>Storage Status</h3>
            <p><strong>SD Card:</strong> )" + (sdCardReady ? "Ready" : "Not Ready") + R"(</p>
            <p><strong>SD Card Info:</strong> )" + sdCardInfo + R"(</p>
            <p><strong>Photos Directory:</strong> )" + (sdCardReady ? "Available" : "Not Available") + R"(</p>
        </div>
        <div class="info">
            <h3>System Components</h3>
            <p>WiFi Access Point - Working</p>
            <p>Web Server - Working</p>
            <p>Camera Module - )" + (cameraReady ? "Working" : "Failed") + R"(</p>
            <p>SD Card Storage - )" + (sdCardReady ? "Working" : "Failed") + R"(</p>
            <p>Photo Capture - )" + (cameraReady && sdCardReady ? "Active (1/sec)" : "Inactive") + R"(</p>
        </div>
        <div class="info">
            <h3>System Info</h3>
            <p><strong>Uptime:</strong> )" + String(millis()/1000) + R"( seconds</p>
            <p><strong>Free Heap:</strong> )" + String(ESP.getFreeHeap()) + R"( bytes</p>
            <p><strong>CPU Frequency:</strong> )" + String(ESP.getCpuFreqMHz()) + R"( MHz</p>
        </div>
    </div>
</body>
</html>
)";
    request->send(200, "text/html", html);
  });

  // Route to serve individual photos from SD card - FIXED version
  server.serveStatic("/photos/", SD_MMC, "/photos/");

  // Route for photo gallery page
  server.on("/gallery", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Photo Gallery - ESP32-S3 Camera</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f0f0; }
        .container { max-width: 1000px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }
        h1 { color: #333; text-align: center; }
        .back-link { display: inline-block; padding: 10px 20px; background: #4CAF50; color: white; text-decoration: none; border-radius: 5px; margin-bottom: 20px; }
        .back-link:hover { background: #45a049; }
        .gallery { display: grid; grid-template-columns: repeat(auto-fill, minmax(250px, 1fr)); gap: 20px; margin-top: 20px; }
        .photo-item { border: 2px solid #ddd; border-radius: 10px; overflow: hidden; background: white; }
        .photo-img { width: 100%; height: 200px; object-fit: cover; }
        .photo-info { padding: 10px; background: #f9f9f9; }
        .photo-name { font-weight: bold; margin-bottom: 5px; }
        .photo-time { color: #666; font-size: 0.9em; }
        .no-photos { text-align: center; color: #666; margin: 40px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Photo Gallery</h1>
        <a href="/" class="back-link">← Back to Main</a>
        <a href="/clear-photos" class="back-link" style="background: #f44336;">Clear All Photos</a>
        <p><strong>Total Photos:</strong> )" + String(photoCount) + R"(</p>
        <div class="gallery">
)";

    // List photos in reverse order (newest first)
    if (sdCardReady && photoCount > 0) {
      File root = SD_MMC.open("/photos");
      if (root) {
        // Create a simple array to store filenames for reverse sorting
        String photoFiles[100]; // Limit to 100 photos for memory
        int fileCount = 0;
        
        File file = root.openNextFile();
        while (file && fileCount < 100) {
          if (!file.isDirectory() && String(file.name()).endsWith(".jpg")) {
            photoFiles[fileCount] = String(file.name());
            fileCount++;
          }
          file = root.openNextFile();
        }
        root.close();
        
        // Display photos in reverse order (newest first)
        for (int i = fileCount - 1; i >= 0; i--) {
          String photoPath = "/photos/" + photoFiles[i];
          String displayName = photoFiles[i];
          // Extract timestamp from filename for display
          int underscorePos = displayName.indexOf('_');
          int secondUnderscorePos = displayName.indexOf('_', underscorePos + 1);
          String timestamp = "";
          if (underscorePos > 0 && secondUnderscorePos > underscorePos) {
            timestamp = displayName.substring(underscorePos + 1, secondUnderscorePos) + " sec";
          }
          
          html += R"(
            <div class="photo-item">
                <img src=")" + photoPath + R"(" class="photo-img" alt=")" + displayName + R"(" loading="lazy">
                <div class="photo-info">
                    <div class="photo-name">)" + displayName + R"(</div>
                    <div class="photo-time">Uptime: )" + timestamp + R"(</div>
                </div>
            </div>)";
        }
      }
    }

    if (photoCount == 0) {
      html += R"(<div class="no-photos">No photos captured yet. Photos will appear here once the camera starts capturing.</div>)";
    }

    html += R"(
        </div>
    </div>
</body>
</html>)";
    request->send(200, "text/html", html);
  });


  // Route to clear all photos from SD card
  server.on("/clear-photos", HTTP_GET, [](AsyncWebServerRequest *request){
    int deletedCount = 0;
    if (sdCardReady) {
      File root = SD_MMC.open("/photos");
      if (root) {
        File file = root.openNextFile();
        while (file) {
          if (!file.isDirectory() && String(file.name()).endsWith(".jpg")) {
            String fullPath = "/photos/" + String(file.name());
            file.close();
            if (SD_MMC.remove(fullPath)) {
              deletedCount++;
            }
            file = root.openNextFile();
          } else {
            file = root.openNextFile();
          }
        }
        root.close();
        // Reset photo counter
        photoCount = 0;
        lastPhotoFilename = "";
      }
    }
    
    String message = "Cleared " + String(deletedCount) + " photos from SD card. Redirecting...";
    String html = "<html><head><meta http-equiv='refresh' content='2;url=/'></head><body>";
    html += "<h2>Photos Cleared!</h2><p>" + message + "</p></body></html>";
    
    Serial.printf("🗑️ User cleared %d photos from SD card\n", deletedCount);
    request->send(200, "text/html", html);
  });

  server.begin();
  Serial.println("✅ Web server started successfully!");
  Serial.printf("🌐 Open browser to: http://%s\n", WiFi.softAPIP().toString().c_str());
  
  // Step 5: Initialize Camera
  Serial.println("📷 Step 5: Initializing camera...");
  cameraReady = initCamera();
  if (cameraReady) {
    Serial.println("✅ Camera initialization successful!");
  } else {
    Serial.println("❌ Camera initialization failed - continuing without camera");
  }
  
  // Step 6: Initialize SD Card
  Serial.println("💾 Step 6: Initializing SD card storage...");
  sdCardReady = initSDCard();
  if (sdCardReady) {
    Serial.println("✅ SD card initialization successful!");
  } else {
    Serial.println("❌ SD card initialization failed - continuing without storage");
  }
  
  Serial.println("🎯 System Complete: WiFi + Web Server + Camera + Storage!");
  Serial.printf("📱 System ready - connect to '%s' and visit http://%s\n", 
                AP_SSID, WiFi.softAPIP().toString().c_str());
}

void loop() {
  // Capture photo every second
  if (millis() - lastPhotoTime >= PHOTO_INTERVAL && cameraReady && sdCardReady) {
    capturePhoto();
    lastPhotoTime = millis();
  }
  
  // Status reporting every 10 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 10000) {  // Every 10 seconds
    int clients = WiFi.softAPgetStationNum();
    Serial.printf("⏱️  Uptime: %lu sec | Heap: %d bytes | WiFi: %d clients | Camera: %s | SD: %s | Photos: %lu\n", 
                  millis() / 1000, ESP.getFreeHeap(), clients, 
                  cameraReady ? "✅ Ready" : "❌ Failed",
                  sdCardReady ? "✅ Ready" : "❌ Failed", photoCount);
    if (clients > 0) {
      Serial.printf("🌐 Web interface: http://%s (Latest photo: %s)\n", 
                    WiFi.softAPIP().toString().c_str(), lastPhotoFilename.c_str());
    }
    lastPrint = millis();
  }
  delay(100);
}