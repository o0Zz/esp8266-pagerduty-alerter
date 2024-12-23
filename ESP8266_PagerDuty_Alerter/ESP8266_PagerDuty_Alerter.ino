#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <map>
#include <vector>

/*********************************************
                    Configuration
**********************************************/

#define PAGERDUTY_FAKE_INCIDENT 0

#define GPIO_SIREN            5 //D1
#define GPIO_SIREN_REVERSED   true

#define GPIO_BUTTON         4 //D2

#define TIMER_MUTE                          60*5 // 5min
#define TIMER_REBOOT_WIFI_AP                60*5 // 5min
#define TIMER_REBOOT_APPLY_SETTINGS         1 // 1s

#define WIFI_AP_SSID                        "PagerDuty_Alerter"
#define WIFI_AP_PWD                         ""

#define PAGERDUTY_DEFAULT_INTERVAL_S 60

#define LOG_COUNT 8
#define LOG_SIZE  80


/*********************************************
                    Logs
**********************************************/

char logBuffer[LOG_COUNT][LOG_SIZE];
unsigned int logTimestamp[LOG_COUNT];
int currentLogIndex = 0;

void Log(const String &message) {
  strncpy(logBuffer[currentLogIndex], message.c_str(), LOG_SIZE - 1);
  logBuffer[currentLogIndex][LOG_SIZE - 1] = '\0';
  logTimestamp[currentLogIndex] = millis();
  currentLogIndex = (currentLogIndex + 1) % LOG_COUNT;

  Serial.write(message.c_str(), message.length());
  Serial.write('\n');
}

/*********************************************
         SimpleTimerManager Class
**********************************************/

class SimpleTimer;

class SimpleTimerManager {
public:
  SimpleTimerManager() 
  {

  }

  void add(SimpleTimer *timer) {
      timers.push_back(timer);
  }

  void remove(SimpleTimer *timer) {
      timers.erase(std::remove(timers.begin(), timers.end(), timer), timers.end());
  }

  void run();

  static SimpleTimerManager &getInstance() {
      static SimpleTimerManager instance;
      return instance;
  }

private:
    std::vector<SimpleTimer *> timers;
};

/*********************************************
          SimpleTimer Class
**********************************************/

class SimpleTimer {
    typedef void (*Callback)(void *userData);

public:
    SimpleTimer(Callback callback = nullptr, void *userData = nullptr, bool autoRearm = false) : 
        callback(callback), 
        userData(userData), 
        autoRearm(autoRearm), 
        running(false), 
        interval_s(0), 
        lastCheckTime_ms(0)
    {
        SimpleTimerManager::getInstance().add(this);
    }

    ~SimpleTimer() {
        stop();
        SimpleTimerManager::getInstance().remove(this);
    }

    void start(unsigned long interval_s) {
        //Log("Timer: started: " + String(interval_s));
        this->interval_s = interval_s;
        restart();
    }

    void stop() {
        running = false;
        interval_s = 0;
        lastCheckTime_ms = 0;
    }

    void restart() {
        running = true;
        lastCheckTime_ms = millis();
    }

    bool is_running() const {
        return running;
    }

    bool is_triggered() {
        if (!running)
            return false;

        return (millis() - lastCheckTime_ms) >= interval_s * 1000;
    }

    void run() {
        if (is_triggered()) {
            running = false;
            
            if (callback != nullptr) {
                callback(userData);
            }

            if (autoRearm) {
                restart();
            }
        }
    }

private:
    Callback callback;
    void *userData;
    bool autoRearm;
    bool running;
    unsigned long interval_s;
    unsigned long lastCheckTime_ms;
};

void SimpleTimerManager::run() {
      for (SimpleTimer *timer : timers) {
          timer->run();
      }
  }

/*********************************************
            Config Class
**********************************************/

class Config {
public:
    Config(const String &filename): 
      filename(filename) 
    {
        if (SPIFFS.begin()) {
            Log(F("SPIFFS: Mounted successfully"));
        } else {
            Log(F("SPIFFS: Failed to mount"));
        }
    }

    bool load() {
        if (SPIFFS.exists(filename)) {
            File file = SPIFFS.open(filename, "r");
            if (file) {
                while (file.available()) {
                    String line = file.readStringUntil('\n');
                    int separatorIndex = line.indexOf('=');
                    if (separatorIndex != -1) {
                        String key = line.substring(0, separatorIndex);
                        String value = line.substring(separatorIndex + 1);
                        key.trim();
                        value.trim();
                        configMap[key] = value;
                    }
                }
                file.close();
                Log(F("Config: Loaded successfully"));
                return true;
            }
        }
        Log(F("Config: Load failed (Not found)"));
        return false;
    }

