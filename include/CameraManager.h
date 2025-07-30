#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include <Arduino.h>
#include "esp_camera.h"
#include "config.h"

class CameraManager {
private:
  camera_config_t camera_config;
  bool cameraInitialized;
  unsigned long lastCaptureTime;
  String lastPhotoFilename;

public:
  CameraManager();
  bool begin();
  bool capturePhoto();
  String getLastPhotoFilename() const;
  bool isCameraReady() const;
  bool shouldTakePhoto() const;
  void handleLoop();
  
private:
  void initCameraConfig();
  String generatePhotoFilename();
  bool savePhotoToSD(camera_fb_t* fb, const String& filename);
};

#endif