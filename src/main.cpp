#include <Arduino.h>

#include <Esp32Base.h>

#include "fan/ButtonDriver.h"
#include "fan/FanController.h"
#include "fan/FanDriver.h"
#include "fan/IRReceiverDriver.h"
#include "fan/LedIndicator.h"
#include "web/FanWeb.h"

// ESP32 DevKit default pin assignment. Keep strapping pins away from fan I/O.
static const uint8_t PIN_FAN_PWM = 25;
static const uint8_t PIN_FAN_TACH = 26;
static const uint8_t PIN_BTN_ACCEL = 32;
static const uint8_t PIN_BTN_DECEL = 33;
static const uint8_t PIN_LED = 2;
static const uint8_t PIN_IR_RECV = 27;
static const uint8_t PIN_BOOT = 0;

static const char* CFG_NS = "fan";
static const char* KEY_WEB_PASS = "web_pass";

static FanDriver fanDriver(PIN_FAN_PWM, PIN_FAN_TACH);
static ButtonDriver btnDriver(PIN_BTN_ACCEL, PIN_BTN_DECEL);
static LedIndicator ledIndicator(PIN_LED, true);
static IRReceiverDriver irDriver(PIN_IR_RECV);
static FanController fanController(fanDriver, btnDriver, ledIndicator, irDriver);
static FanWeb fanWeb(fanController, irDriver);

static const uint32_t BOOT_CLEAR_WIFI_MS = 1000;
static uint32_t bootPressStartMs = 0;
static bool bootPressed = false;
static bool bootActionDone = false;
static bool bootClearArmed = false;

static void loadWebAuthBeforeBaseBegin() {
    char pass[32];
    Esp32BaseConfig::begin();
    if (!Esp32BaseConfig::getStr(CFG_NS, KEY_WEB_PASS, pass, sizeof(pass), "admin123") || pass[0] == '\0') {
        strlcpy(pass, "admin123", sizeof(pass));
    }
    Esp32BaseWeb::setAuth("admin", pass);
}

static void registerFanRoutes() {
    bool ok = true;
    ok &= Esp32BaseWeb::addPage("/fan", FanWeb::handleStatusPage);
    ok &= Esp32BaseWeb::addPage("/config", FanWeb::handleConfigPage);
    ok &= Esp32BaseWeb::addApi("/api/status", FanWeb::handleApiStatus);
    ok &= Esp32BaseWeb::addApi("/api/speed", FanWeb::handleApiSpeed);
    ok &= Esp32BaseWeb::addApi("/api/timer", FanWeb::handleApiTimer);
    ok &= Esp32BaseWeb::addApi("/api/stop", FanWeb::handleApiStop);
    ok &= Esp32BaseWeb::addApi("/api/config", FanWeb::handleApiConfig);
    ok &= Esp32BaseWeb::addApi("/api/ir/learn", FanWeb::handleApiIrLearn);
    ok &= Esp32BaseWeb::addApi("/api/ir/status", FanWeb::handleApiIrStatus);
    ok &= Esp32BaseWeb::addApi("/api/reset", FanWeb::handleApiReset);
    if (!ok) {
        ESP32BASE_LOG_E("main", "custom Web route registration incomplete");
    }
}

static void handleBootButton() {
    const bool pressed = digitalRead(PIN_BOOT) == LOW;
    const uint32_t now = millis();

    if (!bootClearArmed) {
        if (!pressed) {
            bootClearArmed = true;
        }
        return;
    }

    if (pressed && !bootPressed) {
        bootPressed = true;
        bootActionDone = false;
        bootPressStartMs = now;
    } else if (!pressed && bootPressed) {
        bootPressed = false;
        bootActionDone = false;
    }

    if (pressed && bootPressed && !bootActionDone && now - bootPressStartMs >= BOOT_CLEAR_WIFI_MS) {
        bootActionDone = true;
        ESP32BASE_LOG_W("main", "BOOT held 1s, clearing WiFi credentials");
        Esp32BaseConfig::flushAll();
        Esp32BaseWiFi::clearCredentials();
        delay(300);
        ESP.restart();
    }
}

void setup() {
    Serial.begin(115200);
    delay(50);

    pinMode(PIN_BOOT, INPUT_PULLUP);

    Esp32Base::setFirmwareInfo("ESP32_Fan_4P", "0.1.0");
    Esp32Base::setHostname("esp32-fan");
    loadWebAuthBeforeBaseBegin();
    registerFanRoutes();

    if (!Esp32Base::begin()) {
        Serial.printf("Esp32Base begin failed: %s\n", Esp32Base::lastError());
        return;
    }

#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::enable("/logs/app.log", 16UL * 1024UL, Esp32BaseLog::DEBUG, 4);
    Esp32BaseConfig::enableConfigAudit(true);
    Esp32BaseConfig::enableConfigReadAudit(true);
#endif

    fanController.begin();
    ESP32BASE_LOG_I("main", "ESP32_Fan_4P started");
}

void loop() {
    Esp32Base::handle();
    fanController.tick();
    handleBootButton();
    yield();
}
