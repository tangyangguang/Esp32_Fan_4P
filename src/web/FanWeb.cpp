#include "web/FanWeb.h"

#include <Esp32Base.h>

FanController* FanWeb::_controller = nullptr;
IRReceiverDriver* FanWeb::_ir = nullptr;

FanWeb::FanWeb(FanController& controller, IRReceiverDriver& ir) {
    _controller = &controller;
    _ir = &ir;
}

// ----------------------------------------------------------------------------
// FanWeb Implementation using Esp32BaseWeb (PROGMEM + Chunked)
// ----------------------------------------------------------------------------

// Static HTML parts in PROGMEM
static const char APP_STYLE[] PROGMEM =
    "<style>"
    "body{padding:10px;max-width:560px}h2{font-size:1.18em;margin:2px 0 8px}h3{font-size:.98em;margin:0 0 8px}"
    ".top{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:center;margin-bottom:8px}.muted{color:#667085;font-size:.82em}"
    ".speed{font-size:2.15em;font-weight:700;line-height:1;text-align:right}.unit{font-size:.38em;color:#667085}"
    ".panel{border:1px solid #d9e0e8;border-radius:7px;padding:10px;margin:8px 0;background:#fff}.tight{margin-top:6px}"
    ".stats{display:grid;grid-template-columns:1fr 1fr;gap:6px}.stat{background:#f7f9fb;border:1px solid #edf1f5;border-radius:6px;padding:7px 8px;min-width:0}"
    ".stat span{display:block;color:#667085;font-size:.75em}.stat b{display:block;font-size:.95em;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
    ".chips{display:grid;grid-template-columns:repeat(5,1fr);gap:6px}.chips3{grid-template-columns:repeat(3,1fr)}"
    "button,.btn{background:#1769aa;color:#fff;border:0;border-radius:6px;padding:10px 7px;cursor:pointer;text-align:center;text-decoration:none;font-size:.92em;min-height:38px;box-sizing:border-box}"
    "button.secondary,.btn.secondary{background:#667085}button.danger{background:#b93815}.row{display:grid;grid-template-columns:1fr 76px;gap:7px;align-items:center;margin-top:4px}"
    ".actions{display:grid;grid-template-columns:1fr 1fr;gap:7px;margin-top:9px}.actions button{min-height:40px}"
    ".formgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.field{min-width:0}label{display:block;font-size:.78em;color:#344054;margin:0 0 3px}"
    "input,select{width:100%;min-height:38px;box-sizing:border-box;border:1px solid #c8d0da;border-radius:6px;padding:8px;background:#fff;font-size:1em;margin:0}"
    ".row input:not([type=submit]){height:42px;min-height:42px;margin:0!important;padding:0 10px;display:block}.row button{height:42px;min-height:42px;padding:0 7px;align-self:center}"
    ".oktxt{color:#087443}.errtxt{color:#b42318}.help{color:#667085;font-size:.76em;margin:3px 0 0;line-height:1.3}.nav2{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-top:8px}"
    ".savebar{display:block;margin-top:8px;padding:7px 8px;border-radius:6px;background:#f7f9fb;border:1px solid #edf1f5;font-size:.82em}.savebar.oktxt{background:#ecfdf3;border-color:#abefc6}.savebar.errtxt{background:#fef3f2;border-color:#fecdca}"
    "pre.log{white-space:pre-wrap;word-break:break-word;background:#111827;color:#e5e7eb;border-radius:7px;padding:9px;max-height:430px;overflow:auto;font-size:.82em}"
    "@media(max-width:390px){body{padding:8px}.chips{grid-template-columns:repeat(3,1fr)}.chips3{grid-template-columns:repeat(3,1fr)}.formgrid{grid-template-columns:1fr}.nav2{grid-template-columns:1fr 1fr}.speed{font-size:1.9em}}"
    "</style>";

static const char FAN_PAGE_TOP[] PROGMEM =
    "<div class=top><div><h2>Fan</h2><div class=muted>Fast presets, exact input</div></div><div class=speed><span id=tgtTop>";
