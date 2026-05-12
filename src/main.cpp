#include <Arduino.h>

#include <Esp32Base.h>

#include "app/FanAppRuntime.h"
#include "fan/ButtonDriver.h"
#include "fan/FanController.h"
#include "fan/FanDriver.h"
#include "fan/FanHistory.h"
#include "fan/IRReceiverDriver.h"
#include "fan/LedIndicator.h"
#include "web/FanWeb.h"

// ESP32 DevKit default pin assignment. Keep strapping pins away from fan I/O.
static const uint8_t PIN_FAN_PWM = 18;
static const uint8_t PIN_FAN_TACH = 19;
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
static FanHistory fanHistory;
static FanWeb fanWeb(fanController, irDriver, &fanHistory);
static BootClearState bootClearState = {};

static void holdFanPwmOff() {
    pinMode(PIN_FAN_PWM, OUTPUT);
    digitalWrite(PIN_FAN_PWM, LOW);
}

void setup() {
    holdFanPwmOff();
    Esp32BaseLog::setSerialLevel(Esp32BaseLog::NONE);
    delay(50);

    pinMode(PIN_BOOT, INPUT_PULLUP);

    Esp32Base::setFirmwareInfo("ESP32_Fan_4P", "0.1.0");
    fanAppConfigureBaseWebBeforeBegin();
    if (!fanAppRegisterFanRoutes()) {
        while (true) {
            delay(1000);
            yield();
        }
    }
    fanAppEnableConfigAuditBeforeBegin();

    if (!Esp32Base::begin()) {
        while (true) {
            delay(1000);
            yield();
        }
    }

    fanController.begin();
    fanHistory.begin();
    ESP32BASE_LOG_I("main", "ESP32_Fan_4P started");
}

void loop() {
    Esp32Base::handle();
    fanController.tick();
    fanHistory.tick(millis(),
                    fanController.getCurrentSpeed(),
                    fanController.getTargetSpeed(),
                    fanController.getCurrentRpm());
    fanAppHandleBootButton(PIN_BOOT, &bootClearState);
    yield();
}
