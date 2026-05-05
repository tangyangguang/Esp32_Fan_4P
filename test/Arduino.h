// Mock Arduino.h for native testing
#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// Arduino types
typedef uint8_t byte;
typedef bool boolean;

// Pin modes
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define FALLING 0x2
#define RISING 0x3
#define CHANGE 0x4
#define HIGH 0x1
#define LOW 0x0
#define IRAM_ATTR

// PROGMEM / PSTR no-ops for native
#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define F(s) (s)

// HTTP method constants (needed by FanWeb.cpp)
#define HTTP_GET  0
#define HTTP_POST 1
#define HTTP_ANY  2

// Undefine any existing ESP macro
#ifdef ESP
#undef ESP
#endif

// Mock functions (implemented in test file)
void pinMode(uint8_t pin, uint8_t mode);
uint8_t digitalRead(uint8_t pin);
void digitalWrite(uint8_t pin, uint8_t val);
void analogWriteFreq(uint32_t freq);
void analogWrite(uint8_t pin, uint8_t val);
void attachInterrupt(uint8_t pin, void (*handler)(), int mode);
void detachInterrupt(uint8_t pin);
uint8_t digitalPinToInterrupt(uint8_t pin);
uint32_t millis();
void yield();
void delay(uint32_t ms);

// min/max helpers
template<typename T> T max(T a, T b) { return a > b ? a : b; }
template<typename T> T min(T a, T b) { return a < b ? a : b; }

// Minimal String class (only the subset used by FanWeb.cpp)
class String {
public:
    String() { _buf[0] = '\0'; _len = 0; }
    String(const char* s) {
        if (s) { strncpy(_buf, s, sizeof(_buf) - 1); _buf[sizeof(_buf)-1] = '\0'; }
        else { _buf[0] = '\0'; }
        _len = strlen(_buf);
    }
    String(int v) { snprintf(_buf, sizeof(_buf), "%d", v); _len = strlen(_buf); }

    size_t length() const { return _len; }
    const char* c_str() const { return _buf; }

    int toInt() const { return atoi(_buf); }
    unsigned long toInt_ul() const { return (unsigned long)atoi(_buf); }

    bool operator==(const char* s) const { return strcmp(_buf, s) == 0; }
    bool operator!=(const char* s) const { return strcmp(_buf, s) != 0; }
    String& operator=(const char* s) {
        if (s) { strncpy(_buf, s, sizeof(_buf) - 1); _buf[sizeof(_buf)-1] = '\0'; }
        else { _buf[0] = '\0'; }
        _len = strlen(_buf);
        return *this;
    }

private:
    char _buf[128];
    size_t _len;
};

// ESP mock (as a class to match Arduino's ESP.restart() syntax)
class ESPClass {
public:
    void restart();
    bool restartCalled();
};
extern ESPClass ESP;

// WiFi mock
class WiFiClass {
public:
    int RSSI() { return -65; }
};
extern WiFiClass WiFi;

// Standard library aliases for Arduino compatibility
using ::atoi;
using ::strtoul;
using ::snprintf;
using ::strncpy;
using ::strlen;
using ::memset;
using ::memcpy;

#endif