static const char FAN_SPEED_END[] PROGMEM =
    "</span><span class=unit>%</span></div></div><div class='panel tight'><div class=stats>";
static const char FAN_STATUS_MID[] PROGMEM =
    "</div></div><div class=panel><h3>Speed</h3><div class=chips>"
    "<button onclick='spd(0)'>Off</button><button onclick='spd(25)'>25</button><button onclick='spd(50)'>50</button><button onclick='spd(75)'>75</button><button onclick='spd(100)'>100</button>"
    "</div><label>Custom (%)</label><div class=row><input id=sv type=number min=0 max=100 value='";
static const char FAN_SPEED_INPUT_END[] PROGMEM =
    "'><button onclick='spd(document.getElementById(\"sv\").value)'>Apply</button></div></div>"
    "<div class=panel><h3>Timer</h3><div class='chips chips3'>"
    "<button onclick='tm(30)'>30 min</button><button onclick='tm(60)'>1 h</button><button onclick='tm(120)'>2 h</button>"
    "</div><label>Custom (min)</label><div class=row><input id=tv type=number min=0 max=5940 value='";
static const char FAN_TIMER_INPUT_END[] PROGMEM =
    "'><button onclick='tm(document.getElementById(\"tv\").value)'>Set</button></div>"
    "<div class=actions><button class=secondary onclick='tm(0)'>Cancel timer</button><button class=danger onclick='stopFan()'>Stop fan</button></div>"
    "<div class=help>Cancel timer keeps the fan running. Stop fan turns the fan off and clears the timer.</div></div>"
    "<div class=nav2><a class=btn href='/config'>Settings</a><a class=btn href='/esp32base'>Base</a>"
    "<a class=btn href='/esp32base/logs'>Logs</a><a class=btn href='/esp32base/ota'>OTA</a><a class=btn href='/esp32base/reboot'>Reboot</a></div>"
    "<script>"
    "var rem=";
static const char FAN_SCRIPT_TIMER_MID[] PROGMEM =
    ";function e(i,v){var x=document.getElementById(i);if(x)x.textContent=v}"
    "function tf(s){s=parseInt(s||0);if(s<=0)return'Off';var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),r=s%60;return(h?h+'h ':'')+m+'m '+r+'s'}"
    "function draw(d){e('st',d.state);e('tgt',d.target_speed+'%');e('tgtTop',d.target_speed);e('out',d.speed+'%');e('tim',tf(d.timer_remaining));e('run',Math.floor(d.run_duration/3600)+' h');e('ip',d.ip);e('rssi',d.rssi+' dBm');e('clk',d.clock);e('blk',d.blocked?'Yes':'No');document.getElementById('blk').className=d.blocked?'errtxt':'oktxt';rem=d.timer_remaining;document.getElementById('sv').value=d.target_speed;document.getElementById('tv').value=Math.floor(rem/60)}"
    "function poll(){fetch('/api/status').then(r=>r.json()).then(j=>{if(j.ok)draw(j.data)})}"
    "function post(u,b,cb){fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(()=>{if(cb)cb();setTimeout(poll,250)})}"
    "function spd(v){v=parseInt(v||0);if(v>=0&&v<=100){e('tgt',v+'%');e('tgtTop',v);document.getElementById('sv').value=v;post('/api/speed','speed='+v)}}"
    "function tm(v){v=parseInt(v||0);if(v>=0&&v<=5940){rem=v*60;e('tim',tf(rem));document.getElementById('tv').value=v;post('/api/timer','seconds='+rem)}}"
    "function stopFan(){rem=0;e('tim','Off');e('tgt','0%');e('tgtTop','0');post('/api/stop','')}"
    "setInterval(function(){if(rem>0){rem--;e('tim',tf(rem))}},1000);setInterval(poll,3000)"
    "</script>";

