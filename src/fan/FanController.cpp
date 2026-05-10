#include "fan/FanController.h"

#ifdef UNIT_TEST
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "Arduino.h"
#include "Esp32Base.h"
#else
#include <stdlib.h>
#include <string.h>
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
const char* KEY_LED_FLASH_MS = "led_ms";
const char* KEY_RUNTIME_SAVE_MIN = "rt_save_m";
const char* KEY_LAST_SPEED = "last_spd";
const char* KEY_LAST_TIMER = "last_tim";
const char* KEY_RUN_DURATION = "run_s";
const char* KEY_IR_ENTRY = "ir_";

// Gear to speed mapping
const uint8_t GEAR_SPEED[5] = {0, 25, 50, 75, 100};

bool cfgSetStr(const char* key, const char* value) {
    return Esp32BaseConfig::setStr(CFG_NS, key, value);
}

bool cfgGetStr(const char* key, char* out, size_t len, const char* def = "") {
    return Esp32BaseConfig::getStr(CFG_NS, key, out, len, def);
}

bool cfgSetInt(const char* key, int32_t value) {
    return Esp32BaseConfig::setInt(CFG_NS, key, value);
}

int32_t cfgGetInt(const char* key, int32_t def = 0) {
    return Esp32BaseConfig::getInt(CFG_NS, key, def);
}

bool cfgSetIntDeferred(const char* key, int32_t value) {
    return Esp32BaseConfig::setIntDeferred(CFG_NS, key, value);
}

bool cfgSetBool(const char* key, bool value) {
    return Esp32BaseConfig::setBool(CFG_NS, key, value);
}

bool cfgGetBool(const char* key, bool def = false) {
    return Esp32BaseConfig::getBool(CFG_NS, key, def);
}

bool parseIRCodeEntry(const char* value, uint8_t* protocol, uint64_t* code) {
    if (!value || !protocol || !code) return false;

    char* end = nullptr;
    unsigned long parsedProtocol = strtoul(value, &end, 10);
    if (end == value || *end != ':' || parsedProtocol > 255) return false;

    char* codeEnd = nullptr;
    unsigned long long parsedCode = strtoull(end + 1, &codeEnd, 16);
    if (codeEnd == end + 1 || *codeEnd != '\0') return false;

    *protocol = static_cast<uint8_t>(parsedProtocol);
    *code = static_cast<uint64_t>(parsedCode);
    return true;
}
}

FanController::FanController(FanDriver& fan, ButtonDriver& btn, LedIndicator& led, IRReceiverDriver& ir)
    : _fan(fan)
    , _btn(btn)
    , _led(led)
    , _ir(ir)
    , _state(SYS_IDLE)
    , _current_gear(0)
    , _target_speed(0)
    , _timer_remaining(0)
    , _run_duration(0)
    , _boot_run_duration(0)
    , _last_run_tick(0)
    , _last_operation_tick(0)
    , _last_runtime_save_tick(0)
    , _sleep_wait_time(60)
    , _soft_start_time(1000)
    , _soft_stop_time(1000)
    , _block_detect_time(1500)
    , _led_flash_duration_ms(200)
    , _runtime_save_interval_min(1)
    , _min_effective_speed(10)
    , _is_sleeping(false)
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
            _syncGearFromSpeed(static_cast<uint8_t>(last_speed));
            _applySpeed(static_cast<uint8_t>(last_speed));
        } else {
            ESP32BASE_LOG_I("FanCtrl", "No restore needed (last_speed=0)");
        }
    } else {
        ESP32BASE_LOG_I("FanCtrl", "Auto-restore disabled");
    }

    _updateLedStatus();

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
        case SYS_RECOVERING:
            _handleRecovering();
            break;
        default:
            _state = SYS_ERROR;
            break;
    }

    _updateLedStatus();
    _led.tick();

    // Feed watchdog to prevent Soft WDT reset during heavy operations (e.g. Flash writes)
    yield();
}

