# Windows 11 从零开始烧录教程

这份教程假设你**什么都没装过**。全程大约 30 分钟，其中大半时间是在等下载。

> 只想看速查表的话，直接跳到最后的「九、完整速查清单」。

---

## 〇、先明确一件事：不需要 TF 卡

**ESP32-S3 只能从它内部那颗 16MB Flash 启动**，TF 卡是给程序存数据用的，
和固件没关系。烧录是**插 USB 线**，用软件写进去。

这块板的 TF 卡槽空着就行，本工程也用不到它。

---

## 一、要装的东西（共 3 个）

| # | 软件 | 干什么用的 | 大小 |
|---|---|---|---|
| 1 | **Arduino IDE 2.x** | 写代码、编译、点上传 | ~200 MB |
| 2 | **ESP32 开发板支持包** | 让 IDE 认识 ESP32-S3（在 IDE 里装，不用单独下载） | ~1 GB |
| 3 | **CH340 驱动** | 让 Windows 认得出板子的串口（Win11 有可能已自动装好） | ~1 MB |

另外还要在 IDE 里装 **2 个库**（LVGL 和 Arduino_GFX），见第五步。

---

## 二、装 Arduino IDE

### 下载

打开 <https://www.arduino.cc/en/software>

选 **「Arduino IDE 2.x.x」** 下面的 **Windows Win 10 and newer, 64 bits**，
下载那个 `.exe` 安装包。

> ⚠️ **不要装 Microsoft Store 版本。** Store 版跑在沙箱里，路径受限，
> 编译 ESP32 时经常报莫名其妙的权限/路径错误。一定要用官网的 `.exe`。

### 安装

一路「下一步」就行。**安装路径建议用默认的**，或者至少保证是纯英文路径。

装完第一次打开，Windows 可能弹防火墙提示，允许即可。

### 顺便调两个设置（省事）

`文件 → 首选项`（File → Preferences）：

1. **显示详细输出** → 两个都勾上（`编译` 和 `上传`）。
   出问题时能看到真实报错，不然只显示一句「编译出错」。
2. **编辑器语言** 可以设成中文。

---

## 三、装 CH340 驱动

这块板上的 USB 转串口芯片把 ESP32 的串口桥接成 USB。Win11 通常会自动装好驱动，
但相当一部分批次需要手动装。

### 怎么判断要不要装

1. **先插上板子**（用能传数据的 USB 线，插板子的 Type-C 口）
2. 右键开始菜单 → **设备管理器**
3. 找 **端口 (COM 和 LPT)** 这一项：
   * **看得到 `USB-SERIAL CH340 (COMx)`** → 已经好了，跳过这一步
   * **看不到「端口」这一项**，或者看到一个带**黄色感叹号**的未知设备 → 需要装驱动

### 装驱动

WCH 官网下载页：<https://www.wch.cn/downloads/CH341SER_EXE.html>

下载 `CH341SER.EXE` → 运行 → 点 **安装** → 拔插一次板子。

> 如果设备管理器里显示的是 **CP210x** 而不是 CH340，那说明你这批用的是
> Silicon Labs 的芯片，去这里下：<https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers>
> 具体是哪种，看板子背面的黑色小芯片丝印。

装好后设备管理器里应该能看到 `USB-SERIAL CH340 (COM3)` 这种字样，
**记住这个 COM 号**，第八步要用。

---

## 四、装 ESP32 开发板支持包

Arduino IDE 默认只认识 Arduino 自己的板子，要手动加 ESP32 的支持。

### 1. 添加开发板管理器网址

`文件 → 首选项`（File → Preferences），找到最下面的
**「附加开发板管理器网址」**（Additional boards manager URLs），把下面这行粘进去：

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

如果框里已经有别的内容，就在末尾**加个逗号再粘**，不用删掉原来的。

点确定。

### 2. 安装

`工具 → 开发板 → 开发板管理器`（Tools → Board → Boards Manager）

在搜索框输入 **esp32**，找到 **「esp32」by Espressif Systems**，点**安装**。

> ⚠️ 这一步要下载约 **1GB**，视网速可能要 **10~30 分钟**，期间界面可能看起来卡住，
> 不要关掉窗口。国内网络如果特别慢，可以挂代理。

### 3. 选哪个版本

装**最新的 3.x 版本**即可（本工程验证用的是 **3.3.11**）。

> 如果你之前装过 2.x 版本，代码也能编译（背光部分做了版本兼容），
> 但推荐 3.x。版本可以在开发板管理器里用下拉框切换。

---

## 五、装两个库

`工具 → 管理库`（Tools → Manage Libraries）

### 1. LVGL —— 版本必须选对！

搜索 `lvgl`，找到 **LVGL** 这个库。