void FanWeb::handleStatusPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    ESP32BASE_LOG_I("FanWeb", "page_view path=/fan");

    Esp32BaseWeb::sendHeader();
    Esp32BaseWeb::sendChunk(APP_STYLE);
    Esp32BaseWeb::sendChunk(FAN_PAGE_TOP);

    char buf[96];

    snprintf(buf, sizeof(buf), "%d", _controller->getTargetSpeed());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(FAN_SPEED_END);
    
    // State
    Esp32BaseWeb::sendChunk("<div class=stat><span>State</span><b id=st>");
    switch (_controller->getState()) {
        case SYS_IDLE: Esp32BaseWeb::sendChunk("Idle"); break;
        case SYS_RUNNING: Esp32BaseWeb::sendChunk("Running"); break;
        case SYS_SLEEP: Esp32BaseWeb::sendChunk("Sleep"); break;
        case SYS_ERROR: Esp32BaseWeb::sendChunk("Error"); break;
        default: Esp32BaseWeb::sendChunk("Unknown"); break;
    }
    Esp32BaseWeb::sendChunk("</b></div>");
    
    // Speed
    snprintf(buf, sizeof(buf), "<div class=stat><span>Target</span><b id=tgt>%d%%</b></div>", _controller->getTargetSpeed());
    Esp32BaseWeb::sendChunk(buf);

    snprintf(buf, sizeof(buf), "<div class=stat><span>Output</span><b id=out>%d%%</b></div>", _controller->getCurrentSpeed());
    Esp32BaseWeb::sendChunk(buf);

    // Timer
    uint32_t timer = _controller->getTimerRemaining();
    if (timer > 0) {
        uint32_t h = timer / 3600, m = (timer % 3600) / 60, s = timer % 60;
        snprintf(buf, sizeof(buf), "%luh %lum %lus", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    } else {
        strcpy(buf, "Off");
    }
    Esp32BaseWeb::sendChunk("<div class=stat><span>Timer</span><b id=tim>");
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk("</b></div>");

    // Run Time
    snprintf(buf, sizeof(buf), "<div class=stat><span>Run time</span><b id=run>%lu h</b></div>",
             (unsigned long)(_controller->getTotalRunDuration() / 3600));
    Esp32BaseWeb::sendChunk(buf);

    // WiFi
    char ip[24];
    strlcpy(ip, "Disconnected", sizeof(ip));
    long rssi = 0;
    if (Esp32BaseWiFi::isConnected()) {
        Esp32BaseWiFi::ip(ip, sizeof(ip));
        rssi = Esp32BaseWiFi::rssi();
    }
    Esp32BaseWeb::sendChunk("<div class=stat><span>IP</span><b id=ip>");
    Esp32BaseWeb::sendChunk(ip);
    Esp32BaseWeb::sendChunk("</b></div>");
    snprintf(buf, sizeof(buf), "<div class=stat><span>RSSI</span><b id=rssi>%ld dBm</b></div>", rssi);
    Esp32BaseWeb::sendChunk(buf);

    // NTP
    if (Esp32BaseNtp::isTimeSynced()) {
        Esp32BaseNtp::formatTime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S");
    } else {
        strcpy(buf, "N/A");
    }
    Esp32BaseWeb::sendChunk("<div class=stat><span>Clock</span><b id=clk>");
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk("</b></div>");

    // Blocked
    snprintf(buf, sizeof(buf), "<div class=stat><span>Blocked</span><b id=blk class=%s>%s</b></div>",
             _controller->isBlocked() ? "errtxt" : "oktxt",
             _controller->isBlocked() ? "Yes" : "No");
    Esp32BaseWeb::sendChunk(buf);

    Esp32BaseWeb::sendChunk(FAN_STATUS_MID);
    snprintf(buf, sizeof(buf), "%d", _controller->getTargetSpeed());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(FAN_SPEED_INPUT_END);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(_controller->getTimerRemaining() / 60));
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(FAN_TIMER_INPUT_END);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)_controller->getTimerRemaining());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(FAN_SCRIPT_TIMER_MID);

    Esp32BaseWeb::sendFooter();
}