SystemState FanController::getState() const { return _state; }
uint8_t FanController::getCurrentGear() const { return _current_gear; }
uint8_t FanController::getCurrentSpeed() const { return _fan.getSpeed(); }
uint8_t FanController::getTargetSpeed() const { return _target_speed; }
uint16_t FanController::getCurrentRpm() const { return _fan.getRpm(); }
uint32_t FanController::getTimerRemaining() const { return _timer_remaining; }
uint32_t FanController::getTotalRunDuration() const { return _run_duration; }
uint32_t FanController::getBootRunDuration() const { return _boot_run_duration; }
bool FanController::isBlocked() const { return _fan.isBlocked(); }
bool FanController::isSleeping() const { return _is_sleeping; }
bool FanController::getAutoRestore() const { return _auto_restore; }

void FanController::setAutoRestore(bool enable) {
    _auto_restore = enable;
    cfgSetBool(KEY_AUTO_RESTORE, _auto_restore);
}

bool FanController::setSpeed(uint8_t speed) {
    _last_operation_tick = millis();
    if (_is_sleeping) {
        _is_sleeping = false;
        Esp32BaseWiFi::setPowerSave(false);
    }

    if (speed > 100) speed = 100;
    if (speed > 0 && speed < _min_effective_speed) {
        speed = _min_effective_speed;
    }

    _target_speed = speed;
    _syncGearFromSpeed(speed);

    if (_state == SYS_ERROR || _state == SYS_RECOVERING) {
        if (speed == 0) {
            return stop();
        }
        if (!_recovery_attempting) {
            _fan.resetBlock();
            _recovery_attempting = true;
            _recovery_start_tick = millis();
        }
        _state = SYS_RECOVERING;
        _fan.setSpeed(speed);
        _saveRuntimeState(true);
    } else {
        _applySpeed(speed, true);
    }
    notifyUserAction();
    ESP32BASE_LOG_I("FanCtrl", "Speed requested: target=%u output=%u state=%u",
                      (unsigned)_target_speed, (unsigned)_fan.getSpeed(), (unsigned)_state);
    return true;
}

bool FanController::setTimer(uint32_t seconds) {
    if (seconds > 356400UL) return false;

    _timer_remaining = seconds;
    _last_timer_tick = millis();
    _last_operation_tick = millis();
    if (_is_sleeping) {
        _is_sleeping = false;
        Esp32BaseWiFi::setPowerSave(false);
    }
    _saveRuntimeState(true);

    if (seconds > 0) {
        ESP32BASE_LOG_I("FanCtrl", "Timer set: %lu seconds", static_cast<unsigned long>(seconds));
    } else {
        ESP32BASE_LOG_I("FanCtrl", "Timer cancelled");
    }
    notifyUserAction();
    return true;
}

bool FanController::stop() {
    _last_operation_tick = millis();
    if (_is_sleeping) {
        _is_sleeping = false;
        Esp32BaseWiFi::setPowerSave(false);
    }
    _timer_remaining = 0;
    _target_speed = 0;
    _current_gear = 0;
    _recovery_attempting = false;
    if (_state == SYS_ERROR || _state == SYS_RECOVERING || _fan.isBlocked()) {
        _fan.resetBlock();
        _state = SYS_IDLE;
    }
    _applySpeed(0, true);
    notifyUserAction();
    ESP32BASE_LOG_I("FanCtrl", "Fan stopped by request");
    return true;
}

bool FanController::resetFactory() {
    bool ok = Esp32BaseConfig::clearNamespace(CFG_NS);
    ok = Esp32BaseConfig::clearLibraryNamespaces() && ok;
    ok = Esp32BaseConfig::flushAll() && ok;
    if (!ok) {
        ESP32BASE_LOG_E("FanCtrl", "Factory reset failed: config clear failed");
        return false;
    }
    ESP.restart();
    return true;
}

