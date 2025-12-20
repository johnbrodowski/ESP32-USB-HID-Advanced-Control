#ifndef ARDUINO_USB_MODE
#error This ESP32 SoC has no Native USB interface
#elif ARDUINO_USB_MODE == 1
#warning This sketch should be used when USB is in OTG mode
#else

#include "esp_event.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "LCD_Driver.h"
#include "GUI_Paint.h"
#include "USB.h"
#include "USBHIDKeyboard.h"

// ============================================================================
// CONFIGURATION - IMPORTANT: Configure via settings.json on SD card
// ============================================================================
// SECURITY: All credentials should be stored in settings.json on the SD card
// Do NOT hardcode credentials in this file before committing to version control
// 
// Example settings.json:
// {
//   "ssid": "YourWiFiName",
//   "password": "YourWiFiPassword",
//   "openai_api_key": "sk-your-api-key-here",
//   "auth_username": "admin",
//   "auth_password": "your-secure-password"
// }

// Pin definitions for SD card SPI
#define SPI1_SCK 36
#define SPI1_MISO 37
#define SPI1_MOSI 35
#define SPI1_SS 34

// ============================================================================
// Global Variables
// ============================================================================
SPIClass spi1(HSPI);
File uploadFile;
USBHIDKeyboard Keyboard;
WebServer server(80);

WiFiClient feedbackClient; 
String feedbackMessage = "";
String requestLogBuffer = "";

// Settings structure with safe defaults
struct Settings {
    String ssid = "";                    // Configure in settings.json
    String password = "";                // Configure in settings.json
    String openai_api_key = "";          // Configure in settings.json
    String model = "gpt-4o";
    String auth_username = "admin";      // Change this!
    String auth_password = "changeme";   // Change this!
    int max_completion_tokens = 1000;
    float temperature = 0.40f;
    float top_p = 1.00f;
    float frequency_penalty = 0.00f;
    float presence_penalty = 0.00f;
    int typing_delay = 150;
    int command_delay = 150;
    int add_more_delay = 0;
    bool auth_enabled = true;
};

Settings settings;
const int buttonPin = 0;

// ============================================================================
// Function Prototypes
// ============================================================================
// Core functions
void initializeSD();
void connectToWiFi();
void startWebServer();
void loadSettings();
void saveSettings();
void displayOnLCD(const String& message);

// Authentication
bool checkAuth();
bool isValidPath(const String& path);

// HTTP Handlers
void handleRoot(); // <<
void handleLogin();
void handleLogout();
void handleNotFoundRouter();
void handleSettingsPage(); // <<
void handleSettings();
void handleFileManagerPage(); // <<
void handleDuckyPage(); // <<
void handleStatus();
void handleFeedback();
void handleEvents();
void handleSysInfo();
void handleGetLogs();
void handleGetFileOrDir();
void handleDeleteFile();
void handleCreateDir();
void handleCreateFile();
void handleSaveScript();
void handleDeleteScript();
void handleListScripts();
void handleExecuteScript();
void handleSubmit();
void handleExecuteDucky();
void handleConvertPowerShell();
void handleFileUpload();
void handleFormatSD();
void handleFileServer();

// Helper functions
void logRequest(WebServer& svr);
void pushSseEvent(const String& eventName, const String& data);
String sendToOpenAI(const String& query, const String& systemPrompt);
String createJsonPayload(const String& userContent, const String& systemPrompt);
String extractContentField(const String& response);
String getScriptList();
String convertPowerShellToSingleCommand(const String& multiLineScript);
String parseAndExecuteAiResponse(const String& aiResponse);
String extractTagContent(const String& source, const String& tagName);

// Keyboard functions
void sendKeyboardText(const String& text);
void executeScriptViaCmd(const String& content);
void executeDuckyScript(const String& script);

// ============================================================================
// Setup and Main Loop
// ============================================================================
void setup() {
    pinMode(buttonPin, INPUT_PULLUP);
    Config_Init();
    LCD_Init();
    LCD_SetBacklight(100);
    Paint_NewImage(LCD_WIDTH, LCD_HEIGHT, 270, WHITE);
    Paint_SetRotate(270);
    LCD_Clear(WHITE);
    Serial.begin(115200);

    displayOnLCD("Init Keyboard");
    Keyboard.begin();
    USB.begin();
   
    displayOnLCD("Init SD Card");
    initializeSD();

    displayOnLCD("Load Settings");
    loadSettings();

    displayOnLCD("Connecting WiFi");
    connectToWiFi();

    displayOnLCD("Start Server");
    startWebServer();

    displayOnLCD(WiFi.localIP().toString());
}

void loop() {
    server.handleClient();
    delay(2);
}

// ============================================================================
// Initialization Functions
// ============================================================================
void initializeSD() {
    spi1.begin(SPI1_SCK, SPI1_MISO, SPI1_MOSI, SPI1_SS);

    if (!SD.begin(SPI1_SS, spi1)) {
        Serial.println("SD Card initialization failed!");
        displayOnLCD("SD FAILED!");
        return;
    }

    Serial.println("SD Card initialized.");

    // Create required directories
    if (!SD.exists("/scripts")) SD.mkdir("/scripts");
    if (!SD.exists("/ducky_scripts")) SD.mkdir("/ducky_scripts");
    if (!SD.exists("/payloads")) SD.mkdir("/payloads");
}

void connectToWiFi() {
    if (settings.ssid.length() == 0) {
        Serial.println("ERROR: No WiFi SSID configured!");
        Serial.println("Create settings.json on SD card with your WiFi credentials.");
        displayOnLCD("NO WiFi CFG!");
        return;
    }
    
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
    Serial.print("Connecting to: ");
    Serial.println(settings.ssid);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed!");
        displayOnLCD("WiFi FAILED!");
    }
}

// ============================================================================
// Authentication Functions
// ============================================================================
bool checkAuth() {
    if (!settings.auth_enabled) return true;
    
    if (!server.authenticate(settings.auth_username.c_str(), settings.auth_password.c_str())) {
        server.requestAuthentication();
        return false;
    }
    return true;
}

