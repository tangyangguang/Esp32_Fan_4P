#include "web/FanWeb.h"

#ifdef UNIT_TEST
#include "Esp32Base.h"
#else
#include <Esp32Base.h>
#endif

#include <string.h>

FanController* FanWeb::_controller = nullptr;
IRReceiverDriver* FanWeb::_ir = nullptr;
FanHistory* FanWeb::_history = nullptr;

namespace {
bool parseUintArg(const char* value, uint32_t min_value, uint32_t max_value, uint32_t* out) {
    if (out == nullptr || value == nullptr || value[0] == '\0') return false;

    uint32_t parsed = 0;
    for (const char* p = value; *p != '\0'; ++p) {
        char c = *p;
        if (c < '0' || c > '9') return false;
        uint32_t digit = static_cast<uint32_t>(c - '0');
        // Check before parsed = parsed * 10 + digit so malformed large inputs fail cleanly.
        if (parsed > (max_value - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    if (parsed < min_value || parsed > max_value) return false;
    *out = parsed;
    return true;
}

bool parseUintParam(const char* name, uint32_t min_value, uint32_t max_value, uint32_t* out) {
    char value[24];
    if (!Esp32BaseWeb::getParam(name, value, sizeof(value))) return false;
    return parseUintArg(value, min_value, max_value, out);
}

bool parseHistoryRange(FanHistoryRange* range) {
    if (!range) return false;
    char value[12];
    if (!Esp32BaseWeb::getParam("range", value, sizeof(value))) {
        *range = FAN_HISTORY_SHORT;
        return true;
    }
    if (strcmp(value, "short") == 0) {
        *range = FAN_HISTORY_SHORT;
        return true;
    }
    if (strcmp(value, "long") == 0) {
        *range = FAN_HISTORY_LONG;
        return true;
    }
    return false;
}

const char* historyRangeName(FanHistoryRange range) {
    return range == FAN_HISTORY_LONG ? "long" : "short";
}

void sendHistoryConfigObject(const FanHistoryConfig& cfg) {
    char buf[240];
    snprintf(buf, sizeof(buf),
             "\"config\":{\"short_points\":%u,\"short_sample_ms\":%u,\"short_window_seconds\":%lu,"
             "\"long_points\":%u,\"long_sample_s\":%u,\"long_window_seconds\":%lu,"
             "\"max_points\":%u}",
             static_cast<unsigned>(cfg.short_points),
             static_cast<unsigned>(cfg.short_sample_ms),
             static_cast<unsigned long>(cfg.short_window_seconds),
             static_cast<unsigned>(cfg.long_points),
             static_cast<unsigned>(cfg.long_sample_s),
             static_cast<unsigned long>(cfg.long_window_seconds),
             static_cast<unsigned>(FanHistory::MAX_POINTS));
    Esp32BaseWeb::sendChunk(buf);
}

void sendHistoryConfigJson(const FanHistoryConfig& cfg) {
    char buf[280];
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"data\":{\"short_points\":%u,\"short_sample_ms\":%u,\"short_window_seconds\":%lu,"
             "\"long_points\":%u,\"long_sample_s\":%u,\"long_window_seconds\":%lu,"
             "\"max_points\":%u}}",
             static_cast<unsigned>(cfg.short_points),
             static_cast<unsigned>(cfg.short_sample_ms),
             static_cast<unsigned long>(cfg.short_window_seconds),
             static_cast<unsigned>(cfg.long_points),
             static_cast<unsigned>(cfg.long_sample_s),
             static_cast<unsigned long>(cfg.long_window_seconds),
             static_cast<unsigned>(FanHistory::MAX_POINTS));
    Esp32BaseWeb::sendJson(200, buf);
}

void formatRunDuration(uint32_t seconds, char* out, size_t out_size) {
    // Keep this format aligned with the client-side rf() helper below.
    if (out == nullptr || out_size == 0) return;
    if (seconds < 3600) {
        snprintf(out, out_size, "%lum %lus",
                 static_cast<unsigned long>(seconds / 60),
                 static_cast<unsigned long>(seconds % 60));
    } else {
        snprintf(out, out_size, "%luh %lum",
                 static_cast<unsigned long>(seconds / 3600),
                 static_cast<unsigned long>((seconds % 3600) / 60));
    }
}

const char* irKeyName(uint8_t index) {
    switch (index) {
        case IR_KEY_SPEED_UP: return "Speed Up";
        case IR_KEY_SPEED_DOWN: return "Speed Down";
        case IR_KEY_STOP: return "Stop";
        case IR_KEY_TIMER_30M: return "30 min";
        case IR_KEY_TIMER_1H: return "1 h";
        case IR_KEY_TIMER_2H: return "2 h";
        case IR_KEY_TIMER_4H: return "4 h";
        case IR_KEY_TIMER_8H: return "8 h";
        default: return "Unknown";
    }
}
}

FanWeb::FanWeb(FanController& controller, IRReceiverDriver& ir, FanHistory* history) {
    _controller = &controller;
    _ir = &ir;
    _history = history;
}

// ----------------------------------------------------------------------------
// FanWeb Implementation using Esp32BaseWeb (PROGMEM + Chunked)
// ----------------------------------------------------------------------------

// Static HTML parts in PROGMEM
static const char APP_STYLE[] PROGMEM =
    "<style>"
    "body{padding:10px;max-width:560px;font:14px/1.45 -apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif;color:#1f2937}h2,h3{font-size:14px;font-weight:400;margin:0 0 8px;color:#111827}"
    ".top{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:center;margin-bottom:8px}.muted{color:#6b7280;font-size:14px}"
    ".speed{font-size:38px;font-weight:600;line-height:1;text-align:right;color:#111827}.unit{font-size:16px;font-weight:400;color:#6b7280}.targetline{margin-top:4px;text-align:right;color:#6b7280;font-size:14px;font-weight:400}"
    ".panel{border:1px solid #d7dee8;border-radius:6px;padding:10px;margin:8px 0;background:#fff}.tight{margin-top:6px}"
    ".stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(118px,1fr));gap:6px}.stat{background:#f8fafc;border:1px solid #e8edf3;border-radius:6px;padding:7px 8px;min-width:0}.stat.state{grid-column:1/-1}"
    ".stat span{display:block;color:#6b7280;font-size:14px}.stat b{display:block;font-size:14px;font-weight:400;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#111827}.stat.state b{white-space:normal}.stat small{display:block;color:#6b7280;font-size:12px;margin-top:2px;line-height:1.3}"
    ".chips{display:grid;grid-template-columns:repeat(5,1fr);gap:6px}"
    "button,.btn{background:#2563a6;color:#fff;border:0;border-radius:6px;padding:8px 7px;cursor:pointer;text-align:center;text-decoration:none;font-size:14px;font-weight:400;min-height:36px;box-sizing:border-box}"
    "button.secondary,.btn.secondary{background:#6b7280}button.danger{background:#b64a2f}.row{display:grid;grid-template-columns:1fr 76px;gap:7px;align-items:center;margin-top:4px}"
    ".actions{display:grid;grid-template-columns:1fr 1fr;gap:7px;margin-top:14px}.actions button{min-height:40px}"
    ".formgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.field{min-width:0}label{display:block;font-size:14px;font-weight:400;color:#374151;margin:0 0 3px}"
    "input,select{width:100%;min-height:38px;box-sizing:border-box;border:1px solid #c8d0da;border-radius:6px;padding:8px;background:#fff;font-size:14px;font-weight:400;margin:0;color:#111827}"
    ".row input:not([type=submit]){height:42px;min-height:42px;margin:0!important;padding:0 10px;display:block}.row button{height:42px;min-height:42px;padding:0 7px;align-self:center}"
    ".oktxt{color:#157347}.errtxt{color:#b42318}.help{color:#6b7280;font-size:14px;margin:7px 0 0;line-height:1.4}"
    ".irlist{display:grid;gap:5px}.irrow{display:grid;grid-template-columns:1fr 58px 58px;gap:6px;align-items:center;background:#f8fafc;border:1px solid #e8edf3;border-radius:6px;padding:6px 8px;transition:background-color .2s,border-color .2s}.irrow.hit{background:#fff7ed;border-color:#fdba74}.irrow b{display:block;font-size:14px;font-weight:400;color:#111827}.irrow span{display:block;color:#6b7280;font-size:13px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.irrow span.warn{color:#b45309}.irrow button{min-height:32px;padding:0 6px}.irrow button.secondary{background:#6b7280}"
    ".savebar{display:block;margin-top:8px;padding:7px 8px;border-radius:6px;background:#f8fafc;border:1px solid #e8edf3;font-size:14px}.savebar.oktxt{background:#f0f9f4;border-color:#b7e4c7}.savebar.errtxt{background:#fff5f3;border-color:#f1b8ad}"
    ".chartbar{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;margin-bottom:8px}.tabs{display:flex;gap:6px}.tabs button{min-height:30px;padding:0 9px;background:#6b7280}.tabs button.active{background:#2563a6}.chart{width:100%;height:210px;border:1px solid #e8edf3;border-radius:6px;background:#fbfdff;display:block}.widechart{height:calc(100vh - 320px);min-height:260px}.legend{display:flex;gap:10px;flex-wrap:wrap;margin:7px 0;color:#6b7280}.legend span{font-size:13px}.sw{display:inline-block;width:10px;height:3px;vertical-align:middle;margin-right:4px}.sw.o{background:#2563a6}.sw.t{background:#b05f1a}.sw.r{background:#3f7d58}.histcfg{display:grid;grid-template-columns:1fr 1fr 1fr 1fr auto;gap:6px;align-items:end}.histcfg label{font-size:12px}.histcfg input{min-height:34px;padding:6px}.histcfg button{min-height:34px;padding:0 9px}"
    "pre.log{white-space:pre-wrap;word-break:break-word;background:#111827;color:#e5e7eb;border-radius:6px;padding:9px;max-height:430px;overflow:auto;font-size:14px;font-weight:400}"
    "@media(max-width:520px){.histcfg{grid-template-columns:1fr 1fr}.histcfg button{grid-column:1/-1}}@media(max-width:390px){body{padding:8px}.chips{grid-template-columns:repeat(3,1fr)}.formgrid{grid-template-columns:1fr}}"
    "</style>";

static const char FAN_PAGE_TOP[] PROGMEM =
    "<div class=top><div><h2>Fan</h2><div class=muted>Fast presets, exact input</div></div><div><div class=speed><span id=outTop>";
static const char FAN_SPEED_END[] PROGMEM =
    "</span><span class=unit>%</span></div><div class=targetline><span id=rpmTop>";
static const char FAN_RPM_END[] PROGMEM =
    "</span><span id=targetTopWrap";
static const char FAN_TARGET_START[] PROGMEM =
    "> &middot; Target <span id=tgtTop>";
static const char FAN_TARGET_WRAP_END[] PROGMEM =
    "</span>%</span></div></div></div><div class='panel tight'><div class=stats>";
static const char FAN_STATUS_MID[] PROGMEM =
    "</div></div><div class=panel><div class=chartbar><h3>History</h3><div class=tabs>"
    "<button id=hbS class=active onclick='histSwitch(\"short\")'>Recent</button><button id=hbL onclick='histSwitch(\"long\")'>Trend</button></div></div>"
    "<canvas id=histChart class=chart height=210></canvas><div class=legend><span><i class='sw o'></i>Output <b id=ho>--%</b></span><span><i class='sw t'></i>Target <b id=ht>--%</b></span><span><i class='sw r'></i>RPM <b id=hr>--</b></span><span id=histMsg>RAM history</span></div></div>"
    "<div class=panel><h3>Speed</h3><div class=chips>"
    "<button onclick='spd(0)'>Off</button><button onclick='spd(25)'>25</button><button onclick='spd(50)'>50</button><button onclick='spd(75)'>75</button><button onclick='spd(100)'>100</button>"
    "</div><label>Custom (%)</label><div class=row><input id=sv type=number min=0 max=100 value='";
static const char FAN_SPEED_INPUT_END[] PROGMEM =
    "'><button onclick='spd(document.getElementById(\"sv\").value)'>Apply</button></div></div>"
    "<div class=panel><h3>Timer</h3><div class=chips>"
    "<button onclick='tm(30)'>30 min</button><button onclick='tm(60)'>1 h</button><button onclick='tm(120)'>2 h</button><button onclick='tm(240)'>4 h</button><button onclick='tm(480)'>8 h</button>"
    "</div><label>Custom (min)</label><div class=row><input id=tv type=number min=0 max=5940 value='";
static const char FAN_TIMER_INPUT_END[] PROGMEM =
    "'><button onclick='tm(document.getElementById(\"tv\").value)'>Set</button></div>"
    "<div class=actions><button class=secondary onclick='tm(0)'>Cancel timer</button><button class=danger onclick='stopFan()'>Stop fan</button></div>"
    "<div class=help>Cancel timer keeps the fan running. Stop fan turns the fan off and clears the timer.</div></div>"
    "<script>"
    "var rem=";
static const char FAN_SCRIPT_TIMER_MID[] PROGMEM =
    ";function e(i,v){var x=document.getElementById(i);if(x)x.textContent=v}"
    "function tf(s){s=parseInt(s||0);if(s<=0)return'Off';var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),r=s%60;return h+'h '+m+'m '+r+'s'}"
    "function rf(s){s=parseInt(s||0);if(s<3600)return Math.floor(s/60)+'m '+(s%60)+'s';return Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m'}"
    "function tt(o,t){var w=document.getElementById('targetTopWrap');if(w)w.style.display=o==t?'none':''}"
    "function sd(d){if(d.state_detail)return d.state_detail;if(d.blocked)return d.state=='Error'?'Error / Blocked':'Blocked';return d.state}"
    "var histRange='short',histPts=[],histBusy=false,histCfgData={short_points:500,long_points:500};function hi(i){return document.getElementById(i)}function histMsg(t,c){var m=hi('histMsg');if(m){m.textContent=t;m.className=c||''}}"
    "function histCfg(c){if(c)histCfgData=c}"
    "function histMerge(a,reset){if(reset)histPts=[];(a||[]).forEach(p=>{if(!histPts.length||p.seq>histPts[histPts.length-1].seq)histPts.push(p)});var cap=histRange=='short'?histCfgData.short_points:histCfgData.long_points;cap=Math.min(1200,cap||500);if(histPts.length>cap)histPts=histPts.slice(histPts.length-cap);histDraw()}"
    "function histLoad(reset){if(histBusy)return;histBusy=true;var u='/api/history?range='+histRange;if(!reset&&histPts.length)u+='&since_seq='+histPts[histPts.length-1].seq;fetch(u).then(r=>r.json()).then(j=>{histBusy=false;if(j.ok){histCfg(j.data.config);histMerge(j.data.points,reset);histMsg((histRange=='short'?'Recent':'Trend')+' '+histPts.length+' points','muted')}else histMsg(j.error||'History failed','errtxt')}).catch(()=>{histBusy=false;histMsg('History network error','errtxt')})}"
    "function histSwitch(r){histRange=r;hi('hbS').className=r=='short'?'active':'';hi('hbL').className=r=='long'?'active':'';histLoad(true)}"
    "function histDraw(){var c=hi('histChart');if(!c)return;var dpr=window.devicePixelRatio||1,w=c.clientWidth||300,h=c.clientHeight||210;c.width=w*dpr;c.height=h*dpr;var x=c.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,w,h);x.font='11px -apple-system,BlinkMacSystemFont,Segoe UI,Arial';var L=42,R=58,T=18,B=28,PW=w-L-R,PH=h-T-B;if(PW<80||PH<60)return;function y(v,max){return T+PH-(v/max)*PH}x.strokeStyle='#d7dee8';x.fillStyle='#6b7280';x.lineWidth=1;x.textBaseline='middle';[0,25,50,75,100].forEach(v=>{var yy=y(v,100);x.strokeStyle=v==0?'#cbd5e1':'#e8edf3';x.beginPath();x.moveTo(L,yy);x.lineTo(L+PW,yy);x.stroke();x.textAlign='right';x.fillText(v+'%',L-6,yy)});x.textAlign='left';x.textBaseline='top';x.fillText('rpm',L+PW+7,2);x.textBaseline='middle';x.strokeStyle='#d7dee8';x.beginPath();x.rect(L,T,PW,PH);x.stroke();if(!histPts.length){x.textAlign='left';x.fillText('No history yet',L+8,T+22);return}var a=histPts[0].t_ms,b=histPts[histPts.length-1].t_ms;if(a==b)b=a+1;var mr=1;histPts.forEach(p=>{if(p.rpm>mr)mr=p.rpm});mr=Math.ceil(mr/100)*100||1;[0,.25,.5,.75,1].forEach(r=>{var yy=T+PH-r*PH;x.textAlign='left';x.fillText(Math.round(mr*r),L+PW+7,yy)});function px(p){return L+(p.t_ms-a)*PW/(b-a)}function pyPct(v){return y(v,100)}function pyR(v){return y(v,mr)}function line(col,key,fn,wid,dash){x.strokeStyle=col;x.lineWidth=wid||1.4;x.setLineDash(dash||[]);x.beginPath();histPts.forEach((p,i)=>{var xx=px(p),yy=fn(p[key]);if(i)x.lineTo(xx,yy);else x.moveTo(xx,yy)});x.stroke();x.setLineDash([])}function step(col,key,fn,wid,dash){x.strokeStyle=col;x.lineWidth=wid||1.2;x.setLineDash(dash||[]);x.beginPath();histPts.forEach((p,i)=>{var xx=px(p),yy=fn(p[key]);if(!i)x.moveTo(xx,yy);else{var q=histPts[i-1],py=fn(q[key]);x.lineTo(xx,py);x.lineTo(xx,yy)}});x.stroke();x.setLineDash([])}line('#3f7d58','rpm',pyR,1.05);step('#b05f1a','target_speed',pyPct,1.25,[5,4]);line('#2563a6','speed',pyPct,1.6);var p=histPts[histPts.length-1];e('ho',p.speed+'%');e('ht',p.target_speed+'%');e('hr',p.rpm+' rpm');function ago(ms){if(ms<=0)return'now';var s=Math.round(ms/1000);if(histRange=='short')return'-'+s+'s';if(s<60)return'-'+s+'s';var m=s/60;if(m<60)return'-'+Math.round(m)+'m';var hr=m/60;return'-'+(hr<10?Math.round(hr*10)/10:Math.round(hr))+'h'}var span=b-a,n=w<380?4:7;x.fillStyle='#6b7280';x.textBaseline='top';for(var i=0;i<n;i++){var r=i/(n-1),xx=L+r*PW,lab=ago(span*(1-r));x.strokeStyle='#edf2f7';x.beginPath();x.moveTo(xx,T);x.lineTo(xx,T+PH);x.stroke();x.textAlign=i==0?'left':(i==n-1?'right':'center');x.fillText(lab,xx,h-20)}}"
    "var pollMs=3000,pollTimer=0;function draw(d){var st=sd(d),bad=d.blocked||d.state=='Error';e('st',st);document.getElementById('st').className=bad?'errtxt':'';e('stDiag','TACH '+(d.tach_pulses||0)+' pulses / '+(d.tach_level?'HIGH':'LOW'));e('tgt',d.target_speed+'%');e('tgtTop',d.target_speed);e('out',d.speed+'%');e('outTop',d.speed);e('rpmTop',(d.rpm||0)+' rpm');tt(d.speed,d.target_speed);e('gear',d.gear);e('rpm',(d.rpm||0)+' rpm');e('tim',tf(d.timer_remaining));e('runTotal',rf(d.run_duration));e('runBoot',rf(d.boot_run_duration));e('rssi',d.rssi+' dBm');rem=d.timer_remaining;pollMs=d.speed==d.target_speed?3000:500;document.getElementById('sv').value=d.target_speed;document.getElementById('tv').value=Math.floor(rem/60);histLoad(false)}"
    "function sched(ms){clearTimeout(pollTimer);pollTimer=setTimeout(poll,ms)}function poll(){fetch('/api/status').then(r=>r.json()).then(j=>{if(j.ok)draw(j.data);sched(pollMs)}).catch(()=>sched(3000))}"
    "function fail(){e('st','Command failed');document.getElementById('st').className='errtxt';setTimeout(poll,250)}"
    "function post(u,b,cb){fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(r=>r.json().then(j=>{if(!r.ok||j.ok===false)throw j;return j})).then(j=>{if(cb)cb(j);setTimeout(poll,250)}).catch(fail)}"
    "function spd(v){v=parseInt(v||0);if(v>=0&&v<=100){post('/api/speed','speed='+v,function(){e('tgt',v+'%');e('tgtTop',v);e('rpmTop','-- rpm');e('rpm','-- rpm');tt(parseInt(document.getElementById('outTop').textContent||0),v);document.getElementById('sv').value=v})}}"
    "function tm(v){v=parseInt(v||0);if(v>=0&&v<=5940){post('/api/timer','seconds='+(v*60),function(){rem=v*60;e('tim',tf(rem));document.getElementById('tv').value=v})}}"
    "function stopFan(){post('/api/stop','',function(){rem=0;e('tim','Off');e('tgt','0%');e('tgtTop','0');e('outTop','0');e('rpmTop','-- rpm');e('rpm','-- rpm');tt(0,0)})}"
    "function uiTick(){if(rem>0)rem--;e('tim',tf(rem))}"
    "window.addEventListener('resize',histDraw);histLoad(true);setInterval(uiTick,1000);sched(3000)"
    "</script>";

static const char HISTORY_PAGE[] PROGMEM =
    "<style>body{max-width:none}.histwide{max-width:none}.histwide .panel{margin:8px 0}.histwide .chartbar{margin-bottom:10px}</style>"
    "<div class=histwide><div class=panel><div class=chartbar><h2>History</h2><div class=tabs>"
    "<button id=hbS class=active onclick='histSwitch(\"short\")'>Recent</button><button id=hbL onclick='histSwitch(\"long\")'>Trend</button></div></div>"
    "<canvas id=histChart class='chart widechart' height=420></canvas>"
    "<div class=legend><span><i class='sw o'></i>Output <b id=ho>--%</b></span><span><i class='sw t'></i>Target <b id=ht>--%</b></span><span><i class='sw r'></i>RPM <b id=hr>--</b></span><span id=histMsg>RAM history</span></div></div></div>"
    "<script>"
    "function e(i,v){var x=document.getElementById(i);if(x)x.textContent=v}var histRange='short',histPts=[],histBusy=false,histTimer=0,histCfgData={short_points:500,short_sample_ms:500,long_points:500,long_sample_s:10};function hi(i){return document.getElementById(i)}function histMsg(t,c){var m=hi('histMsg');if(m){m.textContent=t;m.className=c||''}}function histCfg(c){if(c)histCfgData=c}"
    "function histMerge(a,reset){if(reset)histPts=[];(a||[]).forEach(p=>{if(!histPts.length||p.seq>histPts[histPts.length-1].seq)histPts.push(p)});var cap=histRange=='short'?histCfgData.short_points:histCfgData.long_points;cap=Math.min(1200,cap||500);if(histPts.length>cap)histPts=histPts.slice(histPts.length-cap);histDraw()}"
    "function nextMs(){return histRange=='short'?Math.max(500,histCfgData.short_sample_ms||500):Math.min(10000,Math.max(3000,(histCfgData.long_sample_s||10)*1000))}function sched(){clearTimeout(histTimer);histTimer=setTimeout(()=>histLoad(false),nextMs())}"
    "function histLoad(reset){if(histBusy)return;histBusy=true;var u='/api/history?range='+histRange;if(!reset&&histPts.length)u+='&since_seq='+histPts[histPts.length-1].seq;fetch(u).then(r=>r.json()).then(j=>{histBusy=false;if(j.ok){histCfg(j.data.config);histMerge(j.data.points,reset);histMsg((histRange=='short'?'Recent':'Trend')+' '+histPts.length+' points','muted')}else histMsg(j.error||'History failed','errtxt');sched()}).catch(()=>{histBusy=false;histMsg('History network error','errtxt');sched()})}"
    "function histSwitch(r){histRange=r;hi('hbS').className=r=='short'?'active':'';hi('hbL').className=r=='long'?'active':'';histLoad(true)}"
    "function histDraw(){var c=hi('histChart');if(!c)return;var dpr=window.devicePixelRatio||1,w=c.clientWidth||300,h=c.clientHeight||420;c.width=w*dpr;c.height=h*dpr;var x=c.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,w,h);x.font='11px -apple-system,BlinkMacSystemFont,Segoe UI,Arial';var L=42,R=58,T=18,B=28,PW=w-L-R,PH=h-T-B;if(PW<80||PH<60)return;function y(v,max){return T+PH-(v/max)*PH}x.strokeStyle='#d7dee8';x.fillStyle='#6b7280';x.lineWidth=1;x.textBaseline='middle';[0,25,50,75,100].forEach(v=>{var yy=y(v,100);x.strokeStyle=v==0?'#cbd5e1':'#e8edf3';x.beginPath();x.moveTo(L,yy);x.lineTo(L+PW,yy);x.stroke();x.textAlign='right';x.fillText(v+'%',L-6,yy)});x.textAlign='left';x.textBaseline='top';x.fillText('rpm',L+PW+7,2);x.textBaseline='middle';x.strokeStyle='#d7dee8';x.beginPath();x.rect(L,T,PW,PH);x.stroke();if(!histPts.length){x.textAlign='left';x.fillText('No history yet',L+8,T+22);return}var a=histPts[0].t_ms,b=histPts[histPts.length-1].t_ms;if(a==b)b=a+1;var mr=1;histPts.forEach(p=>{if(p.rpm>mr)mr=p.rpm});mr=Math.ceil(mr/100)*100||1;[0,.25,.5,.75,1].forEach(r=>{var yy=T+PH-r*PH;x.textAlign='left';x.fillText(Math.round(mr*r),L+PW+7,yy)});function px(p){return L+(p.t_ms-a)*PW/(b-a)}function pyPct(v){return y(v,100)}function pyR(v){return y(v,mr)}function line(col,key,fn,wid,dash){x.strokeStyle=col;x.lineWidth=wid||1.4;x.setLineDash(dash||[]);x.beginPath();histPts.forEach((p,i)=>{var xx=px(p),yy=fn(p[key]);if(i)x.lineTo(xx,yy);else x.moveTo(xx,yy)});x.stroke();x.setLineDash([])}function step(col,key,fn,wid,dash){x.strokeStyle=col;x.lineWidth=wid||1.2;x.setLineDash(dash||[]);x.beginPath();histPts.forEach((p,i)=>{var xx=px(p),yy=fn(p[key]);if(!i)x.moveTo(xx,yy);else{var q=histPts[i-1],py=fn(q[key]);x.lineTo(xx,py);x.lineTo(xx,yy)}});x.stroke();x.setLineDash([])}line('#3f7d58','rpm',pyR,1.05);step('#b05f1a','target_speed',pyPct,1.25,[5,4]);line('#2563a6','speed',pyPct,1.6);var p=histPts[histPts.length-1];e('ho',p.speed+'%');e('ht',p.target_speed+'%');e('hr',p.rpm+' rpm');function ago(ms){if(ms<=0)return'now';var s=Math.round(ms/1000);if(histRange=='short')return'-'+s+'s';if(s<60)return'-'+s+'s';var m=s/60;if(m<60)return'-'+Math.round(m)+'m';var hr=m/60;return'-'+(hr<10?Math.round(hr*10)/10:Math.round(hr))+'h'}var span=b-a,n=w<520?5:9;x.fillStyle='#6b7280';x.textBaseline='top';for(var i=0;i<n;i++){var r=i/(n-1),xx=L+r*PW,lab=ago(span*(1-r));x.strokeStyle='#edf2f7';x.beginPath();x.moveTo(xx,T);x.lineTo(xx,T+PH);x.stroke();x.textAlign=i==0?'left':(i==n-1?'right':'center');x.fillText(lab,xx,h-20)}}"
    "window.addEventListener('resize',histDraw);histLoad(true)"
    "</script>";

void FanWeb::handleStatusPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    ESP32BASE_LOG_I("FanWeb", "page_view path=/fan");

    Esp32BaseWeb::sendHeader();
    Esp32BaseWeb::sendChunk(APP_STYLE);
    Esp32BaseWeb::sendChunk(FAN_PAGE_TOP);

    char buf[240];

    snprintf(buf, sizeof(buf), "%d", _controller->getCurrentSpeed());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(FAN_SPEED_END);
    snprintf(buf, sizeof(buf), "%u rpm", _controller->getCurrentRpm());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(FAN_RPM_END);
    if (_controller->getCurrentSpeed() == _controller->getTargetSpeed()) {
        Esp32BaseWeb::sendChunk(" style='display:none'");
    }
    Esp32BaseWeb::sendChunk(FAN_TARGET_START);
    snprintf(buf, sizeof(buf), "%d", _controller->getTargetSpeed());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(FAN_TARGET_WRAP_END);

    // State
    const bool blocked = _controller->isBlocked();
    const uint32_t tachPulses = _controller->getTachPulseTotal();
    const uint8_t tachLevel = _controller->getTachPinLevel();
    const char* stateText = "Unknown";
    switch (_controller->getState()) {
        case SYS_IDLE: stateText = "Idle"; break;
        case SYS_RUNNING: stateText = "Running"; break;
        case SYS_SLEEP: stateText = "Sleep"; break;
        case SYS_ERROR:
            if (blocked) {
                stateText = "Error / Blocked";
            } else if (_controller->getTargetSpeed() > 0 && _controller->getCurrentRpm() == 0) {
                stateText = tachPulses == 0 ? "Error / No TACH pulses" : "Error / No RPM";
            } else {
                stateText = "Error";
            }
            break;
        case SYS_RECOVERING: stateText = "Recovering"; break;
        default: break;
    }
    if (blocked && _controller->getState() != SYS_ERROR) {
        stateText = "Blocked";
    }
    snprintf(buf, sizeof(buf), "<div class='stat state'><span>State</span><b id=st%s>%s</b><small id=stDiag>TACH %lu pulses / %s</small></div>",
             blocked ? " class=errtxt" : "",
             stateText,
             (unsigned long)tachPulses,
             tachLevel ? "HIGH" : "LOW");
    Esp32BaseWeb::sendChunk(buf);

    // Speed
    snprintf(buf, sizeof(buf), "<div class=stat><span>Target</span><b id=tgt>%d%%</b></div>", _controller->getTargetSpeed());
    Esp32BaseWeb::sendChunk(buf);

    snprintf(buf, sizeof(buf), "<div class=stat><span>Output</span><b id=out>%d%%</b></div>", _controller->getCurrentSpeed());
    Esp32BaseWeb::sendChunk(buf);

    snprintf(buf, sizeof(buf), "<div class=stat><span>Gear</span><b id=gear>%u</b></div>", _controller->getCurrentGear());
    Esp32BaseWeb::sendChunk(buf);

    snprintf(buf, sizeof(buf), "<div class=stat><span>RPM</span><b id=rpm>%u rpm</b></div>", _controller->getCurrentRpm());
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
    char duration[24];
    formatRunDuration(_controller->getTotalRunDuration(), duration, sizeof(duration));
    snprintf(buf, sizeof(buf), "<div class=stat><span>Total run</span><b id=runTotal>%s</b></div>", duration);
    Esp32BaseWeb::sendChunk(buf);
    formatRunDuration(_controller->getBootRunDuration(), duration, sizeof(duration));
    snprintf(buf, sizeof(buf), "<div class=stat><span>Boot run</span><b id=runBoot>%s</b></div>", duration);
    Esp32BaseWeb::sendChunk(buf);

    long rssi = Esp32BaseWiFi::isConnected() ? (long)Esp32BaseWiFi::rssi() : 0;
    snprintf(buf, sizeof(buf), "<div class=stat><span>RSSI</span><b id=rssi>%ld dBm</b></div>", rssi);
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

void FanWeb::handleHistoryPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    ESP32BASE_LOG_I("FanWeb", "page_view path=/history");

    Esp32BaseWeb::sendHeader();
    Esp32BaseWeb::sendChunk(APP_STYLE);
    Esp32BaseWeb::sendChunk(HISTORY_PAGE);
    Esp32BaseWeb::sendFooter();
}

// Config Page HTML parts
static const char CONFIG_PAGE_TOP[] PROGMEM =
    "<div class=top><div><h2>Settings</h2><div class=muted>Fan behavior and IR learning</div></div></div>"
    "<form class=panel onsubmit='saveCfg(this);return false'>"
    "<h3>Fan behavior</h3><div class=formgrid><div class=field><label>Min speed (%)</label><input type=number name=min_speed min=0 max=50 value='";
static const char CONFIG_MIN_END[] PROGMEM = "'><div class=help>Low commands rise to this value.</div></div>"
    "<div class=field><label>Sleep wait (s)</label><input type=number name=sleep_wait min=1 max=3600 value='";
static const char CONFIG_SLEEP_END[] PROGMEM = "'><div class=help>Stopped this long before modem sleep.</div></div>"
    "<div class=field><label>Soft start (ms)</label><input type=number name=soft_start min=0 max=10000 value='";
static const char CONFIG_START_END[] PROGMEM = "'><div class=help>Ramp up time.</div></div>"
    "<div class=field><label>Soft stop (ms)</label><input type=number name=soft_stop min=0 max=10000 value='";
static const char CONFIG_STOP_END[] PROGMEM = "'><div class=help>Ramp down time.</div></div>"
    "<div class=field><label>Block detect (ms)</label><input type=number name=block_detect min=100 max=5000 value='";
static const char CONFIG_BLOCK_END[] PROGMEM = "'><div class=help>No RPM for this long means blocked.</div></div>"
    "<div class=field><label>LED flash (ms)</label><input type=number name=led_flash_ms min=0 max=2000 value='";
static const char CONFIG_LED_FLASH_END[] PROGMEM = "'><div class=help>0 disables action feedback flash.</div></div>"
    "<div class=field><label>Runtime save (min)</label><input type=number name=runtime_save_min min=1 max=60 value='";
static const char CONFIG_RUNTIME_SAVE_END[] PROGMEM = "'><div class=help>Larger values reduce Flash wear; after power loss, timer and total run can lose up to this interval.</div></div>"
    "<div class=field><label>Power-on restore</label><select name=auto_restore><option value=1 ";
static const char CONFIG_AUTO_END[] PROGMEM = ">Enabled</option><option value=0 ";
static const char CONFIG_AUTO_END2[] PROGMEM = ">Disabled</option></select><div class=help>Restore last speed and timer after reboot.</div></div></div>"
    "<button id=saveBtn type=submit>Save</button><span id=saveMsg class='savebar muted'>Ready</span></form>"
    "<div class=panel><h3>Runtime</h3><div class=stat><span>Total run</span><b id=cfgRunTotal>";
static const char CONFIG_RUNTIME_END[] PROGMEM =
    "</b></div><button id=runResetBtn type=button onclick='resetTotalRun()'>Clear total run</button>"
    "<span id=runResetMsg class='savebar muted'>Boot run is not cleared.</span></div>"
    "<form class=panel onsubmit='saveHistCfg(this);return false'><h3>History</h3><div class=histcfg>"
    "<div><label>Recent points</label><input name=short_points type=number min=100 max=1200 value=500></div>"
    "<div><label>Recent ms</label><input name=short_sample_ms type=number min=100 max=5000 value=500></div>"
    "<div><label>Trend points</label><input name=long_points type=number min=100 max=1200 value=500></div>"
    "<div><label>Trend s</label><input name=long_sample_s type=number min=10 max=600 value=10></div>"
    "<button id=histSaveBtn type=submit>Save</button></div>"
    "<div class=help>RAM only. Max 1200 points per range; restart clears history.</div><span id=histCfgMsg class='savebar muted'>Ready</span></form>"
    "<div class=panel><h3>IR learning</h3><div class=irlist>";
static const char CONFIG_IR_END[] PROGMEM =
    "</div><div class=help>Press one, then point the remote within 10 seconds.</div><span id=irMsg class='savebar muted'>Ready</span></div>"
    "<script>"
    "var irNames=['Speed Up','Speed Down','Stop','30 min','1 h','2 h','4 h','8 h'],irToken=0,irDeadline=0,irDup=-1,irReject=0,hitTimers={};function irName(i){return irNames[i]||('key '+i)}function irLeft(){return Math.max(0,Math.ceil((irDeadline-Date.now())/1000))}"
    "function setMsg(t,c){var m=document.getElementById('saveMsg');m.textContent=t;m.className='savebar '+c}"
    "function rf(s){s=parseInt(s||0);if(s<3600)return Math.floor(s/60)+'m '+(s%60)+'s';return Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m'}"
    "function setRun(t,c){var m=document.getElementById('runResetMsg');m.textContent=t;m.className='savebar '+c}"
    "function setIr(t,c){var m=document.getElementById('irMsg');m.textContent=t;m.className='savebar '+c}"
    "function setIrRow(i,t,c){var e=document.getElementById('irv'+i);if(e){e.textContent=t;e.className=c||''}}"
    "function hitIr(i){var e=document.getElementById('irr'+i);if(!e)return;if(hitTimers[i])clearTimeout(hitTimers[i]);e.className='irrow hit';hitTimers[i]=setTimeout(()=>{e.className='irrow';delete hitTimers[i]},600)}"
    "function applyCfg(d,f){if(!d)return;f.min_speed.value=d.min_effective_speed;f.sleep_wait.value=d.sleep_wait;f.soft_start.value=d.soft_start;f.soft_stop.value=d.soft_stop;f.block_detect.value=d.block_detect;f.led_flash_ms.value=d.led_flash_ms;f.runtime_save_min.value=d.runtime_save_min;f.auto_restore.value=d.auto_restore?1:0}"
    "function reloadCfg(f){fetch('/api/config').then(r=>r.json()).then(j=>{if(j.ok)applyCfg(j.data,f)})}"
    "function saveCfg(f){var b=document.getElementById('saveBtn');b.disabled=true;b.textContent='Saving';setMsg('Saving...','muted');fetch('/api/config',{method:'POST',body:new URLSearchParams(new FormData(f))}).then(r=>r.json().then(j=>({ok:r.ok,j:j}))).then(x=>{b.disabled=false;b.textContent='Save';if(x.ok&&x.j.ok){applyCfg(x.j.data,f);var n=x.j.changed||0;setMsg('Saved - '+(n?n+' changed':'no changes')+' - '+new Date().toLocaleTimeString(),'oktxt')}else{reloadCfg(f);setMsg(x.j&&x.j.error?x.j.error:'Save failed','errtxt')}}).catch(()=>{b.disabled=false;b.textContent='Save';setMsg('Save failed: network error','errtxt')})}"
    "function resetTotalRun(){if(!confirm('Clear total run? Boot run will continue.'))return;var b=document.getElementById('runResetBtn');b.disabled=true;b.textContent='Clearing';setRun('Clearing...','muted');fetch('/api/runtime/reset',{method:'POST'}).then(r=>r.json().then(j=>({ok:r.ok,j:j}))).then(x=>{b.disabled=false;b.textContent='Clear total run';if(x.ok&&x.j.ok){document.getElementById('cfgRunTotal').textContent=rf(x.j.run_duration);setRun('Total run cleared','oktxt')}else setRun(x.j&&x.j.error?x.j.error:'Clear failed','errtxt')}).catch(()=>{b.disabled=false;b.textContent='Clear total run';setRun('Clear failed: network error','errtxt')})}"
    "function setHistCfgMsg(t,c){var m=document.getElementById('histCfgMsg');m.textContent=t;m.className='savebar '+c}"
    "function hw(s){s=parseInt(s||0);if(s<60)return s+'s';if(s<3600)return Math.round(s/60)+'m';var h=Math.floor(s/3600),m=Math.round((s%3600)/60);return h+'h '+m+'m'}"
    "function applyHistCfg(d){var f=document.querySelector('form .histcfg').closest('form');if(!d||!f)return;f.short_points.value=d.short_points;f.short_sample_ms.value=d.short_sample_ms;f.long_points.value=d.long_points;f.long_sample_s.value=d.long_sample_s;setHistCfgMsg('Recent '+d.short_points+' points - '+hw(d.short_window_seconds)+' · Trend '+d.long_points+' points - '+hw(d.long_window_seconds),'muted')}"
    "function loadHistCfg(){fetch('/api/history/config').then(r=>r.json()).then(j=>{if(j.ok)applyHistCfg(j.data)}).catch(()=>setHistCfgMsg('Load failed','errtxt'))}"
    "function saveHistCfg(f){var b=document.getElementById('histSaveBtn');b.disabled=true;b.textContent='Saving';setHistCfgMsg('Saving...','muted');fetch('/api/history/config',{method:'POST',body:new URLSearchParams(new FormData(f))}).then(r=>r.json().then(j=>({ok:r.ok,j:j}))).then(x=>{b.disabled=false;b.textContent='Save';if(x.ok&&x.j.ok){applyHistCfg(x.j.data);setHistCfgMsg('Saved in RAM - history reset','oktxt')}else setHistCfgMsg(x.j&&x.j.error?x.j.error:'Save failed','errtxt')}).catch(()=>{b.disabled=false;b.textContent='Save';setHistCfgMsg('Save failed: network error','errtxt')})}"
    "function finishIr(i,n,seq,tok,retry){fetch('/api/status').then(r=>r.json()).then(j=>{if(tok!=irToken)return;var d=j.data;if(d&&d.ir_learn_seq!=seq){var v='Protocol '+d.ir_last_protocol+' - '+d.ir_last_code;setIrRow(i,v,'');setIr('Learned '+n+' - '+v,'oktxt')}else if(d&&d.ir_learning&&(retry||0)<2)setTimeout(()=>finishIr(i,n,seq,tok,(retry||0)+1),500);else setIr('Learn timeout - no valid signal','errtxt')}).catch(()=>{if(tok==irToken)setIr('Learn timeout - no valid signal','errtxt')})}"
    "function showLearn(n){var l=irLeft();if(l<=0)return false;if(irDup>=0)setIr('Already assigned to '+irName(irDup)+'. Clear that key first - '+l+'s','errtxt');else setIr('Learning '+n+' - '+l+'s','muted');return true}"
    "function watchIr(i,n,seq,tok){if(tok!=irToken)return;if(!showLearn(n)){finishIr(i,n,seq,tok);return}fetch('/api/status').then(r=>r.json()).then(j=>{if(tok!=irToken)return;var d=j.data;if(!d)return;if(d.ir_learn_seq!=seq){var v='Protocol '+d.ir_last_protocol+' - '+d.ir_last_code;setIrRow(i,v,'');setIr('Learned '+n+' - '+v,'oktxt');return}if(!d.ir_learning){setIr('Learn timeout - no valid signal','errtxt');return}var nd=d.ir_duplicate_key<irNames.length?d.ir_duplicate_key:-1;if(nd>=0&&(nd!=irDup||d.ir_reject_seq!=irReject))hitIr(nd);irDup=nd;irReject=d.ir_reject_seq;setTimeout(()=>watchIr(i,n,seq,tok),500)}).catch(()=>{if(tok!=irToken)return;if(showLearn(n))setTimeout(()=>watchIr(i,n,seq,tok),500);else finishIr(i,n,seq,tok)})}"
    "function learn(i,n){irToken++;irDup=-1;irReject=0;var tok=irToken;setIr('Starting '+n+'...','muted');fetch('/api/ir/learn',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key_index='+i}).then(r=>r.json()).then(d=>{if(d.ok){irReject=d.rej_seq||0;irDeadline=Date.now()+d.timeout*1000;watchIr(i,n,d.seq,tok)}else setIr('Learn failed','errtxt')}).catch(()=>setIr('Learn failed: network error','errtxt'))}"
    "function clearIr(i,n){if(!confirm('Clear IR code for '+n+'?'))return;setIr('Clearing '+n+'...','muted');fetch('/api/ir/learn',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key_index='+i+'&clear=1'}).then(r=>r.json().then(j=>({ok:r.ok,j:j}))).then(x=>{if(x.ok&&x.j.ok){setIrRow(i,'Not learned','');setIr(x.j.changed?('Cleared '+n):('No code for '+n),x.j.changed?'oktxt':'muted');setTimeout(()=>location.reload(),600)}else setIr('Clear failed','errtxt')}).catch(()=>setIr('Clear failed: network error','errtxt'))}"
    "loadHistCfg();"
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

