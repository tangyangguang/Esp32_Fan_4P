#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <stdint.h>

enum ButtonEvent {
    BTN_NONE,
    BTN_ACCEL_SHORT,    // 加速键短按
    BTN_DECEL_SHORT,    // 减速键短按
    BTN_BOTH_LONG       // 两键同时长按 >5s
};

class ButtonDriver {
public:
    ButtonDriver(uint8_t accel_pin, uint8_t decel_pin);

    bool begin();
    ButtonEvent getEvent();  // 每帧调用

private:
    uint8_t _accel_pin;
    uint8_t _decel_pin;

    uint8_t _last_accel;
    uint8_t _last_decel;
    uint8_t _stable_accel;
    uint8_t _stable_decel;

    uint32_t _accel_press_tick;
    uint32_t _decel_press_tick;
    uint32_t _accel_debounce_tick;
    uint32_t _decel_debounce_tick;
    bool _accel_pressed;
    bool _decel_pressed;
    bool _both_long_triggered;

    static const uint32_t BOTH_LONG_THRESHOLD_MS = 5000;
    static const uint32_t DEBOUNCE_MS = 50;
};

#endif
