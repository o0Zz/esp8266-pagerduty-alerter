# ESP8266 PagerDuty Alerter
This project provide a PagerDuty physical alerter such as Siren, Buzzer, Light etc...
It rely on ESP8266 NodeMCU board and can be configure via a WebInterface.
![image](https://github.com/user-attachments/assets/aaff4a03-6967-4704-ad7e-c522b85fbfd3)

## Hardware
 - NodeMCU v2 board (With CP2102 chipset) (https://www.aliexpress.com/item/1005006246641996.html)
 - Siren (https://www.aliexpress.com/item/1005007384223586.html)
 - Relay 5v (https://www.aliexpress.com/item/1005007598074881.html)

## Setup environemnt
 1. Download and install latest arduino version (https://www.arduino.cc/en/software)
 2. In arduinoIDE: File > Preferences > add below line in "Additional boards manager URLs":
 ```
 http://arduino.esp8266.com/stable/package_esp8266com_index.json
 ```
 Split it with a comma if needed
 3. In arduinoIDE: Tools > Board > Board Manager > Install "esp8266 by ESP8266 Community"
  
## Run
 1. Plug your NodeMCU board to your PC
 2. Open "ESP8266_PagerDuty_Alerter.ino" from this repository
 3. In arduinoIDE: Tools > Board > esp8266 > Select "NodeMCU 1.0 (ESP-12E Module)"
 4. In arduinoIDE: Tools > Port > Select your COMX

## Usage
When the board boot it will automatically create an access point named: "PagerDuty_Alerter"
Connect to it, Open a browser and go to: http://192.168.4.1
Configure you WiFi and PagerDuty access

A button is available to mute the siren for 5min.

### Configure API Key
- In PagerDuty, click on your profile > "My Profile" > "User Settings" > "Create API User Token"
Copy/Paste the key in your pagerduty alerter webinterface

### Configure User ID
User ID allow to filter alarms only on a given userID. This is usefully in case you have multiple oncall people and want to alert only for a given user.
- In PagerDuty, click on your profile > "My Profile"
In the URL you should found: `https://dicksondata.pagerduty.com/users/XXXXXX`, where XXXXXX is your UserID.
Copy/Paste the user id in your pagerduty alerter webinterface

# Wiring 
 - D1 - Relay
 - D2 - Button
 
Then plug the Siren to the Relay.
 
