#include <unity.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "Arduino.h"
#include "Esp32Base.h"

static uint32_t g_mockMillis = 0;
static uint8_t g_pinMode[64] = {};
static uint8_t g_pinState[64] = {};
static uint8_t g_pwmValue[64] = {};
static void (*g_isr[64])() = {};
static bool g_restartCalled = false;

uint32_t millis() { return g_mockMillis; }
void yield() {}
void delay(uint32_t ms) { g_mockMillis += ms; }
void pinMode(uint8_t pin, uint8_t mode) { g_pinMode[pin] = mode; }
uint8_t digitalRead(uint8_t pin) { return g_pinState[pin]; }
void digitalWrite(uint8_t pin, uint8_t val) { g_pinState[pin] = val; }
void analogWriteFreq(uint32_t) {}
void analogWrite(uint8_t pin, uint8_t val) { g_pwmValue[pin] = val; }
void attachInterrupt(uint8_t pin, void (*handler)(), int) { g_isr[pin] = handler; }
void detachInterrupt(uint8_t pin) { g_isr[pin] = nullptr; }
uint8_t digitalPinToInterrupt(uint8_t pin) { return pin; }
void noInterrupts() {}
void interrupts() {}

ESPClass ESP;
void ESPClass::restart() { g_restartCalled = true; }
bool ESPClass::restartCalled() {
    const bool called = g_restartCalled;
    g_restartCalled = false;
    return called;
}

WiFiClass WiFi;

namespace {
struct IntEntry {
    char ns[16];
    char key[16];
    int32_t value;
    bool used;
};
struct BoolEntry {
    char ns[16];
    char key[16];
    bool value;
    bool used;
};
struct StrEntry {
    char ns[16];
    char key[16];
    char value[64];
    bool used;
};

IntEntry g_ints[80] = {};
BoolEntry g_bools[24] = {};
StrEntry g_strs[12] = {};
bool g_configReady = false;
bool g_wifiConnected = true;
bool g_wifiPowerSave = false;
bool g_libraryNamespacesCleared = false;
char g_authUser[32] = "admin";
char g_authPass[32] = "admin";
bool g_authEnabled = true;
bool g_flushFail = false;
uint16_t g_flushCount = 0;
uint16_t g_wifiClearCount = 0;
bool g_configAuditEnabled = false;
bool g_configReadAuditEnabled = false;
uint8_t g_addPageCount = 0;
uint8_t g_addApiCount = 0;
char g_failAddPagePath[32] = "";
char g_failAddApiPath[32] = "";
char g_failSetIntKey[16] = "";
char g_failSetBoolKey[16] = "";
char g_chunkBody[20000] = "";

struct WebParam {
    char name[32];
    char value[128];
};
WebParam g_params[12] = {};
uint8_t g_paramCount = 0;
Esp32BaseWeb::Method g_method = Esp32BaseWeb::METHOD_GET;
int g_lastCode = 0;
char g_lastBody[1024] = "";

int findInt(const char* ns, const char* key) {
    for (uint8_t i = 0; i < 80; ++i) {
        if (g_ints[i].used && strcmp(g_ints[i].ns, ns) == 0 && strcmp(g_ints[i].key, key) == 0) return i;
    }
    return -1;
}

int findBool(const char* ns, const char* key) {
    for (uint8_t i = 0; i < 24; ++i) {
        if (g_bools[i].used && strcmp(g_bools[i].ns, ns) == 0 && strcmp(g_bools[i].key, key) == 0) return i;
    }
    return -1;
}

int findStr(const char* ns, const char* key) {
    for (uint8_t i = 0; i < 12; ++i) {
        if (g_strs[i].used && strcmp(g_strs[i].ns, ns) == 0 && strcmp(g_strs[i].key, key) == 0) return i;
    }
    return -1;
}

void webReset() {
    memset(g_params, 0, sizeof(g_params));
    g_paramCount = 0;
    g_method = Esp32BaseWeb::METHOD_GET;
    g_lastCode = 0;
    g_lastBody[0] = '\0';
    g_chunkBody[0] = '\0';
}

void webSetMethod(Esp32BaseWeb::Method method) {
    g_method = method;
}

void webSetParam(const char* name, const char* value) {
    if (g_paramCount >= 12) return;
    strncpy(g_params[g_paramCount].name, name, sizeof(g_params[g_paramCount].name) - 1);
    strncpy(g_params[g_paramCount].value, value, sizeof(g_params[g_paramCount].value) - 1);
    ++g_paramCount;
}
}

