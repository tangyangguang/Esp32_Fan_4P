#ifndef FAN_WEB_H
#define FAN_WEB_H

#include "fan/FanController.h"
#include "fan/IRReceiverDriver.h"

class FanWeb {
public:
    FanWeb(FanController& controller, IRReceiverDriver& ir);

    // Page handlers (public for testing)
    static void handleStatusPage();
    static void handleConfigPage();

    // API handlers (public for testing)
    static void handleApiStatus();
    static void handleApiSpeed();
    static void handleApiTimer();
    static void handleApiStop();
    static void handleApiConfig();
    static void handleApiIrLearn();

private:
    static FanController* _controller;
    static IRReceiverDriver* _ir;
};

#endif
