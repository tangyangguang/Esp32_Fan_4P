#include "fan/FanController.h"

#ifdef UNIT_TEST
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "Arduino.h"
#include <Esp32Base.h>
#else
#include <Arduino.h>
#include <Esp32Base.h>
#endif

namespace {
const char* CFG_NS = "fan";
const char* KEY_MIN_SPEED = "min_spd";
const char* KEY_SOFT_START = "soft_on";
const char* KEY_SOFT_STOP = "soft_off";
const char* KEY_BLOCK_DETECT = "blk_ms";
const char* KEY_SLEEP_WAIT = "slp_s";
const char* KEY_AUTO_RESTORE = "restore";
const char* KEY_WEB_PASSWORD = "web_pass";
const char* KEY_LAST_SPEED = "last_spd";
const char* KEY_LAST_TIMER = "last_tim";
const char* KEY_RUN_DURATION = "run_s";
const char* KEY_IR_PROTO = "ir_p";
const char* KEY_IR_CODE_LOW = "ir_l";
const char* KEY_IR_CODE_HIGH = "ir_h";

// Gear to speed mapping
const uint8_t GEAR_SPEED[5] = {0, 25, 50, 75, 100};

bool cfgSetInt(const char* key, int32_t value) { return Esp32BaseConfig::setInt(CFG_NS, key, value); }
int32_t cfgGetInt(const char* key, int32_t def) { return Esp32BaseConfig::getInt(CFG_NS, key, def); }
bool cfgSetBool(const char* key, bool value) { return Esp32BaseConfig::setBool(CFG_NS, key, value); }
bool cfgGetBool(const char* key, bool def) { return Esp32BaseConfig::getBool(CFG_NS, key, def); }
bool cfgSetStr(const char* key, const char* value) { return Esp32BaseConfig::setStr(CFG_NS, key, value); }
bool cfgGetStr(const char* key, char* out, size_t len, const char* def) {
    return Esp32BaseConfig::getStr(CFG_NS, key, out, len, def);
}
bool cfgSetIntDeferred(const char* key, int32_t value) {
    return Esp32BaseConfig::setIntDeferred(CFG_NS, key, value);
}
}

FanController::FanController(FanDriver& fan, ButtonDriver& btn, LedIndicator& led, IRReceiverDriver& ir)
    : _fan(fan)
    , _btn(btn)
    , _led(led)
    , _ir(ir)
    , _state(SYS_INIT)
    , _current_gear(0)
    , _target_speed(0)
    , _timer_remaining(0)
    , _run_duration(0)
    , _last_run_tick(0)
    , _last_operation_tick(0)
    , _sleep_wait_time(60)
    , _soft_start_time(1000)
    , _soft_stop_time(1000)
    , _block_detect_time(1500)
    , _min_effective_speed(10)
    , _is_sleeping(false)
    , _sleep_entry_tick(0)
    , _auto_restore(true)
    , _recovery_start_tick(0)
    , _recovery_attempting(false)
    , _last_timer_tick(0) {
}

bool FanController::begin() {
    _fan.begin();
    _btn.begin();
    _led.begin();
    _ir.begin();

    _loadConfig();  // also applies soft-start/stop/block-detect to _fan

    _state = SYS_IDLE;
    _last_operation_tick = millis();

    // Power-on restore
    if (_auto_restore) {
        int32_t last_speed = cfgGetInt(KEY_LAST_SPEED, 0);
        int32_t last_timer = cfgGetInt(KEY_LAST_TIMER, 0);

        if (last_speed > 0) {
            ESP32BASE_LOG_I("FanCtrl", "Restoring last state: speed=%d%%, timer=%lds",
                      static_cast<int>(last_speed), static_cast<long>(last_timer));
            _timer_remaining = static_cast<uint32_t>(last_timer);
            _last_timer_tick = millis();
            // Calculate gear from speed
            for (uint8_t g = 0; g < 5; g++) {
                if (GEAR_SPEED[g] == last_speed) {
                    _current_gear = g;
                    break;
                }
            }
            _applySpeed(static_cast<uint8_t>(last_speed));
        } else {
            ESP32BASE_LOG_I("FanCtrl", "No restore needed (last_speed=0)");
        }
    } else {
        ESP32BASE_LOG_I("FanCtrl", "Auto-restore disabled");
    }

    // Set initial LED state
    _led.setGear(_current_gear);

    ESP32BASE_LOG_I("FanCtrl", "Controller ready, min_speed=%d%%, auto_restore=%s",
             _min_effective_speed, _auto_restore ? "true" : "false");
    return true;
}

