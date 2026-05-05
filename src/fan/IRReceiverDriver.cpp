#include "fan/IRReceiverDriver.h"

#include <Arduino.h>
#include <IRrecv.h>
#include <Esp32BaseLog.h>

IRReceiverDriver::IRReceiverDriver(uint8_t recv_pin)
    : _recv_pin(recv_pin)
    , _learning(false)
    , _learning_key_index(0)
    , _learning_start_tick(0)
    , _learned_dirty(false)
    , _last_protocol(0)
    , _last_code(0) {
    for (uint8_t i = 0; i < IR_KEY_COUNT; i++) {
        _protocols[i] = 0;
        _codes[i] = 0;
    }
}

bool IRReceiverDriver::begin() {
    // IRremoteESP8266 will be initialized on first use
    ESP32BASE_LOG_I("IR", "Initialized: RECV=GPIO%d", _recv_pin);
    return true;
}

IREvent IRReceiverDriver::getEvent() {
    static IRrecv irrecv(_recv_pin, kCaptureBufferSize, kCaptureTimeout, true);
    static bool initialized = false;

    if (!initialized) {
        irrecv.enableIRIn();
        initialized = true;
        ESP32BASE_LOG_D("IR", "IR receiver enabled");
    }

    decode_results results;

    if (!irrecv.decode(&results)) {
        // Check learning timeout
        if (_learning && (millis() - _learning_start_tick >= LEARNING_TIMEOUT_MS)) {
            ESP32BASE_LOG_W("IR", "Learning timeout (key=%d)", _learning_key_index);
            _learning = false;
        }
        return IR_EVENT_NONE;
    }

#ifndef UNIT_TEST
    if (results.repeat) {
        irrecv.resume();
        ESP32BASE_LOG_D("IR", "Ignored repeat IR frame: protocol=%d, code=0x%08llX",
                          (int)results.decode_type, static_cast<unsigned long long>(results.value));
        return IR_EVENT_NONE;
    }
#endif

    if ((int)results.decode_type <= 0 || results.value == 0) {
        irrecv.resume();
        ESP32BASE_LOG_W("IR", "Ignored undecoded IR frame: protocol=%d, code=0x%08llX",
                          (int)results.decode_type, static_cast<unsigned long long>(results.value));
        return IR_EVENT_NONE;
    }

    // Store decoded info
    _last_protocol = (uint8_t)results.decode_type;
    _last_code = results.value;

    irrecv.resume();

    ESP32BASE_LOG_D("IR", "Received: protocol=%d, code=0x%08llX",
             _last_protocol, static_cast<unsigned long long>(_last_code));

    // If in learning mode, save the code and exit learning
    if (_learning) {
        setKeyCode(_learning_key_index, _last_protocol, _last_code);
        _learning = false;
        _learned_dirty = true;
        return IR_EVENT_NONE;
    }

    // Match against learned codes
    return matchCode(_last_protocol, _last_code);
}

bool IRReceiverDriver::startLearning(uint8_t key_index) {
    if (key_index >= IR_KEY_COUNT) return false;

    _learning = true;
    _learning_key_index = key_index;
    _learning_start_tick = millis();

    ESP32BASE_LOG_I("IR", "Learning mode started for key %d (10s timeout)", key_index);
    return true;
}

bool IRReceiverDriver::isLearning() const {
    return _learning;
}

uint8_t IRReceiverDriver::getLearnedKeyIndex() const {
    return _learning_key_index;
}

uint32_t IRReceiverDriver::getLearningRemaining() const {
    if (!_learning) return 0;

    uint32_t elapsed = millis() - _learning_start_tick;
    if (elapsed >= LEARNING_TIMEOUT_MS) return 0;

    return (LEARNING_TIMEOUT_MS - elapsed) / 1000;
}

void IRReceiverDriver::setKeyCode(uint8_t key_index, uint8_t protocol, uint64_t code) {
    if (key_index >= IR_KEY_COUNT) return;

    _protocols[key_index] = protocol;
    _codes[key_index] = code;
    // Only log if actually learned (non-zero code)
    if (protocol != 0 || code != 0) {
        ESP32BASE_LOG_I("IR", "Key %d learned: protocol=%d, code=0x%08llX",
                 key_index, protocol, static_cast<unsigned long long>(code));
    }
}

bool IRReceiverDriver::getKeyCode(uint8_t key_index, uint8_t* protocol, uint64_t* code) const {
    if (key_index >= IR_KEY_COUNT || protocol == nullptr || code == nullptr) return false;

    *protocol = _protocols[key_index];
    *code = _codes[key_index];
    return true;
}

bool IRReceiverDriver::consumeLearnedCode() {
    bool dirty = _learned_dirty;
    _learned_dirty = false;
    return dirty;
}

uint8_t IRReceiverDriver::getLastProtocol() const {
    return _last_protocol;
}

uint64_t IRReceiverDriver::getLastCode() const {
    return _last_code;
}

IREvent IRReceiverDriver::matchCode(uint8_t protocol, uint64_t code) {
    for (uint8_t i = 0; i < IR_KEY_COUNT; i++) {
        if (_protocols[i] == protocol && _codes[i] == code && _codes[i] != 0) {
            switch (i) {
                case IR_KEY_SPEED_UP:
                    ESP32BASE_LOG_D("IR", "Matched: SPEED_UP");
                    return IR_EVENT_SPEED_UP;
                case IR_KEY_SPEED_DOWN:
                    ESP32BASE_LOG_D("IR", "Matched: SPEED_DOWN");
                    return IR_EVENT_SPEED_DOWN;
                case IR_KEY_STOP:
                    ESP32BASE_LOG_D("IR", "Matched: STOP");
                    return IR_EVENT_STOP;
                case IR_KEY_TIMER_30M:
                    ESP32BASE_LOG_D("IR", "Matched: TIMER_30M");
                    return IR_EVENT_TIMER_30M;
                case IR_KEY_TIMER_1H:
                    ESP32BASE_LOG_D("IR", "Matched: TIMER_1H");
                    return IR_EVENT_TIMER_1H;
                case IR_KEY_TIMER_2H:
                    ESP32BASE_LOG_D("IR", "Matched: TIMER_2H");
                    return IR_EVENT_TIMER_2H;
                default:
                    return IR_EVENT_NONE;
            }
        }
    }

    // Unknown code, ignore
    return IR_EVENT_NONE;
}
