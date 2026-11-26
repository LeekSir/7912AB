# 7912AB 故事机移植记录

日期：2026-11-23
分支：`debug/7912ab-power-uart-test`

## 一、硬件与已验证事实

| 项 | 值 |
|---|---|
| 芯片 | 7912AB（AC791N 系列） |
| Flash / SDRAM | 8MB / 8MB |
| 调试串口 | UART0，TX=PB1，RX=PB2，115200 8N1 |
| 电源键 | PB6 / KEY_ADC_IN_IC，ADC 通道 `AD_CH_PB06` |
| 电源键 ADC | 松开 ~1022，按下 ~72；阈值 300 |
| 电源自锁 | PB7 / PW_EN，高电平保持供电，低电平断电 |

摄像头参考板已确认的外设：

- LCD ILI9481 + EMI 接口
- SD0 D 组
- audio DAC + mic
- camera / IIC（故事机不需要）

已验证固件：

- `demo_hello` 最小电源/串口测试：UART0 + PB6 ADC + PB7 自锁，运行稳定
- `wifi_camera` 7912AB + DEVELOP 参考固件：LCD/EMI、SD、audio DAC、camera 均可启动

## 二、两套 demo 的差异

| 项 | wifi_camera | wifi_story_machine |
|---|---|---|
| 定位 | 摄像头采集/图传/录像/LCD 预览 | WiFi/BT 音频、语音、UI 故事机 |
| CONFIG_BT_ENABLE | 未定义 | 1（经典 BT + BLE） |
| CONFIG_AUDIO_ENABLE | 1 | 1 |
| CONFIG_VIDEO_ENABLE | 1 | Makefile 定义，DUI 下 `#undef` |
| CONFIG_UI_ENABLE | 1（LCD UI） | 当前构建未定义 |
| app.bin（7912AB） | ~0.99MB | ~2.9MB |
| fw（7912AB） | ~3.52MB | ~4.58MB |
| 板级文件 | board_develop.c（LCD/EMI/camera/audio） | board_7911B.c（SD/keys/audio，无 LCD/EMI） |

产品需求：WiFi、豆包 AI、经典蓝牙音箱、UI 设置、喇叭 + mic。
结论：故事机是正确底座；摄像头 demo 作为硬件 bring-up 参考。

## 三、阶段计划

- 阶段 1：故事机板级 bring-up（本阶段）
  - 只保留 UART0/PB1/PB2、PB6 ADC、PB7 PW_EN 和心跳
  - 不启动 app_music / BT / WiFi / UI / audio / camera
- 阶段 2：audio DAC / PA / mic，喇叭和 mic 验证
- 阶段 3：LCD UI / 设置界面（进行中：JD9853 屏已点亮，DUI UI 演示已跑通，SPT5113C 触摸已接入；产品 UI 待做，见 §八）
- 阶段 4：WiFi + 经典蓝牙音箱（A2DP 接收）
- 阶段 5：豆包 AI 接入（WiFi + ASR/LLM/TTS）

## 四、阶段 1 实现

开关：`apps/wifi_story_machine/include/app_config.h`

```c
#define CONFIG_STORY_BRINGUP 1
```

改动：

1. `app_main.c`
   - `CONFIG_STORY_BRINGUP` 下不启动 `app_music`/BT/WiFi
   - 创建心跳任务，每秒打印 `STORY_BRINGUP_HB KEY_ADC=xxx`
2. `board/wl82/board_7911B.c`
   - 增加 PB6 ADC 按键、PB7 PW_EN 自锁、3s 长按/1s 短按逻辑
   - `board_power_hold_init()` 在 `setup_arch` 最早拉高 PB7
   - bring-up 下跳过 audio DAC、key_driver、BT cfg 初始化
   - 3s 判定在 `board_init`（UART/ADC 初始化之后），期间清 WDT