void handleLogin() {
    if (server.hasArg("username") && server.hasArg("password")) {
        if (server.arg("username") == settings.auth_username && 
            server.arg("password") == settings.auth_password) {
            server.sendHeader("Location", "/");
            server.send(302, "text/plain", "OK");
            return;
        }
    }
    
    String html = R"(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Login - ESP32</title>
<style>
body{font-family:Arial;background:#1a1a2e;display:flex;justify-content:center;align-items:center;height:100vh;margin:0}
form{background:#16213e;padding:40px;border-radius:12px;color:#eaeaea;box-shadow:0 4px 20px rgba(0,0,0,.3)}
input{width:100%;padding:12px;margin:10px 0;border-radius:8px;border:1px solid #0f3460;background:#1a1a2e;color:#eaeaea;box-sizing:border-box}
button{width:100%;padding:14px;background:#00bf63;border:none;border-radius:8px;color:#fff;cursor:pointer;font-weight:bold;font-size:1rem}
button:hover{opacity:0.9}
h2{text-align:center;margin-top:0;color:#e94560}
</style></head><body>
<form method="POST" action="/login">
<h2>ESP32 Login</h2>
<input name="username" placeholder="Username" required>
<input name="password" type="password" placeholder="Password" required>
<button type="submit">Login</button>
</form></body></html>)";
    server.send(401, "text/html", html);
}

void handleLogout() {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "Logged out");
}










void handleRoot() {
    logRequest(server);
    String html = R"WEBUI(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Advanced Control</title>
    <script src="https://cdn.jsdelivr.net/npm/monaco-editor@0.34.0/min/vs/loader.js"></script>
    <style>
        :root{--bg-color:#2c3e50;--primary-color:#34495e;--secondary-color:#2980b9;--font-color:#ecf0f1;--border-color:#2980b9;--success-color:#27ae60;--danger-color:#c0392b;--output-bg:#1e2b38;--good-color:#2ecc71;--bad-color:#e74c3c}
        *{box-sizing:border-box;margin:0;padding:0}
        body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background-color:var(--bg-color);color:var(--font-color);line-height:1.6}
        .container{max-width:1400px;margin:auto;padding:20px}
        header{background:var(--primary-color);padding:1rem;margin-bottom:20px;border-radius:8px;text-align:center}
        nav a{color:var(--font-color);text-decoration:none;padding:5px 15px;border-radius:5px;transition:background-color .3s;display:inline-block;margin:0 5px}
        nav a:hover{background-color:var(--secondary-color)}
        .main-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(450px,1fr));gap:20px}
        .card{background:var(--primary-color);border-radius:8px;padding:20px;box-shadow:0 4px 8px rgba(0,0,0,.2);display:flex;flex-direction:column}
        .card h2{margin-top:0;margin-bottom:15px;border-bottom:2px solid var(--secondary-color);padding-bottom:10px}
        input,select,button,textarea{width:100%;padding:12px;margin-bottom:10px;border-radius:5px;border:1px solid var(--border-color);background-color:var(--bg-color);color:var(--font-color);font-size:1rem}
        button{background-color:var(--secondary-color);border:none;cursor:pointer;transition:background-color .3s;font-weight:bold}
        button.success{background-color:var(--success-color)}
        button.danger{background-color:var(--danger-color)}
        textarea{resize:vertical;min-height:120px;font-family:"Courier New",Courier,monospace}
        .status-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px}
        .info-label{color:var(--secondary-color);font-weight:bold}
        .info-value{color:var(--font-color)}
        .admin-true{color:var(--good-color);font-weight:bold}
        .admin-false{color:var(--bad-color);font-weight:bold}
        #editorContainer{width:100%;height:400px;border:1px solid var(--border-color);border-radius:5px;margin-bottom:10px}
        .log-panel{background-color:var(--output-bg);height:200px;overflow-y:scroll;padding:10px;border-radius:5px;font-family:'Courier New',monospace;white-space:pre-wrap;margin-bottom:10px;flex-grow:1}
        .log-panel:empty::before{content:'Waiting for data...';color:#777}
        .chat-message{margin-bottom:10px}
        .chat-message pre{white-space:pre-wrap;word-wrap:break-word;margin-top:5px;padding:10px;background:var(--bg-color);border-radius:5px}
    </style>
</head>
<body>
    <div class="container">
        <header><h1>ESP32 Advanced Control</h1>
            <nav>
                <a href="/settings_page">Settings</a>
                <a href="/ducky">Ducky Studio</a>
                <a href="/file_manager">File Manager</a>
            </nav>
        </header>
        <div class="main-grid">
            <div class="card" style="grid-column: 1 / -1;"><h2>System Information</h2><div class="status-grid"><div><span class="info-label">Hostname:</span> <span id="info-hostname" class="info-value">...</span></div><div><span class="info-label">User:</span> <span id="info-user" class="info-value">...</span> (<span id="info-admin">...</span>)</div><div><span class="info-label">Operating System:</span> <span id="info-os" class="info-value">...</span></div><div><span class="info-label">Architecture:</span> <span id="info-arch" class="info-value">...</span></div></div></div>
            <div class="card"><h2>Live Command Output</h2><div id="output-log" class="log-panel"></div></div>
            <div class="card"><h2>Incoming Request Log</h2><div id="device-log-panel" class="log-panel"></div></div>
            <div class="card"><h2>AI Assistant</h2><div id="chatbox" class="log-panel"></div><input type="text" id="userInput" placeholder="Ask AI to generate a script..."><button type="button" onclick="sendMessage()">Send to AI</button></div>
            <div class="card"><h2>Device Status</h2><div class="status-grid"><div><span class="info-label">ESP32 IP:</span> <span id="status-ip" class="info-value">...</span></div><div><span class="info-label">Signal:</span> <span id="status-rssi" class="info-value">...</span></div><div><span class="info-label">Network:</span> <span id="status-ssid" class="info-value">...</span></div><div><span class="info-label">SD Card:</span> <span id="status-sd" class="info-value">...</span> MB free</div></div></div>
            <div class="card" style="grid-column: 1 / -1;"><h2>Script Editor</h2><select id="scriptList" onchange="loadScript()"></select><input type="text" id="scriptName" placeholder="Enter new script name"><div id="editorContainer"></div><div style="display: flex; gap: 10px;"><button type="button" class="success" onclick="saveScript()">Save Script</button><button type="button" class="danger" onclick="deleteScript()">Delete Script</button><button type="button" class="success" onclick="executeScript()">Execute on Target</button></div></div>
        </div>
    </div>

    <script>
        var scriptEditor;
        require.config({ paths: { 'vs': 'https://cdn.jsdelivr.net/npm/monaco-editor@0.34.0/min/vs' }});
        require(['vs/editor/editor.main'], function() {
            scriptEditor = monaco.editor.create(document.getElementById('editorContainer'), {
                value: '# Paste a PowerShell script here, or select one to load.',
                language: 'powershell',
                theme: 'vs-dark',
                automaticLayout: true
            });
        });

        function setupEventSource() {
            const outputLog = document.getElementById('output-log');
            const eventSource = new EventSource('/events');
            
            eventSource.onopen = () => console.log("SSE Connection for live output is open.");
            
            eventSource.addEventListener('message', (event) => {
                outputLog.innerHTML += event.data.replace(/</g, "<").replace(/>/g, ">") + '\n';
                outputLog.scrollTop = outputLog.scrollHeight;
            });

            eventSource.addEventListener('sysinfo', (event) => {
                try {
                    const data = JSON.parse(event.data);
                    document.getElementById('info-hostname').innerText = data.Hostname || 'N/A';
                    document.getElementById('info-user').innerText = data.CurrentUser || 'N/A';
                    const adminSpan = document.getElementById('info-admin');
                    adminSpan.innerText = data.IsAdmin ? 'ADMIN' : 'User';
                    adminSpan.className = data.IsAdmin ? 'admin-true' : 'admin-false';
                    document.getElementById('info-os').innerText = data.OS_Name || 'N/A';
                    document.getElementById('info-arch').innerText = data.OS_Architecture || 'N/A';
                } catch(e) { console.error("Failed to parse sysinfo JSON:", e); }
            });

            eventSource.onerror = () => { eventSource.close(); setTimeout(setupEventSource, 5000); };
        }
        
        function fetchRequestLog() {
            const deviceLogPanel = document.getElementById('device-log-panel');
            fetch('/get_logs')
                .then(response => response.text())
                .then(text => {
                    if (text) {
                        deviceLogPanel.innerHTML += text.replace(/</g, "<").replace(/>/g, ">");
                        deviceLogPanel.scrollTop = deviceLogPanel.scrollHeight;
                    }
                })
                .catch(e => console.error("Failed to fetch request log:", e));
        }

        function escapeHtml(text) {
            return text.replace(/</g, "<").replace(/>/g, ">");
        }

        function sendMessage(){
            var userInput = document.getElementById("userInput");
            var message = userInput.value;
            if(!message) return;
            var chatbox = document.getElementById("chatbox");
            chatbox.innerHTML += '<div class="chat-message"><strong>You:</strong> ' + escapeHtml(message) + '</div>';
            chatbox.scrollTop = chatbox.scrollHeight;
            userInput.value = "";
            fetch("/submit", { method: "POST", headers: {"Content-Type":"application/x-www-form-urlencoded"}, body: "query=" + encodeURIComponent(message) })
                .then(response => response.text())
                .then(text => {
                    chatbox.innerHTML += '<div class="chat-message"><strong>AI:</strong><pre>' + escapeHtml(text) + '</pre></div>';
                    chatbox.scrollTop = chatbox.scrollHeight;
                });
        }
        
        function updateStatus(){fetch("/status").then(r=>r.json()).then(data=>{document.getElementById("status-ip").innerText=data.ip;document.getElementById("status-rssi").innerText=data.rssi+" dBm";document.getElementById("status-ssid").innerText=data.ssid;document.getElementById("status-sd").innerText=data.free_sd+" / "+data.total_sd}).catch(e=>console.error("Status update failed:",e))}
        function executeScript(){var script=scriptEditor.getValue();if(!script){alert("Script content is empty!");return}fetch("/execute",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"script="+encodeURIComponent(script)})}
        function saveScript(){var name=document.getElementById("scriptName").value;var content=scriptEditor.getValue();if(!name){alert("Script name is required!");return}fetch("/save_script",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:`name=${encodeURIComponent(name)}&content=${encodeURIComponent(content)}`}).then(r=>r.ok?alert("Script saved"):Promise.reject("Save failed")).then(()=>loadScriptList()).catch(e=>alert(e))}
        function deleteScript(){var name=document.getElementById("scriptName").value;if(!name){alert("Select a script to delete!");return}fetch("/delete_script",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"name="+encodeURIComponent(name)}).then(r=>r.ok?alert("Script deleted"):Promise.reject("Delete failed")).then(()=>{loadScriptList();document.getElementById("scriptName").value="";scriptEditor.setValue("")}).catch(e=>alert(e))}
        function loadScript(){var name=document.getElementById("scriptList").value;document.getElementById("scriptName").value=name;if(name){var path="/scripts/"+name+".txt";fetch("/files?path="+encodeURIComponent(path)).then(r=>r.ok?r.text():Promise.reject("File not found")).then(content=>scriptEditor.setValue(content)).catch(e=>{alert("Could not load script: "+e);scriptEditor.setValue("")})}else{scriptEditor.setValue("")}}
        function loadScriptList(){fetch("/list_scripts").then(r=>r.json()).then(scripts=>{var list=document.getElementById("scriptList");list.innerHTML="<option value=''>Select a saved script</option>";scripts.forEach(script=>{list.innerHTML+=`<option value="${script}">${script}</option>`})})}

        window.onload = () => {
            loadScriptList();
            updateStatus();
            setInterval(updateStatus, 10000);
            setupEventSource();
            setInterval(fetchRequestLog, 3000);
        };
    </script>
