#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "WebManager.h"

// Bring in role definitions from main.cpp
extern enum DeviceRole currentRole;
extern String deviceName;
extern std::map<String, Device> managedDevices;
// Need to add forward declarations or include headers for pump commands, etc.
extern void sendPumpCommand(const String& wellId, bool turnOn);


const char* HTML_TEMPLATE_START = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>HydroControl - %ROLE%</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: system-ui, sans-serif; background: #f0f2f5; color: #333; margin: 0; }
        .header { background: #007bff; color: white; padding: 20px; text-align: center; }
        .header h1 { margin: 0; }
        .header p { opacity: 0.8; }
        .container { padding: 20px; }
        .card { background: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; padding: 20px; }
        h2 { border-bottom: 2px solid #007bff; padding-bottom: 10px; margin-top: 0; }
        .status-on { color: #28a745; font-weight: bold; }
        .status-off { color: #dc3545; font-weight: bold; }
        button { background: #007bff; color: white; border: none; padding: 10px 15px; border-radius: 5px; cursor: pointer; font-size: 1em; }
        button:hover { background: #0056b3; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }
    </style>
</head>
<body>
    <div class="header">
        <h1>HydroControl-GE</h1>
        <p><strong>Device:</strong> <span id="deviceName">%DEVICENAME%</span> | <strong>Role:</strong> %ROLE%</p>
    </div>
    <div class="container">
)rawliteral";

const char* HTML_TEMPLATE_END = R"rawliteral(
    </div>
    <script>
        const ws = new WebSocket(`ws://${window.location.host}/ws`);
        ws.onmessage = function(event) {
            const data = JSON.parse(event.data);
            console.log(data);
            // Logic to update page elements based on received data will go here
            if (data.type === 'statusUpdate') {
                // Example: document.getElementById('level').textContent = data.level;
            }
        };
        function sendWsMessage(msg) {
            ws.send(JSON.stringify(msg));
        }
    </script>
</body>
</html>
)rawliteral";

// --- Role-Specific HTML ---
const char* HTML_AQUA_RESERV_PRO = R"rawliteral(
<div class="card">
    <h2>Reservoir Status</h2>
    <p><strong>Level:</strong> <span id="level" class="status-off">Unknown</span></p>
    <p><strong>Assigned to Well:</strong> <span id="assignedWell">N/A</span></p>
    <button onclick="sendWsMessage({type:'manualFillRequest'})">Request Manual Fill</button>
</div>
)rawliteral";

const char* HTML_WELLGUARD_PRO = R"rawliteral(
<div class="card">
    <h2>Pump Status</h2>
    <p><strong>Pump:</strong> <span id="pump" class="status-off">OFF</span></p>
    <p><strong>Fault Sensor:</strong> <span id="fault" class="status-on">Normal</span></p>
    <button onclick="sendWsMessage({type:'manualPumpToggle'})">Toggle Pump Manually</button>
</div>
<div class="card">
    <h2>Assigned Reservoirs</h2>
    <ul id="assignedReservoirs">
        <li>N/A</li>
    </ul>
</div>
)rawliteral";

const char* HTML_HYDRO_CONTROL_GE = R"rawliteral(
<div class="grid">
    <div class="card">
        <h2>System Overview</h2>
        <p><strong>Connected Modules:</strong> <span id="connectedModules">0</span></p>
    </div>
    <div class="card" id="modulesList">
        <h2>Modules</h2>
        <!-- Module cards will be injected here -->
    </div>
</div>
<div class="card">
    <h2>Assignments</h2>
    <!-- Assignment UI will go here -->
</div>
<div class="card">
    <h2>Logs</h2>
    <pre id="logs" style="max-height: 200px; overflow-y: scroll; background: #eee; padding: 10px;"></pre>
</div>
)rawliteral";

WebManager::WebManager() : server(80), ws("/ws") {}

void WebManager::begin() {
    setupWebServer();
    ws.onEvent(std::bind(&WebManager::onWsEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    server.addHandler(&ws);
    server.begin();
}

void WebManager::notifyClients(const JsonDocument& doc) {
    String jsonString;
    serializeJson(doc, jsonString);
    ws.textAll(jsonString);
}

void WebManager::setupWebServer() {
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String html = HTML_TEMPLATE_START;
        String roleStr, content;

        switch (currentRole) {
            case AQUA_RESERV_PRO:
                roleStr = "AquaReservPro";
                content = HTML_AQUA_RESERV_PRO;
                break;
            case WELLGUARD_PRO:
                roleStr = "WellguardPro";
                content = HTML_WELLGUARD_PRO;
                break;
            case HYDRO_CONTROL_GE:
                roleStr = "HydroControl-GE";
                content = HTML_HYDRO_CONTROL_GE;
                break;
            default:
                roleStr = "N/A";
                content = "<p>Device not configured.</p>";
        }

        html.replace("%ROLE%", roleStr);
        html.replace("%DEVICENAME%", deviceName);
        html += content;
        html += HTML_TEMPLATE_END;

        request->send(200, "text/html", html);
    });
}

void WebManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.println("WebSocket client connected");
        // Send initial state to the new client
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.println("WebSocket client disconnected");
    } else if (type == WS_EVT_DATA) {
        JsonDocument doc;
        deserializeJson(doc, (char*)data);
        String msgType = doc["type"];

        if (msgType == "manualPumpToggle" && currentRole == WELLGUARD_PRO) {
            // Need to implement the actual toggle logic
        } else if (msgType == "manualFillRequest" && currentRole == AQUA_RESERV_PRO) {
            // Need to send LoRa request
        } else if (msgType == "forcePump" && currentRole == HYDRO_CONTROL_GE) {
            String wellId = doc["wellId"];
            bool state = doc["state"];
            sendPumpCommand(wellId, state);
        }
    }
}
