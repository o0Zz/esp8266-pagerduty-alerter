#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <map>
#include <vector>

/*********************************************
          SimpleTimer Class
**********************************************/

class SimpleTimer;

class SimpleTimerManager {
public:
    SimpleTimerManager();
    void add(SimpleTimer *timer);
    void run();
    static SimpleTimerManager &getInstance();

private:
    std::vector<SimpleTimer *> timers;
};

class SimpleTimer {
    typedef void (*Callback)(void *userData);

public:
    SimpleTimer(Callback callback = nullptr, void *userData = nullptr, bool autoRearm = false)
        : callback(callback), userData(userData), autoRearm(autoRearm), running(false), interval_s(0), lastCheckTime_ms(0) {
        SimpleTimerManager::getInstance().add(this);
    }

    ~SimpleTimer() {
        stop();
    }

    void start(unsigned long interval_s) {
        //Serial.println("Timer started: " + String(interval_s));
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
            if (callback != nullptr) {
                callback(userData);
            }

            running = false;
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

/*********************************************
         SimpleTimerManager Class
**********************************************/

SimpleTimerManager::SimpleTimerManager() 
{

}

void SimpleTimerManager::add(SimpleTimer *timer) {
    timers.push_back(timer);
}

void SimpleTimerManager::run() {
    for (SimpleTimer *timer : timers) {
        timer->run();
    }
}

SimpleTimerManager &SimpleTimerManager::getInstance() {
    static SimpleTimerManager instance;
    return instance;
}


/*********************************************
            Config Class
**********************************************/

class Config {
public:
    Config(const String &filename) : filename(filename) {}

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
                return true;
            }
        }
        return false;
    }

    bool save() {
        File file = SPIFFS.open(filename, "w");
        if (file) {
            for (const auto &entry : configMap) {
                file.println(entry.first + "=" + entry.second);
            }
            file.close();
            return true;
        }
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
    Siren(int gpio) : gpio(gpio), is_on(false), timer_mute(unmuteCallback, this, false) {
        pinMode(gpio, OUTPUT);
        digitalWrite(gpio, LOW);
    }

    void on() {
        is_on = true;
        if (!timer_mute.is_running()) {
            digitalWrite(gpio, HIGH);
        }
    }

    void off() {
        is_on = false;
        digitalWrite(gpio, LOW);
    }

    void mute(unsigned long duration_s) {
        Serial.println("Siren: Mute for " + String(duration_s));
        digitalWrite(gpio, LOW);
        timer_mute.start(duration_s);
    }

    void unmute() {
        Serial.println("Siren: Unmute");
        if (is_on) {
            digitalWrite(gpio, HIGH);
        }
    }

private:
    static void unmuteCallback(void *userData) {
        Siren *siren = static_cast<Siren *>(userData);
        siren->unmute();
    }

    int gpio;
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
                Application
**********************************************/

typedef enum WifiMode
{
    WIFI_MODE_UNKNOWN,
    WIFI_MODE_STA,
    WIFI_MODE_AP
} WifiMode;

#define GPIO_SIREN          5 //D1
#define GPIO_BUTTON         4 //D2

#define TIMER_MUTE                          60*5 // 5min
#define TIMER_REBOOT_WIFI_AP                60*5 // 5min
#define TIMER_REBOOT_APPLY_SETTINGS         1 // 1s

#define WIFI_AP_SSID                        "PagerDuty_Alerter"
#define WIFI_AP_PWD                         ""

#define PAGERDUTY_URL "https://api.pagerduty.com/incidents?statuses[]=triggered"
#define PAGERDUTY_QUERY_URL_USER "&user_ids[]="
#define PAGERDUTY_DEFAULT_INTERVAL_S 60

void startHttpServer();
void startApMode();
void checkPagerDuty(void *);
void reboot(void *);

WifiMode wifi_mode = WIFI_MODE_UNKNOWN;

SimpleTimer         timer_reboot(reboot, NULL, false);
SimpleTimer         timer_pager_duty_check(checkPagerDuty, NULL, true);

ESP8266WebServer    server(80);
Config              config("/config.ini");
Siren               siren(GPIO_SIREN); 
Button              button(GPIO_BUTTON);

void setup() {
    Serial.begin(115200);

    if (SPIFFS.begin()) {
        Serial.println("SPIFFS mounted successfully");
    } else {
        Serial.println("Failed to mount SPIFFS");
    }

    config.load();

    String wifi_ssid = config.getStr("wifi_ssid");
    String wifi_pwd = config.getStr("wifi_pwd");
    if (wifi_ssid.length() > 0)
    {
        wifi_mode = WIFI_MODE_STA;
        WiFi.begin(wifi_ssid.c_str(), wifi_pwd.c_str());
        Serial.println("Connecting to Wi-Fi " + wifi_ssid + "...");

        SimpleTimer timer_wifi_connection;
        timer_wifi_connection.start(10);
        while (WiFi.status() != WL_CONNECTED && timer_wifi_connection.is_running()) 
        {
            delay(500);
            Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) 
        {
            Serial.println("Successfully connected to Wi-Fi");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());

            timer_pager_duty_check.start(config.getInt("pagerduty_interval_s", PAGERDUTY_DEFAULT_INTERVAL_S));
        }
        else
        {
            Serial.println("Failed to connect to Wi-Fi: " + wifi_ssid);
            startApMode();
        }
    }
    else
    {
        Serial.println("No Wi-Fi configuration found");
        startApMode();
    }

    startHttpServer();
}

