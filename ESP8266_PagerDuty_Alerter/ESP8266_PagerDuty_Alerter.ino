#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>

ESP8266WebServer server(80);

#define GPIO_SIREN          5 //D1
#define GPIO_LED_STATUS     6 //D2

const char* ssid_ap = "PagerDuty_Alerter";
const char* password_ap = "";

String pagerduty_api_key;
String pagerduty_user_id;
int pagerduty_interval_s = 60;

#define PAGERDUTY_URL "https://api.pagerduty.com/incidents?statuses[]=triggered"
#define PAGERDUTY_QUERY_URL_USER "&user_ids[]="

void startHttpServer();
void startApMode();
void checkPagerDuty(String pagerduty_api_key, String pagerduty_user_id);

unsigned long lastCheckTime = 0;

void setup() {
    Serial.begin(115200);
    
    pinMode(GPIO_SIREN, OUTPUT);
    digitalWrite(GPIO_SIREN, LOW);

    if (SPIFFS.begin()) {
        Serial.println("SPIFFS mounted successfully");
    } else {
        Serial.println("Failed to mount SPIFFS");
    }

    if (SPIFFS.exists("/pagerduty_config.txt")) {
        File file = SPIFFS.open("/pagerduty_config.txt", "r");
        if (file) {
            pagerduty_api_key = file.readStringUntil('\n');
            pagerduty_user_id = file.readStringUntil('\n');
            pagerduty_interval_s = file.readStringUntil('\n').toInt();
            pagerduty_api_key.trim();
            pagerduty_user_id.trim();
            file.close();
            Serial.println("PagerDuty configuration loaded");
        }
    }

    if (SPIFFS.exists("/wifi_config.txt")) {
        File file = SPIFFS.open("/wifi_config.txt", "r");
        if (file) {
            String ssid = file.readStringUntil('\n');
            String password = file.readStringUntil('\n');
            ssid.trim();
            password.trim();
            file.close();
            Serial.println("Wifi configuration loaded");

            WiFi.begin(ssid.c_str(), password.c_str());
            Serial.println("Connecting to Wi-Fi " + ssid + "...");

            unsigned long startAttemptTime = millis();

            while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
                delay(500);
                Serial.print(".");
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("Connected to Wi-Fi");
                Serial.print("IP Address: ");
                Serial.println(WiFi.localIP());

                startHttpServer();
                return;
            }
        }
    }

    Serial.println("Failed to connect to Wi-Fi, starting AP mode...");

    startApMode();
    startHttpServer();
}

void loop() {
    server.handleClient();

    // Check PagerDuty API at regular intervals
    if (WiFi.status() == WL_CONNECTED && (lastCheckTime == 0 || (millis() - lastCheckTime) > (pagerduty_interval_s*1000))) {
        lastCheckTime = millis();
        checkPagerDuty(pagerduty_api_key, pagerduty_user_id);
    }
}

