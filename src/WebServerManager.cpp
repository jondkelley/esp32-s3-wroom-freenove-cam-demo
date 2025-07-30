#include "WebServerManager.h"
#include "esp_system.h"
#include "FS.h"
#include "SD_MMC.h"

WebServerManager::WebServerManager(WiFiManager* wifiMgr, CameraManager* cameraMgr) 
  : server(nullptr), mainServer(nullptr), dnsServer(nullptr), 
    wifiManager(wifiMgr), cameraManager(cameraMgr),
    configMode(false), resetRequested(false), resetTime(0), startTime(0) {
}

WebServerManager::~WebServerManager() {
  stopConfigMode();
  stopMainServer();
}

void WebServerManager::begin(unsigned long startupTime) {
  startTime = startupTime;
}

void WebServerManager::startConfigMode() {
  configMode = true;
  
  // Start Access Point
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(2000); // Give AP time to start
  
  IPAddress IP = WiFi.softAPIP();
  Serial.println("✓ Access Point started");
  Serial.print("Network Name: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("IP Address: ");
  Serial.println(IP);
  
  // Start DNS server for captive portal
  dnsServer = new DNSServer();
  dnsServer->start(DNS_PORT, "*", IP);
  
  // Start web server for configuration
  server = new AsyncWebServer(HTTP_PORT);
  
  server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleRoot(request);
  });
  
  server->on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handleSave(request);
  });
  
  // Catch all handler for captive portal
  server->onNotFound([this](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", HTMLTemplates::getConfigPage());
  });
  
  server->begin();
  Serial.println("✓ Configuration web server started");
  Serial.println("Connect to the WiFi network and you'll be redirected to setup page");
}

void WebServerManager::stopConfigMode() {
  configMode = false;
  
  if (server) {
    server->end();
    delete server;
    server = nullptr;
  }
  
  if (dnsServer) {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }
  
  WiFi.softAPdisconnect(true);
  Serial.println("Configuration mode stopped");
}

void WebServerManager::startMainServer() {
  mainServer = new AsyncWebServer(HTTP_PORT);
  
  mainServer->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleMainRoot(request);
  });
  
  mainServer->on("/reset", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleReset(request);
  });
  
  mainServer->on("/reset/confirm", HTTP_POST, [this](AsyncWebServerRequest *request) {
    this->handleResetConfirm(request);
  });
  
  mainServer->on("/photo", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handlePhotoRequest(request);
  });
  
  mainServer->on("/latest", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleLatestPhoto(request);
  });
  
  mainServer->begin();
  Serial.println("✓ Main application web server started");
  Serial.println("Access http://" + wifiManager->getIPAddress() + "/reset to reset WiFi settings");
  Serial.println("Camera feed: http://" + wifiManager->getIPAddress() + "/");
}

void WebServerManager::stopMainServer() {
  if (mainServer) {
    mainServer->end();
    delete mainServer;
    mainServer = nullptr;
    Serial.println("Main server stopped");
  }
}

void WebServerManager::handleLoop() {
  if (configMode) {
    // Handle DNS requests for captive portal
    if (dnsServer) {
      dnsServer->processNextRequest();
    }
  } else {
    // Check for pending reset request
    if (resetRequested && millis() >= resetTime) {
      Serial.println("Executing delayed WiFi reset...");
      
      // Stop the main server first
      stopMainServer();
      
      // Clear the stored configuration
      wifiManager->clearConfig();
      
      Serial.println("Configuration cleared, restarting...");
      Serial.flush(); // Ensure all serial output is sent
      delay(500);
      
      // Use hardware reset instead of software reset
      esp_restart(); // Hardware reset - more reliable than ESP.restart()
    }
  }
}

bool WebServerManager::isResetRequested() const {
  return resetRequested;
}

void WebServerManager::clearResetRequest() {
  resetRequested = false;
}