</body>
</html>
)WEBUI";
    server.send(200, "text/html", html);
}



 

void handleSettingsPage() {
    logRequest(server); // Add logging
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Settings</title>
    <style>
        :root {
            --bg-color: #2c3e50; --primary-color: #34495e; --secondary-color: #2980b9;
            --font-color: #ecf0f1; --border-color: #2980b9; --success-color: #27ae60;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background-color: var(--bg-color); color: var(--font-color); line-height: 1.6; }
        .container { max-width: 800px; margin: auto; padding: 20px; }
        header { background: var(--primary-color); padding: 1rem; margin-bottom: 20px; border-radius: 8px; }
        header h1 { text-align: center; color: var(--font-color); font-size: 1.5rem; }
        header a { color: var(--font-color); text-decoration: none; float: left; transition: transform 0.2s; }
        header a:hover { transform: scale(1.1); }
        .card { background: var(--primary-color); border: 1px solid var(--border-color); border-radius: 8px; padding: 25px; box-shadow: 0 4px 8px rgba(0,0,0,0.2); }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input { width: 100%; padding: 12px; border-radius: 5px; border: 1px solid var(--border-color); background-color: var(--bg-color); color: var(--font-color); font-size: 1rem; }
        button { width: 100%; padding: 12px; border-radius: 5px; border: none; cursor: pointer; transition: background-color 0.3s; font-weight: bold; font-size: 1.1rem; background-color: var(--success-color); color: var(--font-color); margin-top: 10px; }
        button:hover { opacity: 0.9; }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1><a href="/">← Back</a> | Device Settings</h1>
        </header>
        <div class="card">
            <form id="settingsForm">
                <div class="form-group"><label for="ssid">WiFi SSID:</label><input type="text" id="ssid" name="ssid"></div>
                <div class="form-group"><label for="password">WiFi Password:</label><input type="password" id="password" name="password"></div>
                <div class="form-group"><label for="openai_api_key">API Key:</label><input type="password" id="openai_api_key" name="openai_api_key"></div>
                <div class="form-group"><label for="model">AI Model:</label><input type="text" id="model" name="model"></div>
                <div class="form-group"><label for="max_completion_tokens">Max Tokens:</label><input type="number" id="max_completion_tokens" name="max_completion_tokens"></div>
                <div class="form-group"><label for="temperature">Temperature:</label><input type="number" step="0.01" id="temperature" name="temperature"></div>
                <div class="form-group"><label for="top_p">Top P:</label><input type="number" step="0.01" id="top_p" name="top_p"></div>
                <div class="form-group"><label for="frequency_penalty">Frequency Penalty:</label><input type="number" step="0.01" id="frequency_penalty" name="frequency_penalty"></div>
                <div class="form-group"><label for="presence_penalty">Presence Penalty:</label><input type="number" step="0.01" id="presence_penalty" name="presence_penalty"></div>
                <div class="form-group"><label for="typing_delay">Typing Delay (ms):</label><input type="number" id="typing_delay" name="typing_delay"></div>
                <div class="form-group"><label for="command_delay">Duck Command Delay (ms):</label><input type="number" id="command_delay" name="command_delay"></div>
                <div class="form-group"><label for="add_more_delay">More Delay (ms):</label><input type="number" step="5" id="add_more_delay" name="add_more_delay"></div>
                <button type="button" onclick="saveSettings()">Save Settings</button>
            </form>
        </div>
    </div>
    <script>
        function loadSettings() {
            fetch('/settings').then(r => r.json()).then(data => {
                for (const key in data) {
                    if (document.getElementById(key)) document.getElementById(key).value = data[key];
                }
            });
        }
        function saveSettings() {
            const form = document.getElementById('settingsForm');
            const formData = new FormData(form);
            const data = Object.fromEntries(formData.entries());
            fetch('/settings', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(data),
            }).then(r => r.text()).then(result => alert(result));
        }
        window.onload = loadSettings;
    </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
}
 