bool Esp32BaseConfig::begin() {
    g_configReady = true;
    return true;
}
void Esp32BaseConfig::handle() {}
bool Esp32BaseConfig::isReady() { return g_configReady; }
bool Esp32BaseConfig::setStr(const char* ns, const char* key, const char* value) {
    int idx = findStr(ns, key);
    if (idx < 0) {
        for (uint8_t i = 0; i < 12; ++i) {
            if (!g_strs[i].used) {
                idx = i;
                g_strs[i].used = true;
                strncpy(g_strs[i].ns, ns, sizeof(g_strs[i].ns) - 1);
                strncpy(g_strs[i].key, key, sizeof(g_strs[i].key) - 1);
                break;
            }
        }
    }
    if (idx < 0) return false;
    strncpy(g_strs[idx].value, value ? value : "", sizeof(g_strs[idx].value) - 1);
    return true;
}
bool Esp32BaseConfig::getStr(const char* ns, const char* key, char* out, size_t len, const char* def) {
    if (!out || len == 0) return false;
    const int idx = findStr(ns, key);
    strncpy(out, idx >= 0 ? g_strs[idx].value : (def ? def : ""), len - 1);
    out[len - 1] = '\0';
    return idx >= 0;
}
bool Esp32BaseConfig::setInt(const char* ns, const char* key, int32_t value) {
    if (strcmp(ns, "fan") == 0 && strcmp(key, g_failSetIntKey) == 0) return false;
    int idx = findInt(ns, key);
    if (idx < 0) {
        for (uint8_t i = 0; i < 80; ++i) {
            if (!g_ints[i].used) {
                idx = i;
                g_ints[i].used = true;
                strncpy(g_ints[i].ns, ns, sizeof(g_ints[i].ns) - 1);
                strncpy(g_ints[i].key, key, sizeof(g_ints[i].key) - 1);
                break;
            }
        }
    }
    if (idx < 0) return false;
    g_ints[idx].value = value;
    return true;
}
int32_t Esp32BaseConfig::getInt(const char* ns, const char* key, int32_t def) {
    const int idx = findInt(ns, key);
    return idx >= 0 ? g_ints[idx].value : def;
}
bool Esp32BaseConfig::setIntDeferred(const char* ns, const char* key, int32_t value, uint32_t) {
    return setInt(ns, key, value);
}
bool Esp32BaseConfig::setBool(const char* ns, const char* key, bool value) {
    if (strcmp(ns, "fan") == 0 && strcmp(key, g_failSetBoolKey) == 0) return false;
    int idx = findBool(ns, key);
    if (idx < 0) {
        for (uint8_t i = 0; i < 24; ++i) {
            if (!g_bools[i].used) {
                idx = i;
                g_bools[i].used = true;
                strncpy(g_bools[i].ns, ns, sizeof(g_bools[i].ns) - 1);
                strncpy(g_bools[i].key, key, sizeof(g_bools[i].key) - 1);
                break;
            }
        }
    }
    if (idx < 0) return false;
    g_bools[idx].value = value;
    return true;
}
bool Esp32BaseConfig::getBool(const char* ns, const char* key, bool def) {
    const int idx = findBool(ns, key);
    return idx >= 0 ? g_bools[idx].value : def;
}
bool Esp32BaseConfig::setBoolDeferred(const char* ns, const char* key, bool value, uint32_t) {
    return setBool(ns, key, value);
}
bool Esp32BaseConfig::setStrDeferred(const char* ns, const char* key, const char* value, uint32_t) {
    return setStr(ns, key, value);
}
bool Esp32BaseConfig::flushAll() {
    ++g_flushCount;
    return !g_flushFail;
}
bool Esp32BaseConfig::clearNamespace(const char* ns) {
    for (auto& item : g_ints) if (item.used && strcmp(item.ns, ns) == 0) item.used = false;
    for (auto& item : g_bools) if (item.used && strcmp(item.ns, ns) == 0) item.used = false;
    for (auto& item : g_strs) if (item.used && strcmp(item.ns, ns) == 0) item.used = false;
    return true;
}
bool Esp32BaseConfig::clearLibraryNamespaces() {
    g_libraryNamespacesCleared = true;
    return true;
}
void Esp32BaseConfig::enableConfigAudit(bool enabled) { g_configAuditEnabled = enabled; }
void Esp32BaseConfig::enableConfigReadAudit(bool enabled) { g_configReadAuditEnabled = enabled; }

