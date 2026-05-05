#ifndef FAN_CONTROLLER_H
#define FAN_CONTROLLER_H

#include "fan/FanDriver.h"
#include "fan/ButtonDriver.h"
#include "fan/LedIndicator.h"
#include "fan/IRReceiverDriver.h"

#include <stdint.h>

enum SystemState {
    SYS_INIT,
    SYS_IDLE,
    SYS_RUNNING,
    SYS_SLEEP,
    SYS_ERROR,
    SYS_COUNT
};

class FanController {
public:
    FanController(FanDriver& fan, ButtonDriver& btn, LedIndicator& led, IRReceiverDriver& ir);

    bool begin();
    void tick();

    SystemState getState() const;
    uint8_t getCurrentGear() const;
    uint8_t getCurrentSpeed() const;
    uint8_t getTargetSpeed() const;
    uint32_t getTimerRemaining() const;
    uint32_t getTotalRunDuration() const;
    bool isBlocked() const;
    bool isSleeping() const;

    // External command interfaces
    bool setSpeed(uint8_t speed);
    bool setTimer(uint32_t seconds);
    bool stop();
    bool resetFactory();

    // Configuration getters/setters
    uint8_t getMinEffectiveSpeed() const;
    void setMinEffectiveSpeed(uint8_t speed);
    uint16_t getSoftStartTime() const;
    void setSoftStartTime(uint16_t ms);
    uint16_t getSoftStopTime() const;
    void setSoftStopTime(uint16_t ms);
    uint16_t getBlockDetectTime() const;
    void setBlockDetectTime(uint16_t ms);
    uint16_t getSleepWaitTime() const;
    void setSleepWaitTime(uint16_t seconds);
    bool getAutoRestore() const;
    void setAutoRestore(bool enable);
    void setWebPassword(const char* password);

    // Block recovery
    void attemptBlockRecovery();

private:
    void _handleInit();
    void _handleIdle();
    void _handleRunning();
    void _handleSleep();
    void _handleError();

    void _processButtonEvents();
    void _processIREvents();
    void _processTimer();
    void _processSleep();

    bool _applySpeed(uint8_t speed);
    void _saveRuntimeState();
    void _loadConfig();
    void _saveConfig();
    void _saveIRCodes();

#ifdef UNIT_TEST
public:
    void testSaveConfig() { _saveConfig(); _saveIRCodes(); }
#endif

    FanDriver& _fan;
    ButtonDriver& _btn;
    LedIndicator& _led;
    IRReceiverDriver& _ir;

    SystemState _state;
    uint8_t _current_gear;
    uint8_t _target_speed;
    uint32_t _timer_remaining;
    uint32_t _run_duration;
    uint32_t _last_run_tick;
    uint32_t _last_operation_tick;
    uint16_t _sleep_wait_time;
    uint16_t _soft_start_time;
    uint16_t _soft_stop_time;
    uint16_t _block_detect_time;
    uint8_t _min_effective_speed;

    bool _is_sleeping;
    uint32_t _sleep_entry_tick;
    bool _auto_restore;
    uint32_t _recovery_start_tick;
    bool _recovery_attempting;
    uint32_t _last_timer_tick;
};

#endif