// Config Page HTML parts
static const char CONFIG_PAGE_TOP[] PROGMEM = 
    "<div class=top><div><h2>Settings</h2><div class=muted>Fan, access, IR</div></div><a class='btn secondary' href='/fan'>Fan</a></div>"
    "<form class=panel onsubmit='saveCfg(this);return false'>"
    "<h3>Fan behavior</h3><div class=formgrid><div class=field><label>Min speed (%)</label><input type=number name=min_speed min=0 max=50 value='";
static const char CONFIG_MIN_END[] PROGMEM = "'><div class=help>Low commands rise to this value.</div></div>"
    "<div class=field><label>Sleep wait (s)</label><input type=number name=sleep_wait min=0 max=3600 value='";
static const char CONFIG_SLEEP_END[] PROGMEM = "'><div class=help>Stopped this long before modem sleep.</div></div>"
    "<div class=field><label>Soft start (ms)</label><input type=number name=soft_start min=0 max=10000 value='";
static const char CONFIG_START_END[] PROGMEM = "'><div class=help>Ramp up time.</div></div>"
    "<div class=field><label>Soft stop (ms)</label><input type=number name=soft_stop min=0 max=10000 value='";
static const char CONFIG_STOP_END[] PROGMEM = "'><div class=help>Ramp down time.</div></div>"
    "<div class=field><label>Block detect (ms)</label><input type=number name=block_detect min=100 max=5000 value='";
static const char CONFIG_BLOCK_END[] PROGMEM = "'><div class=help>No RPM for this long means blocked.</div></div>"
    "<div class=field><label>Power-on restore</label><select name=auto_restore><option value=1 ";
static const char CONFIG_AUTO_END[] PROGMEM = ">Enabled</option><option value=0 ";
static const char CONFIG_AUTO_END2[] PROGMEM = ">Disabled</option></select><div class=help>Restore last speed and timer after reboot.</div></div></div>"
    "<h3>Web access</h3><label>Admin password</label><div class=row><input type=password name=password maxlength=23 placeholder='Keep current'>"
    "<button id=saveBtn type=submit>Save</button></div><div class=help>User is admin. Blank keeps the old password.</div><span id=saveMsg class='savebar muted'>Ready</span></form>"
    "<div class=panel><h3>IR learning</h3><div class='chips chips3'>"
    "<button onclick='learn(0,\"Speed Up\")'>Speed Up</button><button onclick='learn(1,\"Speed Down\")'>Speed Down</button><button onclick='learn(2,\"Stop\")'>Stop</button>"
    "<button onclick='learn(3,\"30 min\")'>30 min</button><button onclick='learn(4,\"1 h\")'>1 h</button><button onclick='learn(5,\"2 h\")'>2 h</button>"
    "</div><div class=help>Press one, then point the remote within 10 seconds.</div></div>"
    "<div class=nav2><a class=btn href='/fan'>Fan</a><a class=btn href='/esp32base'>Base</a>"
    "<a class=btn href='/esp32base/logs'>Logs</a><a class=btn href='/esp32base/ota'>OTA</a><a class=btn href='/esp32base/reboot'>Reboot</a></div>"
    "<script>"
    "function setMsg(t,c){var m=document.getElementById('saveMsg');m.textContent=t;m.className='savebar '+c}"
    "function applyCfg(d,f){if(!d)return;f.min_speed.value=d.min_effective_speed;f.sleep_wait.value=d.sleep_wait;f.soft_start.value=d.soft_start;f.soft_stop.value=d.soft_stop;f.block_detect.value=d.block_detect;f.auto_restore.value=d.auto_restore?1:0;f.password.value=''}"
    "function saveCfg(f){var b=document.getElementById('saveBtn');b.disabled=true;b.textContent='Saving';setMsg('Saving...','muted');fetch('/api/config',{method:'POST',body:new URLSearchParams(new FormData(f))}).then(r=>r.json().then(j=>({ok:r.ok,j:j}))).then(x=>{b.disabled=false;b.textContent='Save';if(x.ok&&x.j.ok){applyCfg(x.j.data,f);var n=x.j.changed||0;setMsg('Saved - '+(n?n+' changed':'no changes')+' - '+new Date().toLocaleTimeString(),'oktxt')}else{setMsg('Save failed','errtxt')}}).catch(()=>{b.disabled=false;b.textContent='Save';setMsg('Save failed: network error','errtxt')})}"
    "function learn(i,n){fetch('/api/ir/learn',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key_index='+i}).then(r=>r.json()).then(d=>alert(d.ok?'Learning '+n:'Learn failed'))}"
    "</script>";

