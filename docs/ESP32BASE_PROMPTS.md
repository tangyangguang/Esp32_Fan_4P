# Esp32Base 待完善提示词

本项目用于验证 `Esp32Base` 全模块能力。凡是后续发现属于基础库职责的能力缺口或 bug，统一记录到本文档，并由基础库完善；本项目不长期保留业务侧绕补丁。

## 当前状态

当前没有新的未处理基础库提示词。

实机联网验证中观察到 `esp32-fan.local` 在 macOS curl 下首次解析约等待 5 秒，但直接 IP 访问响应正常。当前判断更像客户端 mDNS 解析表现，不作为基础库 bug 记录；若浏览器和多设备访问均稳定复现，再按模板补充提示词。

## 提示词：Web 内置首页和顶栏需要支持应用入口

标题：Esp32Base Web 增加应用入口/首页扩展能力

背景：

`Esp32_Fan_4P` 通过 `Esp32BaseWeb::addPage("/fan", ...)`、`addPage("/config", ...)` 注册业务页面。当前 Esp32Base 内置 `/` 会重定向到 `/esp32base`，内置顶栏只有 Home、WiFi、OTA、Logs、Reboot，内置首页也只展示基础库状态和基础功能入口。用户从首页无法发现业务页面 `/fan`，只能手输 URL。业务页可以链接回 Esp32Base 页面，但 Esp32Base 页面无法链接回业务页，导航闭环不完整。

复现步骤：

1. 应用在 `Esp32Base::begin()` 前注册 `/fan` 和 `/config`。
2. 浏览器访问设备根路径 `/`，被重定向到 `/esp32base`。
3. 查看 `/esp32base`、`/esp32base/wifi`、`/esp32base/logs`、`/esp32base/ota` 顶栏。
4. 顶栏和首页没有 `/fan` 或应用首页入口。

当前行为：

Esp32Base 内置页面只知道基础库自己的页面，无法展示应用注册的主入口，也没有公开 API 让应用设置“应用名称、应用首页、应用导航项、根路径策略”。

期望行为：

Esp32BaseWeb 提供应用入口扩展 API，例如：

```cpp
Esp32BaseWeb::setAppHome("Fan", "/fan");
Esp32BaseWeb::addNavLink("Fan", "/fan");
Esp32BaseWeb::addNavLink("Settings", "/config");
Esp32BaseWeb::setRootRedirect("/fan");  // 可选，允许应用决定 / 的默认落点
```

内置 `sendHeader()` 顶栏和 `/esp32base` 首页应显示应用入口；`/` 默认仍可保持基础库行为，但应用应能显式改成跳转到业务首页。

影响范围：

- 所有基于 Esp32BaseWeb 注册业务页面的项目。
- 影响用户从设备首页发现业务功能的能力。
- 不影响现有基础库 API；可以作为向后兼容的可选能力。

建议方向：

- 在 Esp32BaseWeb 内维护最多若干个应用导航项，限制 label/path 长度。
- `sendHeader()` 在基础库链接之前或之后追加应用入口。
- `/esp32base` 首页增加 “Application” 区块。
- 提供根路径重定向配置，未配置时保持当前 `/ -> /esp32base`。

验收标准：

1. 应用调用 `setAppHome("Fan", "/fan")` 后，`/esp32base` 首页出现 Fan 入口。
2. 应用调用 `addNavLink("Settings", "/config")` 后，所有 Esp32Base 内置页面顶栏出现 Settings 入口。
3. 应用调用 `setRootRedirect("/fan")` 后，访问 `/` 302 到 `/fan`。
4. 未调用这些 API 的老项目行为不变。

## 设计记录：Config 读取审计输出明文值是调试设计

`Esp32BaseConfig::enableConfigReadAudit(true)` 会让 `getStr()` 日志输出配置明文值，包括 `eb_wifi/pass`、`fan/web_pass` 等字段。这是为了成熟项目调试和现场观察保留的设计行为，不是 bug，本项目不得关闭读取审计，也不得要求基础库默认脱敏。

本项目要求：

- 保持 `Esp32BaseConfig::enableConfigReadAudit(true)`。
- `/esp32base/logs` 中出现 WiFi 密码、Web 密码等配置明文属于预期行为。
- 不再记录“Config 审计日志需要敏感值脱敏”之类提示词。
- 如果后续需要脱敏能力，也只能作为可选调试模式讨论，不能改变当前默认调试可观测性。

## 提示词：Health tick 日志级别和频率需要可配置

标题：Esp32BaseHealth 周期 tick 不应以 INFO 高频写入文件日志

背景：

`Esp32_Fan_4P` 的 `/esp32base/logs` 中出现大量类似日志：

```text
[240121] INFO  health       tick loopMax=210 ms
[270121] INFO  health       tick loopMax=210 ms
```

这是 `Esp32BaseHealth::handle()` 每 `ESP32BASE_HEALTH_TICK_INTERVAL_MS` 输出一次主循环最大间隔。当前默认 30000 ms，运行数小时后会产生大量 INFO 日志，占用 64 KB 文件日志空间，挤掉更有价值的业务、错误、OTA、WiFi 日志。

复现步骤：

1. 启用 Full profile 和 FileLog。
2. 设备空闲运行数小时。
3. 查看 `/esp32base/logs`。

当前行为：

health tick 固定以 INFO 级别输出，且 `loopMax` 是启动后累计最大值，正常情况下也会重复输出相同数值。

期望行为：

- 默认不把周期 health tick 写入 INFO 文件日志，或改为 DEBUG。
- 支持应用配置 tick 日志级别、周期、是否只在 loopMax 变化/超过阈值时输出。
- Web/API 可查询 health 指标，日志只记录异常或阈值变化。

影响范围：

- Full profile 长时间运行项目。
- 文件日志容量较小的 ESP32 设备。

建议方向：

- 增加编译宏或运行时 API：

```cpp
Esp32BaseHealth::setTickLogEnabled(false);
Esp32BaseHealth::setTickLogLevel(Esp32BaseLog::DEBUG);
Esp32BaseHealth::setLoopMaxWarnThresholdMs(1000);
```

- 默认策略：周期指标走 API，超过阈值才 WARN。

验收标准：

1. 默认运行 1 小时不会产生大量重复 `health tick` INFO。
2. `loopMax` 超过阈值时仍有 WARN。
3. `/esp32base/api/status` 或 health API 能查看 loopMax。
4. 老项目可通过开关恢复当前周期日志行为。

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