3. `cpu/wl82/setup.c` / `cpu/wl82/debug.c`
   - 共享的 `board_power_hold_init` 弱钩子
   - `DEBUG_UART = JL_UART0`

### 1.1 修订（2026-11-23）

首次 Stage 1 实测仍会启动 `audio_server`、`wireless_net_init`、camera sensor 探测，原因是这些 feature 不走 `app_main`，而是任务表/late_initcall 启动的。已补充隔离：

- `app_main.c` task_info_table：bring-up 下移除 audio/UI 任务
- late_initcall 隔离：`wireless_net_init`、`camera_to_lcd_init`、`ui_demo_task_init`、`gr202_led_ui_init`、`http_post_test_init`、`wl80_test_init`、`lte_net_init`
- 3s 判定原来用 `delay(10)`，实测不生效（hold 后 5ms 就打印 3s OK），改为 `mdelay(10)`
- 构建产物统一留在 `cpu/wl82/tools/`，不往仓库根目录拷贝

### 1.2 修订（2026-11-23）

- 3s 未满松手时不再 `while(1)` + `wdt_close()` 卡死，改为回到外层等待下一次 S1，可反复重试，不需要拔插 USB/串口
- 恢复 `audio_server` 等任务表项，避免 `server_open("audio_server")` 报 `no find task audio_server info`
  - 阶段 1 音频服务仍会启动，但不做音频业务；阶段 2 再接入 DAC/PA/mic
- WiFi/camera/UI/test 的 late_initcall 继续隔离

### 1.3 修订（2026-11-23）

- 成功开机后的 1s 关机路径原来 `wdt_close(); PWR_CTL(0);` 后固件仍在跑（USB 供电），再按 S1 无法重新进开机流程
- 改为 `PWR_CTL(0); mdelay(50); cpu_reset();`：有电时复位回等待 S1，支持反复开关；电池供电时 PB7 拉低后掉电
- 注意：如果复位后 PB7 又被 setup_arch 拉高，会回到等待 S1 而不是彻底掉电；产品最终需要结合电源保持策略再确认

### 1.4 修订（2026-11-23）

- 重试等待期间不再 `PWR_CTL(0)`，改为保持 `PWR_CTL(1)` 供电；否则电池供电下短按会直接切断固件，`hold S1` 都来不及打印，也无法再次重试
- 现在短按 <3s 只打印 `released <3s, retry` 并回到 `waiting for S1...`，可反复按 S1 重试
- 代价：未完成 3s 时板子会停留在预启动等待状态，需要后续加超时/放弃策略

### 1.5 修订（2026-11-23）

> 本节及 §1.6 为历史修订记录，最终行为以 §1.7 与第五节为准。

现象：串口停在

```
STORY_BRINGUP: KEY_ADC=1
STORY_BRINGUP: waiting for S1...
STORY_BRINGUP: hold S1 for 3s...
```

短按松手后没有任何后续打印，再长按也没有反应。

原因：

- `KEY_ADC=1` 不是实测值。`adc_add_sample_ch()` 会把该通道缓存预置成 `1`，真正读数要等 `adc_scan`（2ms 定时器）扫一轮后才有效。原代码在 `power_key_init()` 后立刻用这个占位值判断，必然误判为"按下"，所以每次上电都会直接进入 `hold S1 for 3s...`。
- 3s 判定原来放在 `board_init()` 的 `while(1)` 里阻塞执行，而 `app_main()`（心跳任务）要等 `board_init()` 返回后才创建。按键状态机一旦异常，整机连心跳都没有，串口看起来就是死机，也无法区分掉电、卡死还是按键逻辑问题。

修改（`board_7911B.c`）：

