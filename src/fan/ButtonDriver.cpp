#include "fan/ButtonDriver.h"

#include <Arduino.h>
#include <Esp32BaseLog.h>

ButtonDriver::ButtonDriver(uint8_t accel_pin, uint8_t decel_pin)
    : _accel_pin(accel_pin)
    , _decel_pin(decel_pin)
    , _last_accel(HIGH)
    , _last_decel(HIGH)
    , _stable_accel(HIGH)
    , _stable_decel(HIGH)
    , _accel_press_tick(0)
    , _decel_press_tick(0)
    , _accel_debounce_tick(0)
    , _decel_debounce_tick(0)
    , _accel_pressed(false)
    , _decel_pressed(false)
    , _both_long_triggered(false) {
}

bool ButtonDriver::begin() {
    pinMode(_accel_pin, INPUT_PULLUP);
    pinMode(_decel_pin, INPUT_PULLUP);

    _last_accel = digitalRead(_accel_pin);
    _last_decel = digitalRead(_decel_pin);
    _stable_accel = _last_accel;
    _stable_decel = _last_decel;

    ESP32BASE_LOG_I("BtnDrv", "Initialized: ACCEL=GPIO%d, DECEL=GPIO%d", _accel_pin, _decel_pin);
    return true;
}

ButtonEvent ButtonDriver::getEvent() {
    uint32_t now = millis();

    // 1. Read raw inputs
    uint8_t raw_accel = digitalRead(_accel_pin);
    uint8_t raw_decel = digitalRead(_decel_pin);

    // 2. Debounce logic (each button has its own timer to avoid mutual interference)
    bool accel_changed = false;
    bool decel_changed = false;

    if (raw_accel != _last_accel) {
        _accel_debounce_tick = now;
        _last_accel = raw_accel;
    }
    if (raw_decel != _last_decel) {
        _decel_debounce_tick = now;
        _last_decel = raw_decel;
    }

    if ((now - _accel_debounce_tick) > DEBOUNCE_MS) {
        if (raw_accel != _stable_accel) {
            _stable_accel = raw_accel;
            accel_changed = true;
        }
    }
    if ((now - _decel_debounce_tick) > DEBOUNCE_MS) {
        if (raw_decel != _stable_decel) {
            _stable_decel = raw_decel;
            decel_changed = true;
        }
    }

    // 3. Detect Events based on stable changes
    // Accelerator
    if (accel_changed) {
        if (_stable_accel == LOW) {
            // Pressed
            _accel_press_tick = now;
            _accel_pressed = true;
            ESP32BASE_LOG_D("BtnDrv", "ACCEL pressed");
        } else {
            // Released
            if (_accel_pressed) {
                uint32_t duration = now - _accel_press_tick;
                if (duration < 1000 && !_both_long_triggered) {
                    _accel_pressed = false;
                    ESP32BASE_LOG_D("BtnDrv", "ACCEL short press (%lums)", static_cast<unsigned long>(duration));
                    return BTN_ACCEL_SHORT;
                }
                _accel_pressed = false;
            }
        }
    }

    // Decelerator
    if (decel_changed) {
        if (_stable_decel == LOW) {
            // Pressed
            _decel_press_tick = now;
            _decel_pressed = true;
            ESP32BASE_LOG_D("BtnDrv", "DECEL pressed");
        } else {
            // Released
            if (_decel_pressed) {
                uint32_t duration = now - _decel_press_tick;
                if (duration < 1000 && !_both_long_triggered) {
                    _decel_pressed = false;
                    ESP32BASE_LOG_D("BtnDrv", "DECEL short press (%lums)", static_cast<unsigned long>(duration));
                    return BTN_DECEL_SHORT;
                }
                _decel_pressed = false;
            }
        }
    }

    // 4. Check Long Press (Both buttons)
    if (_accel_pressed && _decel_pressed && !_both_long_triggered) {
        uint32_t start_time = max(_accel_press_tick, _decel_press_tick);
        if (now - start_time >= BOTH_LONG_THRESHOLD_MS) {
            _both_long_triggered = true;
            ESP32BASE_LOG_I("BtnDrv", "Both buttons long press detected");
            return BTN_BOTH_LONG;
        }
    }

    // Reset long press trigger when both released
    if (!_accel_pressed && !_decel_pressed) {
        _both_long_triggered = false;
    }

    return BTN_NONE;
}
