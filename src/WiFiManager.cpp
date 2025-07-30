#include "WiFiManager.h"

WiFiManager::WiFiManager() {
}

void WiFiManager::begin() {
  Serial.println("🔧 Starting WiFi manager initialization...");
  
  try {
    Serial.println("📋 Step 1: Initializing preferences storage...");
    preferences.begin("wifi-config", false);
    Serial.println("✓ NVS preferences initialized successfully");
    
    Serial.println("📋 Step 2: Loading saved WiFi configuration...");
    currentConfig = loadConfig();
    
    if (currentConfig.isValid()) {
      Serial.printf("✓ Found saved WiFi config - SSID: %s\n", currentConfig.ssid.c_str());
    } else {
      Serial.println("ℹ No saved WiFi configuration found");
    }
    
    Serial.println("✓ WiFi manager initialization completed");
    
  } catch (const std::exception& e) {
    Serial.print("💥 Exception during WiFi manager initialization: ");
    Serial.println(e.what());
  } catch (...) {
    Serial.println("💥 Unknown exception during WiFi manager initialization");
  }
}

bool WiFiManager::connectToSavedWiFi() {
  Serial.println("🔍 Checking for saved WiFi credentials...");
  
  if (currentConfig.isValid()) {
    Serial.printf("✓ Found saved WiFi config - SSID: %s\n", currentConfig.ssid.c_str());
    Serial.println("🔗 Attempting to connect to saved network...");
    return connectToWiFi(currentConfig);
  } else {
    Serial.println("ℹ No valid saved WiFi configuration found");
    Serial.println("📡 Will start in configuration mode");
    return false;
  }
}

bool WiFiManager::connectToWiFi(const WiFiConfig& config) {
  Serial.printf("🔗 Connecting to WiFi network: %s\n", config.ssid.c_str());
  Serial.println("⏳ Connection attempt in progress...");
  
  // Set WiFi mode and start connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  
  int attempts = 0;
  const int maxAttempts = 20; // 10 seconds total
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    attempts++;
    
    if (attempts % 4 == 0) {
      Serial.printf("🔄 Connection attempt %d/%d - Status: %d\n", 
                    attempts, maxAttempts, WiFi.status());
    } else {
      Serial.print(".");
    }
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    currentConfig = config;
    Serial.println("🎉 Successfully connected to WiFi!");
    Serial.printf("✓ SSID: %s\n", config.ssid.c_str());
    Serial.printf("✓ IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("✓ Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("✓ DNS: %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("✓ Signal Strength: %d dBm\n", WiFi.RSSI());
    return true;
  } else {
    Serial.printf("❌ Failed to connect to WiFi after %d attempts\n", attempts);
    Serial.printf("⚠ Final status code: %d\n", WiFi.status());
    Serial.println("💡 Will start configuration mode instead");
    return false;
  }
}

void WiFiManager::saveConfig(const WiFiConfig& config) {
  preferences.putString("ssid", config.ssid);
  preferences.putString("password", config.password);
  currentConfig = config;
  Serial.println("WiFi config saved to memory");
}

void WiFiManager::clearConfig() {
  preferences.clear();
  currentConfig = WiFiConfig();
  Serial.println("WiFi config cleared from memory");
}

WiFiConfig WiFiManager::getCurrentConfig() const {
  return currentConfig;
}

bool WiFiManager::setupMDNS() {
  Serial.println("Initializing mDNS...");
  if (MDNS.begin(MDNS_NAME)) {
    Serial.println("✓ mDNS responder started");
    MDNS.addService("http", "tcp", HTTP_PORT);
    Serial.println("✓ HTTP service advertised");
    Serial.println("Access your ESP32 at: http://" + String(MDNS_NAME) + ".local");
    return true;
  } else {
    Serial.println("✗ Error setting up mDNS responder");
    return false;
  }
}

String WiFiManager::getSSID() const {
  return currentConfig.ssid;
}

String WiFiManager::getIPAddress() const {
  return WiFi.localIP().toString();
}

String WiFiManager::getMACAddress() const {
  return WiFi.macAddress();
}

int WiFiManager::getSignalStrength() const {
  return WiFi.RSSI();
}

bool WiFiManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

WiFiConfig WiFiManager::loadConfig() {
  WiFiConfig config;
  config.ssid = preferences.getString("ssid", "");
  config.password = preferences.getString("password", "");
  
  if (config.isValid()) {
    Serial.println("Loaded WiFi config from memory");
  }
  
  return config;
} 