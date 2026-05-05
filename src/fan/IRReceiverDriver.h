#ifndef IR_RECEIVER_DRIVER_H
#define IR_RECEIVER_DRIVER_H

#include <stdint.h>

// IR receive buffer size (reduced from 1024 to 512 to save RAM)
static const uint16_t kCaptureBufferSize = 512;
// Timeout for IR receive (ms)
static const uint16_t kCaptureTimeout = 50;

enum IREvent {
    IR_EVENT_NONE,
    IR_EVENT_SPEED_UP,
    IR_EVENT_SPEED_DOWN,
    IR_EVENT_STOP,
    IR_EVENT_TIMER_30M,
    IR_EVENT_TIMER_1H,
    IR_EVENT_TIMER_2H
};

// Key indices for learning
enum IRKeyIndex {
    IR_KEY_SPEED_UP = 0,
    IR_KEY_SPEED_DOWN = 1,
    IR_KEY_STOP = 2,
    IR_KEY_TIMER_30M = 3,
    IR_KEY_TIMER_1H = 4,
    IR_KEY_TIMER_2H = 5,
    IR_KEY_COUNT = 6
};

class IRReceiverDriver {
public:
    IRReceiverDriver(uint8_t recv_pin);

    bool begin();
    IREvent getEvent();

    // Infrared learning
    bool startLearning(uint8_t key_index);
    bool isLearning() const;
    uint8_t getLearnedKeyIndex() const;
    uint32_t getLearningRemaining() const;  // seconds until timeout

    // Configuration
    void setKeyCode(uint8_t key_index, uint8_t protocol, uint64_t code);
    bool getKeyCode(uint8_t key_index, uint8_t* protocol, uint64_t* code) const;
    bool consumeLearnedCode();

    // Raw decode info (for debugging/learning display)
    uint8_t getLastProtocol() const;
    uint64_t getLastCode() const;

private:
    IREvent matchCode(uint8_t protocol, uint64_t code);

    uint8_t _recv_pin;
    bool _learning;
    uint8_t _learning_key_index;
    uint32_t _learning_start_tick;
    bool _learned_dirty;
    static const uint32_t LEARNING_TIMEOUT_MS = 10000;

    uint8_t _protocols[IR_KEY_COUNT];
    uint64_t _codes[IR_KEY_COUNT];

    // Last decoded values
    uint8_t _last_protocol;
    uint64_t _last_code;
};

#endif
