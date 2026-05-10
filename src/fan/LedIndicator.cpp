#include "fan/LedIndicator.h"

#include <Arduino.h>
#ifdef UNIT_TEST
#include "Esp32Base.h"
#else
#include <Esp32Base.h>
#endif

#ifndef ESP32_FAN_LED_PWM_FREQ
#define ESP32_FAN_LED_PWM_FREQ 1000
#endif

#ifndef ESP32_FAN_LED_PWM_RESOLUTION
#define ESP32_FAN_LED_PWM_RESOLUTION 8
#endif

namespace {
const uint8_t LED_PWM_CHANNEL = 1;
}

LedIndicator::LedIndicator(uint8_t pin, bool active_low)
    : _pin(pin)
    , _active_low(active_low)
    , _current_gear(0)
    , _gear_mode(LED_OFF)
    , _override_mode(LED_OFF)
    , _override_active(false)
    , _last_toggle(0)
    , _blink_state(false)
    , _flash_start(0)
    , _flash_active(false)
    , _flash_output_on(false)
    , _output_on(false)
    , _flash_duration_ms(DEFAULT_FLASH_DURATION) {
}

bool LedIndicator::begin() {
    pinMode(_pin, OUTPUT);
#ifndef UNIT_TEST
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(_pin, ESP32_FAN_LED_PWM_FREQ, ESP32_FAN_LED_PWM_RESOLUTION);
#else
    ledcSetup(LED_PWM_CHANNEL, ESP32_FAN_LED_PWM_FREQ, ESP32_FAN_LED_PWM_RESOLUTION);
    ledcAttachPin(_pin, LED_PWM_CHANNEL);
#endif
#endif
    writeDigital(false);

    ESP32BASE_LOG_I("LedInd", "Initialized: GPIO%d, active_low=%s", _pin, _active_low ? "true" : "false");
    return true;
}

void LedIndicator::tick() {
    update();
}

void LedIndicator::setGear(uint8_t gear) {
    if (gear > 4) gear = 4;
    _current_gear = gear;
    LedMode next = gear > 0 ? LED_ON : LED_OFF;
    if (_override_active || _gear_mode != next) {
        _gear_mode = next;
        _override_active = false;
        resetBlinkClock();
    }
}

void LedIndicator::setOverride(LedMode mode) {
    if (mode == LED_SINGLE_FLASH) {
        flashOnce();
        return;
    }
    if (!_override_active || _override_mode != mode) {
        _override_mode = mode;
        _override_active = true;
        resetBlinkClock();
    }
    if (mode == LED_FAST_BLINK) {
        _flash_active = false;
    }
}

void LedIndicator::setFlashDuration(uint16_t ms) {
    _flash_duration_ms = ms;
    if (_flash_duration_ms == 0) {
        _flash_active = false;
    }
}

uint16_t LedIndicator::getFlashDuration() const {
    return _flash_duration_ms;
}

void LedIndicator::flashOnce() {
    if (_flash_duration_ms == 0) return;
    LedMode mode = _override_active ? _override_mode : _gear_mode;
    if (mode == LED_FAST_BLINK) return;
    _flash_start = millis();
    _flash_output_on = !_output_on;
    _flash_active = true;
}

void LedIndicator::update() {
    uint32_t now = millis();
    LedMode mode = _override_active ? _override_mode : _gear_mode;
    if (_flash_active && mode != LED_FAST_BLINK) {
        if (now - _flash_start < _flash_duration_ms) {
            writeDigital(_flash_output_on);
            return;
        }
        _flash_active = false;
    }

    switch (mode) {
        case LED_OFF:
            writeDigital(false);
            break;

        case LED_ON:
            if (_current_gear <= 4) {
                static const uint8_t brightness_by_gear[5] = {0, 64, 128, 192, 255};
                writeBrightness(brightness_by_gear[_current_gear]);
            } else {
                writeBrightness(255);
            }
            break;

        case LED_SLOW_BLINK:
            if (now - _last_toggle >= SLOW_BLINK_INTERVAL) {
                _blink_state = !_blink_state;
                _last_toggle = now;
            }
            writeDigital(_blink_state);
            break;

        case LED_FAST_BLINK:
            if (now - _last_toggle >= FAST_BLINK_INTERVAL) {
                _blink_state = !_blink_state;
                _last_toggle = now;
            }
            writeDigital(_blink_state);
            break;

        default:
            writeDigital(false);
            break;
    }
}

void LedIndicator::writeDigital(bool on) {
    _output_on = on;
#ifndef UNIT_TEST
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(_pin, on ? (_active_low ? 0 : 255) : (_active_low ? 255 : 0));
#else
    ledcWrite(LED_PWM_CHANNEL, on ? (_active_low ? 0 : 255) : (_active_low ? 255 : 0));
#endif
#else
    digitalWrite(_pin, _active_low ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
#endif
}

void LedIndicator::writeBrightness(uint8_t brightness) {
    if (brightness == 0) {
        writeDigital(false);
        return;
    }

    _output_on = true;
#ifndef UNIT_TEST
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(_pin, _active_low ? static_cast<uint8_t>(255 - brightness) : brightness);
#else
    ledcWrite(LED_PWM_CHANNEL, _active_low ? static_cast<uint8_t>(255 - brightness) : brightness);
#endif
#else
    analogWrite(_pin, _active_low ? static_cast<uint8_t>(255 - brightness) : brightness);
#endif
}

void LedIndicator::resetBlinkClock() {
    _last_toggle = millis();
    _blink_state = false;
}
