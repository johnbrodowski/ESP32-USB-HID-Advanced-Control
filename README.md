<img width="905" height="627" alt="image" src="https://github.com/user-attachments/assets/9e503acd-031d-4f37-86e6-19d1264eda6a" />

![root](https://github.com/user-attachments/assets/10118534-a6a5-4a7e-a1b0-ee548673e9cc)

![settings](https://github.com/user-attachments/assets/9e988a10-c6ec-4163-9c33-e259335031a5)

![duck](https://github.com/user-attachments/assets/db9758d4-656b-44e6-911a-0cee96083963)

![file](https://github.com/user-attachments/assets/76e86718-f617-4899-b7d4-4737b6a089d2)

<img width="1345" height="627" alt="image" src="https://github.com/user-attachments/assets/6b1ed904-5088-47b3-97e3-5e87084fdad5" />

<img width="1351" height="631" alt="image" src="https://github.com/user-attachments/assets/1e12a1ea-a0f3-4674-8f66-eec5496cf722" />

<img width="1347" height="628" alt="image" src="https://github.com/user-attachments/assets/8a35ca8f-939f-406a-98f0-b9e83549791a" />

<img width="742" height="1301" alt="image" src="https://github.com/user-attachments/assets/8d229ba2-5b5a-4888-93f4-0d9447314fa4" />


# ESP32 Advanced Control

A powerful ESP32-based HID (Human Interface Device) controller with web interface, AI integration, Ducky Script support, and advanced networking features including a secure text-only browser and Pi-Hole style DNS blocking.

## Important Security Notice

This tool is designed for **authorized security testing and educational purposes only**.

- Always obtain proper authorization before using this device
- Never use this tool for malicious purposes
- The authors are not responsible for misuse of this software

## Features

### Core Features
- **Web-based Control Panel** - Modern, responsive UI accessible from any browser
- **AI Script Generation** - Generate PowerShell and Ducky scripts using OpenAI
- **Ducky Script Support** - Full USB Rubber Ducky script compatibility
- **File Manager** - Browse, upload, edit, and manage files on the SD card
- **HID Keyboard Emulation** - Execute scripts via USB keyboard emulation
- **HTTP Authentication** - Secure access to the web interface
- **Live Status Monitoring** - Real-time device and target system information
- **Typing Benchmark** - Test and calibrate keyboard typing speed

### Advanced Networking Features
- **Portal IDE** (`/ide/portal`) - Web-based HTML editor for creating and managing captive portal pages
- **Secure Browser** (`/browser`) - Text-only web browser with configurable HTML sanitization
- **DNS Server** - Pi-Hole style DNS blocking using memory-efficient Bloom filter
- **Blocklist Editor** (`/blocklist`) - Manage DNS blocklists with real-time reload capability

## Hardware Requirements

- ESP32-S3 with native USB support (e.g., ESP32-S3-GEEK)
- SD Card module (SPI interface)
- LCD Display (optional, for status display)
- Micro USB cable

### Pin Configuration

| Function | GPIO Pin |
|----------|----------|
| SPI SCK  | 36       |
| SPI MISO | 37       |
| SPI MOSI | 35       |
| SPI SS   | 34       |

## Installation

### 1. Install Arduino IDE and ESP32 Board Support

1. Install [Arduino IDE](https://www.arduino.cc/en/software) (2.0+ recommended)
2. Add ESP32 board support:
   - Go to File → Preferences
   - Add to "Additional Board URLs": `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Go to Tools → Board → Boards Manager
   - Search for "esp32" and install "esp32 by Espressif Systems"

### 2. Install Required Libraries

Using Arduino Library Manager (Sketch → Include Library → Manage Libraries):

- ArduinoJson (by Benoit Blanchon)
- (LCD and GUI libraries as required by your display)

### 3. Configure the Device

**IMPORTANT:** Do not hardcode credentials in the source code!

Create a `settings.json` file on your SD card:

```json
{
    "ssid": "YourWiFiNetwork",
    "password": "YourWiFiPassword",
    "openai_api_key": "sk-your-openai-api-key",
    "auth_username": "admin",
    "auth_password": "your-secure-password",
    "model": "gpt-4o",
    "auth_enabled": true
}
```

### 4. Upload the Firmware

1. Select your board: Tools → Board → ESP32S3 Dev Module
2. Enable USB CDC: Tools → USB CDC On Boot → Enabled
3. Select USB Mode: Tools → USB Mode → USB-OTG (TinyUSB)
4. Connect and upload

## SD Card Directory Structure

The following directories are automatically created on first boot:

```
/
├── settings.json          # WiFi and API configuration
├── scripts/               # PowerShell scripts
├── ducky_scripts/         # Ducky Script files
├── payloads/              # Payload files
├── portals/               # Captive portal HTML files
├── blocklists/            # DNS blocklist files (one domain per line)
├── config/                # System configuration
│   └── system.json        # Network and browser settings
└── www/                   # Static web files
```

## Usage

### First Boot

1. Insert SD card with `settings.json`
2. Power on the ESP32
3. The LCD will display the assigned IP address
4. Open a web browser and navigate to that IP

### Web Interface Pages

| Page | URL | Description |
|------|-----|-------------|
| Dashboard | `/` | Main control panel with AI chat and script editor |
| Settings | `/settings_page` | Configure WiFi, API keys, keyboard delays |
| Ducky Studio | `/ducky` | Create and execute Ducky scripts |
| File Manager | `/file_manager` | Browse and manage SD card contents |
| Portal IDE | `/ide/portal` | Edit captive portal HTML files |
| Secure Browser | `/browser` | Text-only web browser with sanitization |
| Browser Settings | `/browser/settings` | Configure browser sanitization options |
| Blocklist Editor | `/blocklist` | Manage DNS blocklists |
| Typing Benchmark | `/typing_benchmark` | Test keyboard typing speed |

### Secure Browser

The secure browser (`/browser`) provides a text-only browsing experience with configurable content sanitization. Content is fetched through the ESP32 and sanitized before display.

#### Sanitization Options

Configure these in Browser Settings (`/browser/settings`):

| Option | Default | Description |
|--------|---------|-------------|
| Strip Scripts | ON | Remove `<script>` tags (security) |
| Strip Event Handlers | ON | Remove onclick, onload, etc. (security) |
| Strip Iframes | ON | Remove `<iframe>` tags (security) |
| Strip Objects/Embeds | ON | Remove `<object>`, `<embed>` tags |
| Strip External Resources | OFF | Remove external CSS/JS links |
| Strip Styles | OFF | Remove `<style>` tags |
| Strip Images | OFF | Replace images with placeholders |
| Strip Forms | OFF | Remove `<form>` elements |

### DNS Blocking (Pi-Hole Style)

The built-in DNS server uses a Bloom filter for memory-efficient domain blocking.

1. Create blocklist files in `/blocklists/` (one domain per line)
2. Enable DNS server in system config
3. Point client DNS to ESP32's IP address
4. Blocked domains return 0.0.0.0

### Portal IDE

Create custom captive portal pages:

1. Navigate to `/ide/portal`
2. Create or edit HTML files
3. Set an active portal for captive portal mode
4. Files are stored in `/portals/`

### Ducky Script Commands

Supported commands:

| Command | Description |
|---------|-------------|
| `REM` | Comment |
| `STRING text` | Type text |
| `DELAY ms` | Wait in milliseconds |
| `ENTER` | Press Enter |
| `GUI` / `WINDOWS` | Windows key |
| `GUI r` | Windows + R |
| `CTRL key` | Control + key |
| `ALT key` | Alt + key |
| `SHIFT key` | Shift + key |
| `TAB` | Tab key |
| `ESCAPE` | Escape key |
| `UP/DOWN/LEFT/RIGHT` | Arrow keys |
| `F1-F12` | Function keys |
| `DEFAULT_DELAY ms` | Set delay between commands |

#### Example Ducky Script

```
REM Open Notepad
DELAY 1000
GUI r
DELAY 500
STRING notepad
ENTER
DELAY 1000
STRING Hello from ESP32!
```

## API Endpoints

### Core Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Main dashboard |
| `/login` | GET/POST | Authentication |
| `/logout` | GET | Log out |
| `/status` | GET | Device status JSON |
| `/settings` | GET/POST | Configuration |
| `/sysinfo` | POST | System information |
| `/get_logs` | GET | Request logs |

### File Management

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/files?path=` | GET | List/read files |
| `/delete_file` | POST | Delete a file |
| `/create_file` | POST | Create a file |
| `/create_dir` | POST | Create a directory |
| `/upload` | POST | Upload a file |
| `/format_sd` | POST | Format SD card |

### Script Execution

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/execute` | POST | Execute PowerShell script |
| `/execute_ducky` | POST | Execute Ducky script |
| `/save_script` | POST | Save script to SD |
| `/delete_script` | POST | Delete a script |
| `/list_scripts` | GET | List saved scripts |
| `/convert_powershell` | POST | Convert PowerShell to Ducky |

### Portal IDE

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/ide/portal` | GET | Portal editor page |
| `/api/portals/list` | GET | List portal files |
| `/api/portals/load` | GET | Load portal content |
| `/api/portals/save` | POST | Save portal file |
| `/api/portals/delete` | POST | Delete portal file |
| `/api/portals/set_active` | POST | Set active portal |

### Secure Browser

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/browser` | GET | Browser page |
| `/browser/settings` | GET/POST | Browser settings |
| `/proxy/fetch?url=` | GET/POST | Fetch and sanitize URL |

### Blocklist & DNS

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/blocklist` | GET | Blocklist editor page |
| `/api/blocklist/list` | GET | List blocklist files |
| `/api/blocklist/load` | GET | Load blocklist content |
| `/api/blocklist/save` | POST | Save blocklist file |
| `/api/blocklist/delete` | POST | Delete blocklist file |
| `/api/blocklist/reload` | POST | Reload Bloom filter |
| `/api/dns/stats` | GET | DNS server statistics |

### System Configuration

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/system/config` | GET | Get system config |
| `/api/system/config` | POST | Update system config |

## System Configuration

The system configuration (`/config/system.json`) controls network and browser behavior:

```json
{
    "rndisEnabled": false,
    "adblockEnabled": true,
    "dnsServerEnabled": true,
    "activePortal": "/portals/default.html",
    "dnsUpstream": "1.1.1.1",
    "browserTimeout": 15000,
    "browserMaxSize": 250000,
    "browserUserAgent": "Mozilla/5.0...",
    "stripScripts": true,
    "stripStyles": false,
    "stripIframes": true,
    "stripObjects": true,
    "stripImages": false,
    "stripForms": false,
    "stripEventHandlers": true,
    "stripExternalResources": false
}
```

## Security Considerations

1. **Change default credentials** - The default auth is `admin`/`changeme`
2. **Use on isolated networks** - Don't expose to the internet
3. **Keep API keys secure** - Store in `settings.json`, not in code
4. **Regular updates** - Check for security patches
5. **Browser sanitization** - Keep script stripping enabled for security

## Troubleshooting

### WiFi Connection Fails
- Verify SSID/password in `settings.json`
- Check WiFi signal strength
- Ensure 2.4GHz network (ESP32 doesn't support 5GHz)

### SD Card Not Detected
- Verify wiring to correct GPIO pins
- Try reformatting as FAT32
- Check SD card capacity (≤32GB recommended)

### Keyboard Not Working
- Ensure USB Mode is set to USB-OTG
- Try different USB cable
- Check USB CDC is enabled

### Secure Browser Issues
- If pages don't load, try increasing `browserTimeout`
- If pages are truncated, increase `browserMaxSize`
- If pages look broken, try disabling `stripStyles`

### DNS Server Not Blocking
- Ensure `dnsServerEnabled` is true
- Check blocklist files exist in `/blocklists/`
- Reload the Bloom filter after adding domains

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## Disclaimer

This software is provided for educational and authorized security testing purposes only. Users are responsible for ensuring they have proper authorization before using this tool. The developers assume no liability for misuse or damage caused by this software.
