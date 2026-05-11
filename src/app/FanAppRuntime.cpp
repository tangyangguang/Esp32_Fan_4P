#include "app/FanAppRuntime.h"

#ifdef UNIT_TEST
#include "Arduino.h"
#include "Esp32Base.h"
#else
#include <Arduino.h>
#include <Esp32Base.h>
#endif

#include "web/FanWeb.h"

namespace {
const uint32_t BOOT_CLEAR_WIFI_MS = 5000;
}

void fanAppConfigureBaseWebBeforeBegin() {
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
    Esp32BaseWeb::setDeviceName("ESP Fan");
    Esp32BaseWeb::setHomePath("/fan");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
}

void fanAppEnableConfigAuditBeforeBegin() {
    Esp32BaseConfig::enableConfigAudit(true);
    Esp32BaseConfig::enableConfigReadAudit(true);
}

bool fanAppRegisterFanRoutes() {
    bool ok = true;
    if (!Esp32BaseWeb::addPage("/fan", "Fan", FanWeb::handleStatusPage)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /fan");
        ok = false;
    }
    if (!Esp32BaseWeb::addPage("/history", "History", FanWeb::handleHistoryPage)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /history");
        ok = false;
    }
    if (!Esp32BaseWeb::addPage("/config", "Settings", FanWeb::handleConfigPage)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /config");
        ok = false;
    }
    if (!Esp32BaseWeb::addApi("/api/status", FanWeb::handleApiStatus)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /api/status");
        ok = false;
    }
    if (!Esp32BaseWeb::addApi("/api/speed", FanWeb::handleApiSpeed)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /api/speed");
        ok = false;
    }
    if (!Esp32BaseWeb::addApi("/api/timer", FanWeb::handleApiTimer)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /api/timer");
        ok = false;
    }
    if (!Esp32BaseWeb::addApi("/api/stop", FanWeb::handleApiStop)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /api/stop");
        ok = false;
    }
    if (!Esp32BaseWeb::addApi("/api/config", FanWeb::handleApiConfig)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /api/config");
        ok = false;
    }
    if (!Esp32BaseWeb::addApi("/api/runtime/reset", FanWeb::handleApiRuntimeReset)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /api/runtime/reset");
        ok = false;
    }
    if (!Esp32BaseWeb::addApi("/api/ir/learn", FanWeb::handleApiIrLearn)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /api/ir/learn");
        ok = false;
    }
    if (!Esp32BaseWeb::addApi("/api/history", FanWeb::handleApiHistory)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /api/history");
        ok = false;
    }
    if (!Esp32BaseWeb::addApi("/api/history/config", FanWeb::handleApiHistoryConfig)) {
        ESP32BASE_LOG_E("main", "custom Web route registration failed: /api/history/config");
        ok = false;
    }
    if (!ok) {
        ESP32BASE_LOG_E("main", "custom Web route registration incomplete");
    }
    return ok;
}

void fanAppHandleBootButton(uint8_t pin, BootClearState* state) {
    if (!state) return;

    const bool pressed = digitalRead(pin) == LOW;
    const uint32_t now = millis();

    if (!state->armed) {
        if (!pressed) {
            state->armed = true;
        }
        return;
    }

    if (pressed && !state->pressed) {
        state->pressed = true;
        state->action_done = false;
        state->press_start_ms = now;
    } else if (!pressed && state->pressed) {
        state->pressed = false;
        state->action_done = false;
    }

    if (pressed && state->pressed && !state->action_done &&
        now - state->press_start_ms >= BOOT_CLEAR_WIFI_MS) {
        state->action_done = true;
        ESP32BASE_LOG_W("main", "BOOT held 5s, clearing WiFi credentials");
        bool ok = Esp32BaseConfig::flushAll();
        ok = Esp32BaseWiFi::clearCredentials() && ok;
        if (!ok) {
            ESP32BASE_LOG_E("main", "BOOT clear WiFi failed; restart skipped");
            return;
        }
        delay(300);
        Esp32BaseSystem::restart("boot clear wifi");
    }
}
