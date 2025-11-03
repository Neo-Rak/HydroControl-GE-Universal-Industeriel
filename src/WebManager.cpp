#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "WebManager.h"
#include <map>
#include <vector>

// --- Extern Global Variables from main.cpp ---
extern enum DeviceRole currentRole;
extern String deviceName;
extern String deviceId;
extern std::map<String, Device> managedDevices;
extern std::map<String, std::vector<String>> wellAssignments;
extern SemaphoreHandle_t sharedDataMutex;
extern bool isFull, pumpOn, faultActive;
extern class ThingsBoardManager tbManager;

// --- Forward Declarations from main.cpp ---
extern void sendPumpCommand(const String& wellId, bool turnOn);
extern void loraSend(const JsonDocument& doc);


WebManager::WebManager() : server(80), ws("/ws") {}

void WebManager::begin() {
    ws.onEvent(std::bind(&WebManager::onWsEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    server.addHandler(&ws);
    setupWebServer();
    server.begin();
}

void WebManager::notifyClients() {
    JsonDocument doc;
    doc["type"] = "STATE_UPDATE";

    if (xSemaphoreTake(sharedDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (currentRole == HYDRO_CONTROL_GE) {
            JsonArray devices = doc.createNestedArray("devices");
            for (auto const& [id, device] : managedDevices) {
                JsonObject d = devices.createNestedObject();
                d["id"] = id;
                d["name"] = device.name;
                d["role"] = (device.role == AQUA_RESERV_PRO) ? "AquaReservPro" : "WellguardPro";
                d["lastSeen"] = (millis() - device.lastSeen) / 1000;
                if (device.role == AQUA_RESERV_PRO) d["isFull"] = device.isFull;
                if (device.role == WELLGUARD_PRO) {
                    d["pumpOn"] = device.pumpOn;
                    d["faultActive"] = device.faultActive;
                }
            }
            JsonObject assignments = doc.createNestedObject("assignments");
            for (auto const& [wellId, reservoirs] : wellAssignments) {
                JsonArray r_ids = assignments.createNestedArray(wellId);
                for(const String& r_id : reservoirs) {
                    r_ids.add(r_id);
                }
            }
            JsonObject tb = doc.createNestedObject("tb");
            tb["server"] = tbManager.getServer();
            tb["port"] = tbManager.getPort();
            tb["token"] = tbManager.getToken();
            tb["enabled"] = tbManager.isEnabled();
        } else { // Peripherals
            doc["deviceId"] = deviceId;
            if(currentRole == AQUA_RESERV_PRO) doc["isFull"] = isFull;
            if(currentRole == WELLGUARD_PRO) {
                doc["pumpOn"] = pumpOn;
                doc["faultActive"] = faultActive;
            }
        }
        xSemaphoreGive(sharedDataMutex);
    }

    String jsonString;
    serializeJson(doc, jsonString);
    ws.textAll(jsonString);
}


void WebManager::setupWebServer() {
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String html = F(
            "<!DOCTYPE html><html><head><title>HydroControl - %ROLE%</title><meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<style>"
                "body{font-family:system-ui,sans-serif;background:#f0f2f5;color:#333;margin:0} .header{background:#007bff;color:white;padding:20px;text-align:center} .header h1{margin:0} .container{padding:20px}"
                ".card{background:white;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);margin-bottom:20px;padding:20px} h2{border-bottom:2px solid #007bff;padding-bottom:10px;margin-top:0}"
                ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px} .status-on{color:#28a745;font-weight:bold} .status-off{color:#dc3545;font-weight:bold}"
                "button{background:#007bff;color:white;border:none;padding:10px 15px;border-radius:5px;cursor:pointer;font-size:1em} button:hover{background:#0056b3} .disconnected{opacity:0.5}"
            "</style>"
            "</head><body><div class='header'><h1>HydroControl-GE</h1><p><strong>Device:</strong> %DEVICENAME% | <strong>Role:</strong> %ROLE%</p></div><div class='container' id='content'></div>"
            "<script>"
                "const ws=new WebSocket(`ws://${window.location.host}/ws`);"
                "ws.onopen=()=>ws.send(JSON.stringify({type:'GET_STATE'}));"
                "ws.onmessage=e=>render(JSON.parse(e.data));"
                "const send=(msg)=>ws.send(JSON.stringify(msg));"
                "const render=(data)=>{if(data.type!=='STATE_UPDATE')return;const c=document.getElementById('content');"
                "if(data.devices){c.innerHTML=renderCentral(data)}else{c.innerHTML=renderPeripheral(data)}};"
                // Render functions will be inserted here by C++
                "%RENDER_SCRIPT%"
            "</script></body></html>"
        );

        String roleStr = "";
        String renderScript = "";

        switch (currentRole) {
            case AQUA_RESERV_PRO:
                roleStr = "AquaReservPro";
                renderScript = R"JS(
                    const renderPeripheral = (data) => `
                        <div class="card">
                            <h2>Reservoir Status</h2>
                            <p><strong>Level:</strong> <span class="${data.isFull ? 'status-on' : 'status-off'}">${data.isFull ? 'FULL' : 'EMPTY'}</span></p>
                            <button onclick="send({type:'MANUAL_FILL_REQUEST'})">Request Manual Fill</button>
                        </div>
                    `;
                )JS";
                break;
            case WELLGUARD_PRO:
                roleStr = "WellguardPro";
                 renderScript = R"JS(
                    const renderPeripheral = (data) => `
                        <div class="card">
                            <h2>Pump Status</h2>
                            <p><strong>Pump:</strong> <span class="${data.pumpOn ? 'status-on' : 'status-off'}">${data.pumpOn ? 'ON' : 'OFF'}</span></p>
                            <p><strong>Fault:</strong> <span class="${data.faultActive ? 'status-off' : 'status-on'}">${data.faultActive ? 'FAULT ACTIVE' : 'Normal'}</span></p>
                            <button onclick="send({type:'MANUAL_PUMP_TOGGLE'})">Toggle Pump Manually</button>
                        </div>
                    `;
                )JS";
                break;
            case HYDRO_CONTROL_GE:
                roleStr = "HydroControl-GE";
                renderScript = R"JS(
                    const renderCentral = (data) => {
                        let wells = data.devices.filter(d => d.role === 'WellguardPro');
                        let reservoirs = data.devices.filter(d => d.role === 'AquaReservPro');
                        return `
                        <div class="grid">
                            ${wells.map(w => renderModule(w, data.assignments)).join('')}
                            ${reservoirs.map(r => renderModule(r, data.assignments)).join('')}
                        </div>
                        <div class="card">
                            <h2>Assignments</h2>
                            <form onsubmit="event.preventDefault(); saveAssignments(this);">
                            ${wells.map(w => `
                                <div>
                                    <strong>${w.name}</strong> is assigned to:
                                    ${reservoirs.map(r => `
                                        <label><input type="checkbox" name="${w.id}" value="${r.id}" ${data.assignments[w.id] && data.assignments[w.id].includes(r.id) ? 'checked' : ''}> ${r.name}</label>
                                    `).join('')}
                                </div>
                            `).join('')}
                            <button type="submit">Save Assignments</button>
                            </form>
                        </div>
                        <div class="card">
                            <h2>ThingsBoard Gateway</h2>
                            <form onsubmit="event.preventDefault(); saveTb(this);">
                                <input type="text" name="tb_server" placeholder="ThingsBoard IP/Host" value="${data.tb ? data.tb.server : ''}">
                                <input type="text" name="tb_port" placeholder="Port (e.g., 1883)" value="${data.tb ? data.tb.port : ''}">
                                <input type="text" name="tb_token" placeholder="Gateway Access Token" value="${data.tb ? data.tb.token : ''}">
                                <label><input type="checkbox" name="tb_enabled" ${data.tb && data.tb.enabled ? 'checked' : ''}> Enable Gateway</label>
                                <button type="submit">Save ThingsBoard Config</button>
                            </form>
                        </div>
                        `;
                    }
                    const renderModule = (d, assignments) => `
                        <div class="card ${d.lastSeen > 900 ? 'disconnected' : ''}">
                            <h2>${d.name} <small>(${d.role})</small></h2>
                            <p>Last seen: ${d.lastSeen}s ago</p>
                            ${d.role === 'AquaReservPro' ? `<p><strong>Level:</strong> <span class="${d.isFull ? 'status-on' : 'status-off'}">${d.isFull ? 'FULL' : 'EMPTY'}</span></p>` : ''}
                            ${d.role === 'WellguardPro' ? `
                                <p><strong>Pump:</strong> <span class="${d.pumpOn ? 'status-on' : 'status-off'}">${d.pumpOn ? 'ON' : 'OFF'}</span></p>
                                <p><strong>Fault:</strong> <span class="${d.faultActive ? 'status-off' : 'status-on'}">${d.faultActive ? 'FAULT ACTIVE' : 'Normal'}</span></p>
                                <button onclick="send({type:'FORCE_PUMP', wellId:'${d.id}', state:true})">Force ON</button>
                                <button onclick="send({type:'FORCE_PUMP', wellId:'${d.id}', state:false})">Force OFF</button>
                            ` : ''}
                        </div>
                    `;
                    const saveAssignments = (form) => {
                        let assignments = {};
                        new FormData(form).forEach((value, key) => {
                            if (!assignments[key]) assignments[key] = [];
                            assignments[key].push(value);
                        });
                        send({type: 'SAVE_ASSIGNMENTS', payload: assignments});
                    }
                    const saveTb = (form) => {
                        let tb = {};
                        new FormData(form).forEach((value, key) => tb[key] = value);
                        tb.tb_enabled = form.querySelector('[name=tb_enabled]').checked;
                        send({type: 'SAVE_TB_CONFIG', payload: tb});
                    }
                )JS";
                break;
            default:
                roleStr = "N/A";
                renderScript = "const renderPeripheral=()=>`<p>Device not configured.</p>`;";
        }

        html.replace("%ROLE%", roleStr);
        html.replace("%DEVICENAME%", deviceName);
        html.replace("%RENDER_SCRIPT%", renderScript);
        request->send(200, "text/html", html);
    });
}


void WebManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT || (type == WS_EVT_DATA && deserializeJson((JsonDocument&)*arg, (char*)data).is<JsonObject>() && (*arg)["type"] == "GET_STATE")) {
        notifyClients(); // Send full state on connect or request
    } else if (type == WS_EVT_DATA) {
        JsonDocument doc;
        deserializeJson(doc, (char*)data);
        String msgType = doc["type"];

        if (msgType == "MANUAL_PUMP_TOGGLE" && currentRole == WELLGUARD_PRO) {
            // This is a local action, handled in main loop
        } else if (msgType == "MANUAL_FILL_REQUEST" && currentRole == AQUA_RESERV_PRO) {
            JsonDocument req;
            req["type"] = "MANUAL_FILL_REQUEST";
            req["id"] = deviceId;
            loraSend(req);
        } else if (msgType == "FORCE_PUMP" && currentRole == HYDRO_CONTROL_GE) {
            sendPumpCommand(doc["wellId"], doc["state"]);
        } else if (msgType == "SAVE_ASSIGNMENTS" && currentRole == HYDRO_CONTROL_GE) {
            if (xSemaphoreTake(sharedDataMutex, portMAX_DELAY) == pdTRUE) {
                wellAssignments.clear();
                for (JsonPairConst kv : doc["payload"].as<JsonObjectConst>()) {
                    for (JsonVariantConst v : kv.value().as<JsonArrayConst>()) {
                        wellAssignments[kv.key().c_str()].push_back(v.as<String>());
                    }
                }
                xSemaphoreGive(sharedDataMutex);
                notifyClients(); // Push updated assignments to all clients
            }
        } else if (msgType == "SAVE_TB_CONFIG" && currentRole == HYDRO_CONTROL_GE) {
            JsonObject payload = doc["payload"];
            tbManager.updateCredentials(payload["tb_server"], payload["tb_port"].as<int>(), payload["tb_token"]);
            tbManager.setEnabled(payload["tb_enabled"]);
            // The tbManager will save to NVS.
            notifyClients();
        }
    }
}