- 3s 长按 / 1s 关机的按键状态机改为 `sys_timer_add(..., 10ms)` 驱动，`board_init()` 立即返回，app_main 心跳正常启动，不再阻塞
- 状态机跳过 ADC 占位值 `1`，首轮扫描完成前的读数不参与判定
- 长按过程中每 500ms 打印 `holding xxxms adc=yyy`；3s 内松手打印 `released <3s, retry (adc=xx)`，方便串口直接观察按键状态机
- 关机路径的 `mdelay(50)` 改为 `delay_us(50 * 1000)`，不再依赖 OS tick
- 代价：阶段 1 下 app_main 不再等 3s 长按完成，上电即进入心跳；3s/1s 只作为按键状态机判定与打印，产品化时再和电源策略一起收紧

### 1.6 修订（2026-11-23）

现象：接 USB（不按 S1）板子就上电，电源指示灯常亮；1s 关机也只是复位，电一直保持。

原因：

- `board_power_hold_init()` 在 `setup_arch` 里**无条件**把 PB7(PW_EN) 拉高。只要 MCU 在 USB 供电下能跑起来，固件就会立刻锁存电源，表现就是"插 USB 自动开机"。
- 1s 关机拉低 PB7 后执行 `cpu_reset()`，复位后 `setup_arch` 又把 PB7 拉高，USB 供电下永远关不掉（电池供电靠电源轨掉电才能真关机）。

修改（`board_7911B.c`）：

- `board_power_hold_init()` 先用数字口读 PB6 判断是否按键上电：按下（低）才拉高 PB7，松开（USB 插入等）不锁存；读完恢复模拟输入给 ADC 用
- 按键状态机 `WAIT → HOLDING` 时补 `PWR_CTL(1)`，USB 供电下按键仍可正常开机锁存
- 1s 关机新增 `POWEROFF` 状态：先打印 → 拉低 PB7 → 等 300ms；电池供电此时电源轨已掉、芯片直接断电；USB 供电继续等按键松手后 `cpu_reset()`，复位后按键已松开，不会重新锁存，保持关机状态

测试预期：

- USB 只插线不按键：电源指示灯不亮（若仍亮，则是硬件充电/自动上电路径，不是固件）
- USB 下按 S1：灯亮并进入 `hold S1 for 3s...`
- 电池供电：按键上电后松手保持；1s 关机应真正掉电

### 1.7 修订（2026-11-23）

现象：点一下 S1 就保持开机；之后长按无法关机。

原因：

- 状态机在 `WAIT` 检测到按键按下就 `PWR_CTL(1)` 锁存，但不足 3s 松手时只回到 `WAIT`，**没有撤销锁存** → 一次点击 = 永久开机；
- 此时再长按，会被状态机当成"开机 3s 确认"处理，而不是关机 → 表现为长按关不掉。

修改（`board_7911B.c`）：

- `HOLDING` 中不足 3s 松手：`released <3s, power off` → `PWR_CTL(0)` → `POWEROFF`
- `RUNNING` 中按住 1s：`power off` → `PWR_CTL(0)` → `POWEROFF`
- `POWEROFF`：等按键松手后回到 `WAIT`，**不再 `cpu_reset()`**，避免复位后 `setup_arch` 把还按着的按键当成上电重新锁存
- `setup_arch` 锁存过 PB7、但 ADC 有效时按键已经松开（点一下就松手）：`key released before boot, power off`，立即撤销开机
- 3s/1s 计时改用 `jiffies_msec()` 时间戳，不再按定时器回调次数累加，避免定时器实际周期偏慢导致"长按很久也不触发"

行为表：

| 操作 | 结果 |
|---|---|
| USB 只插线不按键 | PB7 不锁存，电源灯不亮 |
| 短按 <3s | 松手即撤销开机（USB 下灯闪一下） |
| 长按 ≥3s | `3s OK` 开机，松手保持 |
| 开机后按 ≥1s | `power off` 关机 |

## 五、构建与测试

```bash
make ac791n_wifi_story_machine
```

产物：`cpu/wl82/tools/jl_isd.fw`

测试：

