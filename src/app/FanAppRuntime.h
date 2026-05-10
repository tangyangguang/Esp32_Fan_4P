#ifndef FAN_APP_RUNTIME_H
#define FAN_APP_RUNTIME_H

#include <stdint.h>

struct BootClearState {
    uint32_t press_start_ms;
    bool pressed;
    bool action_done;
    bool armed;
};

void fanAppConfigureBaseWebBeforeBegin();
void fanAppEnableConfigAuditBeforeBegin();
bool fanAppRegisterFanRoutes();
void fanAppHandleBootButton(uint8_t pin, BootClearState* state);

#endif
