![ESP32-S3-GEEK-details-3](https://github.com/user-attachments/assets/9e503acd-031d-4f37-86e6-19d1264eda6a)

![root](https://github.com/user-attachments/assets/10118534-a6a5-4a7e-a1b0-ee548673e9cc)
 
![settings](https://github.com/user-attachments/assets/9e988a10-c6ec-4163-9c33-e259335031a5)
 
![duck](https://github.com/user-attachments/assets/db9758d4-656b-44e6-911a-0cee96083963)

![file](https://github.com/user-attachments/assets/76e86718-f617-4899-b7d4-4737b6a089d2)

 
# ESP32 Advanced Control

A powerful ESP32-based HID (Human Interface Device) controller with web interface, AI integration, and Ducky Script support.

## ⚠️ Important Security Notice

This tool is designed for **authorized security testing and educational purposes only**. 

- Always obtain proper authorization before using this device
- Never use this tool for malicious purposes
- The authors are not responsible for misuse of this software

## Features

- 🌐 **Web-based Control Panel** - Modern, responsive UI accessible from any browser
- 🤖 **AI Script Generation** - Generate PowerShell and Ducky scripts using OpenAI
- 🦆 **Ducky Script Support** - Full USB Rubber Ducky script compatibility
- 📁 **File Manager** - Browse and manage scripts on the SD card
- ⌨️ **HID Keyboard Emulation** - Execute scripts via USB keyboard emulation
- 🔐 **HTTP Authentication** - Secure access to the web interface
- 📊 **Live Status Monitoring** - Real-time device and target system information

## Hardware Requirements

- ESP32-S3 with native USB support
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

## Usage

### First Boot

1. Insert SD card with `settings.json`
2. Power on the ESP32
3. The LCD will display the assigned IP address
4. Open a web browser and navigate to that IP

### Web Interface

- **Main Dashboard** - System info, AI chat, script editor
- **Settings** - Configure WiFi, API keys, keyboard delays
- **Ducky Studio** - Create and execute Ducky scripts
- **File Manager** - Browse SD card contents

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

### Example Ducky Script

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

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Main dashboard |
| `/login` | GET/POST | Authentication |
| `/status` | GET | Device status JSON |
| `/settings` | GET/POST | Configuration |
| `/files?path=` | GET | List/read files |
| `/execute` | POST | Execute PowerShell |
| `/execute_ducky` | POST | Execute Ducky script |
| `/save_script` | POST | Save script to SD |
| `/list_scripts` | GET | List saved scripts |

## Security Considerations

1. **Change default credentials** - The default auth is `admin`/`changeme`
2. **Use on isolated networks** - Don't expose to the internet
3. **Keep API keys secure** - Store in `settings.json`, not in code
4. **Regular updates** - Check for security patches

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

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## Disclaimer

This software is provided for educational and authorized security testing purposes only. Users are responsible for ensuring they have proper authorization before using this tool. The developers assume no liability for misuse or damage caused by this software.