void handleFileManagerPage() {
    logRequest(server); // Add logging
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 File Manager</title>
    <style>
        :root {
            --bg-color: #2c3e50; --primary-color: #34495e; --secondary-color: #2980b9;
            --font-color: #ecf0f1; --border-color: #2980b9; --danger-color: #c0392b;
            --success-color: #27ae60;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background-color: var(--bg-color); color: var(--font-color); }
        .container { max-width: 1000px; margin: auto; padding: 20px; }
        header { background: var(--primary-color); padding: 1rem; margin-bottom: 20px; border-radius: 8px; }
        header h1 { text-align: center; font-size: 1.5rem; }
        header a { color: var(--font-color); text-decoration: none; float: left; transition: transform 0.2s; }
        header a:hover { transform: scale(1.1); }
        .card { background: var(--primary-color); border-radius: 8px; padding: 20px; box-shadow: 0 4px 8px rgba(0,0,0,0.2); }
        #currentPath { padding: 10px; background-color: var(--bg-color); border-radius: 5px; margin-bottom: 15px; font-family: monospace; word-break: break-all; }
        table { width: 100%; border-collapse: collapse; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid var(--secondary-color); }
        th { background-color: var(--secondary-color); }
        tr:hover { background-color: #3f5870; }
        td a { color: var(--font-color); text-decoration: none; font-weight: bold; cursor: pointer; }
        td a:hover { text-decoration: underline; }
        .action-btn { color: white; border: none; padding: 8px 12px; border-radius: 5px; cursor: pointer; }
        .action-btn.danger { background-color: var(--danger-color); }
        .action-btn.success { background-color: var(--success-color); }
        .action-btn:hover { opacity: 0.8; }
        .file-icon { margin-right: 10px; }
        
        .create-item-container { display: flex; gap: 10px; margin-bottom: 20px; align-items: center; }
        .create-item-container input { flex-grow: 1; margin: 0; padding: 10px; background-color: var(--bg-color); color: var(--font-color); border: 1px solid var(--border-color); border-radius: 5px; }
        .button-group { display: flex; gap: 10px; }
        
        .modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; overflow: auto; background-color: rgba(0,0,0,0.7); }
        .modal-content { background-color: var(--primary-color); margin: 5% auto; padding: 20px; border: 1px solid var(--border-color); border-radius: 8px; width: 80%; max-width: 900px; }
        .modal-header { display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid var(--secondary-color); padding-bottom: 10px; margin-bottom: 15px; }
        .close-btn { color: #aaa; font-size: 28px; font-weight: bold; cursor: pointer; }
        .close-btn:hover, .close-btn:focus { color: white; }
        #fileContent { background-color: var(--bg-color); color: var(--font-color); white-space: pre-wrap; word-wrap: break-word; max-height: 60vh; overflow-y: auto; padding: 15px; border-radius: 5px; font-family: 'Courier New', Courier, monospace; }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1><a href="/">← Back</a> | SD Card File Manager</h1>
        </header>
        <div class="card">
            <!-- NEW UI ELEMENTS FOR CREATING ITEMS -->
            <div class="create-item-container">
                <input type="text" id="newItemName" placeholder="New file or directory name...">
                <div class="button-group">
                    <button class="action-btn success" onclick="createItem('file')">Create File</button>
                    <button class="action-btn success" onclick="createItem('dir')">Create Dir</button>
                </div>
            </div>
            
            <div id="currentPath">/</div>
            <table>
                <thead><tr><th>Name</th><th>Size</th><th>Actions</th></tr></thead>
                <tbody id="fileList"></tbody>
            </table>
        </div>
    </div>
    <div id="fileModal" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <h2 id="modalFileName"></h2>
                <span class="close-btn" onclick="closeModal()">×</span>
            </div>
            <pre id="fileContent"></pre>
        </div>
    </div>

    <script>
        let currentPath = '/';
        const modal = document.getElementById('fileModal');

        function formatBytes(bytes, d = 2) {
            if (!+bytes) return '0 Bytes';
            const k = 1024, sizes = ['Bytes', 'KB', 'MB', 'GB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return `${parseFloat((bytes/Math.pow(k,i)).toFixed(d))} ${sizes[i]}`;
        }

        function renderFileList(files) {
            const fileListBody = document.getElementById('fileList');
            let content = '';
            if (currentPath !== '/') {
                let upPath = currentPath.substring(0, currentPath.lastIndexOf('/')) || '/';
                content += `<tr><td><a onclick="fetchFiles('${upPath}')"><span class="file-icon">📁</span>..</a></td><td></td><td></td></tr>`;
            }
            files.sort((a,b) => (a.type === b.type) ? a.name.localeCompare(b.name) : (a.type === 'dir' ? -1 : 1))
                 .forEach(f => {
                    const icon = f.type === 'dir' ? '📁' : '📄';
                    const path = (currentPath === '/' ? '' : currentPath) + '/' + f.name;
                    content += `<tr>
                        <td><a onclick="${f.type === 'dir' ? `fetchFiles('${path}')` : `viewFile('${path}')`}"><span class="file-icon">${icon}</span>${f.name}</a></td>
                        <td>${f.type === 'file' ? formatBytes(f.size) : ''}</td>
                        <td><button class="action-btn danger" onclick="deleteItem('${path}')">Delete</button></td></tr>`;
                });
            fileListBody.innerHTML = content;
        }

        function fetchFiles(path) {
            currentPath = path;
            document.getElementById('currentPath').textContent = 'Current Path: ' + path;
            fetch('/files?path=' + encodeURIComponent(path))
                .then(r => r.ok ? r.json() : Promise.reject('Network error'))
                .then(data => renderFileList(data))
                .catch(e => alert('Error fetching file list: ' + e.message));
        }

        function viewFile(path) {
            fetch('/files?path=' + encodeURIComponent(path))
                .then(r => r.ok ? r.text() : Promise.reject('Could not load file'))
                .then(text => {
                    document.getElementById('modalFileName').textContent = path.substring(path.lastIndexOf('/') + 1);
                    document.getElementById('fileContent').textContent = text;
                    modal.style.display = 'block';
                })
                .catch(e => alert('Error viewing file: ' + e.message));
        }
        
        // NEW JAVASCRIPT FUNCTION
        function createItem(type) {
            const nameInput = document.getElementById('newItemName');
            const newItemName = nameInput.value.trim();
            if (!newItemName) {
                alert('Please enter a name.');
                return;
            }
            if (/[~#%&*:<>?\/\\{|}"]/.test(newItemName)) {
                alert('Name contains invalid characters.');
                return;
            }

            const path = (currentPath === '/' ? '' : currentPath) + '/' + newItemName;
            const endpoint = type === 'file' ? '/create_file' : '/create_dir';

            fetch(endpoint, {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'path=' + encodeURIComponent(path)
            })
            .then(response => response.text().then(text => {
                if (!response.ok) return Promise.reject(text);
                return text;
            }))
            .then(result => {
                alert(result);
                nameInput.value = ''; // Clear input field
                fetchFiles(currentPath); // Refresh the view
            })
            .catch(error => alert('Error: ' + error));
        }

        function deleteItem(path) {
            if (!confirm('Are you sure you want to delete ' + path + '? This cannot be undone.')) return;
            fetch('/delete_file', { method: 'POST', body: 'path=' + encodeURIComponent(path), headers: {'Content-Type': 'application/x-www-form-urlencoded'} })
                .then(r => r.text()).then(result => { alert(result); fetchFiles(currentPath); })
                .catch(e => alert('Error deleting item: ' + e.message));
        }

        function closeModal() { modal.style.display = 'none'; }
        window.onclick = (event) => { if (event.target == modal) closeModal(); }
        window.onload = () => fetchFiles('/');
    </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}







void handleDuckyPage() {
    logRequest(server);
    String html = R"WEBUI(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ducky Script Editor</title>
    <script src="https://cdn.jsdelivr.net/npm/monaco-editor@0.34.0/min/vs/loader.js"></script>
    <style>
        :root{--bg-color:#2c3e50;--primary-color:#34495e;--secondary-color:#2980b9;--font-color:#ecf0f1;--border-color:#2980b9;--success-color:#27ae60;--danger-color:#c0392b;}
        
        html, body { height: 100%; margin: 0; padding: 0; overflow: hidden; }
        body{font-family:-apple-system,sans-serif;background-color:var(--bg-color);color:var(--font-color);}

        .page-wrapper { display: flex; flex-direction: column; height: 100vh; padding: 20px; box-sizing: border-box; }
        header { flex-shrink: 0; background:var(--primary-color); padding:1rem; margin-bottom:20px; border-radius:8px; text-align:center; }
        
        .content-container { display: flex; gap: 20px; flex-grow: 1; min-height: 0; }

        .toolbar{flex:0 0 250px;background:var(--primary-color);padding:15px;border-radius:8px;overflow-y:auto;}
        .main-content{flex-grow:1;display:flex;flex-direction:column; min-width: 0;}
        
        #editor-container{width:100%;flex-grow:1;border:1px solid var(--border-color);border-radius:5px;}
        .button,button,input[type=file]::file-selector-button{background-color:var(--secondary-color);color:var(--font-color);border:none;border-radius:5px;padding:10px;margin-bottom:10px;width:100%;text-align:center;cursor:pointer;font-size:1rem;font-weight:bold;transition:opacity .2s;}
        .button:hover,button:hover{opacity:.9;}
        .button-group{display:flex;gap:10px;margin-top:10px;}
        input,select{width:100%;padding:10px;margin-bottom:10px;border-radius:5px;border:1px solid var(--border-color);background-color:var(--bg-color);color:var(--font-color);}
        h2,h3{border-bottom:2px solid var(--secondary-color);padding-bottom:10px;margin-top:0;margin-bottom:15px;}

        /* THE CSS FIX IS HERE */
        .action-bar {
            flex-shrink: 0; /* This is the critical rule: it prevents the bar from shrinking */
            padding-top: 10px;
        }

    </style>
</head>
<body>
    <div class="page-wrapper">
        <header><h1><a href="/" style="color:var(--font-color);text-decoration:none;">← Main Menu</a> | Ducky Script Studio</h1></header>
        
        <div class="content-container">
            <div class="toolbar">
                <h3>Command Palette</h3>
                <div class="button" onclick="insertCommand('REM ')">REM (Comment)</div>
                <div class="button" onclick="insertCommand('DELAY ')">DELAY</div>
                <div class="button" onclick="insertCommand('STRING ')">STRING</div>
                <div class="button" onclick="insertCommand('ENTER')">ENTER</div>
                <div class="button" onclick="insertCommand('GUI r')">GUI r (Run)</div>
                <div class="button" onclick="insertCommand('GUI ')">GUI (Windows Key)</div>
                <div class="button" onclick="insertCommand('CTRL ')">CTRL</div>
                <div class="button" onclick="insertCommand('ALT ')">ALT</div>
                <div class="button" onclick="insertCommand('SHIFT ')">SHIFT</div>
                <div class="button" onclick="insertCommand('CTRL-ALT ')">CTRL-ALT</div>
                <div class="button" onclick="insertCommand('CTRL-SHIFT ')">CTRL-SHIFT</div>
                <div class="button" onclick="insertCommand('DELETE')">DELETE</div>
                <div class="button" onclick="insertCommand('UP')">UPARROW</div>
                <div class="button" onclick="insertCommand('DOWN')">DOWNARROW</div>
            </div>
            <div class="main-content">
                <h2>Editor</h2>
                <div id="editor-container"></div>

                <!-- THE HTML FIX IS HERE: Wrapping the buttons in an action-bar div -->
                <div class="action-bar">
                    <div class="button-group">
                        <select id="scriptList" onchange="loadDuckyScript()"><option value="">Load Saved Script</option></select>
                        <input type="text" id="scriptName" placeholder="Script name to save...">
                        <button class="success" onclick="saveDuckyScript()">Save</button>
                    </div>
                     <div class="button-group">
                        <label for="importFile" class="button">Import from .txt</label>
                        <input type="file" id="importFile" onchange="importDuckyScript()" style="display:none;" accept=".txt">
                        <button class="danger" onclick="executeDucky()">Execute on Target</button>
                    </div>
                </div>

            </div>
        </div>
    </div>

    <script>
        // The JavaScript does not need to change.
        var duckyEditor;
        require.config({ paths: { 'vs': 'https://cdn.jsdelivr.net/npm/monaco-editor@0.34.0/min/vs' }});
        require(['vs/editor/editor.main'], function() {
            monaco.languages.register({ id: 'ducky' });
            monaco.languages.setMonarchTokensProvider('ducky', {
                tokenizer: { root: [ [/REM.*/,'comment'],[/\b(STRING|DELAY|ENTER|GUI|WINDOWS|CTRL|ALT|SHIFT|UP|DOWN|LEFT|RIGHT|DELETE|TAB|ESCAPE)\b/,'keyword'],[/\d+/,'number'], ] }
            });
            duckyEditor = monaco.editor.create(document.getElementById('editor-container'), {
                value: 'REM Ducky Script Editor\nDELAY 1000\nGUI r\nDELAY 500\nSTRING powershell\nENTER',
                language: 'ducky',
                theme: 'vs-dark',
                automaticLayout: true
            });
        });
        function insertCommand(command) { var position = duckyEditor.getPosition(); duckyEditor.executeEdits('my-source', [{ range: new monaco.Range(position.lineNumber, position.column, position.lineNumber, position.column), text: command + (command.endsWith(' ') ? '' : '\n') }]); duckyEditor.focus(); }
        function importDuckyScript() { const fileInput = document.getElementById('importFile'); if (fileInput.files.length === 0) return; const file = fileInput.files[0]; const reader = new FileReader(); reader.onload = (e) => duckyEditor.setValue(e.target.result); reader.readAsText(file); document.getElementById('scriptName').value = file.name.replace(/\.[^/.]+$/, ""); }
        function executeDucky() { const scriptContent = duckyEditor.getValue(); if (!scriptContent) { alert("Script is empty!"); return; } fetch('/execute_ducky', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: scriptContent }).then(r => r.text()).then(alert); }
        function saveDuckyScript() { const scriptName = document.getElementById('scriptName').value; const scriptContent = duckyEditor.getValue(); if (!scriptName) { alert("Please enter a name to save the script."); return; } fetch('/save_script', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: `path=/ducky_scripts/${scriptName}.txt&content=${encodeURIComponent(scriptContent)}` }).then(r => r.text()).then(alert).then(() => loadDuckyScriptList()); }
        function loadDuckyScript() { const scriptName = document.getElementById('scriptList').value; if (!scriptName) return; document.getElementById('scriptName').value = scriptName; fetch(`/files?path=/ducky_scripts/${scriptName}.txt`).then(r => r.ok ? r.text() : Promise.reject('File not found')).then(content => duckyEditor.setValue(content)).catch(alert); }
        function loadDuckyScriptList() { fetch('/files?path=/ducky_scripts').then(r => r.ok ? r.json() : Promise.resolve([])).then(files => { const list = document.getElementById('scriptList'); list.innerHTML = "<option value=''>Load Saved Script</option>"; files.forEach(file => { if (file.name.endsWith('.txt')) { const scriptName = file.name.replace('.txt', ''); list.innerHTML += `<option value="${scriptName}">${scriptName}</option>`; } }); }); }
        window.onload = loadDuckyScriptList;
    </script>
</body>
</html>
)WEBUI";
    server.send(200, "text/html", html);
}



 
// ============================================================================
// Path Validation (Security)
// ============================================================================
bool isValidPath(const String& path) {
    if (path.indexOf("..") != -1) return false;
    if (path.indexOf("//") != -1) return false;
    if (!path.startsWith("/")) return false;
    for (int i = 0; i < path.length(); i++) {
        if (path.charAt(i) == '\0') return false;
    }
    return true;
}

// ============================================================================
// Web Server Setup
// ============================================================================
void startWebServer() {
    // Public endpoint
    server.on("/login", HTTP_GET, handleLogin);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/logout", handleLogout);
    
    // Protected endpoints
    server.on("/", handleRoot);
    server.on("/settings_page", HTTP_GET, handleSettingsPage);
    server.on("/file_manager", HTTP_GET, handleFileManagerPage);
    server.on("/ducky", HTTP_GET, handleDuckyPage);
    
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/feedback", HTTP_POST, handleFeedback);
    server.on("/events", HTTP_GET, handleEvents);
    server.on("/sysinfo", HTTP_POST, handleSysInfo);
    server.on("/get_logs", HTTP_GET, handleGetLogs);

    server.on("/files", HTTP_GET, handleGetFileOrDir);
    server.on("/delete_file", HTTP_POST, handleDeleteFile);
    server.on("/create_file", HTTP_POST, handleCreateFile);
    server.on("/create_dir", HTTP_POST, handleCreateDir);

    server.on("/execute_ducky", HTTP_POST, handleExecuteDucky);
    server.on("/submit", HTTP_POST, handleSubmit);
    server.on("/settings", HTTP_POST, handleSettings);
    server.on("/settings", HTTP_GET, handleSettings);
    server.on("/execute", HTTP_POST, handleExecuteScript);
    server.on("/save_script", HTTP_POST, handleSaveScript);
    server.on("/delete_script", HTTP_POST, handleDeleteScript);
    server.on("/list_scripts", HTTP_GET, handleListScripts);
    server.on("/format_sd", HTTP_POST, handleFormatSD);
    server.on("/convert_powershell", HTTP_POST, handleConvertPowerShell);
    
    server.on("/upload", HTTP_POST, []() {
        if (!checkAuth()) return;
        logRequest(server);
        server.send(200, "text/plain", "File uploaded");
    }, handleFileUpload);
    
    server.onNotFound(handleNotFoundRouter);
    server.begin();
    Serial.println("HTTP server started");
}

// ============================================================================
// Settings Management
// ============================================================================
void loadSettings() {
    File file = SD.open("/settings.json");
    if (!file) {
        Serial.println("No settings.json - using defaults");
        Serial.println("IMPORTANT: Create settings.json with your WiFi and API credentials!");
        return;
    }
    
    JsonDocument doc;
    if (deserializeJson(doc, file)) {
        Serial.println("Failed to parse settings.json");
        file.close();
        return;
    }
    
    settings.ssid = doc["ssid"] | settings.ssid;
    settings.password = doc["password"] | settings.password;
    settings.openai_api_key = doc["openai_api_key"] | settings.openai_api_key;
    settings.model = doc["model"] | settings.model;
    settings.auth_username = doc["auth_username"] | settings.auth_username;
    settings.auth_password = doc["auth_password"] | settings.auth_password;
    settings.max_completion_tokens = doc["max_completion_tokens"] | settings.max_completion_tokens;
    settings.temperature = doc["temperature"] | settings.temperature;
    settings.top_p = doc["top_p"] | settings.top_p;
    settings.frequency_penalty = doc["frequency_penalty"] | settings.frequency_penalty;
    settings.presence_penalty = doc["presence_penalty"] | settings.presence_penalty;
    settings.typing_delay = doc["typing_delay"] | settings.typing_delay;
    settings.command_delay = doc["command_delay"] | settings.command_delay;
    settings.add_more_delay = doc["add_more_delay"] | settings.add_more_delay;
    settings.auth_enabled = doc["auth_enabled"] | settings.auth_enabled;
    
    file.close();
    Serial.println("Settings loaded from SD card");
}

void saveSettings() {
    File file = SD.open("/settings.json", FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open settings.json for writing");
        return;
    }
    
    JsonDocument doc;
    doc["ssid"] = settings.ssid;
    doc["password"] = settings.password;
    doc["openai_api_key"] = settings.openai_api_key;
    doc["model"] = settings.model;
    doc["auth_username"] = settings.auth_username;
    doc["auth_password"] = settings.auth_password;
    doc["max_completion_tokens"] = settings.max_completion_tokens;
    doc["typing_delay"] = settings.typing_delay;
    doc["command_delay"] = settings.command_delay;
    doc["add_more_delay"] = settings.add_more_delay;
    doc["auth_enabled"] = settings.auth_enabled;
    
    char buf[10];
    snprintf(buf, sizeof(buf), "%.2f", settings.temperature); doc["temperature"] = buf;
    snprintf(buf, sizeof(buf), "%.2f", settings.top_p); doc["top_p"] = buf;
    snprintf(buf, sizeof(buf), "%.2f", settings.frequency_penalty); doc["frequency_penalty"] = buf;
    snprintf(buf, sizeof(buf), "%.2f", settings.presence_penalty); doc["presence_penalty"] = buf;
    
    serializeJson(doc, file);
    file.close();
    Serial.println("Settings saved");
}

void handleSettings() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (server.method() == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, server.arg("plain"))) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }
        
        if (doc.containsKey("ssid")) settings.ssid = doc["ssid"].as<String>();
        if (doc.containsKey("password") && doc["password"].as<String>() != "********") 
            settings.password = doc["password"].as<String>();
        if (doc.containsKey("openai_api_key") && !doc["openai_api_key"].as<String>().startsWith("sk-..."))
            settings.openai_api_key = doc["openai_api_key"].as<String>();
        if (doc.containsKey("model")) settings.model = doc["model"].as<String>();
        if (doc.containsKey("auth_username")) settings.auth_username = doc["auth_username"].as<String>();
        if (doc.containsKey("auth_password") && doc["auth_password"].as<String>() != "********")
            settings.auth_password = doc["auth_password"].as<String>();
        if (doc.containsKey("max_completion_tokens")) settings.max_completion_tokens = doc["max_completion_tokens"];
        if (doc.containsKey("temperature")) settings.temperature = doc["temperature"];
        if (doc.containsKey("typing_delay")) settings.typing_delay = doc["typing_delay"];
        if (doc.containsKey("command_delay")) settings.command_delay = doc["command_delay"];
        
        saveSettings();
        server.send(200, "text/plain", "Settings saved");
    } else {
        JsonDocument doc;
        doc["ssid"] = settings.ssid;
        doc["password"] = settings.password.length() > 0 ? "********" : "";
        doc["openai_api_key"] = settings.openai_api_key.length() > 0 ? "sk-...configured" : "";
        doc["model"] = settings.model;
        doc["auth_username"] = settings.auth_username;
        doc["auth_password"] = settings.auth_password.length() > 0 ? "********" : "";
        doc["max_completion_tokens"] = settings.max_completion_tokens;
        doc["temperature"] = settings.temperature;
        doc["typing_delay"] = settings.typing_delay;
        doc["command_delay"] = settings.command_delay;
        
        String jsonString;
        serializeJson(doc, jsonString);
        server.send(200, "application/json", jsonString);
    }
}