bool FanController::clearIRCode(uint8_t key_index) {
    if (key_index >= IR_KEY_COUNT) return false;

    uint8_t proto = 0;
    uint64_t code = 0;
    _ir.getKeyCode(key_index, &proto, &code);
    if (proto == 0 && code == 0) return false;

    char key[20];
    snprintf(key, sizeof(key), "%s%d", KEY_IR_ENTRY, key_index);
    _ir.setKeyCode(key_index, 0, 0);
    bool ok = cfgSetStr(key, "");
    ok = Esp32BaseConfig::flushAll() && ok;
    ESP32BASE_LOG_I("FanCtrl", "IR code cleared key=%u result=%s", key_index, ok ? "success" : "failed");
    return ok;
}

void FanController::notifyUserAction() {
    _led.flashOnce();
}

uint8_t FanController::getMinEffectiveSpeed() const { return _min_effective_speed; }

void FanController::setMinEffectiveSpeed(uint8_t speed) {
    if (speed > 50) speed = 50;
    _min_effective_speed = speed;
    _fan.setMinEffectiveSpeed(_min_effective_speed);
    cfgSetInt(KEY_MIN_SPEED, _min_effective_speed);
}

uint16_t FanController::getSoftStartTime() const {
    return _soft_start_time;
}

void FanController::setSoftStartTime(uint16_t ms) {
    if (ms > 10000) ms = 10000;
    _soft_start_time = ms;
    cfgSetInt(KEY_SOFT_START, static_cast<int32_t>(ms));
    _fan.setSoftStartTime(ms);
}

uint16_t FanController::getSoftStopTime() const {
    return _soft_stop_time;
}

void FanController::setSoftStopTime(uint16_t ms) {
    if (ms > 10000) ms = 10000;
    _soft_stop_time = ms;
    cfgSetInt(KEY_SOFT_STOP, static_cast<int32_t>(ms));
    _fan.setSoftStopTime(ms);
}

uint16_t FanController::getBlockDetectTime() const {
    return _block_detect_time;
}

void FanController::setBlockDetectTime(uint16_t ms) {
    if (ms < 100) ms = 100;
    if (ms > 5000) ms = 5000;
    _block_detect_time = ms;
    cfgSetInt(KEY_BLOCK_DETECT, static_cast<int32_t>(ms));
    _fan.setBlockDetectTime(ms);
}

uint16_t FanController::getSleepWaitTime() const { return _sleep_wait_time; }

void FanController::setSleepWaitTime(uint16_t seconds) {
    if (seconds < 1) seconds = 1;
    if (seconds > 3600) seconds = 3600;
    _sleep_wait_time = seconds;
    cfgSetInt(KEY_SLEEP_WAIT, _sleep_wait_time);
}

uint16_t FanController::getLedFlashDuration() const {
    return _led_flash_duration_ms;
}

void FanController::setLedFlashDuration(uint16_t ms) {
    if (ms > 2000) ms = 2000;
    _led_flash_duration_ms = ms;
    _led.setFlashDuration(_led_flash_duration_ms);
    cfgSetInt(KEY_LED_FLASH_MS, static_cast<int32_t>(_led_flash_duration_ms));
}

uint8_t FanController::getRuntimeSaveIntervalMinutes() const {
    return _runtime_save_interval_min;
}

void FanController::setRuntimeSaveIntervalMinutes(uint8_t minutes) {
    if (minutes < 1) minutes = 1;
    if (minutes > 60) minutes = 60;
    _runtime_save_interval_min = minutes;
    cfgSetInt(KEY_RUNTIME_SAVE_MIN, static_cast<int32_t>(_runtime_save_interval_min));
}

void FanController::_handleIdle() {
    _processButtonEvents();
    _processIREvents();
    _processTimer();
    _processSleep();
}

