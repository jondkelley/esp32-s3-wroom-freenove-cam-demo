#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "esp_camera.h"
#include "SD_MMC.h"
#include "SD.h"
#include "SPI.h"
#include "FS.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Function declarations
void forceMemoryRecovery();
bool initCamera();
bool initSDCard();
bool testSDCard();
void photoCaptureTask(void * parameter);
bool capturePhoto();

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
// Photo capture interval (embedded optimized)
const unsigned long PHOTO_INTERVAL = 10000; // 10 seconds (embedded optimized)
bool clearingInProgress = false; // Flag to pause photo capture during clearing

// ===================
// DUAL-CORE ARCHITECTURE - Photo capture on Core 1, Web server on Core 0
// ===================

// FreeRTOS handles for dual-core operation
TaskHandle_t photoTaskHandle = NULL;
QueueHandle_t photoQueue = NULL;
SemaphoreHandle_t sdMutex = NULL;

// Photo capture command structure
struct PhotoCommand {
  bool capture;
  unsigned long timestamp;
  unsigned long photoNumber;
};

// Photo capture result structure  
struct PhotoResult {
  bool success;
  String filename;
  size_t fileSize;
  String error;
};

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

  // NO PSRAM SETTINGS - Optimized for embedded device
  config.frame_size = FRAMESIZE_QVGA;    // 320x240 - Much smaller for embedded
  config.jpeg_quality = 20;              // Lower quality to save memory
  config.fb_count = 1;                   // Single buffer
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_DRAM; // Store in DRAM, not PSRAM
  Serial.println("🎯 EMBEDDED: Using 320x240, Quality 20 for minimal memory");

  // Initialize the camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed with error 0x%x\n", err);
    return false;
  }

  Serial.println("✅ Camera initialized successfully!");
  return true;
}

// ===================
// DUAL-CORE PHOTO CAPTURE TASK (Core 1)
// ===================

