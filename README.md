# Esp32_Fan_4P

`Esp32_Fan_4P` 是基于 ESP32 的四线 PWM 风扇控制器项目，用于壁炉烟囱正压送风场景。

本项目的业务功能对齐成熟项目 `/Users/tyg/dir/claude_dir/ESP12F_Fan_4P`，但硬件平台改为 ESP32，并使用 `/Users/tyg/dir/claude_dir/Esp32Base` 提供基础能力。项目建设过程中同时验证 Esp32Base 的完整模块能力；凡是属于基础库能力缺口或 bug 的问题，直接在协作回复中按功能输出提示词，不在本项目业务代码中长期绕补丁，也不维护独立提示词文件。

## 当前阶段

当前阶段已经完成文档、最小工程环境、核心代码迁移和首轮 ESP32 实机网络验证：

- PlatformIO ESP32 工程环境。
- Esp32Base Full profile 引入。
- IRremoteESP8266 依赖引入。
- ESP32 版需求、硬件、架构和代码设计文档。
- Esp32Base Full profile framework 依赖锚定。
- 核心 native 单元测试。
- 串口烧录已验证；每次烧录前以 `ls /dev/cu.*` 重新确认实际 ESP32 串口。
- 已通过 Esp32Base AP 配网并连接局域网。
- 已验证 `/fan`、`/config`、业务 API 和 `/esp32base/api/status` 可访问；`/ir` 为红外学习独立入口。

## 目标功能

- 0-100% PWM 调速，目标 PWM 频率 25 kHz。
- 四线风扇 TACH 转速反馈和堵转保护。
- 软启动、软停止。
- 本地加速/减速按键；两键同时长按 5 秒执行完整出厂重置，清除 `fan` 业务配置以及 Esp32Base 的 `eb_wifi`、`eb_sys`、`eb_log`、`eb_web` 配置。
- 红外遥控学习和控制。
- 定时运行、断电恢复和累计运行时长统计。
- 速度、目标速度和 RPM 的 RAM 实时曲线；页面刷新保留，设备重启清空，不写 Flash。
- Web 控制页、配置页、REST API 和日志页。
- WiFi 配网、mDNS、NTP、OTA、文件日志、看门狗、低功耗策略。
- BOOT 键长按清除 WiFi 凭证并重启进入配网流程。

## 基础库使用原则

本项目要求使用 Esp32Base 的所有模块能力，工程默认使用：

```ini
-DESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL
```

对应能力包括：

- Core：日志、配置、系统信息。
- Runtime：事件总线、看门狗、Sleep、FS、文件日志、健康诊断。
- Network：WiFi、DNS captive portal、NTP、mDNS。
- Web：基础管理页面、鉴权、应用自定义路由、日志页面。
- OTA：Web OTA。

## 初始引脚规划

| 功能 | ESP32 GPIO | 说明 |
| --- | --- | --- |
| 风扇 PWM | GPIO18 | LEDC PWM 输出 |
| 风扇 TACH | GPIO19 | 中断输入，`INPUT_PULLUP` |
| 加速按键 | GPIO32 | `INPUT_PULLUP` |
| 减速按键 | GPIO33 | `INPUT_PULLUP` |
| 板载 LED | GPIO2 | 常见 DevKit 板载 LED，默认 active-low |
| 红外接收 | GPIO27 | 1838 红外接收头 |
| BOOT / 清 WiFi | GPIO0 | 长按 5 秒清除 WiFi 凭证 |

实际硬件打板前应以目标 ESP32 模组和开发板原理图复核 GPIO 启动绑定位、输入输出能力和外设冲突。

## 构建命令

```bash
pio run -e esp32dev
pio run -e esp32dev -t upload
pio run -e esp32dev -t webota
pio device monitor -e esp32dev
```

工程已把 `upload_speed` 固定为 `115200`。烧录前需要用 `ls /dev/cu.*` 确认当前 ESP32 串口。
`webota` target 来自相邻目录 `../Esp32Base/scripts/esp32base_webota.py`。仓库通过 gitignored 的 `platformio.local.ini` 加载本机目标配置，因此当前工作机可直接使用 `pio run -e esp32dev -t webota`；设备 IP 或 Web Auth 变化时只需更新本地 `[esp32base_webota]` 私有段。也可用环境变量 `ESP32BASE_WEBOTA_HOST`、`ESP32BASE_WEBOTA_USER`、`ESP32BASE_WEBOTA_PASSWORD` 临时覆盖目标。

当前构建状态摘要如下，具体设备 IP、串口、Auth 持久化值和网络表现以实测设备当前 NVS/网络状态为准，详细验证记录以 [DOC-00 项目建设计划](/Users/tyg/dir/claude_dir/Esp32_Fan_4P/docs/DOC-00_项目建设计划.md) 为准：

- `pio run -e esp32dev` 通过。
- `pio test -e native` 通过。
- 串口上传已验证；每次烧录前应重新确认实际串口，上传速率固定为 115200。
- `pio run -e esp32dev -t webota` 通过。
- AP 配网、`/esp32base/api/status`、`/api/status`、`/fan`、`/config`、`/api/speed`、`/api/timer`、`/api/stop`、`/esp32base/logs` 和 `/esp32base/ota` 已完成首轮实机请求验证；`/ir` 页面需随下一轮 Web 验证确认。
- 已通过 `/esp32base/ota` 上传固件，OTA 后基础库状态、业务 API、`/fan` 和日志页恢复正常。
- 已验证 WiFi power save 后 `/api/status` 仍可访问；具体 `sleep_wait` 测试值以验证记录为准。
- 业务页使用 `Esp32BaseWeb::addPage(path, title, handler)` 注册，Esp32Base 首页和内置顶栏可展示 `Fan`、`History`、`Settings`、`IR` 入口。
- Web Auth 已迁移到 Esp32Base 内置持久化能力；本项目设置默认 `admin/admin`，账号密码修改入口为 `/esp32base/auth`。
- Esp32Base Health tick、NTP 未同步降噪和 mDNS 首次解析延迟已完成首轮观察；mDNS 若多设备稳定复现明显延迟，再反馈 Esp32Base。

`platformio.ini` 显式列出 Esp32Base Full profile 使用到的 Arduino framework 库，并通过 `src/deps_esp32base_full.cpp` 锚定 LDF 链接依赖；这与 Esp32Base 示例工程保持一致。

## 文档

- [DOC-00 项目建设计划](/Users/tyg/dir/claude_dir/Esp32_Fan_4P/docs/DOC-00_项目建设计划.md)
- [DOC-01 需求规格说明](/Users/tyg/dir/claude_dir/Esp32_Fan_4P/docs/DOC-01_需求规格说明.md)
- [DOC-02 硬件设计说明](/Users/tyg/dir/claude_dir/Esp32_Fan_4P/docs/DOC-02_硬件设计说明.md)
- [DOC-03 软件架构设计](/Users/tyg/dir/claude_dir/Esp32_Fan_4P/docs/DOC-03_软件架构设计.md)
- [DOC-04 代码设计说明](/Users/tyg/dir/claude_dir/Esp32_Fan_4P/docs/DOC-04_代码设计说明.md)
- [Run Duration 64-bit 决策记录](/Users/tyg/dir/claude_dir/Esp32_Fan_4P/docs/RUN_DURATION_DECISION.md)