// ============================================================================
// Logging Functions
// ============================================================================
void logRequest(WebServer& svr) {
    if (requestLogBuffer.length() > 2048) {
        requestLogBuffer = requestLogBuffer.substring(requestLogBuffer.length() - 1024);
    }
    
    String clientIP = svr.client().remoteIP().toString();
    String method = (svr.method() == HTTP_GET) ? "GET" : (svr.method() == HTTP_POST) ? "POST" : "OTHER";
    String log = "[" + clientIP + "] " + method + " " + svr.uri() + "\n";
    requestLogBuffer += log;
    Serial.print(log);
}

void handleGetLogs() {
    if (!checkAuth()) return;
    
    if (requestLogBuffer.length() > 0) {
        String temp = requestLogBuffer;
        requestLogBuffer = "";
        server.send(200, "text/plain", temp);
    } else {
        server.send(200, "text/plain", "");
    }
}

void pushSseEvent(const String& eventName, const String& data) {
    if (feedbackClient && feedbackClient.connected()) {
        feedbackClient.print("event: " + eventName + "\n");
        feedbackClient.print("data: " + data + "\n\n");
        feedbackClient.flush();
    }
}

// ============================================================================
// Status and Events Handlers
// ============================================================================
void handleStatus() {
    if (!checkAuth()) return;
    logRequest(server);
    
    JsonDocument doc;
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["free_sd"] = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
    doc["total_sd"] = SD.totalBytes() / (1024 * 1024);
    doc["heap"] = ESP.getFreeHeap();
    
    String jsonString;
    serializeJson(doc, jsonString);
    server.send(200, "application/json", jsonString);
}