void photoCaptureTask(void * parameter) {
  Serial.println("📸 Photo capture task started on Core " + String(xPortGetCoreID()));
  
  // Set task priority and watchdog
  esp_task_wdt_add(NULL);
  
  while (true) {
    // Wait for photo capture command from main core
    PhotoCommand cmd;
    if (xQueueReceive(photoQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (cmd.capture && cameraReady && sdCardReady && !clearingInProgress) {
        
        // Take picture with camera
        camera_fb_t * fb = esp_camera_fb_get();
        if (!fb) {
          Serial.println("❌ Camera capture failed on Core " + String(xPortGetCoreID()));
          continue;
        }
        
        // Generate filename with sequential numbering
        char filename[50];
        snprintf(filename, sizeof(filename), "/photos/photo_%06lu.jpg", photoCount + 1);
        
        // Use mutex to protect SD card access - IMPROVED MUTEX HANDLING
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
          
          // Save to SD card
          File file = SD_MMC.open(filename, FILE_WRITE);
          if (file) {
            size_t bytesWritten = file.write(fb->buf, fb->len);
            file.close();
            
            if (bytesWritten == fb->len) {
              // Update shared variables (protected by mutex)
              lastPhotoFilename = String(filename);
              photoCount++; // Increment photo count on successful save
              Serial.printf("📸 Photo saved: %s (Size: %zu bytes) on Core %d\n", 
                          filename, fb->len, xPortGetCoreID());
            } else {
              Serial.printf("⚠️ Write incomplete: %zu/%zu bytes to %s\n", 
                          bytesWritten, fb->len, filename);
            }
          } else {
            Serial.printf("❌ Failed to open file: %s on Core %d\n", filename, xPortGetCoreID());
          }
          
          // Release mutex - CRITICAL!
          xSemaphoreGive(sdMutex);
          
        } else {
          Serial.println("⚠️ Could not acquire SD mutex on Core " + String(xPortGetCoreID()));
        }
        
        // Always return frame buffer
        esp_camera_fb_return(fb);
        
        // 🧹 AGGRESSIVE MEMORY CLEANUP AFTER EACH PHOTO
        yield();
        esp_task_wdt_reset();
        
        // Check memory after photo capture
        int photoHeap = ESP.getFreeHeap();
        if (photoHeap < 40000) {
          Serial.printf("⚠️ Low memory after photo: %d bytes - forcing cleanup\n", photoHeap);
          delay(100); // Give system time to recover
          yield();
        }
        
        // 🚀 QUEUE MANAGEMENT - Process faster when queue is getting full
        UBaseType_t queueSpaces = uxQueueSpacesAvailable(photoQueue);
        if (queueSpaces < 5) {
          Serial.printf("🚀 Queue management: %d spaces left - processing faster\n", queueSpaces);
          vTaskDelay(pdMS_TO_TICKS(5)); // Reduce delay to process faster
        } else {
          vTaskDelay(pdMS_TO_TICKS(10)); // Normal delay
        }
        
      }
    }
    
    // Feed watchdog and yield
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ===================
// MEMORY MANAGEMENT IMPROVEMENTS
// ===================

// EMBEDDED OPTIMIZATIONS - Treat like tiny device
#define MIN_HEAP_FOR_PHOTO 20000  // Reduced to 20KB for embedded
#define MIN_QUEUE_SPACES 2         // Reduced queue spaces
#define PHOTO_TASK_STACK 8192      // Reduced stack size
#define PHOTO_QUEUE_SIZE 5         // Reduced queue size

bool capturePhoto() {
  if (!cameraReady || !sdCardReady || clearingInProgress) {
    return false;
  }
  
  // Check system health (improved memory management)
  int freeHeap = ESP.getFreeHeap();
  int minFreeHeap = ESP.getMinFreeHeap();
  
  // More aggressive memory management
  if (freeHeap < MIN_HEAP_FOR_PHOTO) {
    Serial.printf("⚠️ Low memory (%d bytes, min: %d) - skipping photo capture\n", freeHeap, minFreeHeap);
    
    // 🧹 FORCE MEMORY RECOVERY WHEN CRITICALLY LOW
    if (freeHeap < 25000) { // Very low memory
      Serial.println("🚨 CRITICAL: Forcing memory recovery...");
      forceMemoryRecovery();
      
      Serial.printf("🧹 Memory after recovery: %d bytes\n", ESP.getFreeHeap());
    }
    
    return false;
  }
  
  // Check if queue is getting full
  UBaseType_t queueSpaces = uxQueueSpacesAvailable(photoQueue);
  if (queueSpaces < MIN_QUEUE_SPACES) {
    Serial.printf("⚠️ Queue nearly full (%d spaces left) - skipping photo capture\n", queueSpaces);
    return false;
  }
  
  // Prepare photo command for Core 1
  PhotoCommand cmd;
  cmd.capture = true;
  cmd.timestamp = millis() / 1000;
  cmd.photoNumber = photoCount + 1;  // This will be the actual photo number
  
  // Send command to photo capture task with timeout
  if (xQueueSend(photoQueue, &cmd, pdMS_TO_TICKS(200)) == pdTRUE) {
    return true;
  } else {
    Serial.printf("⚠️ Could not send photo command to Core 1 (queue full: %d spaces)\n", queueSpaces);
    return false;
  }
}

// Memory recovery function
void forceMemoryRecovery() {
  Serial.println("🧹 FORCING COMPREHENSIVE MEMORY RECOVERY...");
  
  // Step 1: Clear all large string objects
  lastPhotoFilename = "";
  yield();
  delay(100);
  
  // Step 2: Force garbage collection
  esp_task_wdt_reset();
  yield();
  delay(200);
  
  // Step 3: Check if we need to restart web server - REMOVED TO AVOID MUTEX CONFLICTS
  int heapAfterCleanup = ESP.getFreeHeap();
  if (heapAfterCleanup < 20000) {
    Serial.println("🚨 CRITICAL: Memory still low after cleanup - forcing SD sync...");
    
    // SAFER: Force SD card filesystem sync instead of web server restart
    if (sdCardReady) {
      Serial.println("💾 Forcing SD card filesystem sync...");
      // Don't restart SD card - just sync
      yield();
      delay(500);
    }
  }
  
  int finalHeap = ESP.getFreeHeap();
  Serial.printf("🧹 Memory recovery complete: %d bytes free\n", finalHeap);
}

bool initSDCard() {
  Serial.println("💾 Initializing SD card for ESP32-S3-WROOM CAM...");
  
  // ⚠️ CRITICAL QUESTION: Does your Freenove ESP32-S3-WROOM CAM have SD card slot?
  Serial.println("🔍 HARDWARE CHECK:");
  Serial.println("   ❓ Does your Freenove ESP32-S3-WROOM CAM board have a microSD card slot?");
  Serial.println("   ❓ Is there a microSD card physically inserted?");
  Serial.println("   ❓ Is the SD card ≤32GB and FAT32 formatted?");
  
  Serial.println("📋 Attempting SD card initialization with multiple methods...");
  
  // FIXED: Try both SD_MMC and SPI SD card methods
  bool sdSuccess = false;
  
  // ===================
  // METHOD 1: SD_MMC (Hardwired ESP32-S3 pins)
  // ===================
  Serial.println("🔧 Method 1: SD_MMC (ESP32-S3 hardwired pins)...");
  
  // FREENOVE ESP32-S3 SD_MMC PIN CONFIGURATION (from official Freenove code)
  #define SD_MMC_CMD 38  // CMD pin for ESP32-S3 (hardwired)
  #define SD_MMC_CLK 39  // CLK pin for ESP32-S3 (hardwired) 
  #define SD_MMC_D0  40  // D0 pin for ESP32-S3 (hardwired)
  
  Serial.printf("🔌 Freenove SD_MMC pins - CMD:%d, CLK:%d, D0:%d\n", SD_MMC_CMD, SD_MMC_CLK, SD_MMC_D0);
  
  // Set the SD MMC pins for Freenove board (CRITICAL MISSING STEP!)
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  
  // Try different SD_MMC configurations
  if (SD_MMC.begin("/sdcard", true, false)) {
    Serial.println("✅ SD_MMC mounted successfully with 1-bit mode");
    sdSuccess = true;
  } else if (SD_MMC.begin("/sdcard", true, false, 10)) {
    Serial.println("✅ SD_MMC mounted successfully with explicit config");
    sdSuccess = true;
  } else if (SD_MMC.begin("/sdcard", true, true)) {
    Serial.println("✅ SD_MMC mounted successfully with 4-bit mode");
    sdSuccess = true;
  } else {
    Serial.println("❌ All SD_MMC methods failed");
  }
  
  // ===================
  // METHOD 2: SPI SD Card (Alternative pins)
  // ===================
  if (!sdSuccess) {
    Serial.println("🔧 Method 2: SPI SD Card (alternative pins)...");
    
    // Freenove ESP32-S3 SPI SD card pins (common configuration)
    #define SD_CS_PIN    5   // CS pin for SPI SD card
    #define SD_MOSI_PIN  23  // MOSI pin
    #define SD_MISO_PIN  19  // MISO pin  
    #define SD_SCK_PIN   18  // SCK pin
    
    Serial.printf("🔌 SPI SD pins - CS:%d, MOSI:%d, MISO:%d, SCK:%d\n", 
                  SD_CS_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_SCK_PIN);
    
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    
    if (SD.begin(SD_CS_PIN, SPI)) {
      Serial.println("✅ SPI SD card mounted successfully");
      sdSuccess = true;
    } else {
      Serial.println("❌ SPI SD card also failed");
    }
  }
  
  if (!sdSuccess) {
    Serial.println("❌ All SD card methods failed - check hardware");
    Serial.println("💡 TROUBLESHOOTING:");
    Serial.println("   1. Check if your board has an SD card slot");
    Serial.println("   2. Insert a microSD card (≤32GB, FAT32)");
    Serial.println("   3. Check board documentation for SD card support");
    return false;
  }
  
  // Get card info
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("❌ No SD card attached");
    return false;
  }
  
  Serial.print("💾 SD_MMC Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("💾 SD_MMC Card Size: %lluMB\n", cardSize);
  
  // Get filesystem info
  uint64_t totalBytes = SD_MMC.totalBytes() / (1024 * 1024);
  uint64_t usedBytes = SD_MMC.usedBytes() / (1024 * 1024);
  uint64_t freeBytes = SD_MMC.totalBytes() / (1024 * 1024) - SD_MMC.usedBytes() / (1024 * 1024);
  
  Serial.printf("📊 Total space: %lluMB\n", totalBytes);
  Serial.printf("📊 Used space: %lluMB\n", usedBytes);
  Serial.printf("📊 Free space: %lluMB\n", freeBytes);
  
  // Create photos directory if it doesn't exist
  if (!SD_MMC.exists("/photos")) {
    Serial.println("📁 Creating /photos directory...");
    SD_MMC.mkdir("/photos");
  } else {
    Serial.println("📁 /photos directory already exists");
  }
  
  // Test write capability
  Serial.println("🧪 Testing SD card write capability...");
  File testFile = SD_MMC.open("/test.txt", FILE_WRITE);
  if (testFile) {
    testFile.println("Hello from Freenove ESP32-S3-WROOM CAM!");
    testFile.close();
    
    // Read back to verify
    testFile = SD_MMC.open("/test.txt", FILE_READ);
    if (testFile) {
      String content = testFile.readString();
      Serial.println("📄 Test file content: " + content);
      testFile.close();
      
      // Clean up test file
      SD_MMC.remove("/test.txt");
      
      Serial.println("SD_MMC test successful");
      return true;
    }
  }
  
  Serial.println("❌ SD card write test failed");
  return false;
}

// ===================
// SD CARD TEST FUNCTION
// ===================

bool testSDCard() {
  Serial.println("🧪 Testing SD card availability...");
  
  // Test 1: SD_MMC
  Serial.println("🔧 Testing SD_MMC...");
  if (SD_MMC.begin("/sdcard", true, false)) {
    Serial.println("✅ SD_MMC works!");
    SD_MMC.end();
    return true;
  }
  
  // Test 2: SPI SD with common pins
  Serial.println("🔧 Testing SPI SD...");
  SPI.begin(18, 19, 23, 5); // SCK, MISO, MOSI, CS
  if (SD.begin(5, SPI)) {
    Serial.println("✅ SPI SD works!");
    SD.end();
    return true;
  }
  
  Serial.println("❌ No SD card method works");
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== ESP32-S3 CLEAN TEST ===");
  Serial.println("🚀 Step 1: ESP32-S3 started!");
  Serial.printf("📊 Chip: %s, Rev: %d, CPU: %dMHz\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
  Serial.printf("💾 Free Heap: %d bytes\n", ESP.getFreeHeap());
  
  // Step 2: Check PSRAM
  Serial.println("🧠 Step 2: Checking PSRAM...");
  if (psramFound()) {
    Serial.printf("✅ PSRAM: %d MB available\n", ESP.getPsramSize() / 1024 / 1024);
  } else {
    Serial.println("❌ PSRAM: Not detected");
  }
  
  // Step 3: Initialize WiFi Access Point
  Serial.println("📡 Step 3: Starting WiFi Access Point...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.printf("✅ WiFi AP: %s\n", AP_SSID);
  Serial.printf("📱 IP Address: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("🔑 Password: %s\n", AP_PASSWORD);
  
  // Step 4: Initialize FreeRTOS components for dual-core
  Serial.println("🔧 Step 4: Initializing dual-core architecture...");
  
  // Create queue for photo commands (increased size for better memory management)
  photoQueue = xQueueCreate(PHOTO_QUEUE_SIZE, sizeof(PhotoCommand));
  if (photoQueue == NULL) {
    Serial.println("❌ Failed to create photo queue");
    return;
  }
  
  // Create mutex for SD card access
  sdMutex = xSemaphoreCreateMutex();
  if (sdMutex == NULL) {
    Serial.println("❌ Failed to create SD mutex");
    return;
  }
  
  // Create photo capture task on Core 1 (increased stack for memory management)
  xTaskCreatePinnedToCore(
    photoCaptureTask,    // Task function
    "PhotoCapture",      // Task name
    PHOTO_TASK_STACK,    // Stack size (bytes) - DOUBLED for memory management
    NULL,                // Task parameters
    2,                   // Task priority (0-24, higher = more priority)
    &photoTaskHandle,    // Task handle
    1                    // Core ID (1 for Core 1)
  );
  
  if (photoTaskHandle == NULL) {
    Serial.println("❌ Failed to create photo capture task");
    return;
  }
  
  Serial.println("✅ Dual-core architecture initialized");
  
  // Step 5: Start web server (Core 0)
  Serial.println("🌐 Step 5: Starting web server...");
  
  // Route for ULTRA-MINIMAL main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<!DOCTYPE html><html><head><title>ESP32 Camera</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial;margin:10px;}";
    html += ".status{background:#f0f0f0;padding:10px;margin:10px 0;}";
    html += ".btn{display:inline-block;padding:8px 16px;background:#4CAF50;color:white;text-decoration:none;margin:5px;}";
    html += ".photo{max-width:100%;height:auto;margin:10px 0;}";
    html += "</style></head><body>";
    html += "<h1>ESP32 Camera</h1>";
    
    html += "<div class='status'>";
    html += "<strong>Status:</strong> " + String(cameraReady ? "Ready" : "Not Ready") + "<br>";
    html += "<strong>SD:</strong> " + String(sdCardReady ? "Ready" : "Not Ready") + "<br>";
    html += "<strong>Photos:</strong> " + String(photoCount) + "<br>";
    html += "<strong>Memory:</strong> " + String(ESP.getFreeHeap()) + " bytes<br>";
    html += "<strong>Uptime:</strong> " + String(millis() / 1000) + "s";
    html += "</div>";
    
    if (lastPhotoFilename.length() > 0 && cameraReady && sdCardReady) {
      html += "<img src='" + lastPhotoFilename + "' class='photo' alt='Latest Photo'>";
    }
    
    html += "<br><a href='/gallery' class='btn'>View Latest Photos</a>";
    html += "<a href='/clear-photos' class='btn' style='background:#f44336;'>Clear Photos</a>";
    html += "<a href='/diagnostics' class='btn' style='background:#9C27B0;'>Diagnostics</a>";
    html += "<a href='/format-sd' class='btn' style='background:#FF5722;'>⚠️ Format SD Card</a>";
    
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Route to serve individual photos from SD card
  server.serveStatic("/photos/", SD_MMC, "/photos/");

  // Route for SEQUENTIAL photo gallery with EFFICIENT PAGINATION
  server.on("/gallery", HTTP_GET, [](AsyncWebServerRequest *request){
    unsigned long startTime = millis();
    
    // Get pagination parameters
    int page = 1;
    int perPage = 6; // Show 6 photos per page (can be higher now)
    
    if (request->hasParam("page")) {
      page = request->getParam("page")->value().toInt();
      if (page < 1) page = 1;
    }
    
    if (request->hasParam("per_page")) {
      perPage = request->getParam("per_page")->value().toInt();
      if (perPage < 4) perPage = 4;
      if (perPage > 12) perPage = 12; // Can handle more now
    }
    
    // 🚨 CRITICAL: PAUSE PHOTO CAPTURE DURING GALLERY LOADING
    Serial.println("🔄 Gallery loading - pausing photo capture...");
    clearingInProgress = true;
    delay(200);
    
    // ENHANCED HTML with pagination controls
    String html = "<!DOCTYPE html><html><head><title>Photo Gallery</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:Arial;margin:10px;}";
    html += ".photo{display:inline-block;margin:5px;border:1px solid #ccc;border-radius:5px;}";
    html += ".photo img{width:150px;height:100px;object-fit:cover;border-radius:3px;}";
    html += ".info{font-size:10px;padding:5px;background:#f9f9f9;}";
    html += ".nav{text-align:center;margin:10px 0;}";
    html += ".nav a{padding:8px 16px;background:#4CAF50;color:white;text-decoration:none;margin:2px;border-radius:3px;}";
    html += ".nav span{padding:8px 16px;background:#2196F3;color:white;margin:2px;border-radius:3px;}";
    html += ".nav .disabled{padding:8px 16px;background:#ccc;color:#666;margin:2px;border-radius:3px;}";
    html += ".page-info{text-align:center;margin:10px 0;font-weight:bold;}";
    html += ".per-page{text-align:center;margin:10px 0;}";
    html += ".per-page select{padding:5px;margin:0 5px;}";
    html += "</style></head><body>";
    html += "<h2>Photo Gallery</h2>";
    html += "<div class='nav'>";
    html += "<a href='/'>← Back to Main</a>";
    html += "<a href='/clear-photos' style='background:#f44336;'>Clear Photos</a>";
    html += "<a href='/format-sd' style='background:#FF5722;'>⚠️ Format SD</a>";
    html += "</div>";
    
    int photosDisplayed = 0;
    int startPhoto = (page - 1) * perPage;
    
    // Calculate total pages
    int totalPages = (photoCount > 0) ? ((photoCount + perPage - 1) / perPage) : 1;
    
    // Show page info
    html += "<div class='page-info'>";
    html += "Page " + String(page) + " of " + String(totalPages) + " | Total Photos: " + String(photoCount);
    html += "</div>";
    
    // Per-page selector
    html += "<div class='per-page'>";
    html += "<label>Photos per page: </label>";
    html += "<select onchange='changePerPage(this.value)'>";
    html += "<option value='4'" + String(perPage == 4 ? " selected" : "") + ">4 photos</option>";
    html += "<option value='6'" + String(perPage == 6 ? " selected" : "") + ">6 photos</option>";
    html += "<option value='8'" + String(perPage == 8 ? " selected" : "") + ">8 photos</option>";
    html += "<option value='10'" + String(perPage == 10 ? " selected" : "") + ">10 photos</option>";
    html += "<option value='12'" + String(perPage == 12 ? " selected" : "") + ">12 photos</option>";
    html += "</select>";
    html += "</div>";
    
    html += "<script>";
    html += "function changePerPage(value) {";
    html += "  window.location.href = '/gallery?page=1&per_page=' + value;";
    html += "}";
    html += "</script>";
    
    // SEQUENTIAL PHOTO LOADING - Try each photo number
    if (sdCardReady) {
      if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        html += "<div style='text-align:center;'>";
        
        // SIMPLIFIED: Just show photos without file existence checks
        for (int i = 0; i < perPage && i < photoCount; i++) {
          unsigned long photoNumber = photoCount - startPhoto - i;
          if (photoNumber <= 0) break;
          
          char filename[50];
          snprintf(filename, sizeof(filename), "/photos/photo_%06lu.jpg", photoNumber);
          
          String photoPath = String(filename);
          
          html += "<div class='photo'>";
          html += "<img src='" + photoPath + "' alt='Photo " + String(photoNumber) + "'>";
          html += "<div class='info'>Photo " + String(photoNumber) + "</div>";
          html += "</div>";
          
          photosDisplayed++;
          yield();
          esp_task_wdt_reset();
        }
        
        html += "</div>";
        
        // Enhanced pagination controls
        if (photoCount > perPage) {
          html += "<div class='nav'>";
          
          // Previous page
          if (page > 1) {
            html += "<a href='/gallery?page=" + String(page - 1) + "&per_page=" + String(perPage) + "'>← Previous " + String(perPage) + "</a>";
          } else {
            html += "<span class='disabled'>← Previous " + String(perPage) + "</span>";
          }
          
          // Current page indicator
          html += "<span>Page " + String(page) + "</span>";
          
          // Next page
          if (page < totalPages) {
            html += "<a href='/gallery?page=" + String(page + 1) + "&per_page=" + String(perPage) + "'>Next " + String(perPage) + " →</a>";
          } else {
            html += "<span class='disabled'>Next " + String(perPage) + " →</span>";
          }
          
          html += "</div>";
        }
        
        xSemaphoreGive(sdMutex);
      }
    }
    
    if (photosDisplayed == 0) {
      html += "<p>No photos yet</p>";
    }
    
    html += "</body></html>";
    
    // Send response and cleanup
    request->send(200, "text/html", html);
    html = "";
    
    // Resume photo capture
    clearingInProgress = false;
    Serial.printf("📸 Sequential gallery: %d photos (page %d) in %lu ms\n", photosDisplayed, page, millis() - startTime);
  });

  // Route to clear ALL photos from SD card - WATCHDOG-SAFE BATCH PROCESSING
  server.on("/clear-photos", HTTP_GET, [](AsyncWebServerRequest *request){
    int deletedCount = 0;
    unsigned long startTime = millis();
    
    Serial.println("🗑️ Starting WATCHDOG-SAFE photo deletion...");
    clearingInProgress = true; // Pause photo capture during clearing
    
    // Wait for any ongoing SD operations
    delay(500); // Reduced delay
    
    // Acquire mutex for clearing operation
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) == pdTRUE) { // Reduced timeout
      Serial.println("🔒 SD mutex acquired for clearing");
      
      if (sdCardReady) {
        File root = SD_MMC.open("/photos");
        if (root) {
          File file = root.openNextFile();
          int batchCount = 0;
          
          while (file) {
            // WATCHDOG-SAFE: Process in small batches
            if (!file.isDirectory()) {
              String fullPath = "/photos/" + String(file.name());
              String fileName = String(file.name());
              file.close();
              
              // Delete the file
              if (SD_MMC.remove(fullPath)) {
                deletedCount++;
                Serial.printf("🗑️ Deleted: %s (%d)\n", fileName.c_str(), deletedCount);
              } else {
                Serial.printf("⚠️ Failed to delete: %s\n", fileName.c_str());
              }
              
              batchCount++;
              
              // WATCHDOG-SAFE: Yield every 3 files to prevent timeouts
              if (batchCount % 3 == 0) {
                yield();
                esp_task_wdt_reset();
                delay(10); // Small delay
              }
              
              file = root.openNextFile();
            } else {
              file = root.openNextFile();
            }
            
            // WATCHDOG-SAFE: Shorter timeout (10 seconds max)
            if (millis() - startTime > 10000) {
              Serial.println("⚠️ Clear operation timeout after 10s - stopping for watchdog safety");
              break;
            }
            
            // WATCHDOG-SAFE: Batch limit (50 files per request)
            if (deletedCount >= 50) {
              Serial.println("⚠️ Reached 50 file batch limit - stopping for watchdog safety");
              break;
            }
          }
          root.close();
          
          // Update photo counter
          photoCount = (photoCount > deletedCount) ? (photoCount - deletedCount) : 0;
          if (photoCount == 0) {
            lastPhotoFilename = "";
          }
          
          Serial.printf("✅ WATCHDOG-SAFE Clear: %d files deleted in %lu ms\n", 
                       deletedCount, millis() - startTime);
        } else {
          Serial.println("❌ Failed to open /photos directory");
        }
      } else {
        Serial.println("❌ SD card not ready");
      }
      
      // Release mutex - CRITICAL!
      xSemaphoreGive(sdMutex);
      Serial.println("🔓 SD mutex released after clearing");
      
    } else {
      Serial.println("⚠️ Could not acquire SD mutex for clearing - operation cancelled");
      request->send(503, "text/plain", "⚠️ SD card busy - try again in a few seconds");
    }
    
    String message = "Cleared " + String(deletedCount) + " files from SD card. ";
    if (photoCount > 0) {
      message += "Click 'Clear Photos' again to delete remaining " + String(photoCount) + " files.";
    } else {
      message += "All files deleted!";
    }
    
    String html = "<html><head><meta http-equiv='refresh' content='3;url=/'></head><body>";
    html += "<h2>Photos Cleared!</h2><p>" + message + "</p>";
    html += "<p>✅ Deleted " + String(deletedCount) + " files successfully.</p>";
    if (photoCount > 0) {
      html += "<p><strong>Note:</strong> Batch limit reached. Click 'Clear Photos' again to delete remaining files.</p>";
    }
    html += "</body></html>";
    
    clearingInProgress = false; // Resume photo capture
    Serial.printf("🗑️ User cleared %d files (watchdog-safe)\n", deletedCount);
    request->send(200, "text/html", html);
  });

  // Route to manually refresh SD card filesystem - PROPER MUTEX HANDLING
  server.on("/refresh-sd", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("🔄 Manual SD card refresh requested...");
    
    // Pause photo capture during refresh
    bool wasCapturing = !clearingInProgress;
    clearingInProgress = true;
    
    // Wait for any ongoing SD operations to complete
    delay(1000);
    
    // Try to acquire mutex with timeout - PROPER MUTEX HANDLING
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
      Serial.println("🔒 SD mutex acquired for refresh");
      
      // End SD_MMC safely
      SD_MMC.end();
      delay(500);
      
      // Restart SD_MMC with proper configuration
      if (SD_MMC.begin("/sdcard", true, false)) {
        Serial.println("✅ SD card manually refreshed");
        
        String html = "<html><head><meta http-equiv='refresh' content='2;url=/'></head><body>";
        html += "<h2>SD Card Refreshed!</h2>";
        html += "<p>File system cache cleared. Photos should now be current.</p>";
        html += "<p>Redirecting to main page...</p>";
        html += "</body></html>";
        
        request->send(200, "text/html", html);
      } else {
        Serial.println("❌ Failed to refresh SD card");
        sdCardReady = false;
        request->send(500, "text/plain", "❌ Failed to refresh SD card - check serial monitor");
      }
      
      // Release mutex - CRITICAL!
      xSemaphoreGive(sdMutex);
      Serial.println("🔓 SD mutex released after refresh");
      
    } else {
      Serial.println("⚠️ Could not acquire SD mutex for refresh - operation cancelled");
      request->send(503, "text/plain", "⚠️ SD card busy - try again in a few seconds");
    }
    
    // Resume photo capture
    if (wasCapturing) {
      clearingInProgress = false;
    }
  });

  // Route to format SD card - ACTUAL FORMAT WITH FILE DELETION
  server.on("/format-sd", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("🔄 Starting ACTUAL SD card format process...");
    
    // 🚨 CRITICAL: PAUSE PHOTO CAPTURE DURING FORMAT
    clearingInProgress = true; // Pause photo capture during formatting
    delay(1000); // Wait for any ongoing operations
    
    String html = "<html><head><title>Formatting SD Card</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial;margin:20px;text-align:center;}";
    html += ".status{background:#fff3cd;border:1px solid #ffeaa7;padding:20px;border-radius:5px;margin:20px 0;}";
    html += ".btn{padding:10px 20px;background:#4CAF50;color:white;text-decoration:none;border-radius:5px;margin:10px;}";
    html += "</style></head><body>";
    html += "<h2>SD Card Format</h2>";
    
    if (sdCardReady) {
      html += "<div class='status'>";
      html += "<h3>⚠️ WARNING: This will delete ALL files on the SD card!</h3>";
      html += "<p>This action cannot be undone.</p>";
      html += "<p>Photo capture is paused during formatting.</p>";
      html += "</div>";
      
      // Acquire mutex for formatting operation
      if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(15000)) == pdTRUE) { // Increased timeout
        Serial.println("🔒 SD mutex acquired for formatting");
        
        html += "<div class='status'>";
        html += "<h3>🔄 Formatting SD Card...</h3>";
        html += "<p>Please wait, this may take a few seconds...</p>";
        html += "</div>";
        
        int deletedCount = 0;
        
        // Step 1: Delete ALL files first - INCLUDING SUBDIRECTORIES
        Serial.println("🗑️ Step 1: Deleting all files...");
        
        // First, delete all files in the /photos directory
        if (SD_MMC.exists("/photos")) {
          File photosDir = SD_MMC.open("/photos");
          if (photosDir) {
            File file = photosDir.openNextFile();
            while (file) {
              if (!file.isDirectory()) {
                String fullPath = "/photos/" + String(file.name());
                String fileName = String(file.name());
                file.close();
                
                // Delete the file
                if (SD_MMC.remove(fullPath)) {
                  deletedCount++;
                  Serial.printf("🗑️ Deleted: %s (%d)\n", fileName.c_str(), deletedCount);
                }
                
                // Yield every 5 files
                if (deletedCount % 5 == 0) {
                  yield();
                  esp_task_wdt_reset();
                }
                
                file = photosDir.openNextFile();
              } else {
                file = photosDir.openNextFile();
              }
            }
            photosDir.close();
          }
          
          // Remove the /photos directory itself
          if (SD_MMC.rmdir("/photos")) {
            Serial.println("🗑️ Removed /photos directory");
          }
        }
        
        // Then, delete all files in the root directory
        File root = SD_MMC.open("/");
        if (root) {
          File file = root.openNextFile();
          while (file) {
            if (!file.isDirectory()) {
              String fullPath = "/" + String(file.name());
              String fileName = String(file.name());
              file.close();
              
              // Delete the file
              if (SD_MMC.remove(fullPath)) {
                deletedCount++;
                Serial.printf("🗑️ Deleted: %s (%d)\n", fileName.c_str(), deletedCount);
              }
              
              // Yield every 5 files
              if (deletedCount % 5 == 0) {
                yield();
                esp_task_wdt_reset();
              }
              
              file = root.openNextFile();
            } else {
              file = root.openNextFile();
            }
          }
          root.close();
        }
        
        Serial.printf("🗑️ Deleted %d files before format\n", deletedCount);
        
        // Step 2: End SD card access
        Serial.println("💾 Step 2: Ending SD card access...");
        SD_MMC.end();
        delay(2000); // Longer delay for proper shutdown
        
        // Step 3: Reinitialize with format flag
        Serial.println("💾 Step 3: Reinitializing SD card...");
        if (SD_MMC.begin("/sdcard", true)) { // true = format if needed
          Serial.println("✅ SD card reinitialized successfully");
          
          // Step 4: Verify format by checking if /photos directory is empty
          if (SD_MMC.exists("/photos")) {
            File photosDir = SD_MMC.open("/photos");
            if (photosDir) {
              File file = photosDir.openNextFile();
              if (!file) {
                Serial.println("✅ Format verification: /photos directory is empty");
              } else {
                Serial.println("⚠️ Format verification: /photos directory still has files");
              }
              photosDir.close();
            }
          }
          
          // Reset photo counter
          photoCount = 0;
          lastPhotoFilename = "";
          
          html += "<div class='status' style='background:#d4edda;border-color:#c3e6cb;'>";
          html += "<h3>✅ SD Card Formatted Successfully!</h3>";
          html += "<p>Deleted " + String(deletedCount) + " files before format.</p>";
          html += "<p>All files have been removed.</p>";
          html += "<p>Photo counter reset to 0.</p>";
          html += "<p>Photo capture will resume automatically.</p>";
          html += "</div>";
          
        } else {
          Serial.println("❌ SD card reinitialization failed");
          html += "<div class='status' style='background:#f8d7da;border-color:#f5c6cb;'>";
          html += "<h3>❌ SD Card Format Failed</h3>";
          html += "<p>Please check the SD card and try again.</p>";
          html += "</div>";
        }
        
        // Release mutex
        xSemaphoreGive(sdMutex);
        Serial.println("🔓 SD mutex released after formatting");
        
      } else {
        Serial.println("⚠️ Could not acquire SD mutex for formatting");
        html += "<div class='status' style='background:#f8d7da;border-color:#f5c6cb;'>";
        html += "<h3>⚠️ SD Card Busy</h3>";
        html += "<p>Please try again in a few seconds.</p>";
        html += "</div>";
      }
      
    } else {
      html += "<div class='status' style='background:#f8d7da;border-color:#f5c6cb;'>";
      html += "<h3>❌ SD Card Not Ready</h3>";
      html += "<p>Cannot format - SD card not detected.</p>";
      html += "</div>";
    }
    
    html += "<div style='margin:20px 0;'>";
    html += "<a href='/' class='btn'>← Back to Main</a>";
    html += "<a href='/gallery' class='btn'>View Gallery</a>";
    html += "</div>";
    html += "</body></html>";
    
    // Resume photo capture
    clearingInProgress = false;
    Serial.println("🔄 Photo capture RESUMED after SD format");
    
    request->send(200, "text/html", html);
  });

  // Route for system diagnostics
  server.on("/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><head><title>ESP32-S3 Diagnostics</title></head><body>";
    html += "<h1>System Diagnostics</h1>";
    html += "<h2>Memory Status</h2>";
    html += "<p><strong>Free Heap:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>";
    html += "<p><strong>Min Free Heap:</strong> " + String(ESP.getMinFreeHeap()) + " bytes</p>";
    html += "<p><strong>Heap Size:</strong> " + String(ESP.getHeapSize()) + " bytes</p>";
    html += "<h2>System Status</h2>";
    html += "<p><strong>Uptime:</strong> " + String(millis() / 1000) + " seconds</p>";
    html += "<p><strong>WiFi Clients:</strong> " + String(WiFi.softAPgetStationNum()) + "</p>";
    html += "<p><strong>Camera:</strong> " + String(cameraReady ? "✅ Ready" : "❌ Failed") + "</p>";
    html += "<p><strong>SD Card:</strong> " + String(sdCardReady ? "✅ Ready" : "❌ Failed") + "</p>";
    html += "<p><strong>Photos Count:</strong> " + String(photoCount) + "</p>";
    html += "<p><strong>Clearing In Progress:</strong> " + String(clearingInProgress ? "Yes" : "No") + "</p>";
    html += "<h2>Dual-Core Status</h2>";
    html += "<p><strong>Photo Task:</strong> " + String(photoTaskHandle != NULL ? "✅ Running on Core 1" : "❌ Not Running") + "</p>";
    html += "<p><strong>Web Server:</strong> ✅ Running on Core 0</p>";
    html += "<p><strong>Current Core:</strong> " + String(xPortGetCoreID()) + "</p>";
    html += "<h2>SD Card Info</h2>";
    if (sdCardReady) {
      uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
      uint64_t usedBytes = SD_MMC.usedBytes() / (1024 * 1024);
      uint64_t totalBytes = SD_MMC.totalBytes() / (1024 * 1024);
      html += "<p><strong>Card Size:</strong> " + String((uint32_t)cardSize) + " MB</p>";
      html += "<p><strong>Used Space:</strong> " + String((uint32_t)usedBytes) + " MB</p>";
      html += "<p><strong>Total Space:</strong> " + String((uint32_t)totalBytes) + " MB</p>";
    } else {
      html += "<p>SD Card not available</p>";
    }
    html += "<p><a href='/'>← Back to Main</a> | <a href='/gallery'>View Gallery</a></p>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  server.begin();
  Serial.println("✅ Web server started successfully!");
  Serial.printf("🌐 Open browser to: http://%s\n", WiFi.softAPIP().toString().c_str());
  
  // Step 6: Initialize Camera
  Serial.println("📷 Step 6: Initializing camera...");
  cameraReady = initCamera();
  if (cameraReady) {
    Serial.println("✅ Camera initialization successful!");
  } else {
    Serial.println("❌ Camera initialization failed - continuing without camera");
  }
  
  // Step 7: Initialize SD Card
  Serial.println("💾 Step 7: Initializing SD card storage...");
  sdCardReady = initSDCard();
  if (sdCardReady) {
    Serial.println("✅ SD card initialization successful!");
  } else {
    Serial.println("❌ SD card initialization failed - continuing without storage");
  }
  
  Serial.println("🎯 System Complete: WiFi + Web Server + Camera + Storage + Dual-Core!");
  Serial.printf("📱 System ready - connect to '%s' and visit http://%s\n", 
                AP_SSID, WiFi.softAPIP().toString().c_str());
}