> ⚠️⚠️ **点开版本下拉框，选 8.4.0，不要装最新的 9.x！**
>
> 库管理器默认选中最新版（9.x），直接点安装会装错。这个工程用的是 LVGL v8 的 API，
> 装成 9.x 会报一堆 `lv_disp_drv_t 未定义` 之类的错。
>
> 为什么不用 v9：中文字库是用 `lv_font_conv` 生成的，它只生成 LVGL v8 格式，
> v9 的字体结构不一样。

### 2. Arduino_GFX

搜索 `GFX Library for Arduino`（作者 **Moon On Our Nation**），装最新版即可。

> 注意别和另一个叫 `Arduino_GFX` 的库搞混，认准作者名。本工程验证用的是 1.6.8。

---

## 六、放 lv_conf.h（最容易漏的一步）

工程里有个文件 `ClockWeather/lv_conf.h`，它是 LVGL 的配置文件。

**它不能留在工程目录里**，必须复制到 Arduino 的 `libraries` 文件夹下，
和 `lvgl` 文件夹**平级**：

```
C:\Users\你的用户名\Documents\Arduino\libraries\
    ├── lvgl\                       ← 第五步装的
    ├── lv_conf.h                   ← 把工程里那个复制到这里 ★
    ├── GFX_Library_for_Arduino\    ← 第五步装的
    └── ...
```

**怎么快速找到这个文件夹**：Arduino IDE 里 `文件 → 首选项`，看 **「项目文件夹位置」**
（Sketchbook location），那个路径下面的 `libraries` 就是了。

漏了这一步，编译会报：

```
fatal error: lv_conf.h: No such file or directory
```

---

## 七、打开工程、改配置

### 1. 打开

把 `ClockWeather` 整个文件夹拷到你电脑上（**放纯英文路径**，
别放桌面或 OneDrive 里，见第十节坑 2），然后双击里面的 `ClockWeather.ino`。

> 注意：Arduino IDE 要求**文件夹名和 .ino 文件名一致**（都是 `ClockWeather`），
> 改名的话两个要一起改。

### 2. 改 config.h

工程里给的是**模板** `config.example.h`，先复制成 `config.h` 再改
（这样你填的 WiFi 密码不会被打包进 git 提交）：

```cmd
cd /d C:\Users\MinG\Documents\data\ClockWeather
copy config.example.h config.h
```

然后在 Arduino IDE 里点开 `config.h` 标签页，至少改这两处：

```cpp
#define WIFI_SSID     "你家的WiFi名"      // ⚠ 只支持 2.4GHz，不支持 5G
#define WIFI_PASSWORD "你家的WiFi密码"

#define CITY_NAME  "北京"
#define CITY_LAT   39.9042f      // 纬度
#define CITY_LON   116.4074f     // 经度
```

* 经纬度去 <https://www.latlong.net/> 搜你的城市就能拿到。
* 懒人法：把 `CITY_LAT` 和 `CITY_LON` 都写成 `0`，程序会自动用城市名去查坐标。

---

## 八、开发板设置 + 上传

### 1. 选开发板

`工具 → 开发板 → esp32 → ESP32S3 Dev Module`

### 2. 按下表设置（`工具` 菜单里逐项改）

| 菜单项（中文 / English） | 设成 | 为什么 |
|---|---|---|
| **PSRAM** | **OPI PSRAM** | ★ 必须！屏幕帧缓冲 750KB 要放这 |
| **Flash Size** | **16MB (128Mb)** | |
| **Flash Mode** | **QIO 80MHz** | |
| **Partition Scheme** | **16M Flash (3MB APP/9.9MB FATFS)** | 程序含中文字库约 1.9MB |
| **USB CDC On Boot** | **Disabled** | ★ GPIO19/20 被触摸占了，见下 |
| **CPU Frequency** | 240MHz (WiFi) | |
| **Upload Speed** | 921600 | 失败就降到 460800 |
| **Arduino Runs On** | Core 1 | |

> **为什么 USB CDC On Boot 要 Disabled**：
> ESP32-S3 的原生 USB 用的是 GPIO19/20，而这块板把这两个脚接给了触摸屏的 I2C。
> 如果开了 USB CDC，USB 外设会抢占这两个脚，**触摸就失灵了**。
> 所以串口日志走板子上的 USB 转串口芯片（也就是你第三步装驱动的那个口）。

### 3. 选端口

`工具 → 端口`，选第三步看到的那个 **COM 口**（比如 COM3）。

> 不确定选哪个？把板子拔下来，看菜单里少了哪个 COM；再插上，看多了哪个。

### 4. 点上传

点左上角的 **→（上传）** 按钮，或者 `项目 → 上传`。

