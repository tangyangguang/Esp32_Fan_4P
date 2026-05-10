# DOC-01 需求规格说明

| 字段 | 内容 |
| --- | --- |
| 文档编号 | DOC-01 |
| 项目名称 | ESP32 壁炉烟囱正压送风控制器 |
| 版本 | v0.1 |
| 日期 | 2026-05-05 |
| 状态 | 建设中 |

## 1. 项目背景

设备用于木材壁炉烟囱正压送风：在刮风或烟气倒灌风险较高时，通过室外送风风扇向烟囱管道内形成正压，降低烟气从壁炉门缝冒出的概率。

本项目功能对齐成熟 ESP12F_Fan_4P 项目 `/Users/tyg/dir/claude_dir/ESP12F_Fan_4P`，但硬件平台改为 ESP32，并使用 `/Users/tyg/dir/claude_dir/Esp32Base` 作为基础库。项目同时承担 Esp32Base 全模块实战验证职责。

## 2. Must 需求

| 编号 | 功能 | 验收标准 | 责任边界 |
| --- | --- | --- | --- |
| F-01 | PWM 无级调速 | 支持 0-100%，PWM 频率 25 kHz，1%-9% 自动提升到最低有效转速，0% 停止 | 本项目 FanDriver |
| F-02 | TACH 转速反馈 | 四线风扇转速测量误差 <= 5%，更新频率 >= 1 Hz | 本项目 FanDriver |
| F-03 | 堵转保护 | 输出达到最低有效转速且无 RPM 持续达到配置值时切断输出并报警；任意启动指令可恢复尝试 | 本项目 FanDriver/FanController |
| F-04 | 本地按键 | 加速/减速短按调档，双键长按 5 秒恢复出厂，响应 <= 200 ms | 本项目 ButtonDriver/FanController |
| F-05 | 红外学习 | Web 触发学习，支持常见协议，记录加速、减速、停止、30min、1h、2h、4h、8h | 本项目 IRReceiverDriver/FanWeb |
| F-06 | 定时运行 | 支持 30min/1h/2h/4h/8h 预设和最大 99 小时自定义，倒计时结束停止 | 本项目 FanController/FanWeb |
| F-07 | Web 控制 | `/fan` 查看状态并控制速度/定时，Basic Auth 鉴权，页面移动端可用 | Esp32Base Web + 本项目 FanWeb |
| F-08 | REST API | 提供 `/api/status`、`/api/speed`、`/api/timer`、`/api/stop`、`/api/config`、`/api/ir/learn` | Esp32Base 路由 + 本项目 FanWeb |
| F-09 | 持久日志 | 文件日志断电不丢失，Web 可查看，至少保留约 64 KB | Esp32Base FileLog/Web Logs |
| F-10 | OTA | Web OTA 可用，复用 Web Basic Auth | Esp32Base OTA |
| F-11 | WiFi 配网 | 无凭证时进入 AP/captive portal 配网；清凭证后重启进入配网 | Esp32Base WiFi/DNS/Web + main |
| F-12 | 断电恢复 | 可配置恢复上次速度和剩余定时，也可配置上电保持停止 | 本项目 FanController + Esp32BaseConfig |
| F-13 | 软启动/软停止 | 默认 1 秒，范围 0-10 秒可配置 | 本项目 FanDriver/FanController |
| F-14 | 低功耗策略 | 风扇停止且空闲达到配置值后启用 WiFi power save，保持 Web 可访问 | 本项目策略 + Esp32BaseWiFi |
| F-15 | mDNS | 使用 `esp32-fan.local` 或后续动态 hostname 访问 | Esp32Base mDNS |
| F-16 | 配置页面 | Web 修改风扇参数、恢复策略、红外学习 | 本项目 FanWeb + Esp32BaseConfig |
| F-17 | Web Auth | 通过 Esp32Base 内置页面修改账号密码 | Esp32Base `/esp32base/auth` |
| F-18 | BOOT 清 WiFi | GPIO0 长按 1 秒清除 WiFi 凭证并重启 | main + Esp32BaseWiFi |
| F-19 | LED 指示 | 档位亮度、WiFi 慢闪、故障快闪、操作闪烁 | 本项目 LedIndicator |
| F-20 | 看门狗 | 主循环正常喂狗，长时间卡死可恢复 | Esp32Base Watchdog |
| F-21 | NTP 时间 | 网络可用后日志和状态页可显示真实时间 | Esp32Base NTP |