bool Esp32BaseWiFi::isConnected() { return g_wifiConnected; }
bool Esp32BaseWiFi::ip(char* out, size_t len) {
    if (!out || len == 0) return false;
    strncpy(out, "192.168.4.10", len - 1);
    out[len - 1] = '\0';
    return true;
}
int32_t Esp32BaseWiFi::rssi() { return -61; }
void Esp32BaseWiFi::setPowerSave(bool enabled) { g_wifiPowerSave = enabled; }
bool Esp32BaseWiFi::powerSave() { return g_wifiPowerSave; }
bool Esp32BaseWiFi::clearCredentials() {
    ++g_wifiClearCount;
    return true;
}
Esp32BaseWiFi::State Esp32BaseWiFi::state() { return CONNECTED; }
const char* Esp32BaseWiFi::stateName() { return "CONNECTED"; }

bool Esp32BaseNtp::isTimeSynced() { return true; }
bool Esp32BaseNtp::formatTime(char* out, size_t len, const char*) {
    if (!out || len == 0) return false;
    strncpy(out, "2026-05-07 12:00:00", len - 1);
    out[len - 1] = '\0';
    return true;
}

bool Esp32BaseWeb::checkAuth() { return true; }
void Esp32BaseWeb::setDefaultAuth(const char* user, const char* pass) {
    strncpy(g_authUser, user ? user : "", sizeof(g_authUser) - 1);
    strncpy(g_authPass, pass ? pass : "", sizeof(g_authPass) - 1);
}
const char* Esp32BaseWeb::authUser() { return g_authUser; }
bool Esp32BaseWeb::isAuthEnabled() { return g_authEnabled; }
void Esp32BaseWeb::setAuthEnabled(bool enabled) { g_authEnabled = enabled; }
bool Esp32BaseWeb::verifyAuth() { return true; }
bool Esp32BaseWeb::verifyAuth(const char* user, const char* pass) {
    return strcmp(g_authUser, user ? user : "") == 0 && strcmp(g_authPass, pass ? pass : "") == 0;
}
bool Esp32BaseWeb::saveAuth(const char* user, const char* pass) {
    setDefaultAuth(user, pass);
    return true;
}
bool Esp32BaseWeb::resetAuth() { return true; }
bool Esp32BaseWeb::addPage(const char* path, const char*, Handler) {
    ++g_addPageCount;
    return strcmp(path ? path : "", g_failAddPagePath) != 0;
}
bool Esp32BaseWeb::addApi(const char* path, Handler) {
    ++g_addApiCount;
    return strcmp(path ? path : "", g_failAddApiPath) != 0;
}
bool Esp32BaseWeb::addNavItem(const char*, const char*) { return true; }
bool Esp32BaseWeb::setDeviceName(const char*) { return true; }
bool Esp32BaseWeb::setHomePath(const char*) { return true; }
void Esp32BaseWeb::setHomeMode(HomeMode) {}
void Esp32BaseWeb::setSystemNavMode(SystemNavMode) {}
bool Esp32BaseWeb::setBuiltinLabel(BuiltinPage, const char*) { return true; }
Esp32BaseWeb::Method Esp32BaseWeb::currentMethod() { return g_method; }
bool Esp32BaseWeb::isMethod(Method method) { return method == METHOD_ANY || g_method == method; }
const char* Esp32BaseWeb::currentMethodName() { return g_method == METHOD_POST ? "POST" : "GET"; }
bool Esp32BaseWeb::hasParam(const char* name) {
    for (uint8_t i = 0; i < g_paramCount; ++i) {
        if (strcmp(g_params[i].name, name) == 0) return true;
    }
    return false;
}
bool Esp32BaseWeb::getParam(const char* name, char* out, size_t len) {
    if (!out || len == 0) return false;
    for (uint8_t i = 0; i < g_paramCount; ++i) {
        if (strcmp(g_params[i].name, name) == 0) {
            strncpy(out, g_params[i].value, len - 1);
            out[len - 1] = '\0';
            return true;
        }
    }
    return false;
}
void Esp32BaseWeb::sendHeader(const char*) {}
void Esp32BaseWeb::sendFooter() {}
void Esp32BaseWeb::sendChunk(const char* text) {
    if (!text) return;
    const size_t used = strlen(g_chunkBody);
    const size_t available = sizeof(g_chunkBody) - used - 1;
    if (available == 0) return;
    strncat(g_chunkBody, text, available);
}
void Esp32BaseWeb::writeHtmlEscaped(const char*) {}
void Esp32BaseWeb::sendText(int code, const char* text) { sendJson(code, text ? text : ""); }
void Esp32BaseWeb::sendHtml(int code, const char* html) { sendJson(code, html ? html : ""); }
void Esp32BaseWeb::sendJson(int code, const char* json) {
    g_lastCode = code;
    strncpy(g_lastBody, json ? json : "", sizeof(g_lastBody) - 1);
    g_lastBody[sizeof(g_lastBody) - 1] = '\0';
}