void WebServerManager::handleRoot(AsyncWebServerRequest *request) {
  request->send(200, "text/html; charset=utf-8", HTMLTemplates::getConfigPage());
}

void WebServerManager::handleSave(AsyncWebServerRequest *request) {
  if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
    request->send(400, "text/plain; charset=utf-8", "Missing SSID or password");
    return;
  }
  
  WiFiConfig newConfig;
  newConfig.ssid = request->getParam("ssid", true)->value();
  newConfig.password = request->getParam("password", true)->value();
  
  Serial.println("Received new WiFi configuration:");
  Serial.print("SSID: ");
  Serial.println(newConfig.ssid);
  
  // Send response before stopping server
  request->send(200, "text/html; charset=utf-8", 
                HTMLTemplates::getConnectingPage(newConfig.ssid));
  
  delay(1000); // Give time for response to be sent
  
  // Save configuration
  wifiManager->saveConfig(newConfig);
  
  // Stop configuration mode
  stopConfigMode();
  
  // Try to connect to new WiFi
  if (wifiManager->connectToWiFi(newConfig)) {
    // Initialize mDNS
    wifiManager->setupMDNS();
    
    // Start main server
    startMainServer();
  } else {
    Serial.println("✗ Failed to connect to new WiFi, restarting...");
    delay(2000);
    esp_restart(); // Restart to try again
  }
}

void WebServerManager::handleMainRoot(AsyncWebServerRequest *request) {
  String html = HTMLTemplates::getCameraStatusPage(
    wifiManager->getSSID(),
    wifiManager->getIPAddress(),
    wifiManager->getMACAddress(),
    wifiManager->getSignalStrength(),
    formatUptime(millis() - startTime),
    cameraManager->isCameraReady(),
    cameraManager->getLastPhotoFilename()
  );
  
  request->send(200, "text/html; charset=utf-8", html);
}

void WebServerManager::handleReset(AsyncWebServerRequest *request) {
  String html = HTMLTemplates::getResetPage(
    wifiManager->getSSID(),
    wifiManager->getIPAddress()
  );
  request->send(200, "text/html; charset=utf-8", html);
}

void WebServerManager::handleResetConfirm(AsyncWebServerRequest *request) {
  Serial.println("WiFi reset requested via web interface");
  
  request->send(200, "text/html; charset=utf-8", HTMLTemplates::getResetConfirmPage());
  
  // Set a flag to reset after a delay (allows response to be sent)
  resetRequested = true;
  resetTime = millis() + 3000; // Reset after 3 seconds
}

String WebServerManager::formatUptime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  
  String uptime = "";
  if (days > 0) uptime += String(days) + "d ";
  if (hours > 0) uptime += String(hours) + "h ";
  if (minutes > 0) uptime += String(minutes) + "m ";
  uptime += String(seconds) + "s";
  
  return uptime;
}

void WebServerManager::handlePhotoRequest(AsyncWebServerRequest *request) {
  String filename = "";
  
  if (request->hasParam("file")) {
    filename = request->getParam("file")->value();
  } else {
    filename = cameraManager->getLastPhotoFilename();
  }
  
  if (filename.length() == 0) {
    request->send(404, "text/plain", "No photo available");
    return;
  }
  
  if (!SD_MMC.exists(filename.c_str())) {
    request->send(404, "text/plain", "Photo not found");
    return;
  }
  
  request->send(SD_MMC, filename.c_str(), "image/jpeg");
}

void WebServerManager::handleLatestPhoto(AsyncWebServerRequest *request) {
  String filename = cameraManager->getLastPhotoFilename();
  
  if (filename.length() == 0) {
    request->send(404, "text/plain", "No photo available");
    return;
  }
  
  if (!SD_MMC.exists(filename.c_str())) {
    request->send(404, "text/plain", "Photo not found");
    return;
  }
  
  request->send(SD_MMC, filename.c_str(), "image/jpeg");
} 