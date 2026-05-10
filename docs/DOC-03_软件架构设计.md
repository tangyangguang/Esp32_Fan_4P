# DOC-03 软件架构设计

| 字段 | 内容 |
| --- | --- |
| 文档编号 | DOC-03 |
| 项目名称 | ESP32 壁炉烟囱正压送风控制器 |
| 版本 | v0.1 |
| 日期 | 2026-05-05 |
| 状态 | 建设中 |

## 1. 架构目标

本项目迁移成熟 ESP12F_Fan_4P 风扇控制逻辑到 ESP32，并以真实业务验证 Esp32Base Full profile。业务代码只处理风扇领域逻辑；配置、日志、网络、Web、OTA、诊断等基础能力由 Esp32Base 提供。

```text
Application
  main.cpp
  - ESP32 引脚配置
  - Esp32Base 初始化
  - 自定义 Web 路由注册
  - BOOT 清 WiFi

Web/Application
  web/FanWeb.*
  - /fan
  - /config
  - /api/*

Business
  fan/FanController.*
  - 状态机
  - 档位/定时/恢复/休眠策略
  - 配置读写

HAL
  fan/FanDriver.*
  fan/ButtonDriver.*
  fan/LedIndicator.*
  fan/IRReceiverDriver.*

Infrastructure
  Esp32Base Full profile
```

## 2. 层间规则

- `fan/` 不依赖 `web/`。
- `web/` 持有 `FanController` 和 `IRReceiverDriver` 引用，只负责页面和 API。
- `Esp32Base` 不依赖本项目。
- ISR 中不调用 Esp32Base 日志、配置、Web、事件总线等同步 API。
- 基础库能力缺口记录到 `docs/ESP32BASE_PROMPTS.md`。

## 3. 模块划分

| 模块 | 文件 | 职责 |
| --- | --- | --- |
| main | `src/main.cpp` | Esp32Base Full profile 初始化、路由注册、BOOT 键清 WiFi |
| FanDriver | `src/fan/FanDriver.*` | ESP32 LEDC PWM、TACH 中断、RPM、堵转、软启动/停止 |
| ButtonDriver | `src/fan/ButtonDriver.*` | 加速/减速按键消抖、短按事件和双键长按事件 |
| LedIndicator | `src/fan/LedIndicator.*` | LED 档位亮度、闪烁、故障提示 |
| IRReceiverDriver | `src/fan/IRReceiverDriver.*` | 红外解码、学习、匹配 |
| FanController | `src/fan/FanController.*` | 核心状态机、配置、定时、断电恢复、低功耗策略 |
| FanWeb | `src/web/FanWeb.*` | 页面 HTML 和 REST API |

## 4. Esp32Base 模块映射

| Esp32Base 模块 | 本项目用途 |
| --- | --- |
| `Esp32BaseLog` | 串口日志、统一日志宏 |
| `Esp32BaseConfig` | NVS 配置、运行状态持久化 |
| `Esp32BaseSystem` | 系统资源和芯片信息 |
| `Esp32BaseBus` | 后续订阅 WiFi/Web 等基础事件 |
| `Esp32BaseWatchdog` | 主循环看门狗 |
| `Esp32BaseWiFi::setPowerSave` | 停止后进入 WiFi power save，替代 ESP8266 modem sleep |
| `Esp32BaseFs` | LittleFS 挂载 |
| `Esp32BaseFileLog` | `/logs/app.log` 滚动日志 |
| `Esp32BaseHealth` | 健康诊断 |
| `Esp32BaseWiFi` | STA 连接、AP 配网、清凭证、power save |
| `Esp32BaseDns` | captive portal DNS |
| `Esp32BaseNtp` | 时间同步和状态页时间 |
| `Esp32BaseMdns` | `.local` 访问 |
| `Esp32BaseWeb` | Basic Auth、内置管理页、自定义路由 |
| `Esp32BaseOta` | Web OTA |

