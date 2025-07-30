#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include "config.h"

class WiFiManager {
private:
  Preferences preferences;
  WiFiConfig currentConfig;

public:
  WiFiManager();
  void begin();
  bool connectToSavedWiFi();
  bool connectToWiFi(const WiFiConfig& config);
  void saveConfig(const WiFiConfig& config);
  void clearConfig();
  WiFiConfig getCurrentConfig() const;
  
  // mDNS functions
  bool setupMDNS();
  
  // Status functions
  String getSSID() const;
  String getIPAddress() const;
  String getMACAddress() const;
  int getSignalStrength() const;
  bool isConnected() const;

private:
  WiFiConfig loadConfig();
};

#endif 