void FanWeb::handleConfigPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    ESP32BASE_LOG_I("FanWeb", "page_view path=/config");

    Esp32BaseWeb::sendHeader();
    Esp32BaseWeb::sendChunk(APP_STYLE);
    Esp32BaseWeb::sendChunk(CONFIG_PAGE_TOP);

    char buf[32];
    
    // Min Speed
    snprintf(buf, sizeof(buf), "%d", _controller->getMinEffectiveSpeed());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(CONFIG_MIN_END);

    // Sleep Wait
    snprintf(buf, sizeof(buf), "%d", _controller->getSleepWaitTime());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(CONFIG_SLEEP_END);

    // Soft Start
    snprintf(buf, sizeof(buf), "%d", _controller->getSoftStartTime());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(CONFIG_START_END);

    // Soft Stop
    snprintf(buf, sizeof(buf), "%d", _controller->getSoftStopTime());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(CONFIG_STOP_END);

    // Block Detect
    snprintf(buf, sizeof(buf), "%d", _controller->getBlockDetectTime());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(CONFIG_BLOCK_END);

    // Auto Restore
    Esp32BaseWeb::sendChunk(_controller->getAutoRestore() ? "selected" : "");
    Esp32BaseWeb::sendChunk(CONFIG_AUTO_END);
    Esp32BaseWeb::sendChunk(_controller->getAutoRestore() ? "" : "selected");
    Esp32BaseWeb::sendChunk(CONFIG_AUTO_END2);

    Esp32BaseWeb::sendFooter();
}

// API Handlers

void FanWeb::handleApiStatus() {
    if (!Esp32BaseWeb::checkAuth()) return;
    
    char buf[384];
    char clock[24];
    const char* stateStr;
    switch (_controller->getState()) {
        case SYS_IDLE: stateStr = "idle"; break;
        case SYS_RUNNING: stateStr = "running"; break;
        case SYS_SLEEP: stateStr = "sleep"; break;
        case SYS_ERROR: stateStr = "error"; break;
        default: stateStr = "unknown"; break;
    }
    
    char ip[24];
    strlcpy(ip, "N/A", sizeof(ip));
    long rssi = 0;
    if (Esp32BaseWiFi::isConnected()) {
        Esp32BaseWiFi::ip(ip, sizeof(ip));
        rssi = Esp32BaseWiFi::rssi();
    }
    if (Esp32BaseNtp::isTimeSynced()) {
        Esp32BaseNtp::formatTime(clock, sizeof(clock), "%H:%M:%S");
    } else {
        strcpy(clock, "N/A");
    }
    
    snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"data\":{\"state\":\"%s\",\"speed\":%d,\"target_speed\":%d,\"timer_remaining\":%lu,"
        "\"run_duration\":%lu,\"blocked\":%s,\"ip\":\"%s\",\"rssi\":%ld,\"clock\":\"%s\"}}",
        stateStr, _controller->getCurrentSpeed(), _controller->getTargetSpeed(),
        (unsigned long)_controller->getTimerRemaining(),
        (unsigned long)_controller->getTotalRunDuration(),
        _controller->isBlocked() ? "true" : "false", ip, rssi, clock
    );
    Esp32BaseWeb::sendJson(200, buf);
}