## 3. Should 需求

| 编号 | 功能 | 验收标准 |
| --- | --- | --- |
| S-01 | 累计运行时长 | 总运行秒数持久化，Web 页面可查看 |
| S-02 | 健康诊断 | Web/API 可观察 heap、flash、FS、重启原因等 Esp32Base 健康信息 |
| S-03 | 动态 hostname | 后续支持 `fan-xxxx.local`，`xxxx` 来自 MAC 后四位 |

## 4. Won't 需求

- 不做云端远程控制。
- 不做 MQTT/Home Assistant 原生接入。
- 不做手机 App。
- 不做电池供电。
- 不做外壳结构设计。
- 不做 HTTPS、多用户权限和大型前端 SPA。

## 5. 非功能需求

| 编号 | 类型 | 要求 |
| --- | --- | --- |
| NF-01 | 可靠性 | 连续运行 90 天无需手动重启 |
| NF-02 | 响应 | 本地 <= 200 ms，红外 <= 300 ms，Web <= 1 s |
| NF-03 | 内存 | RAM 峰值 <= 80%，为 OTA 和 Web 预留空间 |
| NF-04 | 安全 | Web Basic Auth，默认 `admin/admin`，支持修改密码 |
| NF-05 | 环境 | -20 到 60 摄氏度正常工作，硬件设计满足户外防护 |
| NF-06 | 可维护 | 基础库问题输出完整提示词，不在本项目长期绕补丁 |

## 6. 边界条件

| 场景 | 处理 |
| --- | --- |
| 断电时正在定时 | 持久化速度和剩余秒数，重启后按恢复策略处理 |
| 多控制源冲突 | 本地、红外、Web 平等，最后一次指令生效 |
| 堵转后再启动 | 清除故障并进入恢复尝试窗口，有 RPM 则恢复，否则保持保护 |
| WiFi 不可用 | 本地和红外继续可用，Web 操作不可用 |
| NTP 失败 | 不影响控制，日志先使用 uptime，后续同步后使用真实时间 |
| OTA 过程中 | 暂停非必要配置 flush，避免影响升级稳定性，由 Esp32Base 保障 |
| Deep sleep | 本项目默认不进入 deep sleep，因为需求要求 Web 保持可访问 |

## 7. Esp32Base 验证清单

本项目必须验证以下 Esp32Base 模块：

- `Esp32BaseLog`
- `Esp32BaseConfig`
- `Esp32BaseSystem`
- `Esp32BaseBus`
- `Esp32BaseWatchdog`
- `Esp32BaseSleep`
- `Esp32BaseFs`
- `Esp32BaseFileLog`
- `Esp32BaseHealth`
- `Esp32BaseWiFi`
- `Esp32BaseDns`
- `Esp32BaseNtp`
- `Esp32BaseMdns`
- `Esp32BaseWeb`
- `Esp32BaseOta`

## 8. 需求追踪

| 需求 | 本项目模块 | Esp32Base 模块 | 当前状态 |
| --- | --- | --- | --- |
| F-01, F-02, F-03, F-13 | FanDriver | Log | 已实现，待 PWM/TACH 实机仪器验证 |
| F-04, F-19 | ButtonDriver, LedIndicator | Log | 已实现，native 测试通过，待硬件操作验证 |
| F-05 | IRReceiverDriver, FanWeb | Web, Config, Log | 已实现，待真实遥控器验证 |
| F-06, F-12, S-01 | FanController | Config, Log | 已实现，native 测试通过 |
| F-07, F-08, F-16 | FanWeb | Web, Config, Log | 已实现，固件构建通过 |
| F-09 | main, FanController | Fs, FileLog, Web Logs | 固件已编译通过，待实机验证 |
| F-10 | main | Ota, Web, Fs | 固件已编译通过，待实机验证 |
| F-11, F-15, F-17, F-18 | main | WiFi, DNS, mDNS, Web | 固件已编译通过，待实机验证 |
| F-14 | FanController | WiFi, Sleep | 已实现，待长时间实机观察 |
| F-20, F-21, S-02 | main | Watchdog, NTP, Health, System | 已集成，待长时间实机观察 |
