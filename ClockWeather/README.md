# JC8048W550 联网时钟 + 天气

给 **JC8048W550 / ESP32-S3-WROOM-1 N16R8 + 5 寸 800×480 IPS 电容屏** 写的
Arduino + LVGL 网络时钟天气应用。开机自动连 WiFi、NTP 对时、拉取实时天气，
主界面是中文大时钟 + 当前天气 + 未来三天预报。

> **第一次上手？** 如果你在 Windows 上什么都没装过，先看
> **[TUTORIAL-Windows11.md](TUTORIAL-Windows11.md)** —— 那份是从装软件开始的
> 保姆级步骤教程。本 README 是参考资料（引脚表、原理、排障），适合已经有环境的人查。

```
┌──────────────────────────────────────────────────────────────┐
│ 宁波 · 2025年4月6日 星期日         ● 已联网 · 更新 14:22      │
├───────────────────────────────┬──────────────────────────────┤
│                               │   ☀☁      23.4°              │
│          08:42                │           多云               │
│    ━━━━━━━━━━━━   37          │   体感 ─────────── 25.1°C    │
│     (秒进度条)                │   湿度 ─────────── 62%       │
│                               │   风速 ─────────── 3.2m/s    │
│                               │   气压 ─────────── 1013hPa   │
├───────────────────────────────┴──────────────────────────────┤
│  ┌── 今天 ──┐   ┌── 明天 ──┐   ┌── 后天 ──┐                  │
│  │      🌧  │   │      ☁   │   │      ☀   │                  │
│  │26° / 18° │   │24° / 17° │   │28° / 19° │                  │
│  └──────────┘   └──────────┘   └──────────┘                  │
└──────────────────────────────────────────────────────────────┘
```

时钟 **193px 粗体**（左），天气卡（右），底部是 今天 / 明天 / 后天。
完整效果图见同目录 `preview.svg`（用浏览器打开）。

---

## 一、硬件信息（已核对，可直接照抄）

这块板是 **ESP32-S3-WROOM-1 N16R8**（16MB Flash + 8MB OPI PSRAM），
屏幕是 **ST7262 驱动、RGB565 并口、800×480**，触摸是 **GT911 电容触摸（I2C）**。

| 信号 | GPIO | 信号 | GPIO |
|---|---|---|---|
| DE | 40 | R0 / R1 / R2 / R3 / R4 | 45 / 48 / 47 / 21 / 14 |
| VSYNC | 41 | G0 / G1 / G2 / G3 / G4 / G5 | 5 / 6 / 7 / 15 / 16 / 4 |
| HSYNC | 39 | B0 / B1 / B2 / B3 / B4 | 8 / 3 / 46 / 9 / 1 |
| **PCLK** | **42** | 背光 BL | 2 |
| 触摸 SDA | 19 | 触摸 SCL | 20 |
| 触摸 RST | 38 | 触摸 INT | 18 |

> ⚠️ **最容易踩的坑：PCLK 是 GPIO42，不是 GPIO12。**
> GPIO12/13/11/10 是 TF 卡（SD）的 SCK/MISO/MOSI/CS。很多人从别的 800×480 板子
> 抄配置时把 PCLK 写成 12，结果屏幕全黑。
>
> GT911 的 I2C 地址由复位时 INT 的电平决定：INT 拉低是 `0x5D`，拉高是 `0x14`。
> 本板通常是 `0x5D`，程序里两个地址都会自动探测。