    // LED Flash
    snprintf(buf, sizeof(buf), "%d", _controller->getLedFlashDuration());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(CONFIG_LED_FLASH_END);

    // Runtime Save
    snprintf(buf, sizeof(buf), "%u", _controller->getRuntimeSaveIntervalMinutes());
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk(CONFIG_RUNTIME_SAVE_END);

    // Auto Restore
    Esp32BaseWeb::sendChunk(_controller->getAutoRestore() ? "selected" : "");
    Esp32BaseWeb::sendChunk(CONFIG_AUTO_END);
    Esp32BaseWeb::sendChunk(_controller->getAutoRestore() ? "" : "selected");
    Esp32BaseWeb::sendChunk(CONFIG_AUTO_END2);
    char duration[24];
    formatRunDuration(_controller->getTotalRunDuration(), duration, sizeof(duration));
    Esp32BaseWeb::sendChunk(duration);
    Esp32BaseWeb::sendChunk(CONFIG_RUNTIME_END);

    for (uint8_t i = 0; i < IR_KEY_COUNT; i++) {
        uint8_t protocol = 0;
        uint64_t code = 0;
        _ir->getKeyCode(i, &protocol, &code);
        bool duplicate = false;
        if (protocol != 0 || code != 0) {
            for (uint8_t j = 0; j < IR_KEY_COUNT; j++) {
                if (j == i) continue;
                uint8_t other_protocol = 0;
                uint64_t other_code = 0;
                _ir->getKeyCode(j, &other_protocol, &other_code);
                if (protocol == other_protocol && code == other_code) {
                    duplicate = true;
                    break;
                }
            }
        }
        char value[56];
        if (protocol != 0 || code != 0) {
            snprintf(value, sizeof(value), "%s%u - 0x%016llX",
                     duplicate ? "Duplicate: protocol " : "Protocol ",
                     protocol, (unsigned long long)code);
        } else {
            strcpy(value, "Not learned");
        }
        char row[320];
        snprintf(row, sizeof(row),
            "<div class=irrow id=irr%u><div><b>%s</b><span id=irv%u%s>%s</span></div>"
            "<button onclick='learn(%u,\"%s\")'>Learn</button>"
            "<button class=secondary onclick='clearIr(%u,\"%s\")'>Clear</button></div>",
            i, irKeyName(i), i, duplicate ? " class=warn" : "", value,
            i, irKeyName(i), i, irKeyName(i));
        Esp32BaseWeb::sendChunk(row);
    }
    Esp32BaseWeb::sendChunk(CONFIG_IR_END);

