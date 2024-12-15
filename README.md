# ESP8266 PagerDuty Alerter
This project provides a physical PagerDuty alerter using devices like a siren, buzzer, or light. It is built on the ESP8266 NodeMCU board and can be configured via a web interface.

<img src="https://github.com/user-attachments/assets/0077dd81-21b2-43b2-95af-ec3f0c575ee8" width="400">

## Features
- Polls the PagerDuty API every 60 seconds (configurable) to check for open incidents.
- Supports filtering incidents by a specific User ID.
- Includes an optional hardware button to mute the siren for 5 minutes.
- Automatically starts an access point for 5 minutes if no WiFi connection is available.

## Hardware Requirements
- **NodeMCU v2 board** (with CP2102 chipset): [Buy on AliExpress](https://www.aliexpress.com/item/1005006246641996.html)
- **Siren**: [Buy on AliExpress](https://www.aliexpress.com/item/1005007384223586.html)
- **5V Relay**: [Buy on AliExpress](https://www.aliexpress.com/item/1005007598074881.html)

## Setup Environment
1. Download and install the latest version of the Arduino IDE: [Download here](https://www.arduino.cc/en/software)
2. In the Arduino IDE:
   - Go to **File > Preferences** and add the following URL under "Additional Boards Manager URLs" (separate multiple URLs with a comma if needed):  
     ```
     http://arduino.esp8266.com/stable/package_esp8266com_index.json
     ```
   - Navigate to **Tools > Board > Board Manager** and install the package: **"esp8266 by ESP8266 Community"**.

## How to Run
1. Plug your NodeMCU board into your PC.
2. Open `ESP8266_PagerDuty_Alerter.ino` from this repository.
3. In the Arduino IDE:
   - Go to **Tools > Board > esp8266** and select **"NodeMCU 1.0 (ESP-12E Module)"**.
   - Go to **Tools > Port** and select the correct COM port (e.g., COMX).
4. Click **Upload** to flash the code.

## Usage
When the board boots, it will automatically create an access point named: **"PagerDuty_Alerter"**.
- Connect to this access point.
- Open a browser and go to: `http://192.168.4.1`.
- Configure your WiFi credentials and PagerDuty access token.

<img src="https://github.com/user-attachments/assets/aaff4a03-6967-4704-ad7e-c522b85fbfd3" width="400">

### Configure API Key
1. In PagerDuty, go to your profile and navigate to:
   **My Profile > User Settings > Create API User Token**.
2. Copy the generated API token and paste it into the PagerDuty Alerter web interface.

### Configure User ID
The User ID filter allows the alerter to focus on incidents assigned to a specific user. This is helpful if you have multiple on-call person and only need alerts for a specific user.

1. In PagerDuty, go to your profile and find your User ID in the URL:  
   `https://<your-domain>.pagerduty.com/users/XXXXXX`  
   (where `XXXXXX` is your User ID).
2. Copy the User ID and paste it into the PagerDuty Alerter web interface.

## Wiring Diagram
- **D1** → Relay  
- **D2** → Button  

<img src="https://github.com/user-attachments/assets/26965de2-5a68-462b-89f3-88047b3f500d" width="300">
<img src="https://github.com/user-attachments/assets/716392f1-f8c0-445f-b513-6feb1eb73d3e" width="300">

Finally, connect the siren to the relay.