    bool save() {
        File file = SPIFFS.open(filename, "w");
        if (file) {
            for (const auto &entry : configMap) {
                file.println(entry.first + "=" + entry.second);
            }
            file.close();
            Log(F("Config: Saved successfully"));
            return true;
        }
        Log(F("Config: Save failed"));
        return false;
    }

    void set(const String &key, const String &value) {
        configMap[key] = value;
    }

    String getStr(const String &key, const String &defaultValue = "") {
        return configMap.count(key) ? configMap[key] : defaultValue;
    }

    int getInt(const String &key, int defaultValue = 0) {
        return configMap.count(key) ? configMap[key].toInt() : defaultValue;
    }

private:
    String filename;
    std::map<String, String> configMap;
};

/*********************************************
             Siren Class
**********************************************/

class Siren {
public:
    Siren(int gpio, bool gpio_reversed) : 
        gpio(gpio), 
        gpio_reversed(gpio_reversed),
        is_on(false), 
        timer_mute(Siren::unmuteCallback, this, false) 
    {
        pinMode(gpio, OUTPUT);
        off();
    }

    void on() {
        is_on = true;
        if (!timer_mute.is_running()) {
            digitalWrite(gpio, gpio_reversed ? LOW : HIGH);
        }
    }

    void off() {
        is_on = false;
        digitalWrite(gpio, gpio_reversed ? HIGH : LOW);
    }

    void mute(unsigned long duration_s) {
        Log("Siren: Mute for " + String(duration_s) + " seconds");
        digitalWrite(gpio, gpio_reversed ? HIGH : LOW);
        timer_mute.start(duration_s);
    }

    void unmute() {
        Log(F("Siren: Unmute"));
        if (is_on) {
            digitalWrite(gpio, gpio_reversed ? LOW : HIGH);
        }
    }

private:
    static void unmuteCallback(void *userData) {
        Siren *siren = static_cast<Siren *>(userData);
        siren->unmute();
    }

    int gpio;
    bool gpio_reversed;
    bool is_on;
    SimpleTimer timer_mute;
};


/*********************************************
                Button class
**********************************************/

typedef enum ButtonState
{
    BUTTON_STATE_UNKNOWN,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_RELEASED
} ButtonState;

class Button
{
public:
    Button(int gpio) : 
        gpio(gpio)
    {
        pinMode(gpio, INPUT_PULLUP);
    }

    bool stateChanged()
    {
        int reading = digitalRead(gpio);

        if (reading != lastButtonState)
            lastDebounceTime = millis();

        if ((millis() - lastDebounceTime) > debounceDelay)
        {
            if (reading != currentState)
            {
                currentState = reading;
                return true;
            }
        }

        lastButtonState = reading;
        return false;
    }

    bool isPressed()
    {
        return (currentState == LOW);
    }

private:
    int gpio;
    int currentState = LOW;
    int lastButtonState = LOW;
    unsigned long lastDebounceTime = 0; 
    const unsigned long debounceDelay = 50;
};

/*********************************************
                PagerDuty class
**********************************************/

#define PAGERDUTY_URL "https://api.pagerduty.com/incidents?statuses[]=triggered"
#define PAGERDUTY_QUERY_URL_USER "&user_ids[]="

class PagerDuty
{
public:
    PagerDuty() {}

    void setup(const String &pagerduty_api_key, const String &pagerduty_user_id) {
        this->pagerduty_api_key = pagerduty_api_key;
        this->pagerduty_user_id = pagerduty_user_id;
    }

    int getIncidentCount() {
        if (pagerduty_api_key.length() == 0)
        {
            Log(F("PagerDuty: API Key not configured"));
            return lastIncidentCount;
        }

        String pagerduty_url = PAGERDUTY_URL;
        if (pagerduty_user_id.length() > 0)
            pagerduty_url += String(PAGERDUTY_QUERY_URL_USER) + pagerduty_user_id;

        WiFiClientSecure client;
        client.setInsecure(); // For simplicity, skip certificate verification

        Log("PagerDuty: GET " + pagerduty_url);
        //Log("PagerDuty: Token " + pagerduty_api_key);

        HTTPClient https;
        if (https.begin(client, pagerduty_url)) {
            https.addHeader("Authorization", String("Token token=") + pagerduty_api_key);
            https.addHeader("Content-Type", "application/json");
            https.addHeader("Accept", "application/json");

            int httpCode = https.GET();
            Log("PagerDuty: HTTP response code: " + String(httpCode));

            if (httpCode/100 == 2) {
                String payload = https.getString();
                Log("PagerDuty: Payload: " + payload);

                // Check if there are open incidents
                if (payload.indexOf("triggered") != -1) {
                    lastIncidentCount = 1;
                } else {
                    lastIncidentCount = PAGERDUTY_FAKE_INCIDENT;
                }
            }
            https.end();

        } else {
            Log(F("PagerDuty: Unable to connect to API"));
        }

        return lastIncidentCount;
    }

private:
    String pagerduty_user_id;
    String pagerduty_api_key;
    int lastIncidentCount = 0;
};

