# 在 Windows 上用 DSH（harness）直接编译 / 烧录 / 读串口

这份是**交给 Windows 上那个 agent 看的交接说明**：环境怎么配、一条命令怎么烧、
这块板子有哪些坑、以及当前还没解决的那个显示问题查到哪一步了。

---

## 一、环境（只配一次）

在 Windows 上需要：**Node ≥ 24.9**（DSH 本体）、**arduino-cli**、ESP32 核心和两个库。

```powershell
winget install ArduinoSA.CLI
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib update-index
arduino-cli lib install "lvgl@8.4.0"
# 注意：ClockWeatherSetup v2.3 起不再依赖 Arduino_GFX（直接调 ESP-IDF 的 esp_lcd），
# 所以既不用装 GFX 库、也不用打 _panel_handle 补丁。只有编译 ClockWeather /
# ScreenTest 时才需要：arduino-cli lib install "GFX Library for Arduino@1.6.8"
# 然后跑 .\tools\patch_gfx.ps1

# 关键：lv_conf.h 必须放在 libraries 目录下（和 lvgl 文件夹同级）
Copy-Item .\ClockWeatherSetup\lv_conf.h "$env:USERPROFILE\Documents\Arduino\libraries\lv_conf.h" -Force
```

> 没放 `lv_conf.h`：LVGL 会用库自带默认配置 —— 缺 KEYBOARD / TEXTAREA，向导页直接编译失败。

## 二、编译 + 烧录（一条命令）

工作区根目录假设是 `C:\Users\MinG\Documents\data`，工程在 `ClockWeatherSetup\`
（**目录名必须和 .ino 同名**）。

```powershell
arduino-cli compile --upload -p COM10 `
  --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default,FlashMode=qio,UploadSpeed=921600" `
  --warnings more .\ClockWeatherSetup
```

* `-p COM10` 换成实际端口：`arduino-cli board list`
* 上传前**关掉 Arduino IDE 的串口监视器**（端口会被占用 → `Could not open COM10`）
* 传不上去：按住 **BOOT**，点一下 **RST**，松 BOOT，再重试
* 只编译不烧：去掉 `--upload -p ...`
* 编译成功会打印 `Sketch uses 1959406 bytes (62%)`；**0 error / 0 warning** 是基线要求

## 三、读串口日志

```powershell
arduino-cli monitor -p COM10 --config baudrate=115200
```

（DSH 里可以放后台跑，然后读输出；注意它会一直占着端口，烧录前要先停掉。）

**开机前几行就是自检清单，先看这三行：**

```
[启动] 固件 ClockWeatherSetup v1.9 (搬运缓冲回到缓存装得下的12行)
[显示] DMA 搬运缓冲 12 行（38400 字节内部 RAM，首选 12 行）
[显示] 像素时钟 16000000 Hz（约 39.0 Hz 刷新）
```

* 固件版本号不是最新的 → 烧进去的还是旧固件，别怀疑代码
* 搬运缓冲显示 `10 行` / `8 行` → 内部 RAM 不够自动降档了
* 还有心跳：每 10 秒一行 `[心跳] 空闲堆 … 最大连续堆 … 主循环栈剩 …`
* 触摸：`[触摸] 按下 (x, y)` / `[触摸] 抬起`

**串口诊断命令**（DEBUG_SERIAL=1 时可用，行尾符设「无」也行，静默 200ms 算一条）：

```
pclk 14      像素时钟 → 14.55MHz（34.1Hz，带宽 −9%）
pclk 13      像素时钟 → 13.33MHz（31.2Hz，离白屏很近，慎用）
pclk 16      回默认 16MHz
```

降过头会**白屏**：白屏时发 `pclk 16` 或断电重启就恢复（不写 Flash）。

## 四、这块板子的硬事实（别踩）

| 项目 | 事实 |
|---|---|
| 开发板 | ESP32-S3-WROOM-1 **N16R8**（16MB Flash + 8MB OPI PSRAM） |
| 屏幕 | 5 寸 800×480 ST7262 **RGB 并口**，PCLK=GPIO42，背光=GPIO2 |
| 触摸 | GT911，I2C 19/20，RST 38，INT 18；**点记录从 0x814F 开始**（不是 0x8150） |
| PSRAM | 必须选 **OPI PSRAM**，否则 750KB 帧缓冲分配失败 |
| 像素时钟 | 16MHz（39Hz）。**12MHz = 29Hz 会白屏**，别往那么低 |
| 背光 | GPIO2 是**使能型**，只有全亮能看，调低直接全黑 → 代码里没有亮度调节 |
| 天气 | Open-Meteo，**故意走明文 HTTP**（`WEATHER_TRY_TLS_FIRST=0`），TLS 要十几 KB 连续内部 RAM，这板子跑起来只有 8~25KB |
| 天气代码 | `weather.cpp` 里 `WiFiClientSecure client;` **必须写在** `HTTPClient http;` **前面**（析构顺序，否则随机花屏 + pbuf_free 崩溃） |
| 搬运缓冲 | `LCD_BOUNCE_LINES` **不能超过 16 行**（L1 数据缓存只有 32KB，见下） |
| 渲染方式 | 分块渲染 `LCD_DIRECT_RENDER 0`（有列表滚动/软键盘，直接渲染会闪） |

## 五、当前未解决的问题：大数字左边「一条一条」

**现象**：满屏大时钟的数字左侧出现 1px 细的短横线（深底上是亮线、白笔画上是暗线），
长度几十像素，**一会儿有一会儿没有**。

**已查明**（不用再从头查）：

1. 横线之间的间距**正好等于搬运缓冲的行数**（16 行时 16 行一条，24 行时 24 行一条）。
   → 问题出在「每块搬运缓冲的开头」。
2. 机制（IDF `esp_lcd_panel_rgb.c`）：两块 bounce buffer 轮转，DMA 读 A 时中断把下一块
   从 PSRAM memcpy 进 B；memcpy 没赶在「DMA 读完 A」之前完成，B 开头就还是旧内容；
   块开头 = 行首 = 屏幕左边。
3. **24 行是错的**（v1.8 踩过的坑）：IDF 会 `Cache_Start_DCache_Preload()` 预读下一块进
   L1 缓存，而 ESP32-S3 的 L1 D-cache 只有 **32KB**（`CONFIG_ESP32S3_DATA_CACHE_SIZE=0x8000`）：
   12 行 19.2KB ✓ / 16 行 25.6KB ○ / **24 行 38.4KB ✗**（装不下 → 预读反而冲掉整个缓存
   → 每块 memcpy 全落回 PSRAM 慢读 → 反而更糟）。v1.9 已回到 **12 行**。
4. 触发因素是**突发**，不是平均带宽：WiFi/天气刷新、满屏重绘的 cache 写回、
   字库/代码从 Flash 读（Flash 与 PSRAM 共用 SPI 总线）都会把那次 memcpy 顶掉。

**接下来值得做的实验**（一次只改一个，每次记下：改了什么、横线间距、出现的频率）：

| # | 实验 | 怎么判断 |
|---|---|---|
| 1 | 只跑 v1.9 观察 1~2 小时 | 还出不出？出现时是否和「更新 HH:MM」变化同步（=天气刷新触发） |
| 2 | `pclk 14` 跑 1 小时 | 干净了 → 把 `board_pins.h` 的 `LCD_PREFER_SPEED` 改成 `14545454` 固化 |
| 3 | `LCD_BOUNCE_LINES` 12 → 16 / 10 各烧一次 | 对比出现频率；**别超过 16** |
| 4 | 仍不行再 `pclk 13`（13.33MHz/31.2Hz） | 可能白屏，风险自负 |

**结论标准**：连续跑几小时（含多次天气刷新）看不到横线 = 可接受。

## 六、改完怎么提交

仓库：https://github.com/humanxp/JC8048W550-ClockWeather （分支 `main`）

Windows 上推送需要自己的凭据（GitHub PAT 或本机已有的 SSH key）。
如果不想在 Windows 上配，也可以只改文件，由 Linux 那台机器提交推送。

提交前自检：

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default,FlashMode=qio,UploadSpeed=921600" --warnings more .\ClockWeatherSetup
# 要求：0 error / 0 warning
```