void FanController::tick() {
    if (_state >= SYS_COUNT) {
        _state = SYS_ERROR;
    }

    // Advance fan state machine (soft start/stop, block detection, RPM)
    _fan.tick();

    switch (_state) {
        case SYS_INIT:
            _handleInit();
            break;
        case SYS_IDLE:
            _handleIdle();
            break;
        case SYS_RUNNING:
            _handleRunning();
            break;
        case SYS_SLEEP:
            _handleSleep();
            break;
        case SYS_ERROR:
            _handleError();
            break;
        default:
            _state = SYS_ERROR;
            break;
    }

    // Update LED every tick
    _led.tick();

    // Feed watchdog to prevent Soft WDT reset during heavy operations (e.g. Flash writes)
    yield();
}

SystemState FanController::getState() const { return _state; }
uint8_t FanController::getCurrentGear() const { return _current_gear; }
uint8_t FanController::getCurrentSpeed() const { return _fan.getSpeed(); }
uint8_t FanController::getTargetSpeed() const { return _target_speed; }
uint32_t FanController::getTimerRemaining() const { return _timer_remaining; }
uint32_t FanController::getTotalRunDuration() const { return _run_duration; }
bool FanController::isBlocked() const { return _fan.isBlocked(); }
bool FanController::isSleeping() const { return _is_sleeping; }
bool FanController::getAutoRestore() const { return _auto_restore; }

void FanController::setAutoRestore(bool enable) {
    _auto_restore = enable;
    cfgSetBool(KEY_AUTO_RESTORE, _auto_restore);
}

void FanController::setWebPassword(const char* password) {
    cfgSetStr(KEY_WEB_PASSWORD, password);
    Esp32BaseWeb::setAuth("admin", password);
    ESP32BASE_LOG_I("FanCtrl", "Web password updated");
}

void FanController::attemptBlockRecovery() {
    if (_state != SYS_ERROR) return;

    _fan.resetBlock();
    _recovery_attempting = true;
    _recovery_start_tick = millis();

    if (_target_speed > 0) {
        _applySpeed(_target_speed);
        ESP32BASE_LOG_I("FanCtrl", "Block recovery attempt started (1.5s window)");
    }
}

bool FanController::setSpeed(uint8_t speed) {
    _last_operation_tick = millis();
    _is_sleeping = false;
    Esp32BaseWiFi::setPowerSave(false);

    if (speed > 0 && speed < _min_effective_speed) {
        speed = _min_effective_speed;
    }

    _target_speed = speed;
    if (_state == SYS_ERROR) {
        _fan.resetBlock();
        _recovery_attempting = false;
        _led.setOverride(LED_OFF);
        _state = SYS_IDLE;
    }

    _applySpeed(speed);
    ESP32BASE_LOG_I("FanCtrl", "Speed requested: target=%u output=%u state=%u",
                      (unsigned)_target_speed, (unsigned)_fan.getSpeed(), (unsigned)_state);
    return true;
}

bool FanController::setTimer(uint32_t seconds) {
    if (seconds > 356400UL) return false;

    _timer_remaining = seconds;
    _last_timer_tick = millis();
    _last_operation_tick = millis();
    _is_sleeping = false;
    Esp32BaseWiFi::setPowerSave(false);

    if (seconds > 0) {
        ESP32BASE_LOG_I("FanCtrl", "Timer set: %lu seconds", static_cast<unsigned long>(seconds));
    } else {
        ESP32BASE_LOG_I("FanCtrl", "Timer cancelled");
    }
    return true;
}

bool FanController::stop() {
    _last_operation_tick = millis();
    _is_sleeping = false;
    Esp32BaseWiFi::setPowerSave(false);
    _timer_remaining = 0;
    _target_speed = 0;
    _applySpeed(0);
    ESP32BASE_LOG_I("FanCtrl", "Fan stopped by request");
    return true;
}

