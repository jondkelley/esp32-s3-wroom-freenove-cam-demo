#ifndef HTML_TEMPLATES_H
#define HTML_TEMPLATES_H

#include <Arduino.h>

class HTMLTemplates {
public:
  static String getConfigPage();
  static String getResetPage(const String& ssid, const String& ip);
  static String getCameraStatusPage(const String& ssid, const String& ip, const String& mac, 
                                   int rssi, const String& uptime, 
                                   bool cameraReady, const String& lastPhoto);
  static String getConnectingPage(const String& ssid);
  static String getResetConfirmPage();

private:
  static String getCameraStatusClass(bool ready);
  static String getCameraStatusText(bool ready);
};

#endif 