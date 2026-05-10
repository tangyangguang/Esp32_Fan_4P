# Esp32Base 待完善提示词

本项目用于验证 `Esp32Base` 全模块能力。凡是后续发现属于基础库职责的能力缺口或 bug，统一记录到本文档，并由基础库完善；本项目不长期保留业务侧绕补丁。

## 当前状态

当前有 2 个基础库边界问题需要在 Esp32Base 项目中确认/完善：

### 出厂重置 namespace 清理范围可声明

背景：本项目两键长按执行完整出厂重置，会清理 `fan` namespace，并调用 `Esp32BaseConfig::clearLibraryNamespaces()` 清理基础库配置。

目标：Esp32Base 提供可枚举、可声明的基础库 namespace 清理范围，至少明确 WiFi、Web Auth、系统、日志配置分别对应的 namespace。

现状问题：业务项目只能依赖文档和源码得知 `clearLibraryNamespaces()` 当前清理 `eb_wifi`、`eb_sys`、`eb_log`、`eb_web`，缺少 dry-run 或清单 API，不利于保护业务持久化资产。

影响范围：所有调用完整出厂重置的业务项目，以及升级/备份/恢复前的影响面说明。

提示词：为 Esp32Base 提供可枚举/可选择的出厂重置 API，明确 WiFi、Web Auth、系统、日志等 namespace 的清理范围，并提供 dry-run 或文档化清单，方便业务项目保护用户持久化资产。

### Config 审计 pre-begin 语义文档化

背景：本项目要求 `Esp32BaseConfig::enableConfigReadAudit(true)` 覆盖 `Esp32Base::begin()` 内部配置读取。

目标：Esp32Base 明确支持并记录 Config audit/read audit 在 `Esp32Base::begin()` 前开启的行为。

现状问题：当前 API 可在 pre-begin 调用，但文档未明确推荐时机，业务项目容易把审计开关放到 begin 之后，漏掉基础库启动读取日志。

影响范围：现场调试、配置读取审计、安全观察和问题复盘。

提示词：请在 Esp32Base 文档中明确 `enableConfigAudit()` / `enableConfigReadAudit()` 可在 `Esp32Base::begin()` 前调用，并说明若需要覆盖基础库初始化读取，必须在 begin 前开启；补充一个最小示例。

实机联网验证中观察到 `esp32-fan.local` 在 macOS curl 下首次解析约等待 5 秒，但直接 IP 访问响应正常。当前判断更像客户端 mDNS 解析表现，不作为基础库 bug 记录；若浏览器和多设备访问均稳定复现，再按模板补充提示词。

## 已完成基础库反馈记录

以下为已完成项的简记；新增未处理提示词必须按文末模板填写。

- Esp32Base Web 已支持业务入口最终 API：`Esp32BaseWeb::addPage(path, title, handler)`。本项目使用 `/fan -> Fan`、`/config -> Settings` 显式注册业务入口。
- Esp32Base Web 已支持内置持久化 Auth、`/esp32base/auth`、业务优先首页和系统导航配置；持久化字段为明文 `eb_web.auth_user` / `eb_web.auth_pass`。本项目已删除应用侧 `fan/web_pass`，改为设置默认 `admin/admin` 并使用基础库 Auth 页面修改凭据。
- Esp32Base Health 已调整：普通 tick 使用 DEBUG，默认 30 分钟最多一次；`ESP32BASE_HEALTH_LOOP_WARN_MS` 默认 3000ms，超过阈值才输出 WARN `health loop_slow ...`。
- Esp32Base NTP 已降噪：未同步状态不再周期性输出 `ntp_sync_pending` WARN。
- Esp32Base WiFi 与 Web Auth 已在 INFO 日志输出明文凭据，这是现场调试设计，不作为问题记录。

## 设计记录：Config 读取审计输出明文值是调试设计

`Esp32BaseConfig::enableConfigReadAudit(true)` 会让 `getStr()` 日志输出配置明文值，包括 `eb_wifi/pass` 等字段；Esp32Base 的 WiFi 和 Web Auth INFO 日志也会输出明文凭据。这是为了成熟项目调试和现场观察保留的设计行为，不是 bug，本项目不得关闭读取审计，也不得要求基础库默认脱敏。

本项目要求：

- 保持 `Esp32BaseConfig::enableConfigReadAudit(true)`。
- `/esp32base/logs` 中出现 WiFi 密码、Web 密码等配置明文属于预期行为。
- 不再记录“Config 审计日志需要敏感值脱敏”之类提示词。
- 如果后续需要脱敏能力，也只能作为可选调试模式讨论，不能改变当前默认调试可观测性。

此前 Full profile 编译时遇到的 framework 组件 include/link 问题，已按 Esp32Base 示例工程方式在本项目中显式声明依赖并增加 LDF 锚定文件解决：

- `platformio.ini` 显式列出 `WiFi`、`DNSServer`、`ESPmDNS`、`LittleFS`、`WebServer`、`Update`。
- `platformio.ini` 使用 `${platformio.packages_dir}` 引入 framework 内置库头文件路径。
- `src/deps_esp32base_full.cpp` include Full profile 需要的 framework 头文件，确保 PlatformIO LDF 链接对应库。

验证结果：

```bash
pio run -e esp32dev
pio test -e native
```

上述命令均已通过。

## 后续记录模板

```text
标题：

背景：

复现步骤：

当前行为：

期望行为：

影响范围：

建议方向：

验收标准：
```