void FanController::_handleRunning() {
    _processButtonEvents();
    _processIREvents();
    _processTimer();

    uint32_t now = millis();
    bool duration_changed = false;
    while (now - _last_run_tick >= 1000) {
        _run_duration++;
        _boot_run_duration++;
        _last_run_tick += 1000;
        duration_changed = true;
    }
    if (duration_changed) {
        _saveRuntimeState();
    }

    if (_fan.isBlocked()) {
        _state = SYS_ERROR;
        ESP32BASE_LOG_E("FanCtrl", "BLOCK DETECTED! Transitioning to ERROR state");
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
    _processTimer();

    if (millis() - _last_operation_tick < 1000) {
        _is_sleeping = false;
        Esp32BaseWiFi::setPowerSave(false);
        if (_state == SYS_SLEEP) {
            _state = SYS_IDLE;
            ESP32BASE_LOG_I("FanCtrl", "Woken from sleep, transitioning to IDLE");
        }
    }
}

void FanController::_handleError() {
    if (_recovery_attempting) {
        _handleRecovering();
        return;
    }

    _processButtonEvents();
    _processIREvents();
    _processTimer();
}

void FanController::_handleRecovering() {
    _processButtonEvents();
    _processIREvents();
    _processTimer();

    if (!_recovery_attempting) {
        if (_state == SYS_RECOVERING) {
            _state = _fan.isBlocked() ? SYS_ERROR : SYS_RUNNING;
        }
        return;
    }

    if (_fan.getRpm() > 0) {
        _recovery_attempting = false;
        _state = SYS_RUNNING;
        _last_run_tick = millis();
        ESP32BASE_LOG_I("FanCtrl", "Recovery successful, back to RUNNING");
        return;
    }

    uint32_t elapsed = millis() - _recovery_start_tick;
    if (_fan.isBlocked() || elapsed >= _recoveryTimeoutMs()) {
        _recovery_attempting = false;
        _fan.resetBlock();
        _state = SYS_ERROR;
        ESP32BASE_LOG_W("FanCtrl", "Recovery failed, still blocked");
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
                ESP32BASE_LOG_I("FanCtrl", "Button: Gear up to %d (%d%%)", _current_gear, speed);
            }
            break;

        case BTN_DECEL_SHORT:
            if (_current_gear > 0) {
                _current_gear--;
                uint8_t speed = GEAR_SPEED[_current_gear];
                setSpeed(speed);
                ESP32BASE_LOG_I("FanCtrl", "Button: Gear down to %d (%d%%)", _current_gear, speed);
            }
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
        uint8_t key = _ir.getLearnedKeyIndex();
        _saveIRCode(key);
        notifyUserAction();
        ESP32BASE_LOG_I("FanCtrl", "IR learned code persisted key=%u", key);
    }
    if (event == IR_EVENT_NONE) return;

    switch (event) {
        case IR_EVENT_SPEED_UP:
            if (_current_gear < 4) {
                _current_gear++;
                uint8_t speed = GEAR_SPEED[_current_gear];
                setSpeed(speed);
                ESP32BASE_LOG_I("FanCtrl", "IR: Gear up to %d (%d%%)", _current_gear, speed);
            }
            break;

        case IR_EVENT_SPEED_DOWN:
            if (_current_gear > 0) {
                _current_gear--;
                uint8_t speed = GEAR_SPEED[_current_gear];
                setSpeed(speed);
                ESP32BASE_LOG_I("FanCtrl", "IR: Gear down to %d (%d%%)", _current_gear, speed);
            }
            break;

        case IR_EVENT_STOP:
            stop();
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

        case IR_EVENT_TIMER_4H:
            setTimer(14400);
            ESP32BASE_LOG_I("FanCtrl", "IR: Timer set 4h");
            break;

        case IR_EVENT_TIMER_8H:
            setTimer(28800);
            ESP32BASE_LOG_I("FanCtrl", "IR: Timer set 8h");
            break;

        default:
            break;
    }
}

