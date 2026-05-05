// Mock IRremoteESP8266 headers for native testing
#ifndef IR_RECV_H
#define IR_RECV_H

#include <stdint.h>
#include <stddef.h>

// Minimal mock for IRrecv
class IRrecv {
public:
    IRrecv(uint8_t recvpin, uint16_t bufsize = 1024, uint16_t timeout = 50, bool use_ms = false) {}
    void enableIRIn() {}
    bool decode(void* results) { return false; }
    void resume() {}
};

// Mock decode_results
struct decode_results {
    uint8_t decode_type;
    uint64_t value;
};

#endif