#include "fan/ButtonDriver.h"
#include "fan/FanController.h"
#include "fan/FanDriver.h"
#include "fan/IRReceiverDriver.h"
#include "fan/LedIndicator.h"
#include "web/FanWeb.h"
#include "app/FanAppRuntime.h"

void setUp() {
    g_mockMillis = 0;
    memset(g_pinMode, 0, sizeof(g_pinMode));
    memset(g_pinState, HIGH, sizeof(g_pinState));
    memset(g_pwmValue, 0, sizeof(g_pwmValue));
    memset(g_isr, 0, sizeof(g_isr));
    memset(g_ints, 0, sizeof(g_ints));
    memset(g_bools, 0, sizeof(g_bools));
    memset(g_strs, 0, sizeof(g_strs));
    g_restartCalled = false;
    g_configReady = false;
    g_wifiConnected = true;
    g_wifiPowerSave = false;
    g_libraryNamespacesCleared = false;
    strcpy(g_authUser, "admin");
    strcpy(g_authPass, "admin");
    g_authEnabled = true;
    g_flushFail = false;
    g_flushCount = 0;
    g_wifiClearCount = 0;
    g_configAuditEnabled = false;
    g_configReadAuditEnabled = false;
    g_addPageCount = 0;
    g_addApiCount = 0;
    g_failAddPagePath[0] = '\0';
    g_failAddApiPath[0] = '\0';
    g_failSetIntKey[0] = '\0';
    g_failSetBoolKey[0] = '\0';
    webReset();
}

void tearDown() {}

static void makeController(FanDriver& fan, ButtonDriver& btn, LedIndicator& led,
                           IRReceiverDriver& ir, FanController& controller) {
    (void)fan;
    (void)btn;
    (void)led;
    (void)ir;
    TEST_ASSERT_TRUE(controller.begin());
}

void test_fan_driver_begin_and_soft_start() {
    FanDriver fan(5, 12);
    TEST_ASSERT_TRUE(fan.begin());
    TEST_ASSERT_EQUAL(OUTPUT, g_pinMode[5]);
    TEST_ASSERT_EQUAL(INPUT_PULLUP, g_pinMode[12]);
    TEST_ASSERT_EQUAL(FAN_STATE_IDLE, fan.getState());

    TEST_ASSERT_TRUE(fan.setSpeed(50));
    TEST_ASSERT_EQUAL(FAN_STATE_SOFT_START, fan.getState());
    g_mockMillis = 1200;
    fan.tick();
    TEST_ASSERT_EQUAL(50, fan.getSpeed());
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan.getState());
}

void test_fan_driver_rpm_and_block_detection() {
    FanDriver fan(5, 12);
    fan.begin();
    fan.setSoftStartTime(0);
    fan.setBlockDetectTime(1000);
    TEST_ASSERT_TRUE(fan.setSpeed(50));
    TEST_ASSERT_EQUAL(FAN_STATE_RUNNING, fan.getState());

    for (uint8_t i = 0; i < 2; ++i) {
        g_isr[12]();
    }
    g_mockMillis = 500;
    fan.tick();
    TEST_ASSERT_EQUAL(120, fan.getRpm());

    g_mockMillis = 1100;
    fan.tick();
    g_mockMillis = 2200;
    fan.tick();
    TEST_ASSERT_TRUE(fan.isBlocked());
    TEST_ASSERT_EQUAL(0, fan.getSpeed());
}