/*********************************************
                Application
**********************************************/

typedef enum WifiMode
{
    WIFI_MODE_UNKNOWN,
    WIFI_MODE_STA,
    WIFI_MODE_AP
} WifiMode;

bool startSTAMode();
bool startAPMode();
void startHttpServer();

void checkPagerDuty(void *);
void reboot(void *);

WifiMode wifi_mode = WIFI_MODE_UNKNOWN;

SimpleTimer         timer_reboot(reboot, NULL, false);
SimpleTimer         timer_pager_duty_check(checkPagerDuty, NULL, true);

ESP8266WebServer    server(80);
Config              config("/config.ini");
Siren               siren(GPIO_SIREN, GPIO_SIREN_REVERSED); 
Button              button(GPIO_BUTTON);
PagerDuty           pagerduty;

void setup() {
    Serial.begin(115200);
    Serial.println("");

    config.load();

    pagerduty.setup(config.getStr("pagerduty_api_key"), config.getStr("pagerduty_user_id"));

    if (startSTAMode()) //Connect to WiFi
    {
        timer_pager_duty_check.start(config.getInt("pagerduty_interval_s", PAGERDUTY_DEFAULT_INTERVAL_S));
    }
    else //No WiFi available, start AP mode to configure the module
    {
        startAPMode();
        timer_reboot.start(TIMER_REBOOT_WIFI_AP); 
    }

    startHttpServer();
}

void loop() {

    server.handleClient(); //Run HTTP Server
        
    SimpleTimerManager::getInstance().run(); //Run timers
    
    if (wifi_mode == WIFI_MODE_STA)
    {
        if (WiFi.status() != WL_CONNECTED) //When Wi-Fi is disconnected, reboot
        {
            Log(F("WiFi: Disconnected !"));
            reboot(NULL);
        }
    }

    if (button.stateChanged())
    {
        if (button.isPressed())
        {
            Log(F("Button: Pressed"));
            siren.mute(TIMER_MUTE);
        }
    }
}