void handleFeedback() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (server.hasArg("plain")) {
        pushSseEvent("message", server.arg("plain"));
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}

void handleEvents() {
    if (!checkAuth()) return;
    
    feedbackClient = server.client();
    feedbackClient.setNoDelay(true);
    server.sendHeader("Content-Type", "text/event-stream");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200);

    while (feedbackClient.connected()) {
        if (feedbackMessage != "") {
            pushSseEvent("message", feedbackMessage);
            feedbackMessage = "";
        }
        delay(100);
    }
}

void handleSysInfo() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (server.hasArg("plain")) {
        pushSseEvent("sysinfo", server.arg("plain"));
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}

// ============================================================================
// File Operation Handlers
// ============================================================================
void handleCreateFile() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "Missing path");
        return;
    }
    
    String path = server.arg("path");
    if (!isValidPath(path)) {
        server.send(400, "text/plain", "Invalid path");
        return;
    }
    
    if (SD.exists(path)) {
        server.send(409, "text/plain", "Already exists");
        return;
    }
    
    File file = SD.open(path, FILE_WRITE);
    if (file) {
        file.close();
        server.send(201, "text/plain", "Created");
    } else {
        server.send(500, "text/plain", "Create failed");
    }
}

void handleCreateDir() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "Missing path");
        return;
    }
    
    String path = server.arg("path");
    if (!isValidPath(path)) {
        server.send(400, "text/plain", "Invalid path");
        return;
    }
    
    if (SD.exists(path)) {
        server.send(409, "text/plain", "Already exists");
        return;
    }
    
    if (SD.mkdir(path)) {
        server.send(201, "text/plain", "Created");
    } else {
        server.send(500, "text/plain", "Create failed");
    }
}

