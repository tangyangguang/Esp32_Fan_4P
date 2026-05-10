本文件记录本项目已经形成的原则性规范。后续任何 AI 助手、自动化脚本或协作者进入本仓库时，应优先遵守这些约定。

## 1. 项目定位
- 本项目是 `ESP12F_Fan_4P` 的 ESP32 版本，业务功能要对齐成熟 ESP12F_Fan_4P 项目。
- 本项目必须使用 `/Users/tyg/dir/claude_dir/Esp32Base` 提供基础能力，并尽量覆盖 Esp32Base Full profile 的所有模块。
- 本项目同时承担 Esp32Base 真实业务验证职责，但不承担修补基础库的职责。

## 2. 基础库边界
- 凡是属于 Esp32Base 的能力缺口、API 不合理、默认行为问题或 bug，不要在本项目长期打补丁。
- 遇到基础库问题时，只在 `docs/ESP32BASE_PROMPTS.md` 写清楚提示词，交由 Esp32Base 仓库完善。
- 本项目只能做必要的业务侧适配，不应复制、绕开或重写 Esp32Base 的基础能力。
- 如果 Esp32Base 已完成对应能力，要删除本项目临时入口或绕补丁，改回正式 API。

## 3. 工程决策原则
- 不管是新需求、bug 修复还是优化，都坚决不要背历史包袱。
- 坚决不做打补丁、临时方案、过渡方案、绕路方案。
- 不因为影响代码多、改动范围大、方案需要重构，就选择次优做法。
- 每次实现都只做最优方案，只采用最佳实践，并让代码和文档一起回到清晰、长期可维护的状态。
- 如果当前基础能力不支持最佳方案，应反馈到 Esp32Base 或相关基础设施中完善，而不是在本项目局部绕补丁。

## 4. 已确认的 Esp32Base 设计行为
- `Esp32BaseConfig::enableConfigReadAudit(true)` 必须保持开启。
- `/esp32base/logs` 中出现配置读取明文值，包括 WiFi 密码、Web 密码等，是调试和现场观察设计，不是 bug。
- 不要提出“配置审计默认脱敏”的基础库提示词，也不要在本项目关闭读取审计。
- Esp32Base Web 业务入口最终 API 为 `Esp32BaseWeb::addPage(path, title, handler)`；本项目显式注册 `/fan -> Fan`、`/config -> Settings`。
- 不保留旧 `addPage(path, handler)` 写法。
- 不在本项目保留临时 `/app` 应用入口页；业务入口由 Esp32Base 首页和内置顶栏展示。
- Web Auth 使用 Esp32Base 内置持久化能力，本项目只设置默认 `Esp32BaseWeb::setDefaultAuth("admin", "admin")`。
- 本项目不保存 `fan/web_pass`，不维护应用侧密码修改逻辑；账号密码修改统一使用 `/esp32base/auth`。
- Web Auth 持久化使用 `eb_web.auth_user` / `eb_web.auth_pass` 明文；INFO 日志输出 WiFi 和 Web Auth 明文凭据是调试设计。
- Esp32Base Health 普通 tick 日志应是 DEBUG，默认 30 分钟最多一次；超过 `ESP32BASE_HEALTH_LOOP_WARN_MS` 才 WARN。
- NTP 未同步状态不应周期性输出 `ntp_sync_pending` WARN。
- 日志中历史残留的旧 INFO health tick 不代表新固件仍有问题，要区分历史段和新启动后的日志。

## 5. Web 和 API 规范
- `/fan` 是风扇主控制页。
- `/config` 是风扇参数和红外学习配置页。
- `/esp32base/*` 是基础库管理页，包括 WiFi、OTA、Logs、Auth、Reboot 等。
- 业务页必须调用 `Esp32BaseWeb::checkAuth()`。
- 业务页只能使用 Esp32BaseWeb 公开 API，不直接依赖底层 `WebServer`。
- 业务页不主动放置 `/esp32base/auth` 修改密码入口；Auth 入口由 Esp32Base 系统导航提供。
- 业务页不自建业务入口或 Esp32Base 系统页面导航；业务入口由基础库顶部导航提供，系统入口由基础库页脚导航提供。
- Clock 显示应保持完整日期时间，API 返回格式为 `YYYY-MM-DD HH:MM:SS`。
- 堵转状态合并到 State 显示；正常显示 Idle/Running/Sleep/Error，堵转时显示 Blocked。

## 6. 日志判断规范
- `config audit op=getStr ... value=...` 是预期调试日志。
- `health tick loopMax=...` 是健康心跳/loop 最大间隔观察，不是错误。
- `health loop_slow ...` 才代表 loop 最大间隔超过阈值，需要关注。
- `/esp32base/logs` 里出现旧历史日志时，要先判断所属启动段，不要把旧固件行为误判为当前问题。
- `web slow_request` 对 `/esp32base/logs` 这类大页面可能正常；持续高延迟才需要进一步分析。
- 未接风扇时设置非零速度会触发堵转保护，这是合理行为，不要误判为软件 bug。

## 7. 硬件和实机验证
- ESP32 曾验证连接口为 `/dev/cu.usbserial-130`；每次烧录前必须以 `ls /dev/cu.*` 重新确认实际串口。
- `/dev/cu.usbserial-120` 当前探测为 ESP8266，不能用于本 ESP32 项目烧录。
- 当前设备局域网 IP 曾验证为 `192.168.2.112`，但后续应以实际状态为准。
- `esp32-fan.local` 可用但 macOS 下首次解析可能较慢；直接 IP 访问更适合自动化验证。
- 上传速率固定为 `115200`，此前 `921600` 在该串口下不稳定。
- Web 默认账号为 `admin`，密码来自 NVS；默认值为 `admin`，但测试时应根据当前设备实际密码验证。
- OTA 验证优先使用 `/esp32base/ota`，上传 `.pio/build/esp32dev/firmware.bin`。
- BOOT/GPIO0 长按 1 秒用于清 WiFi；这一步需要人工按键，不应通过软件臆测完成。
- PWM 25 kHz 和占空比必须用示波器或逻辑分析仪验证。
- TACH RPM、堵转保护、软启动/停止必须接真实四线风扇验证。

## 8. 构建、测试和交付
- 修改代码后至少运行 `pio run -e esp32dev` 和 `pio test -e native`。
- 涉及实机行为时，构建通过后应 OTA 或串口烧录到 ESP32 并验证。
- 修改 Web 页面后，要用浏览器或 curl 实际访问对应页面和 API。
- 修改文档或提示词后，要保持 README、DOC-00、DOC-04 和 `docs/ESP32BASE_PROMPTS.md` 语义一致。
- 需要上传 GitHub 时，提交后推送到 `https://github.com/tangyangguang/Esp32_Fan_4P.git`。

## 9. 回复用户的固定要求
- 每次回复都要包含“剩余工作列表”和“详细下一步计划”。
- 如果涉及基础库问题，先说明边界，再给出简洁、清楚的提示词；不要在本项目打补丁。
- 如果用户要求“提示词简洁”，只说明问题，不给实现方案。
- 如果做了错误判断，要直接承认并撤回对应改动。

## 10. 当前剩余硬件工作
- 用示波器验证 GPIO25 PWM 25 kHz 和占空比。
- 接四线风扇验证 TACH RPM、堵转保护、软启动和软停止。
- 人工长按 BOOT 验证清 WiFi 并重新配网。
- 继续观察 mDNS 首次解析延迟，如多设备稳定复现再反馈 Esp32Base。
