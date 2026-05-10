#ifndef FAN_DRIVER_H
#define FAN_DRIVER_H

#include <stdint.h>

enum FanState {
    FAN_STATE_IDLE,
    FAN_STATE_SOFT_START,
    FAN_STATE_RUNNING,
    FAN_STATE_SOFT_STOP,
    FAN_STATE_BLOCKED
};

class FanDriver {
public:
    FanDriver(uint8_t pwm_pin, uint8_t tach_pin);

    bool begin();
    void tick();

    bool setSpeed(uint8_t speed);
    uint8_t getSpeed() const;
    uint16_t getRpm() const;
    FanState getState() const;

    void setSoftStartTime(uint16_t ms);
    void setSoftStopTime(uint16_t ms);
    void setBlockDetectTime(uint16_t ms);
    void setMinEffectiveSpeed(uint8_t speed);

    bool isBlocked() const;
    void resetBlock();

private:
    static void tachISR();
    static FanDriver* s_instance;
    void _writePwm(uint8_t speed);

    uint8_t _pwm_pin;
    uint8_t _tach_pin;
    uint8_t _current_speed;
    uint8_t _target_speed;
    uint8_t _ramp_start_speed;
    uint16_t _soft_start_time;
    uint16_t _soft_stop_time;
    uint16_t _block_detect_time;
    uint8_t _min_effective_speed;
    uint32_t _soft_start_tick;
    uint32_t _block_start_tick;
    FanState _state;

    // Tachometer
    volatile uint32_t _tach_count;
    volatile uint32_t _last_tach_ms;
    uint16_t _rpm;
    uint32_t _last_rpm_update_ms;
};

#endif