void loop() {

    server.handleClient(); //Run HTTP Server
        
    SimpleTimerManager::getInstance().run(); //Run timers
    
    if (wifi_mode = WIFI_MODE_STA)
    {
        if (WiFi.status() != WL_CONNECTED) //When Wi-Fi is disconnected, reboot
            timer_reboot.start(1);
    }
        
    if (button.stateChanged())
    {
        if (button.isPressed())
        {
            Serial.println("Button Pressed, mute siren for " + String(TIMER_MUTE) + "s");
            siren.mute(TIMER_MUTE);
        }
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
        html += "SSID: <input type='text' name='ssid' value='" + config.getStr("wifi_ssid") + "'><br>";
        html += "Password: <input type='password' name='pwd'><br>";
        html += "<input type='submit' value='Save'></form>";
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
        html += "<input type='submit' value='Save'></form>";
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

    server.begin();
    Serial.println("HTTP server started");
}

void startApMode() {
    wifi_mode = WIFI_MODE_AP;
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PWD);
    Serial.println("AP Mode started. Connect to '" WIFI_AP_SSID "' and access 'http://192.168.4.1'");
    timer_reboot.start(TIMER_REBOOT_WIFI_AP); 
}

void reboot(void *) {
    Serial.println("Rebooting...");
    ESP.restart();
}

void checkPagerDuty(void *) {
    Serial.println("Checking PagerDuty for active incident ...");
    
    String pagerduty_user_id = config.getStr("pagerduty_user_id");
    String pagerduty_api_key = config.getStr("pagerduty_api_key");

    if (pagerduty_api_key.length() == 0)
    {
        Serial.println("PagerDuty API Key not configured");
        return;
    }

    String pagerduty_url = PAGERDUTY_URL;
    if (pagerduty_user_id.length() > 0)
        pagerduty_url += String(PAGERDUTY_QUERY_URL_USER) + pagerduty_user_id;

    WiFiClientSecure client;
    client.setInsecure(); // For simplicity, skip certificate verification

    Serial.println("GET " + pagerduty_url);
    //Serial.println("Token: " + pagerduty_api_key);

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
            if (payload.indexOf("triggered") != -1) {
                Serial.println("Open incident detected!");
                siren.on();
            } else {
                Serial.println("No open incidents.");
                siren.off();
            }
        }
        https.end();

    } else {
        Serial.println("Unable to connect to PagerDuty API");
    }
}