void FanWeb::handleApiSpeed() {
    if (!Esp32BaseWeb::checkAuth()) return;

    char speedStr[12];
    if (Esp32BaseWeb::getParam("speed", speedStr, sizeof(speedStr))) {
        int speed = atoi(speedStr);
        if (speed >= 0 && speed <= 100) {
            ESP32BASE_LOG_I("FanWeb", "user_action speed=%d", speed);
            _controller->setSpeed(speed);
            char buf[80];
            snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%d,\"target_speed\":%d}",
                     _controller->getCurrentSpeed(), _controller->getTargetSpeed());
            Esp32BaseWeb::sendJson(200, buf);
            return;
        }
        ESP32BASE_LOG_W("FanWeb", "invalid_speed_request value=%s", speedStr);
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid request\"}");
        return;
    }

    char buf[80];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%d,\"target_speed\":%d}",
             _controller->getCurrentSpeed(), _controller->getTargetSpeed());
    Esp32BaseWeb::sendJson(200, buf);
}

void FanWeb::handleApiTimer() {
    if (!Esp32BaseWeb::checkAuth()) return;

    char secStr[16];
    if (Esp32BaseWeb::getParam("seconds", secStr, sizeof(secStr))) {
        unsigned long seconds = strtoul(secStr, nullptr, 10);
        if (seconds <= 356400UL) {
            ESP32BASE_LOG_I("FanWeb", "user_action timer_seconds=%lu", seconds);
            _controller->setTimer(seconds);
            char buf[80];
            snprintf(buf, sizeof(buf), "{\"ok\":true,\"timer_remaining\":%lu}",
                     (unsigned long)_controller->getTimerRemaining());
            Esp32BaseWeb::sendJson(200, buf);
            return;
        }
        ESP32BASE_LOG_W("FanWeb", "invalid_timer_request seconds=%s", secStr);
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid request\"}");
        return;
    }

    char buf[80];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"timer_remaining\":%lu}", (unsigned long)_controller->getTimerRemaining());
    Esp32BaseWeb::sendJson(200, buf);
}

void FanWeb::handleApiStop() {
    if (!Esp32BaseWeb::checkAuth()) return;
    ESP32BASE_LOG_I("FanWeb", "user_action stop_fan");
    _controller->stop();
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
}

void FanWeb::handleApiConfig() {
    if (!Esp32BaseWeb::checkAuth()) return;

    char value[32];
    bool hasAnyParam = Esp32BaseWeb::hasParam("min_speed") ||
                       Esp32BaseWeb::hasParam("soft_start") ||
                       Esp32BaseWeb::hasParam("soft_stop") ||
                       Esp32BaseWeb::hasParam("block_detect") ||
                       Esp32BaseWeb::hasParam("sleep_wait") ||
                       Esp32BaseWeb::hasParam("auto_restore") ||
                       Esp32BaseWeb::hasParam("password");

    if (hasAnyParam) {
        ESP32BASE_LOG_I("FanWeb", "user_action save_config");
        uint8_t changed = 0;

        if (Esp32BaseWeb::getParam("min_speed", value, sizeof(value))) {
            uint8_t v = static_cast<uint8_t>(atoi(value));
            uint8_t old = _controller->getMinEffectiveSpeed();
            if (v != old) {
                _controller->setMinEffectiveSpeed(v);
                changed++;
            }
        }
        if (Esp32BaseWeb::getParam("soft_start", value, sizeof(value))) {
            uint16_t v = static_cast<uint16_t>(atoi(value));
            uint16_t old = _controller->getSoftStartTime();
            if (v != old) {
                _controller->setSoftStartTime(v);
                changed++;
            }
        }
        if (Esp32BaseWeb::getParam("soft_stop", value, sizeof(value))) {
            uint16_t v = static_cast<uint16_t>(atoi(value));
            uint16_t old = _controller->getSoftStopTime();
            if (v != old) {
                _controller->setSoftStopTime(v);
                changed++;
            }
        }
        if (Esp32BaseWeb::getParam("block_detect", value, sizeof(value))) {
            uint16_t v = static_cast<uint16_t>(atoi(value));
            uint16_t old = _controller->getBlockDetectTime();
            if (v != old) {
                _controller->setBlockDetectTime(v);
                changed++;
            }
        }
        if (Esp32BaseWeb::getParam("sleep_wait", value, sizeof(value))) {
            uint16_t v = static_cast<uint16_t>(atoi(value));
            uint16_t old = _controller->getSleepWaitTime();
            if (v != old) {
                _controller->setSleepWaitTime(v);
                changed++;
            }
        }
        if (Esp32BaseWeb::getParam("auto_restore", value, sizeof(value))) {
            bool v = atoi(value) != 0;
            bool old = _controller->getAutoRestore();
            if (v != old) {
                _controller->setAutoRestore(v);
                changed++;
            }
        }
        if (Esp32BaseWeb::getParam("password", value, sizeof(value))) {
            if (value[0] != '\0') {
                _controller->setWebPassword(value);
                ESP32BASE_LOG_I("FanWeb", "config_password_updated");
                changed++;
            }
        }
        bool flushed = Esp32BaseConfig::flushAll();
        ESP32BASE_LOG_I("FanWeb", "config_save_complete changed=%u flushed=%u",
                          (unsigned)changed, flushed ? 1U : 0U);
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"changed\":%u,\"flushed\":%s,\"data\":{\"min_effective_speed\":%d,\"soft_start\":%d,\"soft_stop\":%d,"
            "\"block_detect\":%d,\"sleep_wait\":%d,\"auto_restore\":%s}}",
            (unsigned)changed,
            flushed ? "true" : "false",
            _controller->getMinEffectiveSpeed(),
            _controller->getSoftStartTime(),
            _controller->getSoftStopTime(),
            _controller->getBlockDetectTime(),
            _controller->getSleepWaitTime(),
            _controller->getAutoRestore() ? "true" : "false"
        );
        Esp32BaseWeb::sendJson(200, buf);
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"data\":{\"min_effective_speed\":%d,\"soft_start\":%d,\"soft_stop\":%d,"
            "\"block_detect\":%d,\"sleep_wait\":%d,\"auto_restore\":%s}}",
            _controller->getMinEffectiveSpeed(),
            _controller->getSoftStartTime(),
            _controller->getSoftStopTime(),
            _controller->getBlockDetectTime(),
            _controller->getSleepWaitTime(),
            _controller->getAutoRestore() ? "true" : "false"
        );
        Esp32BaseWeb::sendJson(200, buf);
    }
}

