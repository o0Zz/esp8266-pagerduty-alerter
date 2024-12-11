# ESP8266 PagerDuty Alerter
This project provide a PagerDuty physical alerter such as Siren, Buzzer, Light etc...
It rely on ESP8266 NodeMCU

# Setup
 1. Buy a NodeMCU v2 board (With CP2102 chipset) (https://www.aliexpress.com/item/1005006246641996.html)
 2. Download and install latest arduino version (https://www.arduino.cc/en/software)
 3. In arduinoIDE: File > Preferences > add below line in "Additional boards manager URLs":
 ```
 http://arduino.esp8266.com/stable/package_esp8266com_index.json
 ```
 Split it with a comma if needed
 4. In arduinoIDE: Tools > Board > Board Manager > Install "esp8266 by ESP8266 Community"
  
# Run
 1. Plug your NodeMCU board to your PC
 2. Open "ESP8266_PagerDuty_Alerter.ino" from this repository
 3. In arduinoIDE: Tools > Board > esp8266 > Select "NodeMCU 1.0 (ESP-12E Module)"
 4. In arduinoIDE: Tools > Port > Select your COMX

# Usage
When the board boot it will automatically create an access point named: "PagerDuty Alerter"
Connect to it, Open a browser and go to: http://192.168.4.1
Configure you WiFi and PagerDuty access

Help:
	How to find UserID ?
	How to create a token ?

# Wiring 

...