void sendHTML(String title, String HTMLContent)
{
  String html = "<html><head>";
  html += "<style>";
  html += "  body {font-family: Arial, sans-serif; background: #f4f4f9; margin: 0; display: flex; justify-content: center; align-items: center; height: 100vh;}";
  html += "  .container {text-align: center; background: #fff; padding: 20px; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1);}";
  html += "  h2 {margin-bottom: 20px;}";
  html += "  a, input[type='submit'] {padding: 10px 20px; border-radius: 5px; cursor: pointer; transition: 0.3s;}";
  html += "  a {text-decoration: none; color: #fff; background: #007bff; margin: 10px 0; display: inline-block;}";
  html += "  a:hover {background: #0056b3;}";
  html += "  input[type='text'], input[type='password'] {padding: 10px; margin: 10px 0; width: calc(100% - 22px); border: 1px solid #ccc; border-radius: 5px;}";
  html += "  input[type='submit'] {background: #28a745; color: #fff; border: none;}";
  html += "  input[type='submit']:hover {background: #218838;}";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class='container'>";
  html += "<h2>" + title + "</h2>";
  html += HTMLContent;
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void startHttpServer() {
    server.on("/", HTTP_GET, []() {
        sendHTML("PagerDuty Alerter", "<a href='/wifi_configure'>Configure Wi-Fi</a><br><a href='/pagerduty_configure'>Configure PagerDuty</a>");
    });

    server.on("/wifi_configure", HTTP_GET, []() {
        String html = "<form method='POST' action='/wifi_save'>";
        html += "SSID: <input type='text' name='ssid'><br>";
        html += "Password: <input type='password' name='pwd'><br>";
        html += "<input type='submit' value='Save'></form>";
        sendHTML("Wi-Fi Configuration", html);
    });

    server.on("/wifi_save", HTTP_POST, []() {
        if (server.hasArg("ssid") && server.hasArg("pwd")) {
            saveConfig("/wifi_config.txt", 2, server.arg("ssid").c_str(), server.arg("pwd").c_str());
            sendHTML("Configuration Saved", "<meta http-equiv='refresh' content='3; url=/'><p>Rebooting...</p>");
            delay(2000);
            ESP.restart();
        } else {
            server.send(400, "text/plain", "Invalid Input");
        }
    });

    server.on("/pagerduty_configure", HTTP_GET, []() {
        String html = "<form method='POST' action='/pagerduty_save'>";
        html += "API Key: <input type='text' name='api_key' value=''><br>";
        html += "User ID: <input type='text' name='user_id' value='" + String(pagerduty_user_id) + "'><br>";
        html += "Interval (Seconds): <input type='text' name='interval_s' value='" + String(pagerduty_interval_s) + "'><br>";
        html += "<input type='submit' value='Save'></form>";
        sendHTML("PagerDuty Configuration", html);
    });

    server.on("/pagerduty_save", HTTP_POST, []() {
        if (server.hasArg("api_key") && server.arg("api_key").length() > 0)
          pagerduty_api_key = server.arg("api_key");
        
        if (server.hasArg("user_id"))
          pagerduty_user_id = server.arg("user_id");

        if (server.hasArg("interval_s") && server.arg("api_key").length() > 0)
          pagerduty_interval_s = server.arg("interval_s").toInt();

        saveConfig("/pagerduty_config.txt", 3, pagerduty_api_key.c_str(), pagerduty_user_id.c_str(), String(pagerduty_interval_s).c_str());
        sendHTML("Configuration Saved", "<meta http-equiv='refresh' content='3; url=/'><p>Rebooting...</p>");
        delay(2000);
        ESP.restart();
    });

    server.begin();
    Serial.println("HTTP server started");
}

void startApMode() {
    WiFi.softAP(ssid_ap, password_ap);
    Serial.println("AP Mode started. Connect to '" + String(ssid_ap) + "' and access 'http://192.168.4.1'");
}

bool saveConfig(const char* filename, int numArgs, ...) {
    File file = SPIFFS.open(filename, "w");
    if (file) {
        va_list args;
        va_start(args, numArgs);

        for (int i = 0; i < numArgs; ++i) {
            String value = va_arg(args, char*);
            file.println(value);
        }

        va_end(args);
        file.close();
        Serial.printf("Configuration saved to %s\n", filename);
        return true;
    } else {
        Serial.printf("Failed to save configuration to %s\n", filename);
        return false;
    }
}

void checkPagerDuty(String pagerduty_api_key, String pagerduty_user_id) {
    Serial.println("Checking PagerDuty for active incident ...");

    String pagerduty_url = PAGERDUTY_URL;
    if (pagerduty_user_id.length() > 0)
      pagerduty_url += String(PAGERDUTY_QUERY_URL_USER) + pagerduty_user_id;

    WiFiClientSecure client;
    client.setInsecure(); // For simplicity, skip certificate verification

    Serial.println("GET " + pagerduty_url);
    Serial.println("Token: " + pagerduty_api_key);

    HTTPClient https;
    if (https.begin(client, pagerduty_url)) {
        https.addHeader("Authorization", String("Token token=") + pagerduty_api_key);
        https.addHeader("Content-Type", "application/json");
        https.addHeader("Accept", "application/json");

        int httpCode = https.GET();
        Serial.print("HTTP response code: ");
        Serial.println(httpCode);

        if (httpCode/100 == 2) {
            String payload = https.getString();
            Serial.print("PagerDuty API Payload:");
            Serial.println(payload);

            // Check if there are open incidents
            if (payload.indexOf("\"status\":\"triggered\"") != -1) {
                Serial.println("Open incident detected! Triggering Siren.");
                digitalWrite(GPIO_SIREN, HIGH); // Turn on GPIO 1
            } else {
                Serial.println("No open incidents.");
                digitalWrite(GPIO_SIREN, LOW); // Turn off GPIO 1
            }
        }
        https.end();

    } else {
        Serial.println("Unable to connect to PagerDuty API");
    }
}