void handleGetFileOrDir() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "Missing path");
        return;
    }
    
    String path = server.arg("path");
    if (!path.startsWith("/")) path = "/" + path;
    if (!isValidPath(path)) {
        server.send(400, "text/plain", "Invalid path");
        return;
    }
    
    File file = SD.open(path);
    if (!file) {
        server.send(404, "text/plain", "Not found");
        return;
    }
    
    if (file.isDirectory()) {
        String json = "[";
        File entry = file.openNextFile();
        while (entry) {
            if (json != "[") json += ",";
            String name = String(entry.name());
            json += "{\"type\":\"" + String(entry.isDirectory() ? "dir" : "file") + "\",";
            json += "\"name\":\"" + name.substring(name.lastIndexOf('/') + 1) + "\",";
            json += "\"size\":" + String(entry.size()) + "}";
            entry.close();
            entry = file.openNextFile();
        }
        json += "]";
        file.close();
        server.send(200, "application/json", json);
    } else {
        server.streamFile(file, "text/plain");
        file.close();
    }
}

void handleDeleteFile() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "Missing path");
        return;
    }
    
    String path = server.arg("path");
    if (!isValidPath(path) || path == "/") {
        server.send(403, "text/plain", "Forbidden");
        return;
    }
    
    if (SD.remove(path) || SD.rmdir(path)) {
        server.send(200, "text/plain", "Deleted");
    } else {
        server.send(500, "text/plain", "Delete failed");
    }
}

void handleFileServer() {
    String uri = server.uri();
    if (uri.startsWith("/f/") && isValidPath(uri)) {
        String sdPath = "/payloads" + uri.substring(2);
        logRequest(server);
        if (SD.exists(sdPath)) {
            File file = SD.open(sdPath, FILE_READ);
            server.streamFile(file, "application/octet-stream");
            file.close();
        } else {
            server.send(404, "text/plain", "Not found");
        }
    } else {
        server.send(403, "text/plain", "Forbidden");
    }
}

void handleNotFoundRouter() {
    if (server.uri().startsWith("/f/")) {
        handleFileServer();
    } else {
        logRequest(server);
        server.send(404, "text/plain", "404 Not Found");
    }
}

void handleFileUpload() {
    if (!checkAuth()) return;
    
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        String filename = "/" + upload.filename;
        if (isValidPath(filename)) {
            uploadFile = SD.open(filename, FILE_WRITE);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) uploadFile.close();
    }
}

void handleFormatSD() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (!server.hasArg("confirm") || server.arg("confirm") != "yes") {
        server.send(400, "text/plain", "Add ?confirm=yes to proceed");
        return;
    }
    
    File root = SD.open("/");
    while (File entry = root.openNextFile()) {
        String name = String(entry.name());
        entry.close();
        if (name != "/settings.json") SD.remove(name);
    }
    root.close();
    server.send(200, "text/plain", "Formatted (settings preserved)");
}

// ============================================================================
// Script Handlers
// ============================================================================
void handleSaveScript() {
    if (!checkAuth()) return;
    logRequest(server);
    
    String path;
    if (server.hasArg("path")) {
        path = server.arg("path");
    } else if (server.hasArg("name")) {
        path = "/scripts/" + server.arg("name") + ".txt";
    } else {
        server.send(400, "text/plain", "Missing path/name");
        return;
    }
    
    if (!isValidPath(path)) {
        server.send(400, "text/plain", "Invalid path");
        return;
    }
    
    if (server.hasArg("content")) {
        File file = SD.open(path, FILE_WRITE);
        if (file) {
            file.print(server.arg("content"));
            file.close();
            server.send(200, "text/plain", "Saved");
        } else {
            server.send(500, "text/plain", "Save failed");
        }
    } else {
        server.send(400, "text/plain", "Missing content");
    }
}

void handleDeleteScript() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (server.hasArg("name")) {
        if (SD.remove("/scripts/" + server.arg("name") + ".txt")) {
            server.send(200, "text/plain", "Deleted");
        } else {
            server.send(500, "text/plain", "Delete failed");
        }
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}

void handleListScripts() {
    if (!checkAuth()) return;
    logRequest(server);
    server.send(200, "application/json", getScriptList());
}

String getScriptList() {
    File root = SD.open("/scripts");
    if (!root) return "[]";
    
    String list = "[";
    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory() && String(entry.name()).endsWith(".txt")) {
            if (list != "[") list += ",";
            String name = String(entry.name());
            name.replace(".txt", "");
            list += "\"" + name + "\"";
        }
        entry.close();
        entry = root.openNextFile();
    }
    list += "]";
    root.close();
    return list;
}

void handleExecuteScript() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (server.hasArg("script")) {
        executeScriptViaCmd(server.arg("script"));
        server.send(200, "text/plain", "Execution started");
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}

// ============================================================================
// AI Integration
// ============================================================================
String createJsonPayload(const String& userContent, const String& systemPrompt) {
    JsonDocument doc;
    doc["model"] = settings.model;
    JsonArray messages = doc["messages"].to<JsonArray>();
    
    JsonObject sysMsg = messages.add<JsonObject>();
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    
    JsonObject userMsg = messages.add<JsonObject>();
    userMsg["role"] = "user";
    userMsg["content"] = userContent;
    
    doc["max_tokens"] = settings.max_completion_tokens;
    doc["temperature"] = settings.temperature;
    
    String output;
    serializeJson(doc, output);
    return output;
}

String sendToOpenAI(const String& query, const String& systemPrompt) {
    if (settings.openai_api_key.length() == 0) {
        return "Error: No API key configured. Set it in Settings.";
    }
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20000);
    
    HTTPClient http;
    http.begin(client, "https://api.openai.com/v1/chat/completions");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + settings.openai_api_key);
    http.setTimeout(20000);
    
    int code = http.POST(createJsonPayload(query, systemPrompt));
    String response = http.getString();
    http.end();
    
    return (code > 0) ? extractContentField(response) : "API Error: " + String(code);
}

String extractContentField(const String& response) {
    JsonDocument doc;
    if (deserializeJson(doc, response)) return "Parse error";
    const char* content = doc["choices"][0]["message"]["content"];
    return content ? String(content) : "No content";
}

void handleSubmit() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (!server.hasArg("query")) {
        server.send(400, "text/plain", "No query");
        return;
    }
    
    String systemPrompt = 
        "You are an expert script generator. Respond ONLY with these XML tags:\n"
        "1. <explanation>Brief description</explanation>\n"
        "2. <script_type>powershell or ducky</script_type>\n"
        "3. <script_name>filename.ext</script_name>\n"
        "4. <script_code>The script code</script_code>";
    
    String response = sendToOpenAI(server.arg("query"), systemPrompt);
    server.send(200, "text/plain", parseAndExecuteAiResponse(response));
}

String extractTagContent(const String& source, const String& tag) {
    String start = "<" + tag + ">";
    String end = "</" + tag + ">";
    int s = source.indexOf(start);
    if (s == -1) return "";
    s += start.length();
    int e = source.indexOf(end, s);
    return (e == -1) ? "" : source.substring(s, e);
}

String parseAndExecuteAiResponse(const String& response) {
    String explanation = extractTagContent(response, "explanation");
    String type = extractTagContent(response, "script_type");
    String code = extractTagContent(response, "script_code");
    
    if (type.length() == 0 || code.length() == 0) {
        return explanation.length() > 0 ? explanation : response;
    }
    
    code.trim();
    
    if (type == "powershell") {
        executeScriptViaCmd(code);
    } else if (type == "ducky") {
        executeDuckyScript(code);
    } else {
        return "Unknown script type: " + type;
    }
    
    return "Executing: " + explanation;
}

// ============================================================================
// Keyboard Functions
// ============================================================================
void sendKeyboardText(const String& text) {
    for (size_t i = 0; i < text.length(); i++) {
        Keyboard.write(text.charAt(i));
        delay(settings.typing_delay);
    }
}

String convertPowerShellToSingleCommand(const String& script) {
    String result = "\"";
    String line = "";
    
    for (int i = 0; i < script.length(); i++) {
        char c = script.charAt(i);
        if (c == '\n' || c == '\r') {
            line.trim();
            if (line.length() > 0 && !line.startsWith("#")) {
                if (result.length() > 1) result += "; ";
                result += line;
            }
            line = "";
        } else {
            line += c;
        }
    }
    
    line.trim();
    if (line.length() > 0 && !line.startsWith("#")) {
        if (result.length() > 1) result += "; ";
        result += line;
    }
    
    return result + "\"";
}

