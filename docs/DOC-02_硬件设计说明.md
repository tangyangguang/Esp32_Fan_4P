# DOC-02 硬件设计说明

| 字段 | 内容 |
| --- | --- |
| 文档编号 | DOC-02 |
| 项目名称 | ESP32 壁炉烟囱正压送风控制器 |
| 版本 | v0.1 |
| 日期 | 2026-05-05 |
| 状态 | 建设中 |

## 1. 目标硬件

| 项目 | 规格 |
| --- | --- |
| MCU | ESP32 DevKit / ESP32-WROOM-32 类模块 |
| Framework | Arduino ESP32 Core |
| Flash | 4 MB 起，建议 OTA 分区 |
| 风扇 | 12 V 四线 PWM 风扇 |
| 供电 | 外置 12 V DC，需提供 ESP32 稳压支路 |
| 控制方式 | Web、本地按键、红外遥控 |

## 2. 初始 GPIO 分配

| 功能 | GPIO | 方向 | 电气要求 | 说明 |
| --- | --- | --- | --- | --- |
| 风扇 PWM | GPIO18 | 输出 | 3.3 V 逻辑，建议开漏/电平适配按风扇规格确认 | LEDC，25 kHz |
| 风扇 TACH | GPIO19 | 输入 | 上拉输入，注意 TACH 多为开集电极 | FALLING 中断计数 |
| 加速按键 | GPIO32 | 输入 | `INPUT_PULLUP`，按下接地 | RTC GPIO，输入稳定 |
| 减速按键 | GPIO33 | 输入 | `INPUT_PULLUP`，按下接地 | RTC GPIO，输入稳定 |
| 板载 LED | GPIO2 | 输出 | 依开发板而定；GPIO2 是 strapping pin，外接电路不得在启动时强拉低 | 默认 active-low，可配置 |
| 红外接收 | GPIO27 | 输入 | 1838 输出接 GPIO，3.3 V 供电 | IRremoteESP8266 |
| BOOT | GPIO0 | 输入 | 开发板 BOOT 键 | 长按 5 秒清 WiFi |

## 3. GPIO 选择原则

- 避免把风扇 PWM、TACH、红外等关键运行信号放在 ESP32 启动绑定位上。
- GPIO0 仅复用为人工 BOOT/清 WiFi，不接外部长线。
- GPIO2 仅按常见 DevKit 板载 LED 使用；若目标模组启动要求不同，应更换 LED GPIO 或调整外接电路，避免影响下载/启动。
- GPIO34-39 仅输入且无内部上拉，不作为默认按键。
- 实际硬件定版前必须按目标 ESP32 模组数据手册复核 strapping pin、ADC2/WiFi 冲突和外设复用。

## 4. 风扇接口

典型四线风扇接口：

| 引脚 | 说明 |
| --- | --- |
| GND | 风扇地，与 ESP32 地共地 |
| +12V | 风扇电源 |
| TACH | 转速反馈，通常开集电极输出 |
| PWM | 调速输入，目标 25 kHz |

注意事项：

- TACH 需要上拉到 3.3 V，不能直接上拉到 12 V。
- PWM 输入规格需按风扇数据手册确认；若要求 5 V/open-drain，应增加三极管/MOS 管适配。
- 未接风扇时，非零输出会触发堵转保护，这是正常保护行为。
- Arduino ESP32 Core 2.x 路径下 Fan PWM 使用 LEDC channel 0，LED 指示使用 channel 2；避免使用 channel 1，防止与 channel 0 共享 timer 后把风扇 25 kHz 覆盖为 LED 的 1 kHz。Esp32Base 后续如新增 LEDC 资源，应避免复用这些 channel 或先提供统一分配能力。

## 5. 电源设计

- 风扇使用 12 V 电源，电流按最大风量留足余量。
- ESP32 使用独立 3.3 V 稳压，建议从 12 V 经 DC-DC 降压再 LDO 或高质量 DC-DC。
- 风扇电源和 ESP32 电源共地。
- 需要为风扇启停、电源线较长和户外环境预留滤波、浪涌和反接保护。

## 6. 指示灯

默认使用 GPIO2 板载 LED：

| 状态 | 行为 |
| --- | --- |
| 0 档 | 熄灭 |
| 1-4 档 | 25%/50%/75%/100% 亮度 |
| WiFi 未连接 | 慢闪，代码已实现，需实机观察闪烁效果和 active-low 方向 |
| 故障 | 快闪 |
| 用户操作 | 闪一下 |

## 7. 后续实机验证

- PWM 频率：旧 GPIO25 接线已用示波器确认 25 kHz；改为 GPIO18 后需复测 GPIO18。
- PWM 占空比：旧 GPIO25 接线下 0/25/50/75/100% 已用示波器校验；改为 GPIO18 后需复测。Rigol DS1202 自动测量需设置合适测量时长，避免 75% 被短窗口误判。
- 软启动/软停止：已用示波器验证通过。
- TACH RPM：与外部转速计对照，误差 <= 5%。
- WiFi power save：确认进入省电后 Web 仍可访问。
- OTA：上传、失败回滚和日志观察。
- BOOT 清 WiFi：长按 5 秒清凭证并进入配网。