    Esp32BaseWeb::sendFooter();
}

// API Handlers

void FanWeb::handleApiStatus() {
    if (!Esp32BaseWeb::checkAuth()) return;

    char buf[2200];
    char clock[24];
    const char* stateStr;
    switch (_controller->getState()) {
        case SYS_IDLE: stateStr = "Idle"; break;
        case SYS_RUNNING: stateStr = "Running"; break;
        case SYS_SLEEP: stateStr = "Sleep"; break;
        case SYS_ERROR: stateStr = "Error"; break;
        case SYS_RECOVERING: stateStr = "Recovering"; break;
        default: stateStr = "Unknown"; break;
    }
    const uint32_t tachPulses = _controller->getTachPulseTotal();
    const uint8_t tachLevel = _controller->getTachPinLevel();
    const char* stateDetail = stateStr;
    if (_controller->isBlocked()) {
        stateDetail = strcmp(stateStr, "Error") == 0 ? "Error / Blocked" : "Blocked";
    } else if (_controller->getState() == SYS_ERROR &&
               _controller->getTargetSpeed() > 0 &&
               _controller->getCurrentRpm() == 0) {
        stateDetail = tachPulses == 0 ? "Error / No TACH pulses" : "Error / No RPM";
    }

    char ip[24];
    if (Esp32BaseWiFi::isConnected()) {
        Esp32BaseWiFi::ip(ip, sizeof(ip));
    } else {
        strcpy(ip, "N/A");
    }
    long rssi = Esp32BaseWiFi::isConnected() ? (long)Esp32BaseWiFi::rssi() : 0;
    if (Esp32BaseNtp::isTimeSynced()) {
        Esp32BaseNtp::formatTime(clock, sizeof(clock), "%Y-%m-%d %H:%M:%S");
    } else {
        strcpy(clock, "N/A");
    }

    uint8_t ir_key = _ir->getLearnedKeyIndex();
    snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"data\":{\"state\":\"%s\",\"state_detail\":\"%s\",\"speed\":%d,\"target_speed\":%d,\"gear\":%u,\"rpm\":%u,"
        "\"tach_pulses\":%lu,\"tach_level\":%u,\"timer_remaining\":%lu,"
        "\"run_duration\":%lu,\"boot_run_duration\":%lu,\"blocked\":%s,\"min_speed\":%u,\"soft_start\":%u,\"soft_stop\":%u,"
        "\"block_detect\":%u,\"sleep_wait\":%u,\"auto_restore\":%s,\"led_flash_ms\":%u,"
        "\"ip\":\"%s\",\"rssi\":%ld,\"clock\":\"%s\","
        "\"ir_learning\":%s,\"ir_key\":%u,\"ir_remaining\":%lu,\"ir_learn_seq\":%lu,"
        "\"ir_reject_seq\":%lu,\"ir_duplicate_key\":%u,"
        "\"ir_last_protocol\":%u,\"ir_last_code\":\"0x%016llX\"}}",
        stateStr, stateDetail, _controller->getCurrentSpeed(), _controller->getTargetSpeed(),
        _controller->getCurrentGear(),
        _controller->getCurrentRpm(),
        (unsigned long)tachPulses,
        tachLevel,
        (unsigned long)_controller->getTimerRemaining(),
        (unsigned long)_controller->getTotalRunDuration(),
        (unsigned long)_controller->getBootRunDuration(),
        _controller->isBlocked() ? "true" : "false",
        _controller->getMinEffectiveSpeed(),
        _controller->getSoftStartTime(),
        _controller->getSoftStopTime(),
        _controller->getBlockDetectTime(),
        _controller->getSleepWaitTime(),
        _controller->getAutoRestore() ? "true" : "false",
        _controller->getLedFlashDuration(),
        ip, rssi, clock,
        _ir->isLearning() ? "true" : "false", ir_key,
        (unsigned long)_ir->getLearningRemaining(),
        (unsigned long)_ir->getLearnedSequence(),
        (unsigned long)_ir->getLearnRejectSequence(),
        _ir->getDuplicateKeyIndex(),
        _ir->getLastProtocol(),
        (unsigned long long)_ir->getLastCode()
    );
    Esp32BaseWeb::sendJson(200, buf);
}

