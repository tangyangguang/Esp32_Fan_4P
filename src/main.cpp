#include <Arduino.h>

#include <Esp32Base.h>

#include "app/FanAppRuntime.h"
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

static FanDriver fanDriver(PIN_FAN_PWM, PIN_FAN_TACH);
static ButtonDriver btnDriver(PIN_BTN_ACCEL, PIN_BTN_DECEL);
static LedIndicator ledIndicator(PIN_LED, true);
static IRReceiverDriver irDriver(PIN_IR_RECV);
static FanController fanController(fanDriver, btnDriver, ledIndicator, irDriver);
static FanWeb fanWeb(fanController, irDriver);
static BootClearState bootClearState = {};

void setup() {
    Serial.begin(115200);
    delay(50);

    pinMode(PIN_BOOT, INPUT_PULLUP);

    Esp32Base::setFirmwareInfo("ESP32_Fan_4P", "0.1.0");
    Esp32Base::setHostname("esp32-fan");
    fanAppConfigureBaseWebBeforeBegin();
    fanAppRegisterFanRoutes();
    fanAppEnableConfigAuditBeforeBegin();

    if (!Esp32Base::begin()) {
        Serial.printf("Esp32Base begin failed: %s\n", Esp32Base::lastError());
        while (true) {
            delay(1000);
            yield();
        }
    }

#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::enable("/logs/app.log", 16UL * 1024UL, Esp32BaseLog::INFO, 4);
#endif

    fanController.begin();
    ESP32BASE_LOG_I("main", "ESP32_Fan_4P started");
}

void loop() {
    Esp32Base::handle();
    fanController.tick();
    fanAppHandleBootButton(PIN_BOOT, &bootClearState);
    yield();
}