bool FanController::resetFactory() {
    if (!Esp32BaseConfig::clearNamespace(CFG_NS)) {
        ESP32BASE_LOG_E("FanCtrl", "Factory reset failed: app namespace clear failed");
    }
    ESP.restart();
    return true;
}

uint8_t FanController::getMinEffectiveSpeed() const { return _min_effective_speed; }

void FanController::setMinEffectiveSpeed(uint8_t speed) {
    if (speed > 50) speed = 50;
    _min_effective_speed = speed;
    cfgSetInt(KEY_MIN_SPEED, _min_effective_speed);
}

uint16_t FanController::getSoftStartTime() const {
    return _soft_start_time;
}

void FanController::setSoftStartTime(uint16_t ms) {
    _soft_start_time = ms;
    cfgSetInt(KEY_SOFT_START, static_cast<int32_t>(ms));
    _fan.setSoftStartTime(ms);
}

uint16_t FanController::getSoftStopTime() const {
    return _soft_stop_time;
}

void FanController::setSoftStopTime(uint16_t ms) {
    _soft_stop_time = ms;
    cfgSetInt(KEY_SOFT_STOP, static_cast<int32_t>(ms));
    _fan.setSoftStopTime(ms);
}

uint16_t FanController::getBlockDetectTime() const {
    return _block_detect_time;
}

void FanController::setBlockDetectTime(uint16_t ms) {
    _block_detect_time = ms;
    cfgSetInt(KEY_BLOCK_DETECT, static_cast<int32_t>(ms));
    _fan.setBlockDetectTime(ms);
}

uint16_t FanController::getSleepWaitTime() const { return _sleep_wait_time; }

void FanController::setSleepWaitTime(uint16_t seconds) {
    _sleep_wait_time = seconds;
    cfgSetInt(KEY_SLEEP_WAIT, _sleep_wait_time);
}

void FanController::_handleInit() { _state = SYS_IDLE; }

void FanController::_handleIdle() {
    _processButtonEvents();
    _processIREvents();
    _processSleep();
}

void FanController::_handleRunning() {
    _processButtonEvents();
    _processIREvents();
    _processTimer();

    uint32_t now = millis();
    if (now - _last_run_tick >= 1000) {
        _run_duration++;
        _last_run_tick = now;
    }

    if (_fan.isBlocked()) {
        _state = SYS_ERROR;
        ESP32BASE_LOG_E("FanCtrl", "BLOCK DETECTED! Transitioning to ERROR state");
        _led.setOverride(LED_FAST_BLINK);
        return;
    }

    if (_fan.getSpeed() == 0 && _fan.getState() == FAN_STATE_IDLE) {
        _state = SYS_IDLE;
        ESP32BASE_LOG_I("FanCtrl", "Fan stopped, transitioning to IDLE");
    }
}

void FanController::_handleSleep() {
    _processButtonEvents();
    _processIREvents();

    if (millis() - _last_operation_tick < 1000) {
        _is_sleeping = false;
        Esp32BaseWiFi::setPowerSave(false);
        _state = SYS_IDLE;
        ESP32BASE_LOG_I("FanCtrl", "Woken from sleep, transitioning to IDLE");
    }
}

void FanController::_handleError() {
    _processButtonEvents();
    _processIREvents();

    // Check recovery progress
    if (_recovery_attempting) {
        uint32_t elapsed = millis() - _recovery_start_tick;
        if (elapsed >= 1500) {  // 1.5s recovery window
            _recovery_attempting = false;
            if (_fan.getRpm() > 0) {
                _state = SYS_RUNNING;
                ESP32BASE_LOG_I("FanCtrl", "Recovery successful, back to RUNNING");
            } else {
                ESP32BASE_LOG_W("FanCtrl", "Recovery failed, still blocked");
                _fan.resetBlock();
            }
        }
    }
}

