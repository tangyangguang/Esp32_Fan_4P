#ifndef ESP32BASE_MOCK_H
#define ESP32BASE_MOCK_H

#include <stddef.h>
#include <stdint.h>

#include "Arduino.h"

class Esp32BaseLog {
public:
    enum Level : uint8_t {
        NONE = 0,
        ERROR = 1,
        WARN = 2,
        INFO = 3,
        DEBUG = 4,
        VERBOSE = 5
    };
};

#define ESP32BASE_LOG_D(tag, fmt, ...) ((void)0)
#define ESP32BASE_LOG_I(tag, fmt, ...) ((void)0)
#define ESP32BASE_LOG_W(tag, fmt, ...) ((void)0)
#define ESP32BASE_LOG_E(tag, fmt, ...) ((void)0)

class Esp32BaseConfig {
public:
    static bool begin();
    static void handle();
    static bool isReady();
    static bool setStr(const char* ns, const char* key, const char* value);
    static bool getStr(const char* ns, const char* key, char* out, size_t len, const char* def = "");
    static bool setInt(const char* ns, const char* key, int32_t value);
    static int32_t getInt(const char* ns, const char* key, int32_t def = 0);
    static bool setIntDeferred(const char* ns, const char* key, int32_t value, uint32_t delayMs = 1000);
    static bool setBool(const char* ns, const char* key, bool value);
    static bool getBool(const char* ns, const char* key, bool def = false);
    static bool setBoolDeferred(const char* ns, const char* key, bool value, uint32_t delayMs = 1000);
    static bool setStrDeferred(const char* ns, const char* key, const char* value, uint32_t delayMs = 1000);
    static bool flushAll();
    static bool clearNamespace(const char* ns);
    static void enableConfigAudit(bool enabled);
    static void enableConfigReadAudit(bool enabled);
};

class Esp32BaseWiFi {
public:
    enum State : uint8_t {
        IDLE,
        CONNECTING,
        CONNECTED,
        CONFIG_PORTAL,
        RETRY_BACKOFF,
        FAILED
    };

    static bool isConnected();
    static bool ip(char* out, size_t len);
    static int32_t rssi();
    static void setPowerSave(bool enabled);
    static bool powerSave();
    static bool clearCredentials();
    static State state();
    static const char* stateName();
};

class Esp32BaseNtp {
public:
    static bool isTimeSynced();
    static bool formatTime(char* out, size_t len, const char* fmt);
};

class Esp32BaseWeb {
public:
    using Handler = void (*)();
    static bool checkAuth();
    static void setAuth(const char* user, const char* pass);
    static bool addPage(const char* path, const char* title, Handler handler);
    static bool addApi(const char* path, Handler handler);
    static bool hasParam(const char* name);
    static bool getParam(const char* name, char* out, size_t len);
    static void sendHeader(const char* title = nullptr);
    static void sendFooter();
    static void sendChunk(const char* text);
    static void writeHtmlEscaped(const char* text);
    static void sendText(int code, const char* text);
    static void sendHtml(int code, const char* html);
    static void sendJson(int code, const char* json);
};

#endif
