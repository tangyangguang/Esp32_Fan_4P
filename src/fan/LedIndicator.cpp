#include "fan/LedIndicator.h"

#include <Arduino.h>
#include <Esp32BaseLog.h>

namespace {
const uint8_t LED_PWM_CHANNEL = 1;
const uint32_t LED_PWM_FREQ = 5000;
const uint8_t LED_PWM_RESOLUTION = 8;

void ledWritePwm(uint8_t pin, uint8_t value) {
#ifdef UNIT_TEST
    analogWrite(pin, value);
#elif defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(pin, value);
#else
    ledcWrite(LED_PWM_CHANNEL, value);
#endif
}
}

LedIndicator::LedIndicator(uint8_t pin, bool active_low)
    : _pin(pin)
    , _active_low(active_low)
    , _current_gear(0)
    , _override_mode(LED_OFF)
    , _saved_mode(LED_OFF)
    , _last_toggle(0)
    , _led_state(false)
    , _flash_start(0)
    , _flashing(false) {
}

bool LedIndicator::begin() {
    pinMode(_pin, OUTPUT);
#ifndef UNIT_TEST
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(_pin, LED_PWM_FREQ, LED_PWM_RESOLUTION);
#else
    ledcSetup(LED_PWM_CHANNEL, LED_PWM_FREQ, LED_PWM_RESOLUTION);
    ledcAttachPin(_pin, LED_PWM_CHANNEL);
#endif
#endif
    digitalWrite(_pin, _active_low ? HIGH : LOW);  // Start OFF

    ESP32BASE_LOG_I("LedInd", "Initialized: GPIO%d, active_low=%s", _pin, _active_low ? "true" : "false");
    return true;
}

void LedIndicator::tick() {
    update();
}

void LedIndicator::setGear(uint8_t gear) {
    if (gear > 4) gear = 4;
    _current_gear = gear;
    _override_mode = LED_OFF;  // Clear override
    _flashing = false;
}

void LedIndicator::setOverride(LedMode mode) {
    _override_mode = mode;
    _flashing = false;
}

void LedIndicator::flashOnce() {
    _saved_mode = _override_mode != LED_OFF ? _override_mode :
                  (_current_gear > 0 ? LED_ON : LED_OFF);
    _override_mode = LED_SINGLE_FLASH;
    _flash_start = millis();
    _flashing = true;
}

void LedIndicator::update() {
    uint32_t now = millis();
    LedMode mode = _override_mode != LED_OFF ? _override_mode :
                   (_current_gear > 0 ? LED_ON : LED_OFF);

    bool target_state = false;

    switch (mode) {
        case LED_OFF:
            target_state = false;
            break;

        case LED_ON:
            // PWM brightness based on gear
            if (_current_gear > 0 && _current_gear <= 4) {
                uint8_t brightness = _current_gear * 64;  // 25%, 50%, 75%, 100%
                if (_active_low) {
                    ledWritePwm(_pin, 255 - brightness);
                } else {
                    ledWritePwm(_pin, brightness);
                }
                return;  // PWM mode, skip digital write
            }
            target_state = true;
            break;

        case LED_SLOW_BLINK:
            if (now - _last_toggle >= SLOW_BLINK_INTERVAL) {
                target_state = !_led_state;
                _last_toggle = now;
            } else {
                target_state = _led_state;
            }
            break;

        case LED_FAST_BLINK:
            if (now - _last_toggle >= FAST_BLINK_INTERVAL) {
                target_state = !_led_state;
                _last_toggle = now;
            } else {
                target_state = _led_state;
            }
            break;

        case LED_SINGLE_FLASH:
            if (now - _flash_start < SINGLE_FLASH_DURATION) {
                target_state = true;
            } else {
                // Return to saved mode
                _override_mode = _saved_mode;
                _flashing = false;
                update();  // Recurse once
                return;
            }
            break;
    }

    if (target_state != _led_state) {
        _led_state = target_state;
        if (_active_low) {
            digitalWrite(_pin, target_state ? LOW : HIGH);
        } else {
            digitalWrite(_pin, target_state ? HIGH : LOW);
        }
    }
}