void FanController::_processButtonEvents() {
    ButtonEvent event = _btn.getEvent();
    if (event == BTN_NONE) return;

    switch (event) {
        case BTN_ACCEL_SHORT:
            if (_current_gear < 4) {
                _current_gear++;
                uint8_t speed = GEAR_SPEED[_current_gear];
                setSpeed(speed);
                _led.setGear(_current_gear);
                ESP32BASE_LOG_I("FanCtrl", "Button: Gear up to %d (%d%%)", _current_gear, speed);
            }
            _led.flashOnce();
            break;

        case BTN_DECEL_SHORT:
            if (_current_gear > 0) {
                _current_gear--;
                uint8_t speed = GEAR_SPEED[_current_gear];
                setSpeed(speed);
                _led.setGear(_current_gear);
                ESP32BASE_LOG_I("FanCtrl", "Button: Gear down to %d (%d%%)", _current_gear, speed);
            }
            _led.flashOnce();
            break;

        case BTN_BOTH_LONG:
            ESP32BASE_LOG_I("FanCtrl", "Both buttons long press -> factory reset");
            resetFactory();
            break;

        default:
            break;
    }
}

void FanController::_processIREvents() {
    IREvent event = _ir.getEvent();
    if (_ir.consumeLearnedCode()) {
        _saveIRCodes();
        ESP32BASE_LOG_I("FanCtrl", "IR learned code persisted");
    }
    if (event == IR_EVENT_NONE) return;

    switch (event) {
        case IR_EVENT_SPEED_UP:
            if (_current_gear < 4) {
                _current_gear++;
                uint8_t speed = GEAR_SPEED[_current_gear];
                setSpeed(speed);
                _led.setGear(_current_gear);
                ESP32BASE_LOG_I("FanCtrl", "IR: Gear up to %d (%d%%)", _current_gear, speed);
            }
            break;

        case IR_EVENT_SPEED_DOWN:
            if (_current_gear > 0) {
                _current_gear--;
                uint8_t speed = GEAR_SPEED[_current_gear];
                setSpeed(speed);
                _led.setGear(_current_gear);
                ESP32BASE_LOG_I("FanCtrl", "IR: Gear down to %d (%d%%)", _current_gear, speed);
            }
            break;

        case IR_EVENT_STOP:
            stop();
            _current_gear = 0;
            _led.setGear(0);
            break;

        case IR_EVENT_TIMER_30M:
            setTimer(1800);
            ESP32BASE_LOG_I("FanCtrl", "IR: Timer set 30min");
            break;

        case IR_EVENT_TIMER_1H:
            setTimer(3600);
            ESP32BASE_LOG_I("FanCtrl", "IR: Timer set 1h");
            break;

        case IR_EVENT_TIMER_2H:
            setTimer(7200);
            ESP32BASE_LOG_I("FanCtrl", "IR: Timer set 2h");
            break;

        default:
            break;
    }
}

void FanController::_processTimer() {
    if (_timer_remaining == 0) return;

    uint32_t now = millis();

    if (now - _last_timer_tick >= 1000) {
        _timer_remaining--;
        _last_timer_tick = now;

        if (_timer_remaining == 0) {
            stop();
            _current_gear = 0;
            _led.setGear(0);
            ESP32BASE_LOG_I("FanCtrl", "Timer expired, fan stopped");
        } else if (_timer_remaining <= 60 && _timer_remaining % 10 == 0) {
            ESP32BASE_LOG_D("FanCtrl", "Timer remaining: %lus", static_cast<unsigned long>(_timer_remaining));
        }
    }
}

void FanController::_processSleep() {
    if (_fan.getSpeed() != 0) {
        _last_operation_tick = millis();
        return;
    }

    // Only enter modem sleep after STA is connected. Entering before connection
    // can interfere with WiFi association and provides no useful power saving.
    if (!Esp32BaseWiFi::isConnected()) {
        _last_operation_tick = millis();
        return;
    }

    uint32_t idle_time = millis() - _last_operation_tick;
    if (idle_time >= (_sleep_wait_time * 1000UL)) {
        _is_sleeping = true;
        _state = SYS_SLEEP;
        _sleep_entry_tick = millis();

        // ESP32 modem sleep equivalent while keeping STA/Web available.
        Esp32BaseWiFi::setPowerSave(true);
        
        ESP32BASE_LOG_I("FanCtrl", "Entering sleep mode (Modem Sleep), idle=%lus",
                 static_cast<unsigned long>(idle_time / 1000));
    }
}

