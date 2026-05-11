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

| key | API 字段 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- | --- |
| `min_spd` | `min_effective_speed` | int | 10 | 最低有效速度，0-50 |
| `soft_on` | `soft_start` | int | 1000 | 软启动 ms，0-10000 |
| `soft_off` | `soft_stop` | int | 1000 | 软停止 ms，0-10000 |
| `blk_ms` | `block_detect` | int | 1500 | 堵转检测 ms，100-5000 |
| `slp_s` | `sleep_wait` | int | 60 | 停止后进入 power save 的等待秒数，最小 1 |
| `restore` | `auto_restore` | bool | true | 上电恢复策略 |
| `led_ms` | `led_flash_ms` | int | 200 | 操作反馈 LED 闪烁时长 ms |
| `rt_save_m` | `runtime_save_min` | int | 1 | 运行状态持久化间隔分钟 |
| `last_spd` | `target_speed` | int | 0 | 上次速度 |
| `last_tim` | `timer_remaining` | int | 0 | 上次剩余定时秒数 |
| `run_s` | `run_duration` | int | 0 | 累计运行秒数；当前不升级 64-bit，见 `docs/RUN_DURATION_DECISION.md` |
| `ir_0..7` | IR 状态字段 | string | 空 | 红外学习码，格式为 `protocol:hexCode` |

应用不得使用 `eb_` 前缀 namespace，避免与 Esp32Base 内部配置冲突。Web Auth 使用 Esp32Base 内置持久化能力，应用只通过 `Esp32BaseWeb::setDefaultAuth("admin", "admin")` 提供默认值，账号密码修改统一走 `/esp32base/auth`。

两键同时长按 >5s 的出厂重置行为与 ESP12F_Fan_4P 对齐：清除 `fan` namespace，并调用 `Esp32BaseConfig::factoryReset()` 清理 Esp32Base 持久化配置。当前 Esp32Base 会清理 WiFi、Web Auth、系统和日志配置，因此 WiFi 凭证、Web Auth、系统计数和文件日志配置也会被清除，随后重启。

## 2. 运行状态

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `current_gear` | uint8_t | 当前档位 0-4 |
| `target_speed` | uint8_t | 用户目标速度 0-100 |
| `current_speed` | uint8_t | 实际 PWM 输出速度 |
| `rpm` | uint16_t | 当前转速 |
| `tach_pulses` | uint32_t | 启动后累计 TACH 下降沿计数，用于现场诊断 |
| `tach_level` | uint8_t | 当前 TACH 输入电平，1 为高、0 为低 |
| `timer_remaining` | uint32_t | 剩余定时秒数 |
| `run_duration` | uint32_t | 累计运行秒数，秒级上限满足 10 年以上运行统计 |
| `is_blocked` | bool | 堵转保护状态 |
| `is_sleeping` | bool | 是否进入 power save 策略 |

`Total run` 清零通过独立 API 执行，只把内存中的 `run_duration` 和 NVS `fan/run_s` 置 0 并立即 flush；失败时回滚内存值。`Boot run` 是本次启动累计值，不持久化，清零 `Total run` 时不修改。

RAM 历史曲线由 `FanHistory` 维护，两组环形缓冲均只保存在 RAM 中，设备重启后清空，不写 NVS、LittleFS 或日志：

| 范围 | 默认点数 | 默认采样 | 自动窗口 | 用途 |
| --- | ---: | ---: | ---: | --- |
| short | 500 | 500 ms | 250 秒 | 观察软启动、软停止、堵转等短时变化 |
| long | 500 | 10 s | 1 小时 23 分钟 | 观察小时级运行趋势 |

单点结构为 `uint32_t t_ms + uint16_t rpm + uint8_t speed + uint8_t target_speed`，每点 8 字节。每组点数可配置为 100..1200，RAM 中保存点数等于图表显示点数；两组最大约 19.2 KB RAM，默认两组共约 8 KB RAM。每组 ring 额外维护 RAM-only `seq` 计数，API 输出时现算点序号，用于前端增量拉取，避免 `millis()` 回绕影响历史更新。

## 3. FanDriver 设计

职责：

- 使用 ESP32 LEDC 输出 PWM。
- 使用 GPIO interrupt 采集 TACH pulse。
- 每 500 ms 计算一次 RPM。
- 执行软启动、软停止和堵转检测。

约定：

- ISR 只递增 `volatile` 计数，不写日志，不访问配置。
- 默认按 2 pulse/revolution 计算 RPM。
- PWM 分辨率默认 10 bit，频率默认 25 kHz。
- 当前产品只支持单风扇实例；TACH ISR 通过 `FanDriver::s_instance` 分发到唯一实例。若未来支持多风扇，需要改为 interrupt arg 或实例映射。
- Arduino ESP32 Core 2.x 使用 `ledcSetup/ledcAttachPin`；代码已为 Core 3.x 增加 `ledcAttach` 条件编译分支，但当前项目仍以 Core 2.x 构建为准，Core 3.x 需独立构建和 PWM 实测确认。

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
- 堵转后任意启动指令进入 `SYS_RECOVERING`，恢复窗口为软启动时间 + 堵转检测时间 + 500 ms；重复速度指令不重置窗口。
- 运行状态持久化需要限频，避免 NVS 高频写入。
- 倒计时最后 60 秒内每 10 秒强制保存一次 `last_tim`，降低断电恢复漂移。
- 加速键和减速键同时长按 >5s 执行完整出厂重置，清除风扇配置、WiFi 凭证和 Web 密码后重启。

