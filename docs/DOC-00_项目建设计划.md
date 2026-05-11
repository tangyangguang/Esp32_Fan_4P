# DOC-00 项目建设计划

| 字段 | 内容 |
| --- | --- |
| 文档编号 | DOC-00 |
| 项目名称 | ESP32 壁炉烟囱正压送风控制器 |
| 版本 | v0.1 |
| 日期 | 2026-05-05 |
| 状态 | 建设中 |

## 1. 建设原则

- 业务功能对齐成熟项目 `/Users/tyg/dir/claude_dir/ESP12F_Fan_4P`。
- 基础能力使用相邻目录 `../Esp32Base`，默认启用 Full profile。
- 项目文档先行，文档是后续实现和验收的基准。
- 涉及 Esp32Base 能力缺口或 bug 时，直接在协作回复中按功能输出完整提示词，不在本项目长期打补丁，也不维护独立提示词文件。
- 本项目按 Esp32Base 示例工程显式声明 Full profile 需要的 Arduino framework 库，避免 PlatformIO LDF 漏链。

## 2. 当前状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| 文档骨架 | 已完成 | README、DOC-00..04 |
| PlatformIO 环境 | 已引入 | `esp32dev`、`espressif32@6.9.0`、Arduino |
| Esp32Base 引入 | 已引入 | `symlink://../Esp32Base` |
| Full profile | 已配置 | `ESP32BASE_PROFILE_FULL` |
| IR 依赖 | 已引入 | `crankyoldgit/IRremoteESP8266@^2.8.6` |
| 编译状态 | 已通过 | `pio run -e esp32dev` 通过 |
| 测试状态 | 已通过 | `pio test -e native` 通过，native 用例覆盖 FanDriver、FanController、FanWeb API/HTML chunk、应用路由注册、Config audit 启用和 BOOT 清 WiFi 时序 |
| 烧录状态 | 已通过 | 串口上传已验证；每次烧录前重新确认实际 ESP32 串口，上传速率固定为 115200 |
| Web OTA CLI | 已接入 | 使用 Esp32Base `scripts/esp32base_webota.py` 的 `webota` target |
| 启动状态 | 已通过 | AP 配网和 Web server ready 已完成首轮验证 |
| 业务代码迁移 | 已完成基础闭环 | Web OTA 基本上传已验证；仍需实机验证 PWM/RPM、真实风扇堵转/恢复和 OTA 异常路径 |
| WiFi 配网与 Web/API | 已完成首轮验证 | 用户已完成 AP 配网，设备在局域网可访问 |

## 3. 阶段计划

### 阶段 1：文档和最小环境

目标：

- 明确需求、硬件、架构、代码设计和建设路线。
- 建立 PlatformIO + Esp32Base Full profile 最小环境。
- 建立 Esp32Base Full profile 工程依赖闭环。

验收：

- 文档之间术语、路径、API、模块边界一致。
- `pio run -e esp32dev` 通过。
- 基础库反馈采用对话中直接输出提示词，当前无未处理阻塞。

### 阶段 2：业务模块 ESP32 化

目标：

- 清理从 ESP8266 项目迁移来的代码。
- 完成 ESP32 LEDC PWM、TACH 中断、LED PWM、BOOT 清 WiFi。
- 配置 key 全部符合 NVS 长度限制。
- 建立 native 测试替身并覆盖核心状态机。

验收：

- 应用固件可编译。
- 无 ESP8266 专用 API 残留。
- 风扇核心模块可在 native 测试中覆盖状态机。
- `pio test -e native` 通过。

### 阶段 3：Web/API 与基础库全模块验证

目标：

- 完成 `/fan`、`/config` 和 `/api/*`。
- 验证 Esp32Base Web、Auth、WiFi、DNS、NTP、mDNS、FileLog、OTA、Watchdog、Health。

验收：

- 局域网可访问 `/fan` 和 `/esp32base/*`。
- OTA 可上传。
- `/esp32base/logs` 可看到应用日志。

当前结果：

- AP 配网、局域网访问、`/esp32base/api/status`、业务 API、`/fan`、`/config`、`/esp32base/logs` 和 `/esp32base/ota` 已完成首轮实机验证；具体 IP、串口和 Auth 持久化值以当前设备实际状态为准。
- 已通过 `/esp32base/ota` 上传固件，重启后基础库状态、业务 API、`/fan` 和日志页恢复正常。
- 已接入并验证 `pio run -e esp32dev -t webota`。
- 已验证 WiFi power save 后 `/api/status` 仍可访问；具体测试参数以当次验证记录为准。
- 业务页使用 `Esp32BaseWeb::addPage(path, title, handler)` 注册，Esp32Base 首页和内置顶栏可展示 `Fan`、`Settings` 入口。
- Web Auth 已使用 Esp32Base 内置持久化能力，默认账号密码由应用提供，修改入口为 `/esp32base/auth`。
- Esp32Base Health tick、NTP 未同步降噪和 mDNS 首次解析延迟已完成首轮观察；mDNS 若多设备稳定复现明显延迟，再反馈 Esp32Base。

### 阶段 4：实机验收

目标：

- 使用真实 ESP32 和四线风扇验证硬件行为。
- 记录示波器/RPM/网页/API/OTA 异常路径/断电恢复结果。

验收：

- PWM 25 kHz。
- RPM 误差 <= 5%。
- 堵转保护可触发和恢复。
- WiFi power save 后 Web 仍可访问。

## 4. 当前剩余工作列表

| 优先级 | 工作 | 类型 | 阻塞 |
| --- | --- | --- | --- |
| P0 | 实机验证 TACH RPM 和堵转保护 | 硬件验证 | 需要四线风扇 |
| P1 | 记录 mDNS 解析延迟观察 | 基础库验证 | 若持续影响浏览器访问，再反馈基础库 |
| P2 | 根据实机结果微调 GPIO/页面/参数默认值 | 本项目 | 依赖实机反馈 |
| P2 | 编写实机验收记录 | 文档/测试 | 依赖硬件 |

## 5. 下一步计划

1. 在浏览器中人工检查 `/esp32base`、`/fan`、`/config`、`/esp32base/logs` 的页面交互体验。
2. 接入四线风扇验证 TACH RPM 和堵转保护。
3. 测试 BOOT 长按清 WiFi 凭证并重新进入配网。
4. 验证两键同时长按 5 秒出厂重置。
5. 根据实机结果更新文档、默认参数和 GitHub 仓库。