void test_button_driver_short_press_and_both_long() {
    ButtonDriver buttons(14, 4);
    TEST_ASSERT_TRUE(buttons.begin());

    g_pinState[14] = LOW;
    buttons.getEvent();
    g_mockMillis = 60;
    TEST_ASSERT_EQUAL(BTN_NONE, buttons.getEvent());
    g_pinState[14] = HIGH;
    buttons.getEvent();
    g_mockMillis = 130;
    TEST_ASSERT_EQUAL(BTN_ACCEL_SHORT, buttons.getEvent());

    g_pinState[4] = LOW;
    buttons.getEvent();
    g_mockMillis = 200;
    buttons.getEvent();
    g_pinState[4] = HIGH;
    buttons.getEvent();
    g_mockMillis = 270;
    TEST_ASSERT_EQUAL(BTN_DECEL_SHORT, buttons.getEvent());

    g_pinState[14] = LOW;
    g_pinState[4] = LOW;
    buttons.getEvent();
    g_mockMillis = 340;
    buttons.getEvent();
    g_mockMillis = 5400;
    TEST_ASSERT_EQUAL(BTN_BOTH_LONG, buttons.getEvent());
}

void test_controller_factory_reset_clears_app_and_library_namespaces() {
    Esp32BaseConfig::setInt("fan", "min_spd", 20);
    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    makeController(fan, buttons, led, ir, controller);

    TEST_ASSERT_TRUE(controller.resetFactory());
    TEST_ASSERT_TRUE(ESP.restartCalled());
    TEST_ASSERT_EQUAL(10, Esp32BaseConfig::getInt("fan", "min_spd", 10));
    TEST_ASSERT_TRUE(g_libraryNamespacesCleared);
}

void test_app_runtime_registers_routes_and_enables_audit() {
    fanAppConfigureBaseWebBeforeBegin();
    TEST_ASSERT_EQUAL_STRING("admin", Esp32BaseWeb::authUser());

    TEST_ASSERT_TRUE(fanAppRegisterFanRoutes());
    TEST_ASSERT_EQUAL(2, g_addPageCount);
    TEST_ASSERT_EQUAL(6, g_addApiCount);

    fanAppEnableConfigAuditBeforeBegin();
    TEST_ASSERT_TRUE(g_configAuditEnabled);
    TEST_ASSERT_TRUE(g_configReadAuditEnabled);
}

void test_app_runtime_reports_route_registration_failure() {
    strcpy(g_failAddApiPath, "/api/stop");

    TEST_ASSERT_FALSE(fanAppRegisterFanRoutes());
    TEST_ASSERT_EQUAL(2, g_addPageCount);
    TEST_ASSERT_EQUAL(6, g_addApiCount);
}

void test_app_runtime_boot_button_clear_wifi_timing() {
    BootClearState state = {};
    const uint8_t bootPin = 0;

    g_pinState[bootPin] = LOW;
    fanAppHandleBootButton(bootPin, &state);
    TEST_ASSERT_FALSE(state.armed);
    TEST_ASSERT_EQUAL(0, g_wifiClearCount);

    g_pinState[bootPin] = HIGH;
    fanAppHandleBootButton(bootPin, &state);
    TEST_ASSERT_TRUE(state.armed);

    g_mockMillis = 10;
    g_pinState[bootPin] = LOW;
    fanAppHandleBootButton(bootPin, &state);
    TEST_ASSERT_EQUAL(0, g_wifiClearCount);
    TEST_ASSERT_FALSE(ESP.restartCalled());

    g_mockMillis = 5009;
    fanAppHandleBootButton(bootPin, &state);
    TEST_ASSERT_EQUAL(0, g_wifiClearCount);

    g_mockMillis = 5010;
    fanAppHandleBootButton(bootPin, &state);
    TEST_ASSERT_EQUAL(1, g_wifiClearCount);
    TEST_ASSERT_GREATER_THAN_UINT16(0, g_flushCount);
    TEST_ASSERT_TRUE(ESP.restartCalled());

    g_mockMillis = 9000;
    fanAppHandleBootButton(bootPin, &state);
    TEST_ASSERT_EQUAL(1, g_wifiClearCount);
}

