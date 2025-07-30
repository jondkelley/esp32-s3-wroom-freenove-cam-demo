#include "CameraManager.h"
#include "FS.h"
#include "SD_MMC.h"

CameraManager::CameraManager() : cameraInitialized(false), lastCaptureTime(0), lastPhotoFilename("") {
}

bool CameraManager::begin() {
  Serial.println("🔧 Starting camera manager initialization...");
  
  try {
    Serial.println("📋 Step 1: Configuring camera parameters...");
    initCameraConfig();
    Serial.println("✓ Camera configuration completed");
    
    Serial.println("⚠ NOTICE: Camera initialization may cause system hang if pins are wrong");
    Serial.println("🔧 If system hangs here, check GPIO pin configuration in config.h");
    Serial.println("📞 Common Freenove ESP32-S3 camera pins differ from standard modules");
    
    // Initialize camera
    Serial.println("📋 Step 2: Initializing ESP32 camera module...");
    Serial.println("⏳ This may take a few seconds...");
    Serial.flush(); // Ensure all debug messages are sent before potential hang
    
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
      Serial.printf("✗ Camera init failed with error 0x%x\n", err);
      Serial.println("💡 Possible causes:");
      Serial.println("   - Camera module not connected");
      Serial.println("   - Wrong GPIO pin configuration for your Freenove board");
      Serial.println("   - Insufficient power supply (camera needs 3.3V with good current)");
      Serial.println("   - Camera module damaged");
      Serial.println("   - SPI pins conflicting with SD card");
      return false;
    }
    Serial.println("✓ Camera module initialized successfully");
    
    // Test camera by taking a test shot
    Serial.println("📋 Step 3: Testing camera functionality...");
    camera_fb_t* test_fb = esp_camera_fb_get();
    if (!test_fb) {
      Serial.println("✗ Camera test shot failed - cannot capture frames");
      return false;
    }
    Serial.printf("✓ Camera test successful - captured %d bytes\n", test_fb->len);
    esp_camera_fb_return(test_fb);
    
    // Initialize SD card using SD MMC interface (hardwired on Freenove board)
    Serial.println("📋 Step 4: Initializing SD card (SD MMC)...");
    Serial.printf("🔌 SD MMC Pins - CMD:%d, CLK:%d, D0:%d (hardwired)\n", 
                  SD_MMC_CMD, SD_MMC_CLK, SD_MMC_D0);
    
    // Set the SD MMC pins for Freenove board
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    
    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode for single data line
      Serial.println("✗ SD Card Mount Failed");
      Serial.println("💡 Possible causes:");
      Serial.println("   - No SD card inserted");
      Serial.println("   - SD card not formatted (use FAT32)");
      Serial.println("   - SD card damaged or corrupted");
      Serial.println("   - SD card not compatible with MMC interface");
      Serial.println("⚠ Continuing without SD card - photos will not be saved");
      return false;
    }
    Serial.println("✓ SD card mounted successfully via SD MMC");
    
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
      Serial.println("✗ No SD card detected after mount");
      return false;
    }
    
    Serial.print("📂 SD Card Type: ");
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
    Serial.printf("💾 SD Card Size: %lluMB\n", cardSize);
    
    // Create photos directory if it doesn't exist
    Serial.println("📋 Step 5: Setting up photos directory...");
    if (!SD_MMC.exists(PHOTOS_DIR)) {
      if (SD_MMC.mkdir(PHOTOS_DIR)) {
        Serial.printf("✓ Created photos directory: %s\n", PHOTOS_DIR);
      } else {
        Serial.printf("✗ Failed to create photos directory: %s\n", PHOTOS_DIR);
        return false;
      }
    } else {
      Serial.printf("✓ Photos directory already exists: %s\n", PHOTOS_DIR);
    }
    
    // Test SD card write
    Serial.println("📋 Step 6: Testing SD card write capability...");
    File testFile = SD_MMC.open("/test.txt", FILE_WRITE);
    if (testFile) {
      testFile.println("ESP32-S3 Camera Test");
      testFile.close();
      SD_MMC.remove("/test.txt");
      Serial.println("✓ SD card write test successful");
    } else {
      Serial.println("✗ SD card write test failed");
      return false;
    }
    
    cameraInitialized = true;
    Serial.println("🎉 Camera manager initialization completed successfully!");
    Serial.println("📸 Ready to capture photos every 1 second");
    
    return true;
    
  } catch (const std::exception& e) {
    Serial.print("💥 Exception during camera initialization: ");
    Serial.println(e.what());
    return false;
  } catch (...) {
    Serial.println("💥 Unknown exception during camera initialization");
    return false;
  }
}