#define HTML_HEADER "<html><head>\
  <style>\
    body {font-family: Arial, sans-serif; background: #f4f4f9; margin: 0; display: flex; justify-content: center; align-items: center; height: 100vh;}\
    .container {text-align: center; background: #fff; padding: 20px; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); width: 400px;}\
    h2 {margin-bottom: 20px;}\
    input, .button {padding: 10px 20px; border-radius: 5px; font-size: 16px; cursor: pointer; transition: 0.3s; text-align: center; display: inline-block; border: none; text-decoration: none;}\
    input[type='text'], input[type='password'] {margin: 10px 0; width: calc(100% - 22px); border: 1px solid #ccc;}\
    .button {color: #fff;}\
    a.button {background: #007bff;} a.button:hover {background: #0056b3;}\
    .back-button {background: #6c757d;} .back-button:hover {background: #5a6268;}\
    .menu-button {width: 300px; margin: 10px;}\
    .save-button {background: #28a745;} .save-button:hover {background: #218838;}\
    .form-actions {display: flex; justify-content: center; gap: 10px; margin-top: 20px;}\
  </style>\
  </head>\
  <body>"

#define HTML_FOOTER "</body></html>"

void sendHTML(const String &title, const String &HTMLContent)
{
  server.setContentLength(CONTENT_LENGTH_UNKNOWN); // Enable chunked transfer
  server.send(200, "text/html", ""); // Send the initial headers
  server.sendContent(HTML_HEADER);
  server.sendContent("<div class='container'><h2>" + title + "</h2>");
  server.sendContent(HTMLContent);
  server.sendContent("</div>");
  server.sendContent(HTML_FOOTER);
  server.sendContent("");
}

void startHttpServer() {
    server.on("/", HTTP_GET, []() {
        sendHTML("PagerDuty Alerter", "<a href='/wifi_configure' class='button menu-button'>Configure Wi-Fi</a><br><a href='/pagerduty_configure' class='button menu-button'>Configure PagerDuty</a><br><a href='/logs' class='button menu-button'>Logs</a>");
    });

    server.on("/wifi_configure", HTTP_GET, []() {
        String html = "<form method='POST' action='/wifi_save'>";
        html += "SSID: <input type='text' name='ssid' value='" + config.getStr("wifi_ssid") + "'><br>";
        html += "Password: <input type='password' name='pwd'><br>";
        html += "<a href='/' class='button back-button'>Back</a> <input type='submit' value='Save' class='button save-button'></form>";
        sendHTML("Wi-Fi Configuration", html);
    });

    server.on("/wifi_save", HTTP_POST, []() {
        if (server.hasArg("ssid") && server.hasArg("pwd")) 
        {
            config.set("wifi_ssid", server.arg("ssid"));
            config.set("wifi_pwd", server.arg("pwd"));
            config.save();

            sendHTML("Configuration Saved", "<meta http-equiv='refresh' content='3; url=/'><p>Rebooting...</p>");
            timer_reboot.start(TIMER_REBOOT_APPLY_SETTINGS);
        } else {
            server.send(400, "text/plain", "Invalid Input");
        }
    });

    server.on("/pagerduty_configure", HTTP_GET, []() {
        String html = "<form method='POST' action='/pagerduty_save'>";
        html += "API Key (Empty = Keep unchanged): <input type='text' name='api_key' value=''><br>";
        html += "User ID (Empty = All users): <input type='text' name='user_id' value='" + config.getStr("pagerduty_user_id") + "'><br>";
        html += "Interval (Seconds): <input type='text' name='interval_s' value='" + config.getStr("pagerduty_interval_s", String(PAGERDUTY_DEFAULT_INTERVAL_S)) + "'><br>";
        html += "<a href='/' class='button back-button'>Back</a> <input type='submit' value='Save' class='button save-button'></form>";
        sendHTML("PagerDuty Configuration", html);
    });

    server.on("/pagerduty_save", HTTP_POST, []() {
        if (server.hasArg("api_key") && server.arg("api_key").length() > 0)
            config.set("pagerduty_api_key", server.arg("api_key"));
        
        if (server.hasArg("user_id"))
            config.set("pagerduty_user_id", server.arg("user_id"));

        if (server.hasArg("interval_s"))
            config.set("pagerduty_interval_s", server.arg("interval_s"));

        if (config.getInt("pagerduty_interval_s", 0) < 10)
            config.set("pagerduty_interval_s", String(10));

        config.save();
        sendHTML("Configuration Saved", "<meta http-equiv='refresh' content='3; url=/'><p>Rebooting...</p>");
        timer_reboot.start(TIMER_REBOOT_APPLY_SETTINGS);
    });

    server.on("/logs", HTTP_GET, []() {
        String html;
        html += "<table border='1'><tr><th>Timestamp (ms)</th><th>Message</th></tr>";
        for (int i = 0; i < LOG_COUNT; i++)
          html += "<tr><td>" + String(logTimestamp[(currentLogIndex + i) % LOG_COUNT]) + "</td><td>" + String(logBuffer[(currentLogIndex + i) % LOG_COUNT]) + "</td></tr>";
        html += "</table><br>";
        html += "<a href='/' class='button back-button'>Back</a>";
        sendHTML("Logs", html);
    });

    server.begin();
    Log(F("HTTPserver: Started"));
}

bool startSTAMode() {
    wifi_mode = WIFI_MODE_STA;

    String wifi_ssid = config.getStr("wifi_ssid");
    String wifi_pwd = config.getStr("wifi_pwd");

    if (wifi_ssid.length() > 0)
    {
        WiFi.begin(wifi_ssid.c_str(), wifi_pwd.c_str());
        Log("WiFi: Connecting to " + wifi_ssid + "...");

        SimpleTimer timer_wifi_connection;
        timer_wifi_connection.start(10);
        while (WiFi.status() != WL_CONNECTED && !timer_wifi_connection.is_triggered()) 
            delay(500);
        
        if (WiFi.status() == WL_CONNECTED) 
        {
            Log("WiFi: Successfully connected, IP Address: " + WiFi.localIP().toString());
            return true;
        }
        else
        {
            Log(F("WiFi: Failed to connect"));
        }
    }
    else
    {
        Log(F("WiFi: No configuration found"));
    }

    return false;
}

bool startAPMode() {
    wifi_mode = WIFI_MODE_AP;
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PWD);

    Log(F("WiFi: AP Mode started. Name: '" WIFI_AP_SSID "' (http://192.168.4.1)"));
    
    return true;
}

void reboot(void *) {
    Log(F("Rebooting..."));
    ESP.restart();
}

void checkPagerDuty(void *) {
    if (pagerduty.getIncidentCount() > 0)
        siren.on();
    else
        siren.off();
}