void FanWeb::handleApiHistory() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!_history) {
        Esp32BaseWeb::sendJson(503, "{\"ok\":false,\"error\":\"history unavailable\"}");
        return;
    }

    FanHistoryRange range;
    if (!parseHistoryRange(&range)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid range\"}");
        return;
    }

    uint32_t sinceSeq = 0;
    const bool hasSinceSeq = Esp32BaseWeb::hasParam("since_seq") &&
                             parseUintParam("since_seq", 0, 4294967295UL, &sinceSeq);

    if (!Esp32BaseWeb::beginResponse(200, "application/json")) {
        return;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"range\":\"%s\",\"data\":{", historyRangeName(range));
    Esp32BaseWeb::sendChunk(buf);
    sendHistoryConfigObject(_history->config());
    Esp32BaseWeb::sendChunk(",\"points\":[");

    bool first = true;
    const uint16_t count = _history->count(range);
    for (uint16_t i = 0; i < count; ++i) {
        FanHistoryPoint point;
        if (!_history->pointAt(range, i, &point)) continue;
        const uint32_t seq = _history->sequenceAt(range, i);
        if (hasSinceSeq && seq <= sinceSeq) continue;
        if (!first) {
            Esp32BaseWeb::sendChunk(",");
        }
        first = false;
        snprintf(buf, sizeof(buf),
                 "{\"seq\":%lu,\"t_ms\":%lu,\"speed\":%u,\"target_speed\":%u,\"rpm\":%u}",
                 static_cast<unsigned long>(seq),
                 static_cast<unsigned long>(point.t_ms),
                 static_cast<unsigned>(point.speed),
                 static_cast<unsigned>(point.target_speed),
                 static_cast<unsigned>(point.rpm));
        Esp32BaseWeb::sendChunk(buf);
        yield();
    }

    Esp32BaseWeb::sendChunk("]}}");
    Esp32BaseWeb::endResponse();
}