**第一次编译会比较慢**（要编译 4MB 多的中文字库源码），大概 2~5 分钟，属正常现象。
之后有缓存就快了。

编译成功后底部会显示：

```
Sketch uses 1904766 bytes (60%) of program storage space.
Global variables use 99196 bytes (30%) of dynamic memory.
```

然后自动开始烧录，看到 `Hash of data verified.` 和 `Hard resetting via RTS pin...` 就成功了。

---

## 九、确认成功

点右上角的 **🔍（串口监视器）**，右下角波特率选 **115200**。

正常的话会依次打印：

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

屏幕同时会亮起来显示时钟和天气。

---

## 十、常见坑（Windows 特有）

### 坑 1：`工具 → 端口` 是灰的 / 里面啥都没有
* CH340 驱动没装 → 回到第三步
* 换一根 **能传数据** 的 USB 线（有些线只供电不传数据，这是最坑的一类问题）
* 板子没进下载模式 → 见坑 3

### 坑 2：路径里有中文、空格或 OneDrive
编译 ESP32 对路径比较敏感。**把 `ClockWeather` 文件夹放到 `D:\ESP32\ClockWeather`**
这种纯英文、无空格、不在 OneDrive 同步目录下的位置。
工程放在 `桌面`、`文档`（可能被 OneDrive 接管）、或中文用户名路径下，都容易出问题。

### 坑 3：点了上传没反应 / 一直卡在 `Connecting...`
手动强制进下载模式：

> **按住 BOOT 键不放 → 点一下 RST 键 → 松开 RST → 再松开 BOOT**

然后再点上传。另外把 **Upload Speed 降到 460800** 试试。

### 坑 4：编译报 `lv_conf.h: No such file or directory`
第六步没做，`lv_conf.h` 没放到 `Arduino/libraries/` 下。

### 坑 5：编译报一大堆 LVGL 错误（`lv_disp_drv_t` 未定义等）
LVGL 装成 9.x 了。库管理器 → 搜 lvgl → 版本下拉框选 **8.4.0** → 重新安装。

### 坑 6：编译报 `Sketch too big` 或分区不够
Partition Scheme 没选 `16M Flash (3MB APP/9.9MB FATFS)`。

### 坑 7：屏幕黑的 / 颜色不对 / 花屏
这类是硬件或时序问题，和 Windows 无关。对照 `README.md` 第七节：
* 全黑 → 检查 PSRAM 有没有选 OPI
* 颜色发蓝/发黄（红蓝颠倒）→ 把 `lv_conf.h` 里 `LV_COLOR_16_SWAP` 改成 `1`
* 横纹闪烁 → 把 `board_pins.h` 里 `LCD_PREFER_SPEED` 从 `16000000` 降到 `12000000`

---

## 十一、完整速查清单

```
□ 1. 装 Arduino IDE 2.x（官网 .exe，不要 Store 版）
□ 2. 插板子 → 设备管理器看有没有 CH340 的 COM 口，没有就装驱动
□ 3. 首选项 → 附加开发板管理器网址 加 https://espressif.github.io/arduino-esp32/package_esp32_index.json
□ 4. 开发板管理器 → 搜 esp32 → 装 Espressif Systems 的 esp32（约 1GB，等十几分钟）
□ 5. 管理库 → 装 lvgl【版本必须选 8.4.0】
□ 6. 管理库 → 装 GFX Library for Arduino (Moon On Our Nation)
□ 7. 把 ClockWeather/lv_conf.h 复制到 Arduino/libraries/ 下  ← 最容易漏
□ 8. 工程放到纯英文路径，打开 ClockWeather.ino
□ 9. 改 config.h 里的 WiFi 名字、密码、城市经纬度
□ 10. 开发板选 ESP32S3 Dev Module
□ 11. 工具菜单设置：
        PSRAM            = OPI PSRAM          ★必须
        Flash Size       = 16MB (128Mb)
        Flash Mode       = QIO 80MHz
        Partition Scheme = 16M Flash (3MB APP/9.9MB FATFS)
        USB CDC On Boot  = Disabled           ★必须
        Upload Speed     = 921600（失败改 460800）
□ 12. 工具 → 端口 → 选 CH340 那个 COM
□ 13. 点 → 上传，等第一次编译 2~5 分钟
□ 14. 串口监视器 115200 看日志，屏幕亮起
```

---

## 十二、还是不行怎么办

把**串口监视器的完整输出**（或编译时的红色报错）复制下来发我，
连同这两条信息：

1. 设备管理器里「端口」那一项显示的是什么
2. 上传失败的话，是卡在哪一步（`Compiling` / `Connecting` / `Writing`）

有这两条基本就能定位问题。