## 5. FanWeb 设计

页面：

- `GET /fan`：状态和控制。
- `GET /history`：宽屏历史曲线。
- `GET /config`：参数配置和红外学习入口。
- Esp32Base 内置页面继续使用 `/esp32base/*`，包括 WiFi、OTA、Logs、Auth、Reboot，并通过 `addPage(path, title, handler)` 展示业务入口。

API：

| 路径 | 参数 | 行为 |
| --- | --- | --- |
| `/api/status` | 无 | 返回风扇、TACH 诊断、网络、时间状态 |
| `/api/speed` | `speed=0..100` | 设置或读取速度 |
| `/api/timer` | `seconds=0..356400` | 设置或读取定时 |
| `/api/stop` | 无 | 停止风扇 |
| `/api/config` | 配置表单字段 | 保存或读取配置 |
| `/api/runtime/reset` | 无，POST | 清零 Total run 和 `fan/run_s`，不清零 Boot run |
| `/api/ir/learn` | `key_index=0..7`，可选 `clear=1` | 启动或清除红外学习码 |
| `/api/history` | `range=short|long`，可选 `since_seq` | 分段返回 RAM 历史曲线点，点内含 `seq` 和 `t_ms` |
| `/api/history/config` | `short_points`、`short_sample_ms`、`long_points`、`long_sample_s` | 配置 RAM 历史点数和采样周期；窗口时长自动计算 |

实现约束：

- 所有页面和 API 都调用 `Esp32BaseWeb::checkAuth()`。
- 业务页面不显式放置 `/esp32base/auth` 修改密码入口；该入口由 Esp32Base 系统导航提供。
- 业务页面不自建业务入口或 Esp32Base 系统页面导航；顶部业务入口和底部系统入口统一由 Esp32Base 输出。
- JSON 输出优先使用固定缓冲区。
- HTML 页面优先 `sendChunk()`，避免大 `String` 拼接。
- 大量历史点输出必须使用 chunked response，不一次性拼接完整 JSON。
- 用户输入进入 JSON/HTML 前要转义；当前短字段先用数值输入，后续补全字符串转义。

## 6. main.cpp 设计

职责：

- 定义 ESP32 GPIO。
- 设置 firmware info；默认 hostname 由 `ESP32BASE_DEFAULT_HOSTNAME` 编译宏提供。
- 在 `Esp32Base::begin()` 前通过 `FanAppRuntime` 设置 Web Auth、业务首页和 Config audit。
- 在 Web 启动前通过 `FanAppRuntime` 注册应用路由。
- 启动 Esp32Base Full profile。
- 启动风扇控制器。
- loop 中调用基础库和业务 tick。
- 通过 `FanAppRuntime` 检测 BOOT 键长按并清除 WiFi 凭证。

## 7. 测试策略

阶段一：文档和基础环境。

- `platformio.ini` 可被 PlatformIO 识别。
- Esp32Base Full profile 能进入编译流程。
- 基础库编译问题记录到提示词。

阶段二：native 单元测试。

- FanController 状态机。
- 定时、恢复、堵转恢复。
- 配置边界。
- 应用路由注册、Config audit 启用和 BOOT 清 WiFi 时序。
- 按键消抖 50 ms 加 loop tick 后需满足本地响应 <= 200 ms。

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

处理方式：直接在协作回复中按功能输出提示词，由基础库完善。

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
- `pio test -e native` 通过，native 用例覆盖 FanDriver、FanController、FanWeb API/HTML chunk、FanAppRuntime 路由注册、Config audit 启用、BOOT 清 WiFi 时序、持久化失败事务边界和 IR 保存失败回滚。
- 串口上传已验证；每次烧录前应重新确认实际串口，上传速率固定为 115200。
- `pio run -e esp32dev -t webota` 通过，使用 Esp32Base `scripts/esp32base_webota.py`。
- AP 配网、局域网访问、`/esp32base/api/status`、业务 API、`/fan`、`/config`、`/esp32base/logs`、`/esp32base/auth` 和 `/esp32base/ota` 已完成首轮实机验证；具体 IP、串口和 Auth 持久化值以当前设备实际状态为准。
- 已通过 `/esp32base/ota` 上传固件，重启后基础库和业务页面/API 恢复正常。
- 已验证 WiFi power save 后 `/api/status` 仍可访问；具体测试参数以当次验证记录为准。
- `/fan` 和 `/config` 通过 `Esp32BaseWeb::addPage(path, title, handler)` 注册为业务入口，`/esp32base` 首页和内置顶栏已展示 Fan、Settings。
- Esp32Base Health tick 和 NTP 未同步降噪已完成首轮观察。

观察项：

- mDNS 首次解析延迟不作为本项目阻塞项；若多设备稳定复现明显延迟，再整理为 Esp32Base mDNS 体验优化提示词。
