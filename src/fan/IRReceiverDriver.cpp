#include "fan/IRReceiverDriver.h"

#include <Arduino.h>
#include <IRrecv.h>
#ifdef UNIT_TEST
#include "Esp32Base.h"
#else
#include <Esp32Base.h>
#endif

namespace {
const uint16_t kCaptureBufferSize = 512;
const uint16_t kCaptureTimeout = 50;
const uint8_t kProtocolNec = 3;  // IRremoteESP8266 decode_type_t::NEC.

uint8_t reverse8(uint8_t value) {
    value = (value & 0xF0) >> 4 | (value & 0x0F) << 4;
    value = (value & 0xCC) >> 2 | (value & 0x33) << 2;
    return (value & 0xAA) >> 1 | (value & 0x55) << 1;
}

uint16_t reverse16(uint16_t value) {
    return (static_cast<uint16_t>(reverse8(value & 0xFF)) << 8) | reverse8(value >> 8);
}

bool decodeNec(uint64_t code, uint16_t* address, bool* extended, uint8_t* command) {
    if (!address || !extended || !command) return false;
    const uint32_t raw = static_cast<uint32_t>(code);
    const uint8_t addr = raw >> 24;
    const uint8_t addr_inv = raw >> 16;
    const uint8_t cmd = raw >> 8;
    const uint8_t cmd_inv = raw;
    if ((cmd ^ cmd_inv) != 0xFF) return false;
    *command = reverse8(cmd);
    if ((addr ^ addr_inv) == 0xFF) {
        *address = reverse8(addr);
        *extended = false;
    } else {
        *address = reverse16(raw >> 16);
        *extended = true;
    }
    return true;
}
}

IRReceiverDriver::IRReceiverDriver(uint8_t recv_pin)
    : _recv_pin(recv_pin)
    , _irrecv(recv_pin, kCaptureBufferSize, kCaptureTimeout, true)
    , _initialized(false)
    , _learning(false)
    , _learning_key_index(0)
    , _learning_start_tick(0)
    , _learned_dirty(false)
    , _learned_sequence(0)
    , _learn_reject_sequence(0)
    , _duplicate_key_index(IR_KEY_COUNT)
    , _ignore_until_tick(0)
    , _last_protocol(0)
    , _last_code(0)
#ifdef UNIT_TEST
    , _test_event(IR_EVENT_NONE)
#endif
{
    for (uint8_t i = 0; i < IR_KEY_COUNT; i++) {
        _protocols[i] = 0;
        _codes[i] = 0;
    }
}

bool IRReceiverDriver::begin() {
    if (!_initialized) {
        _irrecv.enableIRIn();
        _initialized = true;
    }
    ESP32BASE_LOG_I("IR", "Initialized: RECV=GPIO%d", _recv_pin);
    return true;
}

