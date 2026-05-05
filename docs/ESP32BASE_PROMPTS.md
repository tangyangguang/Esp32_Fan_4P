# Esp32Base 待完善提示词

本项目用于验证 `Esp32Base` 全模块能力。凡是后续发现属于基础库职责的能力缺口或 bug，统一记录到本文档，并由基础库完善；本项目不长期保留业务侧绕补丁。

## 当前状态

当前没有新的未处理基础库提示词。

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