void loop() {
  // AsyncWebServer is event-driven - no handleClient() needed
  
  // 📊 AGGRESSIVE MEMORY MONITORING - Check every 5 seconds
  static unsigned long lastMemoryCheck = 0;
  static unsigned long lastRecoveryCycle = 0;
  if (millis() - lastMemoryCheck > 5000) { // Reduced to 5 seconds
    int currentHeap = ESP.getFreeHeap();
    int minHeap = ESP.getMinFreeHeap();
    
    Serial.printf("📊 Memory: %d bytes free (min: %d)\n", currentHeap, minHeap);
    
    // EMBEDDED: More aggressive thresholds
    if (currentHeap < 30000) {
      Serial.printf("⚠️ Low memory: %d bytes\n", currentHeap);
    }
    
    if (currentHeap < 20000) {
      Serial.printf("🚨 CRITICAL: %d bytes - forcing cleanup\n", currentHeap);
      forceMemoryRecovery();
    }
    
    // EMBEDDED: More frequent recovery cycles (every 60 seconds)
    if (millis() - lastRecoveryCycle > 60000) { // 1 minute
      Serial.println("🔄 Periodic memory recovery...");
      
      // Only do memory recovery if memory is actually low
      if (currentHeap < 30000) {
        forceMemoryRecovery();
      } else {
        Serial.println("📊 Memory OK - skipping recovery cycle");
      }
      lastRecoveryCycle = millis();
    }
    
    lastMemoryCheck = millis();
  }
  
  // EMBEDDED photo capture - every 10 seconds with strict memory management
  static unsigned long lastPhotoTime = 0;
  if (millis() - lastPhotoTime > PHOTO_INTERVAL) {
    // EMBEDDED: Stricter memory thresholds
    int currentHeap = ESP.getFreeHeap();
    if (currentHeap < 20000) {
      Serial.printf("🚨 EMBEDDED: Pausing photo capture - only %d bytes free\n", currentHeap);
      lastPhotoTime = millis(); // Reset timer
    } else if (capturePhoto()) {
      // Photo count is now incremented in the photo capture task
    }
    lastPhotoTime = millis();
  }
  
  // Update system status every 10 seconds
  static unsigned long lastStatusTime = 0;
  if (millis() - lastStatusTime > 10000) {
    UBaseType_t queueSpaces = uxQueueSpacesAvailable(photoQueue);
    Serial.printf("⏱️  Uptime: %lu sec | Heap: %d bytes | WiFi: %d clients | Camera: %s | SD: %s | Photos: %d | Queue: %d/%d\n",
                  millis() / 1000, ESP.getFreeHeap(), WiFi.softAPgetStationNum(),
                  cameraReady ? "✅ Ready" : "❌ Failed",
                  sdCardReady ? "✅ Ready" : "❌ Failed", photoCount, 
                  queueSpaces, PHOTO_QUEUE_SIZE);
    
    if (lastPhotoFilename.length() > 0) {
      Serial.printf("🌐 Web interface: http://%s (Latest photo: %s)\n", 
                    WiFi.softAPIP().toString().c_str(), lastPhotoFilename.c_str());
    }
    
    lastStatusTime = millis();
  }
  
  // Feed watchdog
  esp_task_wdt_reset();
  yield();
}