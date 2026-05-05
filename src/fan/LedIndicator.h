#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <stdint.h>

enum LedMode {
    LED_OFF,           // 熄灭
    LED_ON,            // 常亮
    LED_SLOW_BLINK,    // 慢闪 1Hz
    LED_FAST_BLINK,    // 快闪 5Hz
    LED_SINGLE_FLASH   // 闪1下后恢复原状态
};

class LedIndicator {
public:
    LedIndicator(uint8_t pin, bool active_low = true);

    bool begin();
    void tick();  // 每帧调用

    void setGear(uint8_t gear);  // 0-4 档，自动设置 PWM 亮度
    void setOverride(LedMode mode);  // WiFi/故障等覆盖模式
    void flashOnce();  // 操作反馈闪1下

private:
    void update();

    uint8_t _pin;
    bool _active_low;
    uint8_t _current_gear;
    LedMode _override_mode;
    LedMode _saved_mode;
    uint32_t _last_toggle;
    bool _led_state;
    uint32_t _flash_start;
    bool _flashing;

    static const uint32_t SLOW_BLINK_INTERVAL = 1000;  // 1Hz
    static const uint32_t FAST_BLINK_INTERVAL = 200;   // 5Hz
    static const uint32_t SINGLE_FLASH_DURATION = 300; // 300ms
};

#endif