void FanWeb::handleApiHistoryConfig() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!_history) {
        Esp32BaseWeb::sendJson(503, "{\"ok\":false,\"error\":\"history unavailable\"}");
        return;
    }

    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendHistoryConfigJson(_history->config());
        return;
    }

    uint32_t shortPoints = 0;
    uint32_t shortSampleMs = 0;
    uint32_t longPoints = 0;
    uint32_t longSampleS = 0;
    if (!parseUintParam("short_points", FanHistory::MIN_POINTS, FanHistory::MAX_POINTS, &shortPoints) ||
        !parseUintParam("short_sample_ms", 100, 5000, &shortSampleMs) ||
        !parseUintParam("long_points", FanHistory::MIN_POINTS, FanHistory::MAX_POINTS, &longPoints) ||
        !parseUintParam("long_sample_s", 10, 600, &longSampleS)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid history config\"}");
        return;
    }

    char error[80];
    if (!_history->configure(static_cast<uint16_t>(shortPoints),
                             static_cast<uint16_t>(shortSampleMs),
                             static_cast<uint16_t>(longPoints),
                             static_cast<uint16_t>(longSampleS),
                             error,
                             sizeof(error))) {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", error);
        Esp32BaseWeb::sendJson(400, buf);
        return;
    }

    sendHistoryConfigJson(_history->config());
}