void test_controller_config_timer_restore_and_sleep() {
    Esp32BaseConfig::setInt("fan", "min_spd", 15);
    Esp32BaseConfig::setInt("fan", "soft_on", 0);
    Esp32BaseConfig::setInt("fan", "blk_ms", 60000);
    Esp32BaseConfig::setInt("fan", "last_spd", 50);
    Esp32BaseConfig::setInt("fan", "last_tim", 120);
    Esp32BaseConfig::setBool("fan", "restore", true);

    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    makeController(fan, buttons, led, ir, controller);

    TEST_ASSERT_EQUAL(15, controller.getMinEffectiveSpeed());
    TEST_ASSERT_EQUAL(50, controller.getCurrentSpeed());
    TEST_ASSERT_EQUAL(120, controller.getTimerRemaining());
    TEST_ASSERT_EQUAL(SYS_RUNNING, controller.getState());

    controller.setTimer(2);
    g_mockMillis = 1000;
    controller.tick();
    TEST_ASSERT_EQUAL(1, controller.getTimerRemaining());
    g_mockMillis = 2000;
    controller.tick();
    TEST_ASSERT_EQUAL(0, controller.getTimerRemaining());

    controller.setSleepWaitTime(1);
    controller.setSoftStopTime(0);
    controller.stop();
    g_mockMillis = 4000;
    controller.tick();
    g_mockMillis = 5100;
    controller.tick();
    TEST_ASSERT_EQUAL(SYS_SLEEP, controller.getState());
    TEST_ASSERT_TRUE(Esp32BaseWiFi::powerSave());
}

void test_controller_force_save_flushes_runtime_state() {
    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    makeController(fan, buttons, led, ir, controller);
    controller.setSoftStartTime(0);
    g_flushCount = 0;

    TEST_ASSERT_TRUE(controller.setSpeed(40));
    TEST_ASSERT_GREATER_THAN_UINT16(0, g_flushCount);
    TEST_ASSERT_EQUAL(40, Esp32BaseConfig::getInt("fan", "last_spd", 0));

    g_flushCount = 0;
    TEST_ASSERT_TRUE(controller.stop());
    TEST_ASSERT_GREATER_THAN_UINT16(0, g_flushCount);
    TEST_ASSERT_EQUAL(0, Esp32BaseConfig::getInt("fan", "last_spd", 99));
}

void test_controller_flush_failure_returns_false_and_rolls_back_visible_state() {
    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    makeController(fan, buttons, led, ir, controller);
    controller.setSoftStartTime(0);
    controller.setBlockDetectTime(60000);

    g_flushFail = true;
    TEST_ASSERT_FALSE(controller.setSpeed(40));
    TEST_ASSERT_EQUAL(0, controller.getTargetSpeed());
    TEST_ASSERT_EQUAL(0, controller.getCurrentGear());

    TEST_ASSERT_FALSE(controller.setTimer(60));
    TEST_ASSERT_EQUAL(0, controller.getTimerRemaining());

    g_flushFail = false;
    TEST_ASSERT_TRUE(controller.setSpeed(40));
    g_flushFail = true;
    TEST_ASSERT_FALSE(controller.stop());
    TEST_ASSERT_EQUAL(40, controller.getTargetSpeed());
    TEST_ASSERT_EQUAL(2, controller.getCurrentGear());
}

void test_controller_auto_restore_enable_saves_current_state() {
    Esp32BaseConfig::setBool("fan", "restore", false);
    Esp32BaseConfig::setInt("fan", "soft_on", 0);
    Esp32BaseConfig::setInt("fan", "blk_ms", 60000);

    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    makeController(fan, buttons, led, ir, controller);
    TEST_ASSERT_FALSE(controller.getAutoRestore());

    TEST_ASSERT_TRUE(controller.setSpeed(35));
    TEST_ASSERT_EQUAL(0, Esp32BaseConfig::getInt("fan", "last_spd", 0));
    g_flushCount = 0;

    TEST_ASSERT_TRUE(controller.setAutoRestore(true));
    TEST_ASSERT_TRUE(controller.getAutoRestore());
    TEST_ASSERT_GREATER_THAN_UINT16(0, g_flushCount);
    TEST_ASSERT_EQUAL(35, Esp32BaseConfig::getInt("fan", "last_spd", 0));
}