void FanController::_processTimer() {
    if (_timer_remaining == 0) return;

    uint32_t now = millis();
    bool timer_changed = false;

    while (_timer_remaining > 0 && now - _last_timer_tick >= 1000) {
        _timer_remaining--;
        _last_timer_tick += 1000;
        timer_changed = true;
    }

    if (!timer_changed) return;

    if (_timer_remaining == 0) {
        stop();
        ESP32BASE_LOG_I("FanCtrl", "Timer expired, fan stopped");
    } else if (_timer_remaining <= 60 && _timer_remaining % 10 == 0) {
        ESP32BASE_LOG_D("FanCtrl", "Timer remaining: %lus", static_cast<unsigned long>(_timer_remaining));
    }
    _saveRuntimeState(_timer_remaining <= 60 && _timer_remaining % 10 == 0);
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

        // Enter modem sleep to save power
        Esp32BaseWiFi::setPowerSave(true);
        
        ESP32BASE_LOG_I("FanCtrl", "Entering sleep mode (Modem Sleep), idle=%lus",
                 static_cast<unsigned long>(idle_time / 1000));
    }
}

uint32_t FanController::_recoveryTimeoutMs() const {
    return static_cast<uint32_t>(_soft_start_time) +
           static_cast<uint32_t>(_block_detect_time) + 500UL;
}

bool FanController::_applySpeed(uint8_t speed, bool force_save) {
    bool ok = _fan.setSpeed(speed);
    if (ok) {
        if (speed > 0) {
            _state = SYS_RUNNING;
            _last_run_tick = millis();
        }
        _saveRuntimeState(force_save);
    }
    return ok;
}

void FanController::_saveRuntimeState(bool force) {
    uint32_t now = millis();

    // Throttle Flash writes. UI uses in-memory counters; persistence can lag to reduce wear.
    uint32_t interval_ms = static_cast<uint32_t>(_runtime_save_interval_min) * 60000UL;
    if (!force && now - _last_runtime_save_tick < interval_ms) return;
    _last_runtime_save_tick = now;

    if (_auto_restore) {
        cfgSetIntDeferred(KEY_LAST_SPEED, _target_speed);
        cfgSetIntDeferred(KEY_LAST_TIMER, static_cast<int32_t>(_timer_remaining));
    }
    cfgSetIntDeferred(KEY_RUN_DURATION, static_cast<int32_t>(_run_duration));
}

void FanController::_syncGearFromSpeed(uint8_t speed) {
    if (speed == 0) {
        _current_gear = 0;
    } else if (speed <= GEAR_SPEED[1]) {
        _current_gear = 1;
    } else if (speed <= GEAR_SPEED[2]) {
        _current_gear = 2;
    } else if (speed <= GEAR_SPEED[3]) {
        _current_gear = 3;
    } else {
        _current_gear = 4;
    }
}

void FanController::_updateLedStatus() {
    if (_state == SYS_ERROR || _state == SYS_RECOVERING || _fan.isBlocked()) {
        _led.setOverride(LED_FAST_BLINK);
    } else if (!Esp32BaseWiFi::isConnected()) {
        _led.setOverride(LED_SLOW_BLINK);
    } else {
        _led.setGear(_current_gear);
    }
}