引脚来源（三份交叉核对）：
[Arduino_GFX issue #472 实测引脚表](https://github.com/moononournation/Arduino_GFX/issues/472)、
[esp32-smartdisplay 的 esp32-8048S050C 板级定义](https://github.com/rzeldent/platformio-espressif32-sunton)、
[JC8048W550 ESP-IDF 工程板级头文件](https://github.com/MayysterUFTD/ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ)。

---

## 二、安装库（版本很关键）

打开 Arduino IDE → **工具 → 管理库**，搜索并安装：

| 库 | 作者 | 版本要求 |
|---|---|---|
| **lvgl** | LVGL | **必须 8.4.0**（不要装 9.x！） |
| **GFX Library for Arduino** | Moon On Our Nation | 最新版即可（Arduino_GFX） |

在库管理器里选中库之后，版本下拉框可以挑选具体版本，装 `8.4.0`。

### ⚠️ 放置 lv_conf.h（这一步最容易漏）

把本工程里的 `lv_conf.h` 复制到 **Arduino 的 libraries 目录下**，和 `lvgl` 文件夹**同级**：

```
Arduino/libraries/
    ├── lvgl/                    ← 库管理器装的
    ├── lv_conf.h                ← 把本工程这个文件放这里 ← ★
    ├── GFX_Library_for_Arduino/
    └── ...
```

常见路径：

* Windows：`C:\Users\你的用户名\Documents\Arduino\libraries\lv_conf.h`
* macOS：`~/Documents/Arduino/libraries/lv_conf.h`
* Linux：`~/Arduino/libraries/lv_conf.h`

> 这个文件**不能**放在 `ClockWeather` 工程目录里，LVGL 找不到它。
> 忘了放的话，编译会报 `lv_conf.h: No such file or directory`。

---

## 三、Arduino IDE 开发板设置

**工具** 菜单里按下表设置（开发板选 `ESP32S3 Dev Module`）：

| 项目 | 值 | 说明 |
|---|---|---|
| Board | **ESP32S3 Dev Module** | |
| PSRAM | **OPI PSRAM** | ★ 必须，屏幕帧缓冲 750KB 要放这里 |
| Flash Size | **16MB (128Mb)** | |
| Flash Mode | **QIO 80MHz** | |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** | 程序含中文字库约 2.3MB |
| USB CDC On Boot | **Disabled** | ★ 见下方说明 |
| CPU Frequency | 240MHz (WiFi) | |
| Upload Speed | 921600 | 烧录失败就降到 460800 |
| Arduino Runs On | Core 1 | |

> **关于 USB CDC On Boot**：GPIO19/20 被用作触摸的 I2C，而这两个脚同时也是
> ESP32-S3 的原生 USB（D-/D+）。所以要用 **UART 口**烧录和看串口日志：
> * 板子上如果只有一个 Type-C，那个口一般就是 USB-UART（CH340）口，直接插它；
> * 如果有两个，插标着 **UART / COM / CH340** 的那个（另一个是原生 USB，本工程用不上）；
> * 串口监视器波特率设 **115200**。
> 如果开了 `USB CDC On Boot = Enabled`，USB 外设会占用 GPIO19/20，触摸会失效。

---

## 四、配置

**先把配置模板复制成 config.h**（这样你填的 WiFi 密码不会被 git 提交）：

```
copy config.example.h config.h        (Windows)
cp   config.example.h config.h        (macOS / Linux)
```

然后打开 `config.h`，至少改这两处：

```cpp
#define WIFI_SSID     "你家的WiFi名"      // ⚠ ESP32 只支持 2.4GHz，不支持 5G 频段
#define WIFI_PASSWORD "你家的WiFi密码"

#define CITY_NAME  "北京"
#define CITY_LAT   39.9042f      // 纬度
#define CITY_LON   116.4074f     // 经度
```

* **推荐**：填准经纬度（[latlong.net](https://www.latlong.net/) 上搜城市就能拿到），天气最准。
* **懒人模式**：把 `CITY_LAT` 和 `CITY_LON` 都写成 `0`，程序会用 `CITY_NAME`
  调 Open-Meteo 的地理编码接口查一次坐标，并把结果存进 Flash，以后不再查询。

时区默认 `"CST-8"`（中国）。**注意负号表示东八区，不是写错了** —— POSIX 时区串里
`CST-8` 的含义是「本地时间 = UTC + 8」。

---

## 五、编译烧录 & 串口日志

### ⚠️ 先说清楚：TF 卡不能刷固件

**ESP32-S3 只能从它自己内部那颗 16MB SPI Flash 启动**，ROM 引导程序不会去读 SD 卡。
这块板的 TF 卡槽接在 GPIO10/11/12/13 上，是独立的 SPI 总线，只用来给程序读写数据
（图片、音乐、日志等）。

如果你看到厂家 `LVGL_Demo_Music` 的教程要往 TF 卡里拷东西 —— 拷的是**素材**，
固件本身仍然是从 USB 串口烧进去的。

**本工程完全不用 TF 卡**：中文字库和时钟字体都编译进 Flash 了，
卡槽空着、或者不插卡，都照样跑。

烧录只有四步：

```
1. 数据线插到板子的 Type-C 口   ← 是插线，不是插卡
2. Arduino IDE 选对开发板和端口（工具 → 端口）
3. 点「上传」按钮（→ 箭头）
4. 等它烧完自动复位重启，界面就出来了
```

**点了上传没反应时的排查顺序：**

1. 按住 **BOOT** 不放 → 点一下 **RST** → 松开 RST → 再松开 BOOT，
   强制进入下载模式，然后重新点上传；
2. 上传波特率从 921600 降到 **460800**；
3. 换一根**能传数据**的 USB 线（有些线只供电不传数据，这是最坑的一类问题）；
4. Windows 上「端口」列表里什么都没有 → 缺 USB 转串口驱动，
   装 CH340 驱动（少数批次是 CP210x，看板子背面芯片丝印）。

### 串口日志

正常的话串口（115200）会依次打印：

```
==========================================
  JC8048W550  联网时钟 + 天气
  ESP32-S3 + ST7262 800x480 + GT911
==========================================
PSRAM 总计 8388608 字节，可用 8385000 字节
[显示] 帧缓冲 @ 0x3c000000，PSRAM 剩余 8384512 字节
[显示] ST7262 800x480 初始化完成
[触摸] GT911 地址 0x5D，产品 ID: 9110
[触摸] 芯片配置分辨率 800x480
[网络] 开始连接 WiFi: xxx
[网络] 已连接，IP 192.168.1.23，信号 -52 dBm
[时间] 启动 NTP 授时，时区 CST-8
[时间] NTP 同步成功
[天气] 23.4°C 体感25.1° 湿度62% 风3.2m/s 气压1013hPa 代码2 多云
```

> 第一次编译会比较慢（要编译 4MB 多的中文字库源码），属正常现象。
> 之后有增量缓存就快了。

### 实测体积（本工程已用 arduino-cli 编译验证过）

| 项目 | 实测值 | 上限 |
|---|---|---|
| Flash | **1,904,766 字节 (60%)** | 3,145,728 字节（3MB APP 分区） |
| 静态 RAM | **99,196 字节 (30%)** | 327,680 字节 |

字库位图合计 617KB，最大的 `font_cjk_22` 占 592KB —— 距离 LVGL 的
1MB 单字库位图上限（`LV_FONT_FMT_TXT_LARGE=0` 时）还有余量，可以放心再加字。

---

## 六、文件结构

```
ClockWeather/
├── ClockWeather.ino     主程序：初始化 + 主循环状态机
├── config.h             ★ 用户配置（WiFi / 城市 / 时区 / 刷新间隔）
├── board_pins.h         板级引脚定义（含 PCLK=42 的说明）
├── display.h/.cpp       ST7262 RGB 屏驱动 + LVGL 移植（直接渲染模式）
├── touch_gt911.h/.cpp   GT911 电容触摸驱动（自己实现，不依赖第三方库）
├── net_time.h/.cpp      WiFi 连接 + NTP 授时（全非阻塞）
├── weather.h/.cpp       Open-Meteo 天气拉取 + 极简 JSON 解析
├── weather_icon.h/.cpp  程序化绘制的天气图标（不用图片）
├── ui.h/.cpp            LVGL 界面
├── lv_conf.h            → 要复制到 Arduino/libraries/ 下（见第二节）
└── font_*.c             自造字库（见下方说明）
```

字库说明：

| 文件 | 字号 | 内容 | 位图大小 |
|---|---|---|---|
| `font_clock_193.c` | 193px | 时钟大数字 `0-9 : -`（**粗体**，思源黑体 Bold） | 15 KB |
| `font_digits_44.c` | 44px | 温度 / 秒数，含空格、`0-9 . ° - + / %` | 3 KB |
| `font_digits_32.c` | 32px | 预报温度 | 2 KB |
| `font_cjk_22.c` | 22px | GB2312 一级字表 + 常见地名补充字（3908 字） | 592 KB |
| `font_cjk_32.c` | 32px | 天气描述用字（28 字） | 6 KB |

> ⚠️ 数字字体里**必须包含空格**：预报卡片显示的是 `18° / 26°`，
> 漏掉空格的话斜杠会贴到度数上变成 `18°/26°`。
> `tools/fonts/digits_ui.txt` 的第一个字符就是空格，重新生成时别删掉。

时钟字号是算出来的，不是拍脑袋定的。**粗体字形比常规体宽 7.4%**：

| 字重 | `HH:MM` 宽度公式 |
|---|---|
| 常规体 Regular | `2.498 × 字号` |
| **粗体 Bold（当前）** | **`2.685 × 字号`** |

时钟区宽 504px，`504 / 2.685 ≈ 187`，留出左右各 16px 边距取 **176px**（实际占 473px）。
想改字号就同步改两处：重新生成字体文件 + `ui.cpp` 里的 `CLOCK_W`。


为什么不用 LVGL 内置字体？因为内置字体**不含中文**；而把整套思源黑体塞进去要 1MB+。
这里按需裁剪到 3908 个常用字，既能显示全国绝大多数地名，体积也还能接受。
想改字库见 `tools/` 目录。

---

## 七、常见问题排查

### 屏幕完全不亮 / 全黑
1. 先确认 **PSRAM = OPI PSRAM**。串口如果打印「没有检测到 PSRAM」，就是这里没选对。
2. 背光单独测一下：把 `display.cpp` 里 `ledcWrite` 那行临时换成
   `pinMode(2, OUTPUT); digitalWrite(2, HIGH);`，看是否亮。
3. 检查 PCLK 是不是 **42**。
4. 串口有没有打印 `gfx->begin() 失败`。

### 画面颜色不对（人脸发蓝、天空发黄 → 红蓝通道颠倒）
把 `lv_conf.h` 里的 `LV_COLOR_16_SWAP` 从 `0` 改成 `1`，重新编译。

### 画面有横纹、闪烁、抖动
1. 确认 `lv_conf.h` 里 `LV_USE_LOG` 打开后串口没有报错；
2. 把 `board_pins.h` 里的 `LCD_PREFER_SPEED` 从 `16000000` 降到 `12000000` 或 `9000000`；
3. WiFi 省电已经在 `net_time.cpp` 里关掉了（`WiFi.setSleep(false)`），这是 RGB 屏闪烁的常见原因。

### 跑一段时间后随机花屏 / 拉丝，一直不复原（只有重启才好）

**症状**：运行几十分钟后，屏幕某一块（常见是左半部分）出现拉丝花屏，
**而且不会自己恢复**，按 RST 重启后好一段时间，然后又会复发。出现时机随机，
和天气刷新、整分钟跳变都对不上。

**原因**：ESP32-S3 的 RGB GDMA **永久失步**（permanent desync）。
ESP-IDF 源码里对这个现象的原话是：

```c
// reset the GDMA channel every VBlank to stop permanent desyncs from happening.
// ... the single-frame desync this leads to is preferable to the
//     permanent desync that could otherwise happen.
```

官方开关是编译期的 `CONFIG_LCD_RGB_RESTART_IN_VSYNC`，但 Arduino 用的 ESP-IDF
是**预编译**好的，改不了这个选项。

**解决**：本工程改成运行时定期调用官方的 `esp_lcd_rgb_panel_restart()`，
让失步在几秒内自愈，不用再重启板子。要做两步：

**第一步：给 Arduino_GFX 打一行补丁**（把 panel 句柄从 private 放开）

文件（用记事本或 VS Code 打开）：

```
Arduino/libraries/GFX_Library_for_Arduino/src/databus/Arduino_ESP32RGBPanel.h
```

搜索 `_panel_handle`，往上找最近的那个 `private:`（大约第 125 行），改成：

```c
public:        // 原来是 private:
```

**第二步：确认 `board_pins.h` 里开着自愈开关**

```c
#define LCD_DMA_RESTART_FIX             1
#define LCD_DMA_RESTART_INTERVAL_MS     5000
```

重新编译上传即可。

> 没打补丁就编译，会报：
> ```
> error: 'esp_lcd_panel_t* Arduino_ESP32RGBPanel::_panel_handle' is private within this context
> ```
> 不想改库的话，把 `LCD_DMA_RESTART_FIX` 改成 `0`，花屏问题就回到原样
> （只能靠重启恢复）。

**如果打完补丁还是花屏**，把 `LCD_DMA_RESTART_INTERVAL_MS` 调小到 `2000`
试试（恢复更快，但可能每 2 秒看到一次轻微闪动）；
如果发现**每隔固定时间有一次可察觉的闪动**，说明 5 秒太频繁，调到 `15000` 或 `30000`。

> 代价说明：这个复位本身偶尔会造成**单帧**错位（Espressif 自己在注释里也承认）。
> 但「偶尔闪一下」比「一直花到重启」好得多。

### 触摸没反应
1. 看串口有没有 `[触摸] GT911 地址 0x5D`。如果打印「未找到 GT911」：
   * 确认 `USB CDC On Boot = Disabled`（否则 GPIO19/20 被 USB 占用）；
   * 确认触摸排线插好。
2. 有地址但点了没反应：把 `lv_conf.h` 的 `LV_USE_LOG` 设 1、`LV_LOG_LEVEL` 设
   `LV_LOG_LEVEL_INFO`，看是否有输入事件。

### 触摸位置和手指对不上
改 `board_pins.h` 底部三个宏（0/1 互换）：
`TOUCH_SWAP_XY`、`TOUCH_MIRROR_X`、`TOUCH_MIRROR_Y`。

> **⚠ 已修正的驱动 bug（2025 追加）**：`touch_gt911.cpp` 里
> `GT911_REG_POINT1` 原本写成 `0x8150`，但 GT911 的触摸点记录是从
> **`0x814F`** 开始的（`0x814F`=track id，`0x8150~51`=X，`0x8152~53`=Y，
> `0x8154~55`=面积）。差这一字节的后果是：X 被读成「X 高字节+Y 低字节」，
> Y 被读成「Y 高字节+面积低字节」，值都变成几千，再乘屏幕尺寸就被 clamp 到
> 右下角 —— **每次触摸坐标都是 `799, 479`**。
>
> 本版界面是只读看板，没有可点控件，所以一直没暴露；做
> `ClockWeatherSetup`（有可点列表）时才被发现。两边的驱动都已改成 `0x814F`，
> 与 [esp-bsp `esp_lcd_touch_gt911.c`](https://github.com/espressif/esp-bsp)
> 和 [LovyanGFX `Touch_GT911.cpp`](https://github.com/lovyan03/LovyanGFX) 一致。
> 如果你手上是旧版代码，把这一行改掉即可。

### 编译报错 `Sketch too big` / 分区不够
* 确认 Partition Scheme 选的是 **16M Flash (3MB APP/9.9MB FATFS)**；
* 还不行就把 `font_cjk_22` 的字集缩小（见 `tools/gen_fonts.sh` 里的说明）。

### 编译报错 `lv_conf.h: No such file or directory`
`lv_conf.h` 没放到 `Arduino/libraries/` 下，见第二节。

### 编译报一堆 LVGL 的错（比如 `lv_disp_drv_t` 未定义）
装成 LVGL 9.x 了。库管理器里把 lvgl 换成 **8.4.0**。

### 天气一直是「获取天气」然后失败
串口会打印具体原因（HTTP 状态码等）：
* `HTTP -1` 一般是网络不通或 DNS 失败；
* 确认城市经纬度没填错；
* 国家网络环境对 `api.open-meteo.com` 的访问通常没问题，若被墙可自己换数据源
  （改 `weather.cpp` 里的 URL 和 `weather_text()` 映射即可）。

### 时间显示 `--:--`
说明 NTP 还没同步上。检查：
* 时区串格式（中国是 `"CST-8"`，注意负号）；
* 路由器是否放行 UDP 123 端口（少数企业网络会拦）。

---

## 九、这套代码验证到什么程度

**✅ 真机验证过的**（JC8048W550 实板，两块板都跑过）

| 项目 | 结果 |
|---|---|
| 屏幕点亮 / 颜色 / 时序 | 正常，无需调整 |
| GT911 触摸 | 识别为 `0x5D`，能读到按下（⚠ 早期版本坐标有 bug，见下方说明） |
| 大时钟 / 秒数 / 秒进度条 | 正常走字 |
| 天气拉取与显示 | 正常 |
| 今天 / 明天 / 后天 + 温度区间条 | 正常 |
| WiFi 连接 / 断线重连 | 正常 |
| NTP 网络授时 | 正常 |

**✅ 代码层面验证过的**

1. **完整编译通过**：用 arduino-cli + `esp32:esp32@3.3.11` + `lvgl@8.4.0` +
   `GFX Library for Arduino@1.6.8`，按第三节的板级参数编译，
   **0 error / 0 warning**（`--warnings more` 级别）。
2. **引脚三方交叉核对**：Arduino_GFX 库自带的 `JC8048W550` 板级定义 /
   esp32-smartdisplay 板级定义 / JC8048W550 的 ESP-IDF 工程头文件，三者一致。
3. **天气解析逻辑**：拿 Open-Meteo 的**真实返回**跑过一遍，7 个 current 字段 + 
   daily 数组全部与标准 JSON 解析结果一致；并验证了不会误匹配 `current_units`、
   `temperature_2m_max`，以及 `null` / 负数 / 科学计数 / 缺字段等边界情况。
4. **字库格式与体积**：确认 `lv_font_conv 1.5.3` 生成的是 LVGL v8 格式
   （v9 的 `lv_font_t` 结构不同，不能用）；每个字库的位图都实测小于 1MB 上限。
5. **字库覆盖**：程序源码里用到的全部汉字（含「雾凇」「霾」等）都在字库内，
   已用脚本逐个校验，零遗漏。

**⚠️ 仍可能存在的个体差异**

同款板子不同批次仍可能有小差别，第七节把症状和对应改法都列全了：

* **颜色红蓝颠倒** → 改 `lv_conf.h` 里的 `LV_COLOR_16_SWAP`
* **画面横纹闪烁** → 降 `board_pins.h` 里的 `LCD_PREFER_SPEED`
* **触摸方向不对** → 改 `board_pins.h` 底部的 `TOUCH_MIRROR_X/Y`、`TOUCH_SWAP_XY`
* **屏幕完全不亮** → 检查 `工具 → PSRAM` 是否选了 OPI PSRAM

---

## 十、二次开发提示

* **再调时钟大小**：时钟字号受时钟区宽度限制。字库生成工具已经装在 `tools/` 下
  （含 `lv_font_conv` 和思源黑体常规/粗体两套），直接：

  ```bash
  cd tools
  ./node_modules/.bin/lv_font_conv \
      --font fonts/NotoSansSC-Bold.otf \
      --size 190 --bpp 4 --format lvgl --lv-include lvgl.h \
      --lv-font-name font_clock_190 \
      --symbols "0123456789:-" \
      -o ../ClockWeather/font_clock_190.c
  ```

  然后同步改三处：删掉旧的 `font_clock_193.c`、`fonts.h` 里的 `extern`、
  `ui.cpp` 里引用的字体名，并按 `CLOCK_W = 字号 × 2.685 + 32` 调整
  `ui.cpp` 的 `CLOCK_W`（宽了就把 `CARD_X` 往右推、`CARD_W` 相应缩小）。

  各字号对应的实测宽度（**粗体**，时钟区至少要这么宽）：

  | 字号 | HH:MM 宽度 | 时钟区至少需要 |
  |---|---|---|
  | 160px | 430px | 462px |
  | **176px** | **473px** | **504px（当前）** |
  | 190px | 510px | 542px |
  | 210px | 564px | 596px |

  > 想要更大的时钟就必须加宽时钟区，而那要缩小天气卡。注意天气卡不能低于
  > 约 240px —— 最长天气描述「雷阵雨伴冰雹」在 32px 下就占 192px，
  > 加上左右留白至少需要 224px。

* **当前界面是只读看板**：触摸驱动已经初始化并挂到了 LVGL（`display.cpp` 里的
  indev），但没有可点控件。想加交互，给任意控件加事件即可：
  ```cpp
  lv_obj_add_event_cb(btn, my_cb, LV_EVENT_CLICKED, NULL);
  ```
* **改布局**：所有坐标集中在 `ui.cpp` 顶部的「布局常量」和 `ui_init()` 里，
  控件都用绝对坐标摆放，改起来很直观。
* **换主题色**：`ui.cpp` 顶部一组 `COL_*` 宏，改完整个界面跟着变。
* **换单位**：`weather.cpp` 里把 `wind_speed_unit=ms` 换成 `kmh`，
  再把 `ui.cpp` 中 `"%.1fm/s"` 改成 `"%.1fkm/h"`。
* **加室内温湿度**：`weather_icon.cpp` 里加个新图标，`ui.cpp` 里加一行即可。
* **降低烧屏风险**：IPS 屏风险不大，但长期常亮建议调低背光：
  `display_set_brightness(70)`。

---

## 十一、数据来源与致谢

* 天气数据：[Open-Meteo](https://open-meteo.com/)（免 API Key、免费、无需注册）
* 显示驱动：[Arduino_GFX](https://github.com/moononournation/Arduino_GFX)
* 图形库：[LVGL 8.4](https://lvgl.io/)
* 字体：[Noto Sans SC](https://github.com/notofonts/noto-cjk)（SIL Open Font License 1.1）