void test_controller_min_speed_change_reapplies_low_target() {
    Esp32BaseConfig::setInt("fan", "soft_on", 0);
    Esp32BaseConfig::setInt("fan", "blk_ms", 60000);

    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    makeController(fan, buttons, led, ir, controller);

    TEST_ASSERT_TRUE(controller.setSpeed(12));
    TEST_ASSERT_EQUAL(12, controller.getTargetSpeed());
    TEST_ASSERT_TRUE(controller.setMinEffectiveSpeed(25));
    TEST_ASSERT_EQUAL(25, controller.getTargetSpeed());
    TEST_ASSERT_EQUAL(25, controller.getCurrentSpeed());
}

void test_controller_ignores_negative_persisted_values() {
    Esp32BaseConfig::setInt("fan", "min_spd", -1);
    Esp32BaseConfig::setInt("fan", "soft_on", 0);
    Esp32BaseConfig::setInt("fan", "blk_ms", 60000);
    Esp32BaseConfig::setInt("fan", "last_spd", 150);
    Esp32BaseConfig::setInt("fan", "last_tim", -20);
    Esp32BaseConfig::setInt("fan", "run_s", -42);
    Esp32BaseConfig::setBool("fan", "restore", true);

    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    makeController(fan, buttons, led, ir, controller);

    TEST_ASSERT_EQUAL(10, controller.getMinEffectiveSpeed());
    TEST_ASSERT_EQUAL(100, controller.getTargetSpeed());
    TEST_ASSERT_EQUAL(0, controller.getTimerRemaining());
    TEST_ASSERT_EQUAL(0, controller.getTotalRunDuration());
}

void test_controller_error_recovery_window_not_reset_by_repeated_speed() {
    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    makeController(fan, buttons, led, ir, controller);
    controller.setSoftStartTime(0);
    controller.setBlockDetectTime(100);

    TEST_ASSERT_TRUE(controller.setSpeed(50));
    g_mockMillis = 200;
    controller.tick();
    g_mockMillis = 350;
    controller.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, controller.getState());

    TEST_ASSERT_TRUE(controller.setSpeed(50));
    TEST_ASSERT_EQUAL(SYS_RECOVERING, controller.getState());
    g_mockMillis = 450;
    TEST_ASSERT_TRUE(controller.setSpeed(60));
    TEST_ASSERT_EQUAL(SYS_RECOVERING, controller.getState());

    g_mockMillis = 1000;
    controller.tick();
    TEST_ASSERT_EQUAL(SYS_ERROR, controller.getState());
}

void test_web_api_speed_timer_config_and_ir() {
    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    FanWeb web(controller, ir);
    (void)web;
    makeController(fan, buttons, led, ir, controller);
    controller.setSoftStartTime(0);
    controller.setBlockDetectTime(60000);

    webSetMethod(Esp32BaseWeb::METHOD_POST);
    webSetParam("speed", "75");
    FanWeb::handleApiSpeed();
    TEST_ASSERT_EQUAL(200, g_lastCode);
    TEST_ASSERT_EQUAL(75, controller.getTargetSpeed());
    TEST_ASSERT_NOT_NULL(strstr(g_lastBody, "\"target_speed\":75"));

    webReset();
    webSetMethod(Esp32BaseWeb::METHOD_POST);
    webSetParam("seconds", "7200");
    FanWeb::handleApiTimer();
    TEST_ASSERT_EQUAL(200, g_lastCode);
    TEST_ASSERT_EQUAL(7200, controller.getTimerRemaining());

    webReset();
    webSetMethod(Esp32BaseWeb::METHOD_POST);
    webSetParam("min_speed", "22");
    FanWeb::handleApiConfig();
    TEST_ASSERT_EQUAL(200, g_lastCode);
    TEST_ASSERT_EQUAL(22, controller.getMinEffectiveSpeed());
    TEST_ASSERT_NOT_NULL(strstr(g_lastBody, "\"changed\":1"));

    webReset();
    webSetMethod(Esp32BaseWeb::METHOD_POST);
    webSetParam("key_index", "2");
    FanWeb::handleApiIrLearn();
    TEST_ASSERT_EQUAL(200, g_lastCode);
    TEST_ASSERT_TRUE(ir.isLearning());
    TEST_ASSERT_EQUAL(2, ir.getLearnedKeyIndex());
}

void test_web_config_write_failure_returns_error_without_applying() {
    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    FanWeb web(controller, ir);
    (void)web;
    makeController(fan, buttons, led, ir, controller);
    strcpy(g_failSetIntKey, "min_spd");

    webSetMethod(Esp32BaseWeb::METHOD_POST);
    webSetParam("min_speed", "22");
    FanWeb::handleApiConfig();

    TEST_ASSERT_EQUAL(500, g_lastCode);
    TEST_ASSERT_EQUAL(10, controller.getMinEffectiveSpeed());
    TEST_ASSERT_NOT_NULL(strstr(g_lastBody, "\"ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(g_lastBody, "\"changed\":0"));
}