## 5. 状态机

### 5.1 系统状态

| 状态 | 含义 | 进入 | 退出 |
| --- | --- | --- | --- |
| `SYS_IDLE` | 空闲 | 停止/初始化完成 | 启动/休眠/故障 |
| `SYS_RUNNING` | 运行 | 非零速度 | 停止/定时结束/堵转 |
| `SYS_SLEEP` | 省电待机 | 停止且空闲超时 | 任意操作 |
| `SYS_ERROR` | 故障 | 堵转 | 启动恢复尝试/重启 |
| `SYS_RECOVERING` | 堵转恢复中 | 故障后重新设置非零速度 | RPM 恢复/恢复超时 |

### 5.2 风扇状态

| 状态 | 含义 |
| --- | --- |
| `FAN_STATE_IDLE` | 输出 0 |
| `FAN_STATE_SOFT_START` | 从 0 渐变到目标速度 |
| `FAN_STATE_RUNNING` | 稳定运行 |
| `FAN_STATE_SOFT_STOP` | 从当前速度渐变到 0 |
| `FAN_STATE_BLOCKED` | 堵转保护 |

## 6. 初始化顺序

1. 设置固件信息和 hostname。
2. 调用 `Esp32BaseWeb::setDefaultAuth("admin", "admin")`，并设置设备名、业务首页和系统导航模式。
3. 注册 `/fan`、`/config` 和 `/api/*` 路由；`/fan`、`/config` 通过 `addPage(path, title, handler)` 进入基础库导航。
4. 在 `Esp32Base::begin()` 前启用 Config write/read audit，覆盖基础库启动期配置读取。
5. 调用 `Esp32Base::begin()`。
6. 启用文件日志。
7. 初始化风扇控制器。
8. loop 中持续调用 `Esp32Base::handle()` 和 `FanController::tick()`。

## 7. 当前基础环境

`platformio.ini` 当前选择：

```ini
platform = espressif32@6.9.0
board = esp32dev
framework = arduino
lib_deps =
    symlink://../Esp32Base
    WiFi
    DNSServer
    ESPmDNS
    LittleFS
    WebServer
    Update
    crankyoldgit/IRremoteESP8266@^2.8.6
build_flags =
    -DESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL
```

`src/deps_esp32base_full.cpp` 用于锚定 PlatformIO LDF，使 Full profile 所需的 framework 库参与链接。当前 `pio run -e esp32dev` 已通过。

命令行 Web OTA 使用相邻目录 `../Esp32Base/scripts/esp32base_webota.py` 注册 `webota` target。本项目在 `platformio.ini` 中通过 `extra_scripts` 引入该脚本；具体设备地址和认证信息不提交到仓库，由本地 `[esp32base_webota]` 配置段或 `ESP32BASE_WEBOTA_*` 环境变量提供。

## 8. 架构风险

| 风险 | 影响 | 处理 |
| --- | --- | --- |
| Full profile framework 依赖未被 LDF 自动发现 | 链接缺少 WiFi/WebServer 等符号 | 按 Esp32Base 示例显式声明 `lib_deps` 并加入 `deps_esp32base_full.cpp` |
| Web Auth 职责混入应用配置 | 重复实现密码持久化和修改页面 | 使用 Esp32Base 内置 Auth；应用只设置默认账号密码，修改入口使用 `/esp32base/auth` |
| Web 路由容量不足 | 自定义 API 注册失败 | 当前 `ESP32BASE_WEB_MAX_ROUTES=16`；若仍不足，反馈基础库或调整 API 聚合 |
| WiFi power save 不等同于 ESP8266 modem sleep | 省电和响应表现需实测 | 实机验证 Web 可访问性和响应时间 |
| IRremoteESP8266 在 ESP32 下资源占用较高 | RAM/实时性风险 | 先沿用成熟库，实测后再决定是否替换 |