void CameraManager::initCameraConfig() {
  Serial.println("🔧 Configuring camera GPIO pins...");
  
  // Configure GPIO pins
  camera_config.ledc_channel = LEDC_CHANNEL_0;
  camera_config.ledc_timer = LEDC_TIMER_0;
  camera_config.pin_d0 = Y2_GPIO_NUM;
  camera_config.pin_d1 = Y3_GPIO_NUM;
  camera_config.pin_d2 = Y4_GPIO_NUM;
  camera_config.pin_d3 = Y5_GPIO_NUM;
  camera_config.pin_d4 = Y6_GPIO_NUM;
  camera_config.pin_d5 = Y7_GPIO_NUM;
  camera_config.pin_d6 = Y8_GPIO_NUM;
  camera_config.pin_d7 = Y9_GPIO_NUM;
  camera_config.pin_xclk = XCLK_GPIO_NUM;
  camera_config.pin_pclk = PCLK_GPIO_NUM;
  camera_config.pin_vsync = VSYNC_GPIO_NUM;
  camera_config.pin_href = HREF_GPIO_NUM;
  camera_config.pin_sccb_sda = SIOD_GPIO_NUM;
  camera_config.pin_sccb_scl = SIOC_GPIO_NUM;
  camera_config.pin_pwdn = PWDN_GPIO_NUM;
  camera_config.pin_reset = RESET_GPIO_NUM;
  camera_config.xclk_freq_hz = 20000000;
  camera_config.pixel_format = PIXFORMAT_JPEG;
  
  Serial.println("📌 Camera Pin Configuration:");
  Serial.printf("   D0-D7: %d,%d,%d,%d,%d,%d,%d,%d\n", 
                Y2_GPIO_NUM, Y3_GPIO_NUM, Y4_GPIO_NUM, Y5_GPIO_NUM,
                Y6_GPIO_NUM, Y7_GPIO_NUM, Y8_GPIO_NUM, Y9_GPIO_NUM);
  Serial.printf("   XCLK: %d, PCLK: %d\n", XCLK_GPIO_NUM, PCLK_GPIO_NUM);
  Serial.printf("   VSYNC: %d, HREF: %d\n", VSYNC_GPIO_NUM, HREF_GPIO_NUM);
  Serial.printf("   SDA: %d, SCL: %d\n", SIOD_GPIO_NUM, SIOC_GPIO_NUM);
  Serial.printf("   PWDN: %d, RESET: %d\n", PWDN_GPIO_NUM, RESET_GPIO_NUM);
  
  // Check PSRAM and optimize for Freenove ESP32-S3-WROOM CAM (8MB PSRAM)
  bool psramAvailable = psramFound();
  Serial.printf("🧠 PSRAM Status: %s\n", psramAvailable ? "Available" : "Not Available");
  
  if (psramAvailable) {
    size_t psram_size = ESP.getPsramSize();
    size_t free_psram = ESP.getFreePsram();
    Serial.printf("📊 PSRAM: %d bytes total (%.1f MB), %d bytes free\n", 
                  psram_size, psram_size / (1024.0 * 1024.0), free_psram);
    
    // Optimize for 8MB PSRAM - use maximum quality settings
    camera_config.frame_size = FRAMESIZE_UXGA;  // 1600x1200 - maximum resolution
    camera_config.jpeg_quality = 8;  // High quality (lower number = better quality)
    camera_config.fb_count = 2;  // Double buffering for smooth capture
    camera_config.grab_mode = CAMERA_GRAB_LATEST;
    camera_config.fb_location = CAMERA_FB_IN_PSRAM;
    
    Serial.println("🎯 PSRAM-optimized camera settings:");
    Serial.println("   - Resolution: UXGA (1600x1200)");
    Serial.println("   - Quality: High (8)");
    Serial.println("   - Frame buffers: 2 (double buffering)");
    Serial.println("   - Buffer location: PSRAM");
  } else {
    Serial.println("⚠ No PSRAM - using conservative settings");
    camera_config.frame_size = FRAMESIZE_CIF;
    camera_config.jpeg_quality = 20;
    camera_config.fb_count = 1;
    camera_config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    camera_config.fb_location = CAMERA_FB_IN_DRAM;
  }
}

bool CameraManager::isCameraReady() const {
  return cameraInitialized;
}

bool CameraManager::shouldTakePhoto() const {
  return cameraInitialized && (millis() - lastCaptureTime >= 1000); // Every 1 second
}

void CameraManager::handleLoop() {
  if (shouldTakePhoto()) {
    capturePhoto();
  }
}

bool CameraManager::capturePhoto() {
  if (!cameraInitialized) {
    Serial.println("⚠ Cannot capture photo - camera not initialized");
    return false;
  }
  
  static int photoCounter = 0;
  photoCounter++;
  
  Serial.printf("📸 Capturing photo #%d...\n", photoCounter);
  
  // Take picture with camera
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("✗ Camera capture failed - no frame buffer");
    return false;
  }
  
  Serial.printf("✓ Frame captured - Size: %d bytes, Format: %s\n", 
                fb->len, (fb->format == PIXFORMAT_JPEG) ? "JPEG" : "RAW");
  
  // Generate filename
  String filename = generatePhotoFilename();
  Serial.printf("💾 Saving as: %s\n", filename.c_str());
  
  // Save to SD card
  bool saved = savePhotoToSD(fb, filename);
  
  // Release the frame buffer
  esp_camera_fb_return(fb);
  
  if (saved) {
    lastPhotoFilename = filename;
    lastCaptureTime = millis();
    Serial.printf("✅ Photo #%d saved successfully: %s\n", photoCounter, filename.c_str());
    return true;
  } else {
    Serial.printf("❌ Failed to save photo #%d\n", photoCounter);
  }
  
  return false;
}

String CameraManager::generatePhotoFilename() {
  static int photoNumber = 1;
  return String(PHOTOS_DIR) + "/photo_" + String(photoNumber++) + ".jpg";
}

bool CameraManager::savePhotoToSD(camera_fb_t* fb, const String& filename) {
  File file = SD_MMC.open(filename.c_str(), FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return false;
  }
  
  size_t written = file.write(fb->buf, fb->len);
  file.close();
  
  if (written != fb->len) {
    Serial.println("Failed to write complete image data");
    return false;
  }
  
  return true;
}

String CameraManager::getLastPhotoFilename() const {
  return lastPhotoFilename;
}