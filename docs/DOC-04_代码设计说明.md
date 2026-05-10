# DOC-04 代码设计说明

| 字段 | 内容 |
| --- | --- |
| 文档编号 | DOC-04 |
| 项目名称 | ESP32 壁炉烟囱正压送风控制器 |
| 版本 | v0.1 |
| 日期 | 2026-05-05 |
| 状态 | 建设中 |

## 1. 配置设计

配置统一存储在 Esp32BaseConfig 的 NVS namespace `fan` 下。ESP32 NVS namespace 和 key 均有长度限制，因此本项目不沿用 ESP8266 版长 key。

| key | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `min_spd` | int | 10 | 最低有效速度，0-50 |
| `soft_on` | int | 1000 | 软启动 ms，0-10000 |
| `soft_off` | int | 1000 | 软停止 ms，0-10000 |
| `blk_ms` | int | 1500 | 堵转检测 ms，100-5000 |
| `slp_s` | int | 60 | 停止后进入 power save 的等待秒数 |
| `restore` | bool | true | 上电恢复策略 |
| `led_ms` | int | 200 | 操作反馈 LED 闪烁时长 ms |
| `rt_save_m` | int | 1 | 运行状态持久化间隔分钟 |
| `last_spd` | int | 0 | 上次速度 |
| `last_tim` | int | 0 | 上次剩余定时秒数 |
| `run_s` | int | 0 | 累计运行秒数 |
| `ir_0..7` | string | 空 | 红外学习码，格式为 `protocol:hexCode` |

应用不得使用 `eb_` 前缀 namespace，避免与 Esp32Base 内部配置冲突。Web Auth 使用 Esp32Base 内置持久化能力，应用只通过 `Esp32BaseWeb::setDefaultAuth("admin", "admin")` 提供默认值，账号密码修改统一走 `/esp32base/auth`。

## 2. 运行状态

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `current_gear` | uint8_t | 当前档位 0-4 |
| `target_speed` | uint8_t | 用户目标速度 0-100 |
| `current_speed` | uint8_t | 实际 PWM 输出速度 |
| `rpm` | uint16_t | 当前转速 |
| `timer_remaining` | uint32_t | 剩余定时秒数 |
| `run_duration` | uint32_t | 累计运行秒数 |
| `is_blocked` | bool | 堵转保护状态 |
| `is_sleeping` | bool | 是否进入 power save 策略 |

## 3. FanDriver 设计

职责：

- 使用 ESP32 LEDC 输出 PWM。
- 使用 GPIO interrupt 采集 TACH pulse。
- 每 500 ms 计算一次 RPM。
- 执行软启动、软停止和堵转检测。

约定：

- ISR 只递增 `volatile` 计数，不写日志，不访问配置。
- 默认按 2 pulse/revolution 计算 RPM。
- PWM 分辨率默认 8 bit，频率默认 25 kHz。
- Arduino ESP32 Core 2.x 使用 `ledcSetup/ledcAttachPin`，Core 3.x 后续需确认 API 兼容。

## 4. FanController 设计

职责：

- 聚合 `FanDriver`、`ButtonDriver`、`LedIndicator`、`IRReceiverDriver`。
- 处理按键、红外、Web 三种控制源。
- 维护系统状态机。
- 读取/保存配置。
- 定时和断电恢复。
- 停止后启用 `Esp32BaseWiFi::setPowerSave(true)`。

控制规则：

- `stop()` 清除定时并输出 0。
- `setSpeed(1..min_spd-1)` 自动提升到 `min_spd`。
- 软启动过程中收到新速度，按当前状态重新调度。
- 堵转后任意启动指令可尝试恢复。
- 运行状态持久化需要限频，避免 NVS 高频写入。

## 5. FanWeb 设计

页面：

- `GET /fan`：状态和控制。
- `GET /config`：参数配置和红外学习入口。
- Esp32Base 内置页面继续使用 `/esp32base/*`，包括 WiFi、OTA、Logs、Auth、Reboot，并通过 `addPage(path, title, handler)` 展示业务入口。

API：

| 路径 | 参数 | 行为 |
| --- | --- | --- |
| `/api/status` | 无 | 返回风扇、网络、时间状态 |
| `/api/speed` | `speed=0..100` | 设置或读取速度 |
| `/api/timer` | `seconds=0..356400` | 设置或读取定时 |
| `/api/stop` | 无 | 停止风扇 |
| `/api/config` | 配置表单字段 | 保存或读取配置 |
| `/api/ir/learn` | `key_index=0..7`，可选 `clear=1` | 启动或清除红外学习码 |

实现约束：

- 所有页面和 API 都调用 `Esp32BaseWeb::checkAuth()`。
- 业务页面不显式放置 `/esp32base/auth` 修改密码入口；该入口由 Esp32Base 系统导航提供。
- 业务页面不自建业务入口或 Esp32Base 系统页面导航；顶部业务入口和底部系统入口统一由 Esp32Base 输出。
- JSON 输出优先使用固定缓冲区。
- HTML 页面优先 `sendChunk()`，避免大 `String` 拼接。
- 用户输入进入 JSON/HTML 前要转义；当前短字段先用数值输入，后续补全字符串转义。