1. 烧录后串口接 PB1/PB2，115200
2. 上电/复位后应立即出现一次 `STORY_BRINGUP: KEY_ADC=1 (1=扫描未完成)`，随后每秒打印 `STORY_BRINGUP_HB KEY_ADC=xxx`（松开约 1022，按下约 72）
3. 按住 S1：出现 `hold S1 for 3s... (adc=72)`，每 500ms 一次 `holding xxxms adc=yyy`，满 3s 后 `3s OK`
4. 3s 内松手：出现 `released <3s (xxxms), power off (adc=xxx)`，撤销 PB7 锁存；USB 供电下灯灭、回到 WAIT，可重新长按 3s 开机
5. 开机确认（`3s OK`）后松手，再按住约 1s：打印 `STORY_BRINGUP: power off` 并释放 PB7；电池供电真正掉电，USB 供电不复位、固件继续跑心跳，需按键重新开机

## 六、风险与下一步

- 阶段 1 只验证故事机内核 + 板级电源/串口，不含音频/BT/WiFi/UI
- 阶段 2（已完成，见 §七）：DAC/功放/喇叭 + MIC0 录音回放；MIC1 AEC 和音量表待后续
- 阶段 3（进行中，见 §八）：故事机板实际是 SPI JD9853 240×296，不是摄像头板的 EMI ILI9481；已新增 JD9853 驱动并打开 `CONFIG_UI_ENABLE`，SPT5113C 触摸已接入（原厂 037_1 固件，5 点触摸靶实测精准）
- 阶段 4 需要验证 BT 天线/与 WiFi 共存
- 阶段 5 豆包 AI 无现成 demo，需要基于 WiFi/HTTP/WebSocket + 音频流实现
- 摄像头固件保留作回退；每阶段单独提交

### 已知限制

- 3s 从出现 `hold S1 for 3s...` 起算，电池冷启动实际按住约 3.4~3.5s
- 电池供电下软复位/WDT/OTA 复位时，`setup_arch` 采样到未按键会拉低 PB7 → 直接断电而不是重启；stage1 无 OTA 可接受，量产前需按复位源保持锁存
- USB 供电下 1s 关机只释放 PB7，不复位，心跳继续打印，属于设计行为

## 七、阶段 2：音频 bring-up（2026-11-23）

原理图：`WB-AC79-01_R01_SCH_1104.pdf`（仓库根目录）。

### 硬件确认

| 项 | 连接 |
|---|---|
| 功放 | NS4150B，单端输入 INN ← `DAC_L_OUT`（SoC pin50 / `AMP_L_OUT_IC`） |
| 功放 SD | `AMP_MUTE_OUT` = SoC **pin18 = PB5**（AC7912A datasheet：pin18 PB5 = `PLNK0_DAT1/PLNK1_DAT1/SPI1_DIA/FPIN2/CAP0`；**PB3 是 pin19 `CHR_PG`**）。低=输出，高=切断；RA8 10K 上拉到 3V3_M |
| 喇叭 | `SPK_LP` / `SPK_LN`（1.25mm 2pin） |
| MIC0 | 差分，CP=PA2、CN=PA1（`LADC_CH_MIC0_P_N`） |
| MIC1 | 差分，PH9/PH8，来自功放输出回采（AEC），不是第二颗 mic |
| 3V3_M | 由 4V2_SUB 经 RD36 10K 上拉常开；PA9 是 SD0_DAT0，不能当 VCC33 控制脚 |

### 代码修改

- `board_7912AB_cfg.h`：`TCFG_DAC_MUTE_PORT = PB5`、`TCFG_DAC_MUTE_VALUE = 1`、`#undef PORT_VCC33_CTRL_IO`；MIC 只开 `LADC_CH_MIC0_P_N`
- `dac_data`：`hw_channel = 0x03`、`ch_num = 2`（单端 L/R）、`pa_auto_mute = 1`
- bring-up 下恢复 `dac_early_init`，`CONFIG_STORY_AUDIO_TEST` 打开
- `app_main.c`：MIC0 录 3s → 内存 → 喇叭回放闭环测试（每 ~5s 一轮）

