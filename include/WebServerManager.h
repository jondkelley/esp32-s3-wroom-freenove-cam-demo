#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "config.h"
#include "WiFiManager.h"
#include "CameraManager.h"
#include "HTMLTemplates.h"

class WebServerManager {
private:
  AsyncWebServer* server;
  AsyncWebServer* mainServer;
  DNSServer* dnsServer;
  WiFiManager* wifiManager;
  CameraManager* cameraManager;
  bool configMode;
  bool resetRequested;
  unsigned long resetTime;
  unsigned long startTime;

public:
  WebServerManager(WiFiManager* wifiMgr, CameraManager* cameraMgr);
  ~WebServerManager();
  
  void begin(unsigned long startupTime);
  void startConfigMode();
  void stopConfigMode();
  void startMainServer();
  void stopMainServer();
  void handleLoop();
  
  bool isResetRequested() const;
  void clearResetRequest();

private:
  // Configuration mode handlers
  void handleRoot(AsyncWebServerRequest *request);
  void handleSave(AsyncWebServerRequest *request);
  
  // Main mode handlers
  void handleMainRoot(AsyncWebServerRequest *request);
  void handleReset(AsyncWebServerRequest *request);
  void handleResetConfirm(AsyncWebServerRequest *request);
  void handlePhotoRequest(AsyncWebServerRequest *request);
  void handleLatestPhoto(AsyncWebServerRequest *request);
  
  // Utility functions
  String formatUptime(unsigned long milliseconds);
};

#endif 