void executeScriptViaCmd(const String& content) {
    Keyboard.begin();
    delay(500);
    
    // Win+R
    Keyboard.press(KEY_LEFT_GUI);
    Keyboard.press('r');
    delay(200);
    Keyboard.releaseAll();
    delay(500);
    
    // Open CMD
    Keyboard.print("cmd");
    delay(200);
    Keyboard.press(KEY_RETURN);
    Keyboard.releaseAll();
    delay(750);
    
    // Create batch file
    String fileName = "exec_" + String(millis()) + ".bat";
    String path = "%USERPROFILE%\\Desktop\\" + fileName;
    
    sendKeyboardText("echo @echo off > " + path);
    Keyboard.press(KEY_RETURN);
    Keyboard.releaseAll();
    delay(300);
    
    String cmd = "echo powershell -Command " + convertPowerShellToSingleCommand(content) + " >> " + path;
    sendKeyboardText(cmd);
    Keyboard.press(KEY_RETURN);
    Keyboard.releaseAll();
    delay(300);
    
    sendKeyboardText("exit");
    Keyboard.press(KEY_RETURN);
    Keyboard.releaseAll();
    delay(500);
    
    // Execute
    Keyboard.press(KEY_LEFT_GUI);
    Keyboard.press('r');
    delay(200);
    Keyboard.releaseAll();
    delay(500);
    
    sendKeyboardText(path);
    Keyboard.press(KEY_RETURN);
    Keyboard.releaseAll();
    
    Keyboard.end();
}

void handleConvertPowerShell() {
    if (!checkAuth()) return;
    if (server.hasArg("script")) {
        server.send(200, "text/plain", convertPowerShellToSingleCommand(server.arg("script")));
    } else {
        server.send(400, "text/plain", "No script");
    }
}

// ============================================================================
// Ducky Script Execution
// ============================================================================
void executeDuckyScript(const String& script) {
    Keyboard.begin();
    delay(500);
    
    int idx = 0;
    int defaultDelay = 0;
    
    while (idx < script.length()) {
        int next = script.indexOf('\n', idx);
        if (next == -1) next = script.length();
        
        String line = script.substring(idx, next);
        line.trim();
        idx = next + 1;
        
        if (line.length() == 0) continue;
        if (defaultDelay > 0) delay(defaultDelay);
        
        if (line.startsWith("REM")) continue;
        else if (line.startsWith("DEFAULT_DELAY ")) defaultDelay = line.substring(14).toInt();
        else if (line.startsWith("DEFAULTDELAY ")) defaultDelay = line.substring(13).toInt();
        else if (line.startsWith("STRING ")) sendKeyboardText(line.substring(7));
        else if (line.startsWith("DELAY ")) delay(line.substring(6).toInt());
        else if (line == "ENTER") { Keyboard.press(KEY_RETURN); delay(50); Keyboard.releaseAll(); }
        else if (line == "TAB") { Keyboard.press(KEY_TAB); delay(50); Keyboard.releaseAll(); }
        else if (line == "ESCAPE" || line == "ESC") { Keyboard.press(KEY_ESC); delay(50); Keyboard.releaseAll(); }
        else if (line == "BACKSPACE") { Keyboard.press(KEY_BACKSPACE); delay(50); Keyboard.releaseAll(); }
        else if (line == "DELETE" || line == "DEL") { Keyboard.press(KEY_DELETE); delay(50); Keyboard.releaseAll(); }
        else if (line == "SPACE") { Keyboard.press(' '); delay(50); Keyboard.releaseAll(); }
        else if (line == "UP" || line == "UPARROW") { Keyboard.press(KEY_UP_ARROW); delay(50); Keyboard.releaseAll(); }
        else if (line == "DOWN" || line == "DOWNARROW") { Keyboard.press(KEY_DOWN_ARROW); delay(50); Keyboard.releaseAll(); }
        else if (line == "LEFT" || line == "LEFTARROW") { Keyboard.press(KEY_LEFT_ARROW); delay(50); Keyboard.releaseAll(); }
        else if (line == "RIGHT" || line == "RIGHTARROW") { Keyboard.press(KEY_RIGHT_ARROW); delay(50); Keyboard.releaseAll(); }
        else if (line == "CAPSLOCK") { Keyboard.press(KEY_CAPS_LOCK); delay(50); Keyboard.releaseAll(); }
        else if (line == "F1") { Keyboard.press(KEY_F1); delay(50); Keyboard.releaseAll(); }
        else if (line == "F2") { Keyboard.press(KEY_F2); delay(50); Keyboard.releaseAll(); }
        else if (line == "F3") { Keyboard.press(KEY_F3); delay(50); Keyboard.releaseAll(); }
        else if (line == "F4") { Keyboard.press(KEY_F4); delay(50); Keyboard.releaseAll(); }
        else if (line == "F5") { Keyboard.press(KEY_F5); delay(50); Keyboard.releaseAll(); }
        else if (line == "F6") { Keyboard.press(KEY_F6); delay(50); Keyboard.releaseAll(); }
        else if (line == "F7") { Keyboard.press(KEY_F7); delay(50); Keyboard.releaseAll(); }
        else if (line == "F8") { Keyboard.press(KEY_F8); delay(50); Keyboard.releaseAll(); }
        else if (line == "F9") { Keyboard.press(KEY_F9); delay(50); Keyboard.releaseAll(); }
        else if (line == "F10") { Keyboard.press(KEY_F10); delay(50); Keyboard.releaseAll(); }
        else if (line == "F11") { Keyboard.press(KEY_F11); delay(50); Keyboard.releaseAll(); }
        else if (line == "F12") { Keyboard.press(KEY_F12); delay(50); Keyboard.releaseAll(); }
        else if (line == "GUI" || line == "WINDOWS") { Keyboard.press(KEY_LEFT_GUI); delay(50); Keyboard.releaseAll(); }
        else if (line.startsWith("GUI ") || line.startsWith("WINDOWS ")) {
            Keyboard.press(KEY_LEFT_GUI);
            Keyboard.press(line.charAt(line.indexOf(' ') + 1));
            delay(50);
            Keyboard.releaseAll();
        }
        else if (line.startsWith("CTRL ")) {
            Keyboard.press(KEY_LEFT_CTRL);
            Keyboard.press(line.charAt(line.indexOf(' ') + 1));
            delay(50);
            Keyboard.releaseAll();
        }
        else if (line.startsWith("ALT ")) {
            Keyboard.press(KEY_LEFT_ALT);
            Keyboard.press(line.charAt(line.indexOf(' ') + 1));
            delay(50);
            Keyboard.releaseAll();
        }
        else if (line.startsWith("SHIFT ")) {
            Keyboard.press(KEY_LEFT_SHIFT);
            Keyboard.press(line.charAt(line.indexOf(' ') + 1));
            delay(50);
            Keyboard.releaseAll();
        }
        else if (line.startsWith("CTRL-ALT ")) {
            Keyboard.press(KEY_LEFT_CTRL);
            Keyboard.press(KEY_LEFT_ALT);
            Keyboard.press(line.charAt(line.indexOf(' ') + 1));
            delay(50);
            Keyboard.releaseAll();
        }
        else if (line.startsWith("CTRL-SHIFT ")) {
            Keyboard.press(KEY_LEFT_CTRL);
            Keyboard.press(KEY_LEFT_SHIFT);
            Keyboard.press(line.charAt(line.indexOf(' ') + 1));
            delay(50);
            Keyboard.releaseAll();
        }
    }
    
    Keyboard.end();
}

void handleExecuteDucky() {
    if (!checkAuth()) return;
    logRequest(server);
    
    if (server.hasArg("plain")) {
        executeDuckyScript(server.arg("plain"));
        server.send(200, "text/plain", "Ducky script executed");
    } else {
        server.send(400, "text/plain", "No script content");
    }
}

void displayOnLCD(const String& message) {
    Paint_Clear(BLACK);
    Paint_DrawString_EN(0, 0, message.c_str(), &Font20, BLACK, BLUE);
}

#endif
