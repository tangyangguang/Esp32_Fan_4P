#ifndef FAN_HISTORY_H
#define FAN_HISTORY_H

#include <stddef.h>
#include <stdint.h>

enum FanHistoryRange : uint8_t {
    FAN_HISTORY_SHORT,
    FAN_HISTORY_LONG
};

struct FanHistoryPoint {
    uint32_t t_ms;
    uint16_t rpm;
    uint8_t speed;
    uint8_t target_speed;
};

struct FanHistoryConfig {
    uint16_t short_points;
    uint16_t short_sample_ms;
    uint32_t short_window_seconds;
    uint16_t long_points;
    uint16_t long_sample_s;
    uint32_t long_window_seconds;
};

class FanHistory {
public:
    static const uint16_t MIN_POINTS = 100;
    static const uint16_t MAX_POINTS = 1200;
    static const uint16_t DEFAULT_POINTS = 500;
    static const uint16_t DEFAULT_SHORT_SAMPLE_MS = 500;
    static const uint16_t DEFAULT_LONG_SAMPLE_S = 10;

    FanHistory();

    void begin();
    void tick(uint32_t now_ms, uint8_t speed, uint8_t target_speed, uint16_t rpm);

    FanHistoryConfig config() const;
    bool configure(uint16_t short_points,
                   uint16_t short_sample_ms,
                   uint16_t long_points,
                   uint16_t long_sample_s,
                   char* error,
                   size_t error_len);

    uint16_t count(FanHistoryRange range) const;
    uint16_t capacity(FanHistoryRange range) const;
    bool pointAt(FanHistoryRange range, uint16_t index, FanHistoryPoint* out) const;
    uint32_t sequenceAt(FanHistoryRange range, uint16_t index) const;

    static bool validate(uint16_t short_points,
                         uint16_t short_sample_ms,
                         uint16_t long_points,
                         uint16_t long_sample_s,
                         char* error,
                         size_t error_len);

private:
    struct Ring {
        FanHistoryPoint points[MAX_POINTS];
        uint16_t head;
        uint16_t count;
        uint16_t capacity;
        uint32_t last_sample_ms;
        uint32_t sample_ms;
        uint32_t next_seq;
    };

    static void setError(char* error, size_t error_len, const char* message);
    static uint32_t windowSeconds(uint16_t points, uint32_t sample_ms);
    void clearRing(Ring& ring, uint16_t capacity, uint32_t sample_ms);
    void sampleRing(Ring& ring, uint32_t now_ms, uint8_t speed, uint8_t target_speed, uint16_t rpm);
    const Ring& ringFor(FanHistoryRange range) const;

    Ring _short;
    Ring _long;
};

#endif