## 6. main.cpp 设计

职责：

- 定义 ESP32 GPIO。
- 设置 firmware info 和 hostname。
- 在 `Esp32Base::begin()` 前设置 Web Auth。
- 在 Web 启动前注册应用路由。
- 启动 Esp32Base Full profile。
- 启动风扇控制器。
- loop 中调用基础库和业务 tick。
- 检测 BOOT 键长按并清除 WiFi 凭证。

## 7. 测试策略

阶段一：文档和基础环境。

- `platformio.ini` 可被 PlatformIO 识别。
- Esp32Base Full profile 能进入编译流程。
- 基础库编译问题记录到提示词。

阶段二：native 单元测试。

- FanController 状态机。
- 定时、恢复、堵转恢复。
- 配置边界。

阶段三：ESP32 实机测试。

- PWM/TACH。
- Web/API。
- WiFi 配网、mDNS、NTP。
- 文件日志、OTA、看门狗。
- power save 后 Web 可访问性。

## 8. 基础库问题处理

如果实现中发现以下问题，不在本项目长期绕补丁：

- Esp32Base 模块无法在 Full profile 下编译。
- Web 路由容量或 handler API 不满足应用需求。
- Web Auth 内置持久化能力无法满足业务默认账号和基础库统一管理。
- FileLog 路径、日志页或 flush 行为不符合应用需求。
- WiFi power save 无法保持 Web 可访问。
- OTA 注册和认证条件不清晰。

处理方式：记录到 `docs/ESP32BASE_PROMPTS.md`，由基础库完善。

## 9. 当前代码状态

当前仓库中的源码已完成 ESP32/Esp32Base 基础迁移，固件构建和 native 核心测试已通过。

后续实现前必须检查：

- 是否仍残留 ESP8266 专用 API。
- Esp32Base Full profile 是否能编译和链接。
- `FanWeb` 是否只使用 Esp32Base 公开 Web API，不直接依赖底层 `WebServer`。
- native 测试替身是否与 Esp32Base 当前 API 一致。

代码完成标准：

- `pio run -e esp32dev` 通过。
- `pio test -e native` 通过。
- 无未记录的基础库绕补丁。
- 文档中的 API、配置 key、引脚和实现保持一致。

当前验证：

- `pio run -e esp32dev` 通过。
- `pio test -e native` 通过，7 个测试用例成功。
- `pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-130` 通过。
- 串口启动日志确认当前进入 `ESP32-Config-65E4` 配网 AP，`web server ready`，FanController 初始化完成。
- 当前设备持久化 Auth 已通过 Esp32Base 内置页面改为 `admin/admin`，旧 `admin/admin123` 已返回 401。
- AP 配网后设备 IP 为 `192.168.2.112`，`esp32-fan.local` 可解析。
- `http://192.168.2.112/api/status` 返回风扇状态、IP、RSSI、网络状态。
- `http://192.168.2.112/fan` 和 `http://192.168.2.112/config` 页面可完整返回。
- `POST http://192.168.2.112/api/speed speed=35`、`/api/timer seconds=60`、`/api/stop` 均返回成功。
- `http://192.168.2.112/esp32base/api/status` 返回 Esp32Base Full profile、heap、flash、WiFi connected 状态。
- `http://192.168.2.112/esp32base/logs` 和 `http://192.168.2.112/esp32base/ota` 页面 GET 返回 HTTP 200。
- `POST http://192.168.2.112/esp32base/ota` 上传当前 `firmware.bin` 返回 `{"ok":true}`，重启后基础库和业务页面/API 恢复正常。
- `http://192.168.2.112/esp32base/auth` 页面 GET 返回 HTTP 200。
- 临时设置 `sleep_wait=3` 后设备进入 `sleep` 状态，`/api/status` 仍可访问；验证后已恢复 `sleep_wait=60`。
- `/fan` 和 `/config` 通过 `Esp32BaseWeb::addPage(path, title, handler)` 注册为业务入口，`/esp32base` 首页和内置顶栏已展示 Fan、Settings。
- 新版 Esp32Base Health 已验证：历史日志仍有旧 `INFO health tick`，新固件启动后的 health tick 以 `DEBUG` 输出，默认 30 分钟最多一次。
- 新版 Esp32Base NTP 未同步状态已降噪，不再周期性输出 `ntp_sync_pending` WARN。

观察项：

- 使用 `esp32-fan.local` 通过 curl 访问时，首次解析约等待 5 秒；直接 IP 访问业务接口响应约 0.1-1.0 秒。
- 当前判断这是客户端侧 mDNS 解析表现，不作为本项目阻塞项；若浏览器长期复现明显延迟，再整理为 Esp32Base mDNS 体验优化提示词。