void FanWeb::handleApiSpeed() {
    if (!Esp32BaseWeb::checkAuth()) return;

    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        uint32_t speed = 0;
        if (parseUintParam("speed", 0, 100, &speed)) {
            ESP32BASE_LOG_I("FanWeb", "user_action speed=%lu", static_cast<unsigned long>(speed));
            if (!_controller->setSpeed(static_cast<uint8_t>(speed))) {
                ESP32BASE_LOG_W("FanWeb", "speed_request_failed speed=%lu", static_cast<unsigned long>(speed));
                Esp32BaseWeb::sendJson(409, "{\"ok\":false,\"error\":\"speed rejected\"}");
                return;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%d,\"target_speed\":%d}",
                     _controller->getCurrentSpeed(), _controller->getTargetSpeed());
            Esp32BaseWeb::sendJson(200, buf);
            return;
        }
        ESP32BASE_LOG_W("FanWeb", "invalid_speed_request");
        Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid request\"}");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"ok\":true,\"speed\":%d,\"target_speed\":%d}",
                 _controller->getCurrentSpeed(), _controller->getTargetSpeed());
        Esp32BaseWeb::sendJson(200, buf);
    }
}

void FanWeb::handleApiTimer() {
    if (!Esp32BaseWeb::checkAuth()) return;

    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        uint32_t seconds = 0;
        if (parseUintParam("seconds", 0, 356400, &seconds)) {
            ESP32BASE_LOG_I("FanWeb", "user_action timer_seconds=%lu", static_cast<unsigned long>(seconds));
            if (!_controller->setTimer(seconds)) {
                ESP32BASE_LOG_W("FanWeb", "timer_request_failed seconds=%lu", static_cast<unsigned long>(seconds));
                Esp32BaseWeb::sendJson(500, "{\"ok\":false,\"error\":\"timer save failed\"}");
                return;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"ok\":true,\"timer_remaining\":%lu}",
                     (unsigned long)_controller->getTimerRemaining());
            Esp32BaseWeb::sendJson(200, buf);
            return;
        }
        ESP32BASE_LOG_W("FanWeb", "invalid_timer_request");
        Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid request\"}");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"ok\":true,\"timer_remaining\":%lu}", (unsigned long)_controller->getTimerRemaining());
        Esp32BaseWeb::sendJson(200, buf);
    }
}

