#ifndef FAN_WEB_H
#define FAN_WEB_H

#include "fan/FanController.h"
#include "fan/FanHistory.h"
#include "fan/IRReceiverDriver.h"

class FanWeb {
public:
    FanWeb(FanController& controller, IRReceiverDriver& ir, FanHistory* history = nullptr);

    // Page handlers (public for testing)
    static void handleStatusPage();
    static void handleConfigPage();
    static void handleHistoryPage();
    static void handleIrPage();

    // API handlers (public for testing)
    static void handleApiStatus();
    static void handleApiSpeed();
    static void handleApiTimer();
    static void handleApiStop();
    static void handleApiConfig();
    static void handleApiRuntimeReset();
    static void handleApiIrLearn();
    static void handleApiHistory();
    static void handleApiHistoryConfig();

private:
    static FanController* _controller;
    static IRReceiverDriver* _ir;
    static FanHistory* _history;
};

#endif
