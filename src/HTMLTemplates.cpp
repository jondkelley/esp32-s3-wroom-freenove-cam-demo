#include "HTMLTemplates.h"
#include "config.h"

String HTMLTemplates::getConfigPage() {
  return R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>ESP32 WiFi Setup</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 40px; background: #f0f0f0; }
        .container { background: white; padding: 30px; border-radius: 10px; max-width: 400px; margin: 0 auto; }
        h1 { color: #333; text-align: center; }
        input { width: 100%; padding: 12px; margin: 8px 0; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }
        button { width: 100%; padding: 15px; background: #4CAF50; color: white; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; }
        button:hover { background: #45a049; }
        .info { background: #e7f3ff; padding: 15px; border-radius: 5px; margin-bottom: 20px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP32-S3 Camera Ready!</h1>
        <h2>WiFi Setup</h2>
        <div class="info">
            <strong>Connection Instructions:</strong><br>
            3. Fill out the form below<br>
            4. Click Save to connect
        </div>
        <form action="/save" method="POST">
            <label>WiFi Network Name (SSID):</label>
            <input type="text" name="ssid" placeholder="Enter WiFi network name" required>
            
            <label>WiFi Password:</label>
            <input type="password" name="password" placeholder="Enter WiFi password" required>
            
            <button type="submit">Save & Connect</button>
        </form>
    </div>
</body>
</html>
)";
}

String HTMLTemplates::getResetPage(const String& ssid, const String& ip) {
  return R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>ESP32 Reset WiFi</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 40px; background: #f0f0f0; }
        .container { background: white; padding: 30px; border-radius: 10px; max-width: 400px; margin: 0 auto; }
        h1 { color: #333; text-align: center; }
        .warning { background: #fff3cd; padding: 15px; border-radius: 5px; margin-bottom: 20px; border-left: 4px solid #ffc107; }
        .button { display: inline-block; padding: 15px 20px; margin: 10px; text-decoration: none; border-radius: 5px; font-size: 16px; text-align: center; cursor: pointer; }
        .btn-danger { background: #dc3545; color: white; }
        .btn-secondary { background: #6c757d; color: white; }
        .btn-danger:hover { background: #c82333; }
        .btn-secondary:hover { background: #5a6268; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Reset WiFi Configuration</h1>
        <div class="warning">
            <strong>Warning:</strong> This will erase the saved WiFi settings and restart the device in configuration mode.
        </div>
        <p>Current WiFi: <strong>)" + ssid + R"(</strong></p>
        <p>Device IP: <strong>)" + ip + R"(</strong></p>

        <div style="text-align: center;">
            <form action="/reset/confirm" method="POST" style="display: inline;">
                <button type="submit" class="button btn-danger">Yes, Reset WiFi</button>
            </form>
            <a href="/" class="button btn-secondary">Cancel</a>
        </div>
    </div>
</body>
</html>
)";
}

String HTMLTemplates::getCameraStatusPage(const String& ssid, const String& ip, const String& mac, 
                                         int rssi, const String& uptime, 
                                         bool cameraReady, const String& lastPhoto) {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>ESP32-S3 Camera Stream</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta http-equiv="refresh" content="5">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .container { max-width: 1000px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; margin-bottom: 30px; }
        .status-section { margin-bottom: 30px; }
        .status-section h2 { color: #444; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }
        .status-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-bottom: 20px; }
        .status-item { background: #f9f9f9; padding: 15px; border-radius: 5px; border-left: 4px solid #4CAF50; }
        .status-item strong { color: #333; }
        .camera-section { text-align: center; margin: 30px 0; }
        .camera-feed { max-width: 100%; height: auto; border: 3px solid #4CAF50; border-radius: 10px; margin: 20px 0; }
        .camera-info { background: #f0f8f0; padding: 20px; border-radius: 10px; margin: 20px 0; }
        .connected { color: #4CAF50; font-weight: bold; }
        .disconnected { color: #f44336; font-weight: bold; }
        .actions { text-align: center; margin-top: 30px; }
        .btn { display: inline-block; padding: 12px 24px; margin: 0 10px; text-decoration: none; border-radius: 5px; font-weight: bold; }
        .btn-danger { background: #f44336; color: white; }
        .btn-info { background: #2196F3; color: white; }
        .btn-success { background: #4CAF50; color: white; }
        .btn:hover { opacity: 0.8; }
        .wifi-status { color: #4CAF50; font-weight: bold; }
        .live-indicator { display: inline-block; width: 10px; height: 10px; background: #ff4444; border-radius: 50%; margin-right: 5px; animation: blink 1s infinite; }
        @keyframes blink { 0%, 50% { opacity: 1; } 51%, 100% { opacity: 0.3; } }
    </style>
</head>
<body>
    <div class="container">
        <h1><span class="live-indicator"></span>ESP32-S3 Camera Stream</h1>
        
        <div class="status-section">
            <h2>Device Status</h2>
            <div class="status-grid">
                <div class="status-item">
                    <strong>WiFi:</strong> <span class="wifi-status">✓ Connected</span>
                </div>
                <div class="status-item">
                    <strong>Network:</strong> )" + ssid + R"(
                </div>
                <div class="status-item">
                    <strong>IP Address:</strong> )" + ip + R"(
                </div>
                <div class="status-item">
                    <strong>MAC Address:</strong> )" + mac + R"(
                </div>
                <div class="status-item">
                    <strong>Signal Strength:</strong> )" + String(rssi) + R"( dBm
                </div>
                <div class="status-item">
                    <strong>Uptime:</strong> )" + uptime + R"(
                </div>
            </div>
        </div>
        
        <div class="status-section">
            <h2>Camera Feed</h2>
            <div class="camera-section">
                <div class="camera-info">
                    <strong>Camera Status:</strong> 
                    <span class=")" + getCameraStatusClass(cameraReady) + R"(">
                        )" + getCameraStatusText(cameraReady) + R"(
                    </span>
                    <br><br>
                    <strong>Auto-capture:</strong> Taking photos every 1 second<br>
                    <strong>Last Photo:</strong> )" + (lastPhoto.length() > 0 ? lastPhoto : "None yet") + R"(
                </div>
                
                )";
    
    if (cameraReady && lastPhoto.length() > 0) {
        html += R"(<img class="camera-feed" src="/latest" alt="Latest Camera Photo" id="cameraImage">
                    <br>
                    <em>Image updates every 5 seconds automatically</em>)";
    } else {
        html += R"(<div style="padding: 40px; background: #f8f8f8; border: 2px dashed #ccc; border-radius: 10px;">
                        <p style="color: #666; margin: 0;">)";
        html += (cameraReady ? "Camera ready - waiting for first photo..." : "Camera not ready");
        html += R"(</p>
                    </div>)";
    }
    
    html += R"(
            </div>
        </div>
        
        <div class="actions">
            <a href="/reset" class="btn btn-danger">Reset WiFi Settings</a>
            <a href="/" class="btn btn-info">Refresh</a>
            )";
    
    if (cameraReady && lastPhoto.length() > 0) {
        html += R"(<a href="/latest" class="btn btn-success" target="_blank">View Full Size</a>)";
    }
    
    html += R"(
        </div>
    </div>
</body>
</html>
)";
    
    return html;
}

String HTMLTemplates::getConnectingPage(const String& ssid) {
  return "<html><head><meta charset='utf-8'></head><body><h2>Connecting...</h2>"
         "<p>Attempting to connect to: <strong>" + ssid + "</strong></p>"
         "<p>If successful, this page will no longer be accessible.</p>"
         "<p>Look for your device on the main network.</p></body></html>";
}

String HTMLTemplates::getResetConfirmPage() {
  return "<html><head><meta http-equiv='refresh' content='8;url=about:blank'><meta charset='utf-8'></head>"
         "<body><h2>Resetting WiFi Configuration...</h2>"
         "<p>The device is clearing WiFi settings and restarting in configuration mode.</p>"
         "<p>Look for the WiFi network: <strong>" + String(AP_SSID) + "</strong></p>"
         "<p>This page will no longer be accessible.</p></body></html>";
}

String HTMLTemplates::getCameraStatusClass(bool ready) {
  return ready ? "connected" : "disconnected";
}

String HTMLTemplates::getCameraStatusText(bool ready) {
  return ready ? "✓ Camera Ready" : "✗ Camera Not Ready";
} 