void FanWeb::handleApiStop() {
    if (!Esp32BaseWeb::checkAuth()) return;
    ESP32BASE_LOG_I("FanWeb", "user_action stop_fan");
    if (!_controller->stop()) {
        ESP32BASE_LOG_W("FanWeb", "stop_request_failed");
        Esp32BaseWeb::sendJson(500, "{\"ok\":false,\"error\":\"stop save failed\"}");
        return;
    }
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
}

void FanWeb::handleApiConfig() {
    if (!Esp32BaseWeb::checkAuth()) return;

    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        ESP32BASE_LOG_I("FanWeb", "user_action save_config");
        uint8_t changed = 0;
        uint8_t min_speed = _controller->getMinEffectiveSpeed();
        uint16_t soft_start = _controller->getSoftStartTime();
        uint16_t soft_stop = _controller->getSoftStopTime();
        uint16_t block_detect = _controller->getBlockDetectTime();
        uint16_t sleep_wait = _controller->getSleepWaitTime();
        uint16_t led_flash_ms = _controller->getLedFlashDuration();
        uint8_t runtime_save_min = _controller->getRuntimeSaveIntervalMinutes();
        bool auto_restore = _controller->getAutoRestore();

        if (Esp32BaseWeb::hasParam("min_speed")) {
            uint32_t parsed = 0;
            if (!parseUintParam("min_speed", 0, 50, &parsed)) {
                Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid min_speed\"}");
                return;
            }
            min_speed = static_cast<uint8_t>(parsed);
        }
        if (Esp32BaseWeb::hasParam("soft_start")) {
            uint32_t parsed = 0;
            if (!parseUintParam("soft_start", 0, 10000, &parsed)) {
                Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid soft_start\"}");
                return;
            }
            soft_start = static_cast<uint16_t>(parsed);
        }
        if (Esp32BaseWeb::hasParam("soft_stop")) {
            uint32_t parsed = 0;
            if (!parseUintParam("soft_stop", 0, 10000, &parsed)) {
                Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid soft_stop\"}");
                return;
            }
            soft_stop = static_cast<uint16_t>(parsed);
        }
        if (Esp32BaseWeb::hasParam("block_detect")) {
            uint32_t parsed = 0;
            if (!parseUintParam("block_detect", 100, 5000, &parsed)) {
                Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid block_detect\"}");
                return;
            }
            block_detect = static_cast<uint16_t>(parsed);
        }
        if (Esp32BaseWeb::hasParam("sleep_wait")) {
            uint32_t parsed = 0;
            if (!parseUintParam("sleep_wait", 1, 3600, &parsed)) {
                Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid sleep_wait\"}");
                return;
            }
            sleep_wait = static_cast<uint16_t>(parsed);
        }
        if (Esp32BaseWeb::hasParam("led_flash_ms")) {
            uint32_t parsed = 0;
            if (!parseUintParam("led_flash_ms", 0, 2000, &parsed)) {
                Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid led_flash_ms\"}");
                return;
            }
            led_flash_ms = static_cast<uint16_t>(parsed);
        }
        if (Esp32BaseWeb::hasParam("runtime_save_min")) {
            uint32_t parsed = 0;
            if (!parseUintParam("runtime_save_min", 1, 60, &parsed)) {
                Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid runtime_save_min\"}");
                return;
            }
            runtime_save_min = static_cast<uint8_t>(parsed);
        }
        if (Esp32BaseWeb::hasParam("auto_restore")) {
            uint32_t parsed = 0;
            if (!parseUintParam("auto_restore", 0, 1, &parsed)) {
                Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid auto_restore\"}");
                return;
            }
            auto_restore = parsed != 0;
        }

        bool ok = _controller->applyConfig(min_speed, soft_start, soft_stop, block_detect,
                                           sleep_wait, led_flash_ms, runtime_save_min,
                                           auto_restore, &changed);
        if (ok && changed > 0) {
            _controller->notifyUserAction();
        }
        ESP32BASE_LOG_I("FanWeb", "config_save_complete changed=%u ok=%u",
                          (unsigned)changed, ok ? 1U : 0U);
        char buf[320];
        snprintf(buf, sizeof(buf),
            "{\"ok\":%s,\"changed\":%u,\"data\":{\"min_effective_speed\":%d,\"soft_start\":%d,\"soft_stop\":%d,"
            "\"block_detect\":%d,\"sleep_wait\":%d,\"led_flash_ms\":%d,\"runtime_save_min\":%u,\"auto_restore\":%s}}",
            ok ? "true" : "false",
            (unsigned)changed,
            _controller->getMinEffectiveSpeed(),
            _controller->getSoftStartTime(),
            _controller->getSoftStopTime(),
            _controller->getBlockDetectTime(),
            _controller->getSleepWaitTime(),
            _controller->getLedFlashDuration(),
            _controller->getRuntimeSaveIntervalMinutes(),
            _controller->getAutoRestore() ? "true" : "false"
        );
        Esp32BaseWeb::sendJson(ok ? 200 : 500, buf);
    } else {
        char buf[320];
        snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"data\":{\"min_effective_speed\":%d,\"soft_start\":%d,\"soft_stop\":%d,"
            "\"block_detect\":%d,\"sleep_wait\":%d,\"led_flash_ms\":%d,\"runtime_save_min\":%u,\"auto_restore\":%s}}",
            _controller->getMinEffectiveSpeed(),
            _controller->getSoftStartTime(),
            _controller->getSoftStopTime(),
            _controller->getBlockDetectTime(),
            _controller->getSleepWaitTime(),
            _controller->getLedFlashDuration(),
            _controller->getRuntimeSaveIntervalMinutes(),
            _controller->getAutoRestore() ? "true" : "false"
        );
        Esp32BaseWeb::sendJson(200, buf);
    }
}