IREvent IRReceiverDriver::getEvent() {
#ifdef UNIT_TEST
    if (_test_event != IR_EVENT_NONE) {
        IREvent event = _test_event;
        _test_event = IR_EVENT_NONE;
        return event;
    }
#endif

    decode_results results;

    if (!_irrecv.decode(&results)) {
        // Check learning timeout
        if (_learning && (millis() - _learning_start_tick >= LEARNING_TIMEOUT_MS)) {
            ESP32BASE_LOG_W("IR", "Learning timeout (key=%d)", _learning_key_index);
            _learning = false;
            _duplicate_key_index = IR_KEY_COUNT;
        }
        return IR_EVENT_NONE;
    }

    if (results.repeat) {
        _irrecv.resume();
        ESP32BASE_LOG_D("IR", "Ignored repeat IR frame: protocol=%d, code=0x%08llX",
                          (int)results.decode_type, static_cast<unsigned long long>(results.value));
        return IR_EVENT_NONE;
    }

    if ((int)results.decode_type <= 0 || results.value == 0) {
        _irrecv.resume();
        ESP32BASE_LOG_D("IR", "Ignored undecoded IR frame: protocol=%d, code=0x%08llX",
                          (int)results.decode_type, static_cast<unsigned long long>(results.value));
        return IR_EVENT_NONE;
    }

    if ((int)results.decode_type > 255) {
        _irrecv.resume();
        ESP32BASE_LOG_W("IR", "Ignored unsupported IR protocol id=%d, code=0x%08llX",
                          (int)results.decode_type, static_cast<unsigned long long>(results.value));
        return IR_EVENT_NONE;
    }

    // Store decoded info
    _last_protocol = (uint8_t)results.decode_type;
    _last_code = results.value;

    _irrecv.resume();

    char decoded[80];
    formatDecodedCode(_last_protocol, _last_code, decoded, sizeof(decoded));
    ESP32BASE_LOG_D("IR", "Received: %s", decoded);

    if (_ignore_until_tick != 0) {
        if ((int32_t)(millis() - _ignore_until_tick) < 0) {
            ESP32BASE_LOG_D("IR", "Ignored debounced IR frame: protocol=%d, code=0x%08llX",
                              _last_protocol, static_cast<unsigned long long>(_last_code));
            return IR_EVENT_NONE;
        }
        _ignore_until_tick = 0;
    }

    // If in learning mode, save the code and exit learning
    if (_learning) {
        completeLearning(_last_protocol, _last_code);
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
    _duplicate_key_index = IR_KEY_COUNT;
    _ignore_until_tick = 0;
    _last_protocol = 0;
    _last_code = 0;

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
        char decoded[80];
        formatDecodedCode(protocol, code, decoded, sizeof(decoded));
        ESP32BASE_LOG_I("IR", "Key %d learned: %s", key_index, decoded);
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

uint32_t IRReceiverDriver::getLearnedSequence() const {
    return _learned_sequence;
}

uint32_t IRReceiverDriver::getLearnRejectSequence() const {
    return _learn_reject_sequence;
}

uint8_t IRReceiverDriver::getDuplicateKeyIndex() const {
    return _duplicate_key_index;
}

void IRReceiverDriver::formatDecodedCode(uint8_t protocol, uint64_t code, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (protocol == 0 && code == 0) {
        snprintf(out, out_size, "None");
        return;
    }
    if (protocol == kProtocolNec) {
        uint16_t address = 0;
        uint8_t command = 0;
        bool extended = false;
        if (decodeNec(code, &address, &extended, &command)) {
            if (extended) {
                snprintf(out, out_size, "NEC ext addr 0x%04X cmd 0x%02X - raw 0x%08llX",
                         address, command, static_cast<unsigned long long>(code));
            } else {
                snprintf(out, out_size, "NEC addr 0x%02X cmd 0x%02X - raw 0x%08llX",
                         static_cast<unsigned>(address), command,
                         static_cast<unsigned long long>(code));
            }
            return;
        }
    }
    snprintf(out, out_size, "Protocol %u - 0x%016llX",
             protocol, static_cast<unsigned long long>(code));
}

bool IRReceiverDriver::findDuplicateKey(uint8_t protocol, uint64_t code, uint8_t except_key, uint8_t* duplicate_key) const {
    if (protocol == 0 && code == 0) return false;
    for (uint8_t i = 0; i < IR_KEY_COUNT; i++) {
        if (i == except_key) continue;
        if (_protocols[i] == protocol && _codes[i] == code) {
            if (duplicate_key != nullptr) *duplicate_key = i;
            return true;
        }
    }
    return false;
}

bool IRReceiverDriver::completeLearning(uint8_t protocol, uint64_t code) {
    if (!_learning || _learning_key_index >= IR_KEY_COUNT) return false;

    uint8_t duplicate_key = 0;
    if (findDuplicateKey(protocol, code, _learning_key_index, &duplicate_key)) {
        _duplicate_key_index = duplicate_key;
        _learn_reject_sequence++;
        _ignore_until_tick = millis() + DUPLICATE_LEARN_IGNORE_MS;
        char decoded[80];
        formatDecodedCode(protocol, code, decoded, sizeof(decoded));
        ESP32BASE_LOG_W("IR", "Rejected duplicate learned code key=%u duplicate_of=%u %s",
                        _learning_key_index, duplicate_key, decoded);
        return false;
    }

    _duplicate_key_index = IR_KEY_COUNT;
    setKeyCode(_learning_key_index, protocol, code);
    _last_protocol = protocol;
    _last_code = code;
    _learning = false;
    _learned_dirty = true;
    _learned_sequence++;
    _ignore_until_tick = millis() + POST_LEARN_IGNORE_MS;
    return true;
}

#ifdef UNIT_TEST
void IRReceiverDriver::testQueueEvent(IREvent event) {
    _test_event = event;
}

void IRReceiverDriver::testMarkLearned(uint8_t key_index, uint8_t protocol, uint64_t code) {
    if (key_index >= IR_KEY_COUNT) return;
    setKeyCode(key_index, protocol, code);
    _learning_key_index = key_index;
    _last_protocol = protocol;
    _last_code = code;
    _learning = false;
    _learned_dirty = true;
    _learned_sequence++;
}

bool IRReceiverDriver::testLearnDecoded(uint8_t key_index, uint8_t protocol, uint64_t code) {
    if (!startLearning(key_index)) return false;
    return completeLearning(protocol, code);
}
#endif

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
                case IR_KEY_TIMER_4H:
                    ESP32BASE_LOG_D("IR", "Matched: TIMER_4H");
                    return IR_EVENT_TIMER_4H;
                case IR_KEY_TIMER_8H:
                    ESP32BASE_LOG_D("IR", "Matched: TIMER_8H");
                    return IR_EVENT_TIMER_8H;
                default:
                    return IR_EVENT_NONE;
            }
        }
    }

    // Unknown code, ignore
    return IR_EVENT_NONE;
}