void FanWeb::handleApiLogs() {
    if (!Esp32BaseWeb::checkAuth()) return;
    Esp32BaseWeb::sendJson(200, "{\"ok\":true,\"message\":\"Use /esp32base/logs\"}");
}

void FanWeb::handleApiReset() {
    if (!Esp32BaseWeb::checkAuth()) return;
    ESP32BASE_LOG_W("FanWeb", "user_action factory_reset");
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
    delay(100);
    _controller->resetFactory();
}

void FanWeb::handleApiIrLearn() {
    if (!Esp32BaseWeb::checkAuth()) return;

    char value[16];
    if (Esp32BaseWeb::getParam("key_index", value, sizeof(value))) {
        int idx = atoi(value);
        if (idx >= 0 && idx < 6 && _ir->startLearning(idx)) {
            ESP32BASE_LOG_I("FanWeb", "user_action ir_learn key=%d", idx);
            Esp32BaseWeb::sendJson(200, "{\"ok\":true,\"learning\":true,\"timeout\":10}");
            return;
        }
    }
    ESP32BASE_LOG_W("FanWeb", "invalid_ir_learn_request");
    Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid request\"}");
}

void FanWeb::handleApiIrStatus() {
    if (!Esp32BaseWeb::checkAuth()) return;

    char buf[512];
    char* p = buf;
    p += snprintf(p, sizeof(buf), "{\"ok\":true,\"learning\":%s,\"codes\":[", _ir->isLearning() ? "true" : "false");

    for (uint8_t i = 0; i < 6; i++) {
        uint8_t proto;
        uint64_t code;
        _ir->getKeyCode(i, &proto, &code);
        p += snprintf(p, buf + sizeof(buf) - p, "{\"protocol\":%d,\"code\":\"0x%08llX\"}%s",
                 proto, (unsigned long long)code, i < 5 ? "," : "");
    }
    snprintf(p, buf + sizeof(buf) - p, "]}");
    
    Esp32BaseWeb::sendJson(200, buf);
}
