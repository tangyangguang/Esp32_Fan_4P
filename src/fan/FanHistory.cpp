#include "fan/FanHistory.h"

#include <stdio.h>
#include <string.h>

namespace {
const uint16_t SHORT_MIN_SAMPLE_MS = 100;
const uint16_t SHORT_MAX_SAMPLE_MS = 5000;
const uint16_t LONG_MIN_SAMPLE_S = 10;
const uint16_t LONG_MAX_SAMPLE_S = 600;
}

FanHistory::FanHistory()
    : _short()
    , _long() {
}

void FanHistory::begin() {
    clearRing(_short, DEFAULT_POINTS, DEFAULT_SHORT_SAMPLE_MS);
    clearRing(_long, DEFAULT_POINTS, static_cast<uint32_t>(DEFAULT_LONG_SAMPLE_S) * 1000UL);
}

void FanHistory::tick(uint32_t now_ms, uint8_t speed, uint8_t target_speed, uint16_t rpm) {
    sampleRing(_short, now_ms, speed, target_speed, rpm);
    sampleRing(_long, now_ms, speed, target_speed, rpm);
}

FanHistoryConfig FanHistory::config() const {
    FanHistoryConfig cfg;
    cfg.short_points = _short.capacity;
    cfg.short_sample_ms = static_cast<uint16_t>(_short.sample_ms);
    cfg.short_window_seconds = windowSeconds(_short.capacity, _short.sample_ms);
    cfg.long_points = _long.capacity;
    cfg.long_sample_s = static_cast<uint16_t>(_long.sample_ms / 1000UL);
    cfg.long_window_seconds = windowSeconds(_long.capacity, _long.sample_ms);
    return cfg;
}

bool FanHistory::configure(uint16_t short_points,
                           uint16_t short_sample_ms,
                           uint16_t long_points,
                           uint16_t long_sample_s,
                           char* error,
                           size_t error_len) {
    if (!validate(short_points,
                  short_sample_ms,
                  long_points,
                  long_sample_s,
                  error,
                  error_len)) {
        return false;
    }

    clearRing(_short, short_points, short_sample_ms);
    clearRing(_long, long_points, static_cast<uint32_t>(long_sample_s) * 1000UL);
    return true;
}

uint16_t FanHistory::count(FanHistoryRange range) const {
    return ringFor(range).count;
}

uint16_t FanHistory::capacity(FanHistoryRange range) const {
    return ringFor(range).capacity;
}

bool FanHistory::pointAt(FanHistoryRange range, uint16_t index, FanHistoryPoint* out) const {
    if (!out) return false;
    const Ring& ring = ringFor(range);
    if (index >= ring.count || ring.capacity == 0) return false;
    const uint16_t start = ring.count == ring.capacity ? ring.head : 0;
    const uint16_t pos = static_cast<uint16_t>((start + index) % ring.capacity);
    *out = ring.points[pos];
    return true;
}

uint32_t FanHistory::sequenceAt(FanHistoryRange range, uint16_t index) const {
    const Ring& ring = ringFor(range);
    if (index >= ring.count) return 0;
    return ring.next_seq - ring.count + index;
}

bool FanHistory::validate(uint16_t short_points,
                          uint16_t short_sample_ms,
                          uint16_t long_points,
                          uint16_t long_sample_s,
                          char* error,
                          size_t error_len) {
    if (short_points < MIN_POINTS || short_points > MAX_POINTS) {
        setError(error, error_len, "short_points must be 100..1200");
        return false;
    }
    if (short_sample_ms < SHORT_MIN_SAMPLE_MS || short_sample_ms > SHORT_MAX_SAMPLE_MS) {
        setError(error, error_len, "short_sample_ms must be 100..5000");
        return false;
    }
    if (long_points < MIN_POINTS || long_points > MAX_POINTS) {
        setError(error, error_len, "long_points must be 100..1200");
        return false;
    }
    if (long_sample_s < LONG_MIN_SAMPLE_S || long_sample_s > LONG_MAX_SAMPLE_S) {
        setError(error, error_len, "long_sample_s must be 10..600");
        return false;
    }

    setError(error, error_len, "");
    return true;
}

void FanHistory::setError(char* error, size_t error_len, const char* message) {
    if (!error || error_len == 0) return;
    strncpy(error, message ? message : "", error_len - 1);
    error[error_len - 1] = '\0';
}

uint32_t FanHistory::windowSeconds(uint16_t points, uint32_t sample_ms) {
    return (static_cast<uint32_t>(points) * sample_ms + 999UL) / 1000UL;
}

void FanHistory::clearRing(Ring& ring, uint16_t capacity, uint32_t sample_ms) {
    ring.head = 0;
    ring.count = 0;
    ring.capacity = capacity;
    ring.last_sample_ms = 0;
    ring.sample_ms = sample_ms;
    ring.next_seq = 1;
}

void FanHistory::sampleRing(Ring& ring, uint32_t now_ms, uint8_t speed, uint8_t target_speed, uint16_t rpm) {
    if (ring.capacity == 0) return;
    if (ring.count > 0 && static_cast<uint32_t>(now_ms - ring.last_sample_ms) < ring.sample_ms) {
        return;
    }
    ring.points[ring.head].t_ms = now_ms;
    ring.points[ring.head].rpm = rpm;
    ring.points[ring.head].speed = speed > 100 ? 100 : speed;
    ring.points[ring.head].target_speed = target_speed > 100 ? 100 : target_speed;
    ++ring.next_seq;
    ring.head = static_cast<uint16_t>((ring.head + 1U) % ring.capacity);
    if (ring.count < ring.capacity) {
        ++ring.count;
    }
    ring.last_sample_ms = now_ms;
}

const FanHistory::Ring& FanHistory::ringFor(FanHistoryRange range) const {
    return range == FAN_HISTORY_LONG ? _long : _short;
}
