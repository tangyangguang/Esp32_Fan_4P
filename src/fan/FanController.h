#ifndef FAN_CONTROLLER_H
#define FAN_CONTROLLER_H

#include "fan/FanDriver.h"
#include "fan/ButtonDriver.h"
#include "fan/LedIndicator.h"
#include "fan/IRReceiverDriver.h"

#include <stdint.h>

enum SystemState {
    SYS_IDLE,
    SYS_RUNNING,
    SYS_SLEEP,
    SYS_ERROR,
    SYS_RECOVERING,
    SYS_COUNT
};

enum IRCodeChangeResult {
    IR_CODE_NO_CHANGE,
    IR_CODE_CHANGED,
    IR_CODE_SAVE_FAILED
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
    uint16_t getCurrentRpm() const;
    uint32_t getTimerRemaining() const;
    uint32_t getTotalRunDuration() const;
    uint32_t getBootRunDuration() const;
    bool isBlocked() const;
    bool isSleeping() const;

    // External command interfaces
    bool setSpeed(uint8_t speed);
    bool setTimer(uint32_t seconds);
    bool stop();
    bool resetFactory();
    void notifyUserAction();
    IRCodeChangeResult clearIRCode(uint8_t key_index);

    // Configuration getters/setters
    uint8_t getMinEffectiveSpeed() const;
    bool setMinEffectiveSpeed(uint8_t speed);
    uint16_t getSoftStartTime() const;
    bool setSoftStartTime(uint16_t ms);
    uint16_t getSoftStopTime() const;
    bool setSoftStopTime(uint16_t ms);
    uint16_t getBlockDetectTime() const;
    bool setBlockDetectTime(uint16_t ms);
    uint16_t getSleepWaitTime() const;
    bool setSleepWaitTime(uint16_t seconds);
    bool getAutoRestore() const;
    bool setAutoRestore(bool enable);
    uint16_t getLedFlashDuration() const;
    bool setLedFlashDuration(uint16_t ms);
    uint8_t getRuntimeSaveIntervalMinutes() const;
    bool setRuntimeSaveIntervalMinutes(uint8_t minutes);
    bool applyConfig(uint8_t min_speed, uint16_t soft_start, uint16_t soft_stop,
                     uint16_t block_detect, uint16_t sleep_wait, uint16_t led_flash_ms,
                     uint8_t runtime_save_min, bool auto_restore, uint8_t* changed = nullptr);

private:
    void _handleIdle();
    void _handleRunning();
    void _handleSleep();
    void _handleError();
    void _handleRecovering();

    void _processButtonEvents();
    void _processIREvents();
    void _processTimer();
    void _processSleep();

    bool _applySpeed(uint8_t speed, bool force_save = false);
    bool _saveRuntimeState(bool force = false);
    void _syncGearFromSpeed(uint8_t speed);
    void _updateLedStatus();
    void _loadConfig();
    bool _saveIRCode(uint8_t key_index);
    uint32_t _recoveryTimeoutMs() const;

#ifdef UNIT_TEST
    void _saveConfig();
    void _saveIRCodes();
#endif

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
    uint32_t _boot_run_duration;
    uint32_t _last_run_tick;
    uint32_t _last_operation_tick;
    uint32_t _last_runtime_save_tick;
    uint16_t _sleep_wait_time;
    uint16_t _soft_start_time;
    uint16_t _soft_stop_time;
    uint16_t _block_detect_time;
    uint16_t _led_flash_duration_ms;
    uint8_t _runtime_save_interval_min;
    uint8_t _min_effective_speed;

    bool _is_sleeping;
    bool _auto_restore;
    uint32_t _recovery_start_tick;
    bool _recovery_attempting;
    uint32_t _last_timer_tick;
};

#endif