void test_web_runtime_save_failure_returns_error() {
    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    FanWeb web(controller, ir);
    (void)web;
    makeController(fan, buttons, led, ir, controller);
    controller.setSoftStartTime(0);
    controller.setBlockDetectTime(60000);

    g_flushFail = true;
    webSetMethod(Esp32BaseWeb::METHOD_POST);
    webSetParam("seconds", "60");
    FanWeb::handleApiTimer();
    TEST_ASSERT_EQUAL(500, g_lastCode);
    TEST_ASSERT_EQUAL(0, controller.getTimerRemaining());
    TEST_ASSERT_NOT_NULL(strstr(g_lastBody, "\"ok\":false"));

    g_flushFail = false;
    TEST_ASSERT_TRUE(controller.setSpeed(40));
    g_flushFail = true;
    webReset();
    webSetMethod(Esp32BaseWeb::METHOD_POST);
    FanWeb::handleApiStop();
    TEST_ASSERT_EQUAL(500, g_lastCode);
    TEST_ASSERT_EQUAL(40, controller.getTargetSpeed());
    TEST_ASSERT_NOT_NULL(strstr(g_lastBody, "\"ok\":false"));
}

void test_web_pages_emit_html_chunks() {
    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    FanWeb web(controller, ir);
    (void)web;
    makeController(fan, buttons, led, ir, controller);

    FanWeb::handleStatusPage();
    TEST_ASSERT_NOT_NULL(strstr(g_chunkBody, "id=outTop"));
    TEST_ASSERT_NOT_NULL(strstr(g_chunkBody, "id=sv"));

    webReset();
    FanWeb::handleConfigPage();
    TEST_ASSERT_NOT_NULL(strstr(g_chunkBody, "name=min_speed"));
    TEST_ASSERT_NOT_NULL(strstr(g_chunkBody, "fetch('/api/config'"));
}

void test_web_api_status() {
    FanDriver fan(5, 12);
    ButtonDriver buttons(14, 4);
    LedIndicator led(2, true);
    IRReceiverDriver ir(13);
    FanController controller(fan, buttons, led, ir);
    FanWeb web(controller, ir);
    (void)web;
    makeController(fan, buttons, led, ir, controller);

    FanWeb::handleApiStatus();
    TEST_ASSERT_EQUAL(200, g_lastCode);
    TEST_ASSERT_NOT_NULL(strstr(g_lastBody, "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(g_lastBody, "\"ip\":\"192.168.4.10\""));
    TEST_ASSERT_NOT_NULL(strstr(g_lastBody, "\"clock\":\"2026-05-07 12:00:00\""));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fan_driver_begin_and_soft_start);
    RUN_TEST(test_fan_driver_rpm_and_block_detection);
    RUN_TEST(test_button_driver_short_press_and_both_long);
    RUN_TEST(test_app_runtime_registers_routes_and_enables_audit);
    RUN_TEST(test_app_runtime_reports_route_registration_failure);
    RUN_TEST(test_app_runtime_boot_button_clear_wifi_timing);
    RUN_TEST(test_controller_config_timer_restore_and_sleep);
    RUN_TEST(test_controller_force_save_flushes_runtime_state);
    RUN_TEST(test_controller_flush_failure_returns_false_and_rolls_back_visible_state);
    RUN_TEST(test_controller_auto_restore_enable_saves_current_state);
    RUN_TEST(test_controller_min_speed_change_reapplies_low_target);
    RUN_TEST(test_controller_ignores_negative_persisted_values);
    RUN_TEST(test_controller_error_recovery_window_not_reset_by_repeated_speed);
    RUN_TEST(test_controller_factory_reset_clears_app_and_library_namespaces);
    RUN_TEST(test_web_api_speed_timer_config_and_ir);
    RUN_TEST(test_web_config_write_failure_returns_error_without_applying);
    RUN_TEST(test_web_runtime_save_failure_returns_error);
    RUN_TEST(test_web_pages_emit_html_chunks);
    RUN_TEST(test_web_api_status);
    return UNITY_END();
}