void FanController::_loadConfig() {
    _min_effective_speed = static_cast<uint8_t>(
        cfgGetInt(KEY_MIN_SPEED, 10));
    if (_min_effective_speed > 50) _min_effective_speed = 50;
    _sleep_wait_time = static_cast<uint16_t>(
        cfgGetInt(KEY_SLEEP_WAIT, 60));
    if (_sleep_wait_time < 1) _sleep_wait_time = 1;
    if (_sleep_wait_time > 3600) _sleep_wait_time = 3600;
    _auto_restore = cfgGetBool(KEY_AUTO_RESTORE, true);
    int32_t led_flash_ms = cfgGetInt(KEY_LED_FLASH_MS, 200);
    if (led_flash_ms < 0) led_flash_ms = 0;
    if (led_flash_ms > 2000) led_flash_ms = 2000;
    _led_flash_duration_ms = static_cast<uint16_t>(led_flash_ms);
    _led.setFlashDuration(_led_flash_duration_ms);
    int32_t runtime_save_min = cfgGetInt(KEY_RUNTIME_SAVE_MIN, 1);
    if (runtime_save_min < 1) runtime_save_min = 1;
    if (runtime_save_min > 60) runtime_save_min = 60;
    _runtime_save_interval_min = static_cast<uint8_t>(runtime_save_min);

    int32_t soft_start = cfgGetInt(KEY_SOFT_START, 1000);
    int32_t soft_stop = cfgGetInt(KEY_SOFT_STOP, 1000);
    int32_t block_detect = cfgGetInt(KEY_BLOCK_DETECT, 1500);
    if (soft_start < 0) soft_start = 0;
    if (soft_start > 10000) soft_start = 10000;
    if (soft_stop < 0) soft_stop = 0;
    if (soft_stop > 10000) soft_stop = 10000;
    if (block_detect < 100) block_detect = 100;
    if (block_detect > 5000) block_detect = 5000;

    _soft_start_time = static_cast<uint16_t>(soft_start);
    _soft_stop_time = static_cast<uint16_t>(soft_stop);
    _block_detect_time = static_cast<uint16_t>(block_detect);
    _fan.setMinEffectiveSpeed(_min_effective_speed);
    _fan.setSoftStartTime(_soft_start_time);
    _fan.setSoftStopTime(_soft_stop_time);
    _fan.setBlockDetectTime(_block_detect_time);

    _run_duration = static_cast<uint32_t>(
        cfgGetInt(KEY_RUN_DURATION, 0));

    // Load IR codes. Each learned key is stored as one protocol:code value.
    char key[20];
    for (uint8_t i = 0; i < IR_KEY_COUNT; i++) {
        uint8_t proto = 0;
        uint64_t code = 0;
        char value[32];
        snprintf(key, sizeof(key), "%s%d", KEY_IR_ENTRY, i);
        if (cfgGetStr(key, value, sizeof(value), "")) {
            if (parseIRCodeEntry(value, &proto, &code)) {
                _ir.setKeyCode(i, proto, code);
            } else if (value[0] != '\0') {
                ESP32BASE_LOG_W("FanCtrl", "Invalid IR config ignored key=%u", i);
            }
        }
    }
}

void FanController::_saveIRCode(uint8_t key_index) {
    if (key_index >= IR_KEY_COUNT) return;

    char key[20];
    uint8_t proto;
    uint64_t code;
    _ir.getKeyCode(key_index, &proto, &code);
    if (proto == 0 && code == 0) return;

    char value[32];
    char current[32];
    snprintf(key, sizeof(key), "%s%d", KEY_IR_ENTRY, key_index);
    snprintf(value, sizeof(value), "%u:%016llX", proto, static_cast<unsigned long long>(code));
    if (cfgGetStr(key, current, sizeof(current), "")) {
        uint8_t currentProto = 0;
        uint64_t currentCode = 0;
        if (parseIRCodeEntry(current, &currentProto, &currentCode) &&
            currentProto == proto && currentCode == code) {
            ESP32BASE_LOG_I("FanCtrl", "IR learned code unchanged key=%u", key_index);
            return;
        }
    }
    cfgSetStr(key, value);
}

#ifdef UNIT_TEST
void FanController::_saveConfig() {
    cfgSetInt(KEY_MIN_SPEED, _min_effective_speed);
    cfgSetInt(KEY_SOFT_START, _soft_start_time);
    cfgSetInt(KEY_SOFT_STOP, _soft_stop_time);
    cfgSetInt(KEY_BLOCK_DETECT, _block_detect_time);
    cfgSetInt(KEY_SLEEP_WAIT, _sleep_wait_time);
    cfgSetBool(KEY_AUTO_RESTORE, _auto_restore);
    cfgSetInt(KEY_LED_FLASH_MS, static_cast<int32_t>(_led_flash_duration_ms));
    cfgSetInt(KEY_RUNTIME_SAVE_MIN, static_cast<int32_t>(_runtime_save_interval_min));
}

void FanController::_saveIRCodes() {
    for (uint8_t i = 0; i < IR_KEY_COUNT; i++) {
        _saveIRCode(i);
    }
}
#endif