bool FanController::_applySpeed(uint8_t speed) {
    bool ok = _fan.setSpeed(speed);
    if (ok) {
        if (speed > 0) {
            _state = SYS_RUNNING;
            _last_run_tick = millis();
        }
        _saveRuntimeState();
    }
    return ok;
}

void FanController::_saveRuntimeState() {
    static uint32_t last_save = 0;
    uint32_t now = millis();

    // Throttle Flash writes: max once every 5 seconds.
    if (now - last_save < 5000) return;
    last_save = now;

    cfgSetIntDeferred(KEY_LAST_SPEED, _fan.getSpeed());
    cfgSetIntDeferred(KEY_LAST_TIMER, static_cast<int32_t>(_timer_remaining));
    cfgSetIntDeferred(KEY_RUN_DURATION, static_cast<int32_t>(_run_duration));
}

void FanController::_loadConfig() {
    _min_effective_speed = static_cast<uint8_t>(
        cfgGetInt(KEY_MIN_SPEED, 10));
    _sleep_wait_time = static_cast<uint16_t>(
        cfgGetInt(KEY_SLEEP_WAIT, 60));
    _auto_restore = cfgGetBool(KEY_AUTO_RESTORE, true);

    int32_t soft_start = cfgGetInt(KEY_SOFT_START, 1000);
    int32_t soft_stop = cfgGetInt(KEY_SOFT_STOP, 1000);
    int32_t block_detect = cfgGetInt(KEY_BLOCK_DETECT, 1500);

    _soft_start_time = static_cast<uint16_t>(soft_start);
    _soft_stop_time = static_cast<uint16_t>(soft_stop);
    _block_detect_time = static_cast<uint16_t>(block_detect);
    _fan.setSoftStartTime(_soft_start_time);
    _fan.setSoftStopTime(_soft_stop_time);
    _fan.setBlockDetectTime(_block_detect_time);

    _run_duration = static_cast<uint32_t>(
        cfgGetInt(KEY_RUN_DURATION, 0));

    char web_password[32];
    if (cfgGetStr(KEY_WEB_PASSWORD, web_password, sizeof(web_password), "") &&
        web_password[0] != '\0') {
        Esp32BaseWeb::setAuth("admin", web_password);
    }

    // Load IR codes
    char key[8];
    for (uint8_t i = 0; i < 6; i++) {
        snprintf(key, sizeof(key), "%s%u", KEY_IR_PROTO, i);
        int32_t proto = cfgGetInt(key, 0);
        snprintf(key, sizeof(key), "%s%u", KEY_IR_CODE_LOW, i);
        int32_t code_low = cfgGetInt(key, 0);
        snprintf(key, sizeof(key), "%s%u", KEY_IR_CODE_HIGH, i);
        int32_t code_high = cfgGetInt(key, 0);

        uint64_t code = (static_cast<uint64_t>(code_high) << 32) | static_cast<uint32_t>(code_low);
        _ir.setKeyCode(i, static_cast<uint8_t>(proto), code);
    }
}

void FanController::_saveConfig() {
    cfgSetInt(KEY_MIN_SPEED, _min_effective_speed);
    cfgSetInt(KEY_SOFT_START, _soft_start_time);
    cfgSetInt(KEY_SOFT_STOP, _soft_stop_time);
    cfgSetInt(KEY_BLOCK_DETECT, _block_detect_time);
    cfgSetInt(KEY_SLEEP_WAIT, _sleep_wait_time);
    cfgSetBool(KEY_AUTO_RESTORE, _auto_restore);
}

void FanController::_saveIRCodes() {
    char key[8];
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t proto;
        uint64_t code;
        _ir.getKeyCode(i, &proto, &code);

        snprintf(key, sizeof(key), "%s%u", KEY_IR_PROTO, i);
        cfgSetInt(key, static_cast<int32_t>(proto));
        snprintf(key, sizeof(key), "%s%u", KEY_IR_CODE_LOW, i);
        cfgSetInt(key, static_cast<int32_t>(code & 0xFFFFFFFF));
        snprintf(key, sizeof(key), "%s%u", KEY_IR_CODE_HIGH, i);
        cfgSetInt(key, static_cast<int32_t>((code >> 32) & 0xFFFFFFFF));
    }
}
