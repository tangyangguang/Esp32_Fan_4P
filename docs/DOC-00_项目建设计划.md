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
- 基础能力使用 `/Users/tyg/dir/claude_dir/Esp32Base`，默认启用 Full profile。
- 项目文档先行，文档是后续实现和验收的基准。
- 涉及 Esp32Base 能力缺口或 bug 时，输出完整提示词到 `docs/ESP32BASE_PROMPTS.md`，不在本项目长期打补丁。
- 本项目按 Esp32Base 示例工程显式声明 Full profile 需要的 Arduino framework 库，避免 PlatformIO LDF 漏链。

## 2. 当前状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| 文档骨架 | 已完成 | README、DOC-00..04、Esp32Base 提示词 |
| PlatformIO 环境 | 已引入 | `esp32dev`、`espressif32@6.9.0`、Arduino |
| Esp32Base 引入 | 已引入 | `symlink:///Users/tyg/dir/claude_dir/Esp32Base` |
| Full profile | 已配置 | `ESP32BASE_PROFILE_FULL` |
| IR 依赖 | 已引入 | `crankyoldgit/IRremoteESP8266@^2.8.6` |
| 编译状态 | 已通过 | `pio run -e esp32dev` 通过 |
| 测试状态 | 已通过 | `pio test -e native`，7 个用例通过 |
| 烧录状态 | 已通过 | `/dev/cu.usbserial-130`，115200 上传成功；`/dev/cu.usbserial-120` 是 ESP8266 |
| 启动状态 | 已通过 | 进入 `ESP32-Config-65E4` 配网 AP，Web server ready |
| 业务代码迁移 | 已完成基础闭环 | 仍需实机验证 PWM/RPM/OTA |
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
- 基础库提示词文档存在且当前无未处理阻塞。

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

- AP 配网完成后，`esp32-fan.local` 解析到 `192.168.2.112`。
- `/esp32base/api/status` 返回 `profile=FULL`、`wifi.connected=true`。
- 直接 IP 访问 `/api/status`、`/fan`、`/config`、`/api/speed`、`/api/timer`、`/api/stop` 均成功。
- 直接 IP 访问 `/esp32base/logs` 和 `/esp32base/ota` 页面均返回 HTTP 200。
- 已通过 `/esp32base/ota` 上传同版本固件，上传返回 `{"ok":true}`，重启后 `/esp32base/api/status`、`/api/status`、`/fan`、`/esp32base/logs` 均恢复正常。
- 已临时设置 `sleep_wait=3` 验证 WiFi power save，进入 `sleep` 状态后 `/api/status` 仍可访问；验证后恢复 `sleep_wait=60`。
- 业务页使用 `Esp32BaseWeb::addPage(path, title, handler)` 注册，Esp32Base 首页和内置顶栏可展示 `Fan`、`Settings` 入口。
- Web Auth 已使用 Esp32Base 内置持久化能力，默认账号密码由应用提供，修改入口为 `/esp32base/auth`。
- 当前设备持久化 Auth 已通过 Esp32Base 内置页面改为 `admin/admin`，旧 `admin/admin123` 已返回 401。
- 新版 Esp32Base Health 已验证：历史日志仍有旧 `INFO health tick`，新固件启动后的 health tick 以 `DEBUG` 输出，默认 30 分钟最多一次。
- 新版 Esp32Base NTP 未同步状态已降噪，不再周期性输出 `ntp_sync_pending` WARN。
- `esp32-fan.local` 访问存在约 5 秒解析等待，直接 IP 访问无此等待；暂判断为客户端侧 mDNS 解析延迟。

### 阶段 4：实机验收

目标：

- 使用真实 ESP32 和四线风扇验证硬件行为。
- 记录示波器/RPM/网页/API/OTA/断电恢复结果。

验收：

- PWM 25 kHz。
- RPM 误差 <= 5%。
- 堵转保护可触发和恢复。
- WiFi power save 后 Web 仍可访问。

## 4. 当前剩余工作列表

| 优先级 | 工作 | 类型 | 阻塞 |
| --- | --- | --- | --- |
| P0 | 实机验证 PWM 25 kHz 和占空比 | 硬件验证 | 需要 ESP32 和示波器 |
| P0 | 实机验证 TACH RPM 和堵转保护 | 硬件验证 | 需要四线风扇 |
| P1 | 记录 mDNS 解析延迟观察 | 基础库验证 | 若持续影响浏览器访问，再反馈基础库 |
| P2 | 根据实机结果微调 GPIO/页面/参数默认值 | 本项目 | 依赖实机反馈 |
| P2 | 编写实机验收记录 | 文档/测试 | 依赖硬件 |

## 5. 下一步计划

1. 在浏览器中人工检查 `/esp32base`、`/fan`、`/config`、`/esp32base/logs` 的页面交互体验。
2. 用示波器验证 GPIO25 PWM 频率和占空比。
3. 接入四线风扇验证 TACH RPM、堵转保护和软启动/停止。
4. 测试 BOOT 长按清 WiFi 凭证并重新进入配网。
5. 根据实机结果更新文档、默认参数和 GitHub 仓库。
