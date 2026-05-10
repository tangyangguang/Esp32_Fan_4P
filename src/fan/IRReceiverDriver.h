#ifndef IR_RECEIVER_DRIVER_H
#define IR_RECEIVER_DRIVER_H

#include <stdint.h>
#include <IRrecv.h>

static const uint16_t kCaptureBufferSize = 512;
static const uint16_t kCaptureTimeout = 50;

enum IREvent {
    IR_EVENT_NONE,
    IR_EVENT_SPEED_UP,
    IR_EVENT_SPEED_DOWN,
    IR_EVENT_STOP,
    IR_EVENT_TIMER_30M,
    IR_EVENT_TIMER_1H,
    IR_EVENT_TIMER_2H,
    IR_EVENT_TIMER_4H,
    IR_EVENT_TIMER_8H
};

// Key indices for learning
enum IRKeyIndex {
    IR_KEY_SPEED_UP = 0,
    IR_KEY_SPEED_DOWN = 1,
    IR_KEY_STOP = 2,
    IR_KEY_TIMER_30M = 3,
    IR_KEY_TIMER_1H = 4,
    IR_KEY_TIMER_2H = 5,
    IR_KEY_TIMER_4H = 6,
    IR_KEY_TIMER_8H = 7,
    IR_KEY_COUNT = 8
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
    uint32_t getLearnedSequence() const;
    uint32_t getLearnRejectSequence() const;
    uint8_t getDuplicateKeyIndex() const;

#ifdef UNIT_TEST
    void testQueueEvent(IREvent event);
    void testMarkLearned(uint8_t key_index, uint8_t protocol, uint64_t code);
    bool testLearnDecoded(uint8_t key_index, uint8_t protocol, uint64_t code);
#endif

private:
    IREvent matchCode(uint8_t protocol, uint64_t code);
    bool findDuplicateKey(uint8_t protocol, uint64_t code, uint8_t except_key, uint8_t* duplicate_key) const;
    bool completeLearning(uint8_t protocol, uint64_t code);

    uint8_t _recv_pin;
    IRrecv _irrecv;
    bool _initialized;
    bool _learning;
    uint8_t _learning_key_index;
    uint32_t _learning_start_tick;
    bool _learned_dirty;
    uint32_t _learned_sequence;
    uint32_t _learn_reject_sequence;
    uint8_t _duplicate_key_index;
    uint32_t _ignore_until_tick;
    static const uint32_t LEARNING_TIMEOUT_MS = 10000;
    static const uint32_t DUPLICATE_LEARN_IGNORE_MS = 500;
    static const uint32_t POST_LEARN_IGNORE_MS = 1200;

    uint8_t _protocols[IR_KEY_COUNT];
    uint64_t _codes[IR_KEY_COUNT];

    // Last decoded values
    uint8_t _last_protocol;
    uint64_t _last_code;

#ifdef UNIT_TEST
    IREvent _test_event;
#endif
};

#endif