void FanWeb::handleApiRuntimeReset() {
    if (!Esp32BaseWeb::checkAuth()) return;

    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendJson(405, "{\"ok\":false,\"error\":\"method not allowed\"}");
        return;
    }

    ESP32BASE_LOG_W("FanWeb", "user_action reset_total_run");
    if (!_controller->resetTotalRunDuration()) {
        Esp32BaseWeb::sendJson(500, "{\"ok\":false,\"error\":\"total run reset failed\"}");
        return;
    }

    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"run_duration\":%lu,\"boot_run_duration\":%lu}",
             static_cast<unsigned long>(_controller->getTotalRunDuration()),
             static_cast<unsigned long>(_controller->getBootRunDuration()));
    Esp32BaseWeb::sendJson(200, buf);
}

void FanWeb::handleApiIrLearn() {
    if (!Esp32BaseWeb::checkAuth()) return;

    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST) && Esp32BaseWeb::hasParam("key_index")) {
        uint32_t idx = 0;
        if (parseUintParam("key_index", 0, IR_KEY_COUNT - 1, &idx)) {
            if (Esp32BaseWeb::hasParam("clear")) {
                IRCodeChangeResult result = _controller->clearIRCode(static_cast<uint8_t>(idx));
                bool changed = result == IR_CODE_CHANGED;
                if (changed) _controller->notifyUserAction();
                ESP32BASE_LOG_I("FanWeb", "user_action ir_clear key=%lu changed=%u",
                                  static_cast<unsigned long>(idx), changed ? 1U : 0U);
                if (result == IR_CODE_SAVE_FAILED) {
                    Esp32BaseWeb::sendJson(500, "{\"ok\":false,\"error\":\"ir save failed\"}");
                    return;
                }
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"changed\":%s}", changed ? "true" : "false");
                Esp32BaseWeb::sendJson(200, buf);
                return;
            }
            if (_ir->startLearning(static_cast<uint8_t>(idx))) {
                ESP32BASE_LOG_I("FanWeb", "user_action ir_learn key=%lu", static_cast<unsigned long>(idx));
                char buf[128];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"learning\":true,\"timeout\":10,\"seq\":%lu,\"rej_seq\":%lu}",
                         (unsigned long)_ir->getLearnedSequence(),
                         (unsigned long)_ir->getLearnRejectSequence());
                Esp32BaseWeb::sendJson(200, buf);
                return;
            }
        }
    }
    ESP32BASE_LOG_W("FanWeb", "invalid_ir_learn_request");
    Esp32BaseWeb::sendJson(400, "{\"error\":\"invalid request\"}");
}