### 实测结论

- 喇叭：PCM 1kHz 与提示音均正常出声
- mic：录音峰值随说话/敲击明显变化，回放可听到
- 测试回放期间强制 `dac_set_gain(31)` / `dac_set_volume(16384)`；正常音量表链路和 MIC1 AEC 回采待后续验证

### 已知限制（阶段 2）

- MIC1（功放回采 AEC）尚未接入，AEC/回声消除后续再做
- 回放音量链路目前用强制最大增益验证，产品音量表需再校准

## 八、阶段 3：LCD / UI bring-up（2026-11-24）

### 硬件

- 屏：**NT1137G0020-V2**，240×296，驱动 IC **JD9853**，4L1P（4 线 SPI）
- 接口：`PAN_CK`=PH0（SPI2_CK_A）、`PAN_MOSI`=PH1（SPI2_DO_A）、`PAN_RS`=PH2、`PAN_CS`=PH3、`PAN_RSTN`=PH4、`PAN_TE`=PH5、`PAN_BL_PWM`=PH6
- 触摸：**SPT5113C**，已接入 `apps/common/ui/touch_panel/spt5113c.c`（原厂 v0.2 驱动流程 + BLST_QZ037-NB09_01 调整版固件 037_1，8750B / CRC 0xccf2，上电自动校验/烧录）；I2C SCL=PC1/SDA=PC2/INT=PC3/RST=PC0

### 关键问题与修复

1. **PH2 引脚冲突**：SPI2 A 组的 PH2 是 `SPI2_DI_A`，但硬件把它用作 LCD 的 RS（命令/数据选择）。给 SPI2 自定义 io 表把 `di_pin = -1`，让 SPI 驱动不把 PH2 配成 DI。
2. **非阻塞 SPI 截断命令**：SDK 的 LCD 框架把 SPI 配成非阻塞发送，`WriteCOM/WriteDAT` 发完字节立刻拉高 CS；10MHz 下 1 字节要 0.8µs，CS 提前拉高会截断命令/数据（初始化全丢、屏保持上电雪花）。已在每次写后 `lcd_interface_non_block_wait()` 等发送完成再拉高 CS。
3. **JD9853 驱动**：SDK 没有现成驱动，新增 `apps/common/ui/lcd_driver/lcd_jd9853.c`（JD9853 参考初始化序列 + 240×296 窗口 + RGB565），ST7789V 驱动保持原样，互不影响。
4. **UI**：打开 `CONFIG_UI_ENABLE`，把 `music_ui.c` 加入构建并启动 `ui_demo_task_init()`；`ui_res` 由构建脚本打包成 UIPACKRES。`music_ui.c` 的静态 `ui_server_init` 改名避免与公共符号冲突。
5. **TE**：7912AB 暂用 `USE_LCD_TE=0`（免 TE 推帧、初始化即开背光），避免 TE 链路未通导致不发帧/黑屏；代价是全屏切换可能有撕裂，后续再单独调 PH5 的 TE。

### 实测结论

- GPIO bit-bang（诊断用，代码保留在 `board_7911B.c`）能正常出纯色，证明屏/接线/初始化/格式正确
- SPI2 路径修复后纯色正常
- UI 正常显示（menu.res 的 PAGE_0 / 杰理宣传页），UI 资源 4/4 加载成功

### 已知限制（阶段 3）

- 免 TE 模式全屏刷新可能有撕裂，TE 消撕裂后续再做
- SPT5113C 触摸已接入：首次上电自动烧录新固件（0x8813→0xccf2），5 点触摸靶在原生竖屏 240×296 下 1:1 映射，实测跟手精准；产品 UI 的触摸交互后续再做
- UI 目前是 SDK 的 music_ui 演示界面，产品 UI 后续替换
