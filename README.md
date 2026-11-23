# JC8048W550 联网时钟 + 天气

ESP32-S3 + 5 寸 800×480 RGB 屏（ST7262）+ GT911 电容触摸，做成一个
**满屏大时钟 + 天气看板**，并且能在屏幕上完成 WiFi / 地区设置，不用改代码重烧。

```
┌──────────────────────────────────────────────────────┐
│                                       ● 已联网       │
│                                                      │
│                06:21                                 │
│                                                      │
│              11月22日 周日                           │
├──────────────────────────────────────────────────────┤
│ ☀ 23.4° 多云 │ 体感 25.1°C 湿度 62% 风速 3.2m/s 气压 1013hPa │
└──────────────────────────────────────────────────────┘
```

点一下屏幕切到「时间 + 天气」看板（三天预报，每张卡下面是降水概率条），
停 5 分钟没操作自动回到大时钟。

## 两个版本

| 工程 | 定位 | 说明 |
|---|---|---|
| **`ClockWeather`** | 最简版 | WiFi / 城市写死在 `config.h`，功能少、代码短，适合先点亮屏幕、看懂驱动 |
| **`ClockWeatherSetup`** | 完整版（推荐） | 屏幕向导配 WiFi / 省份城市（存 Flash）；满屏大时钟 ⇄ 时间+天气看板；三天预报带降水概率条 |
| `ScreenTest` | 硬件自检 | 纯 Arduino_GFX：刷色、画格子、测触摸，用来确认接线/时序/PSRAM 是否正确 |

两个版本共用同一套板级定义思路（`board_pins.h`）和同一个 GT911 触摸驱动。

## 硬件

* 开发板：JC8048W550 / **ESP32-S3-WROOM-1 N16R8**（16MB Flash + 8MB OPI PSRAM）
* 屏幕：5 寸 800×480 IPS，**ST7262 RGB 接口**（PCLK=GPIO42，背光=GPIO2）
* 触摸：**GT911** 电容触摸（I2C：SDA=19 / SCL=20，RST=38，INT=18）

## 烧录（Arduino IDE）

工具 → 开发板选 **ESP32S3 Dev Module**，然后：

| 选项 | 值 |
|---|---|
| PSRAM | **OPI PSRAM**（必须，否则 750KB 帧缓冲分配失败） |
| Flash Size | 16MB |
| 分区表 | app3M_fat9M_16MB |
| Flash Mode | QIO 80MHz |
| USB CDC On Boot | default |
| 上传速度 | 921600 |

依赖库：`lvgl@8.4.0`。

* **ClockWeatherSetup（v2.3 起）** 直接调 ESP-IDF 的 `esp_lcd_new_rgb_panel()`，
  **不再需要 Arduino_GFX**，也不用给库打补丁；固件还小了约 12KB。
* **ClockWeather / ScreenTest** 仍然用 `GFX Library for Arduino@1.6.8`，
  且需要一行补丁（`_panel_handle` private → public），
  `tools/setup_build_env.sh` 或 `tools/patch_gfx.ps1` 会自动改。

细的坑（时序、PSRAM、花屏、亮度、触摸坐标基准）都写在各自的 README 里：
[`ClockWeatherSetup/README.md`](ClockWeatherSetup/README.md) 最完整。

## 目录

```
ClockWeather/          最简版（26 个文件）
ClockWeatherSetup/     完整版：设置向导 + 大时钟 + 看板 + 天气
ScreenTest/            硬件自检
tools/                 构建与资源生成脚本
  build.sh              命令行编译校验（需要先跑 setup_build_env.sh）
  setup_build_env.sh    装 arduino-cli + ESP32 核心 + 库，全在 tools/.build（约 6.4GB）
  gen_fonts.sh          用 lv_font_conv 从 tools/fonts/*.otf 生成 LVGL 字库
  gen_regions.js        生成全国省市数据（region_data.cpp）
  gen_preview.js        生成效果示意图 preview.svg
  patch_gfx.js/.ps1     给 Arduino_GFX 打 _panel_handle 补丁
  verified-build.log    每次改动后的编译验证记录
```

## 已验证到什么程度

* **编译**：`tools/build.sh` 命令行完整编译，三个 sketch 全部 **0 error / 0 warning**；
  `ClockWeatherSetup` 占 Flash 62%、静态 RAM 50%（数字见 `tools/verified-build.log`）。
* **实板**：屏幕点亮、触摸坐标、WiFi/NTP、天气拉取、设置向导、大时钟/看板切换
  都在这块 JC8048W550 上跑过；期间踩过的坑（GT911 寄存器基准、PSRAM 带宽导致的
  左边拉丝、背光只能开不能调）都记在 README 和 `verified-build.log` 里。

## 数据来源

* 天气：[Open-Meteo](https://open-meteo.com/)（免 API Key）
* 行政区划坐标：阿里 [DataV.GeoAtlas](https://datav.aliyun.com/portal/school/atlas/area_selector)
* 图形库：[LVGL 8.4](https://lvgl.io/)；显示驱动：ESP-IDF `esp_lcd`（ClockWeatherSetup v2.3+）/
  [Arduino_GFX](https://github.com/moononournation/Arduino_GFX)（ClockWeather / ScreenTest）
* 字体：[Noto Sans SC](https://github.com/notofonts/noto-cjk)（SIL Open Font License 1.1）