别忘了同步更新 `ClockWeatherSetup/README.md` 里对应章节和 `tools/verified-build.log`。

---

## 七、如果 harness 只允许写工作区

DSH 默认的文件权限是「只能改工作区」。而 `arduino-cli` 默认要写
`%LOCALAPPDATA%\Arduino15`（核心/缓存）和 `%USERPROFILE%\Documents\Arduino`（库），
**这两个都在工作区外，会被沙箱挡掉**。两个办法，任选一个：

**A. 放权**：让 DSH 这个会话用完整文件权限（danger-full-access）。

**B. 把 arduino-cli 的目录也搬进工作区**（推荐，干净）：

```powershell
# 在工作区根目录建一个 arduino-cli.yaml
@"
directories:
  data: C:\Users\MinG\Documents\data\.arduino\data
  downloads: C:\Users\MinG\Documents\data\.arduino\staging
  user: C:\Users\MinG\Documents\data\.arduino\user
"@ | Set-Content -Encoding utf8 .\arduino-cli.yaml

# 之后所有命令都带上 --config-file
arduino-cli --config-file .\arduino-cli.yaml core install esp32:esp32@3.3.11
arduino-cli --config-file .\arduino-cli.yaml lib install "lvgl@8.4.0"
# （只在编译 ClockWeather / ScreenTest 时才需要 GFX 库）
# arduino-cli --config-file .\arduino-cli.yaml lib install "GFX Library for Arduino@1.6.8"

# lv_conf.h 放到这个「工作区内」的 libraries 目录
Copy-Item .\ClockWeatherSetup\lv_conf.h .\.arduino\user\libraries\lv_conf.h -Force

# 编译时把构建目录也指定到工作区
arduino-cli --config-file .\arduino-cli.yaml compile --upload -p COM10 `
  --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default,FlashMode=qio,UploadSpeed=921600" `
  --build-path .\.build\ClockWeatherSetup .\ClockWeatherSetup
```

注意：`patch_gfx.ps1` 默认改的是 `Documents\Arduino\libraries` 下那份库；
把目录搬进工作区后，要改的是 `.arduino\user\libraries\...` 那份 ——
脚本支持传路径参数，或者改完用 `Select-String _panel_handle` 确认已经是 `public:`。
