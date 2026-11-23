/**
 * ============================================================================
 *  board_pins.h  —  JC8048W550 / ESP32-S3-WROOM-1 N16R8 板级定义
 *
 *  这块板屏幕是 ST7262 驱动、RGB565 并口（16 位色），触摸是 GT911 电容屏。
 *
 *  ⚠ 最容易踩的坑：PCLK 是 GPIO42，不是 GPIO12！
 *     GPIO12/13/11/10 是 TF 卡（SD）的 SCK/MISO/MOSI/CS。
 *
 *  引脚来源（交叉核对过三份）：
 *    - Arduino_GFX issue #472 用户实测引脚表
 *    - esp32-smartdisplay 的 esp32-8048S050C 板级定义
 *    - MayysterUFTD/ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ 板级头文件
 * ============================================================================
 */
#pragma once

/* ---------------------------- 屏幕分辨率 ---------------------------- */
#define LCD_WIDTH           800
#define LCD_HEIGHT          480

/* ---------------------------- RGB 并口时序 --------------------------- */
#define PIN_LCD_DE          40
#define PIN_LCD_VSYNC       41
#define PIN_LCD_HSYNC       39
#define PIN_LCD_PCLK        42

/* 红色 R0~R4 */
#define PIN_LCD_R0          45
#define PIN_LCD_R1          48
#define PIN_LCD_R2          47
#define PIN_LCD_R3          21
#define PIN_LCD_R4          14

/* 绿色 G0~G5 */
#define PIN_LCD_G0          5
#define PIN_LCD_G1          6
#define PIN_LCD_G2          7
#define PIN_LCD_G3          15
#define PIN_LCD_G4          16
#define PIN_LCD_G5          4

/* 蓝色 B0~B4 */
#define PIN_LCD_B0          8
#define PIN_LCD_B1          3
#define PIN_LCD_B2          46
#define PIN_LCD_B3          9
#define PIN_LCD_B4          1

/* 背光：GPIO2，高电平点亮。
 * 如果你的屏完全不亮，可以先单独测这一路：
 *     pinMode(2, OUTPUT); digitalWrite(2, HIGH);
 *
 * ★ 2025 实板结论：这块屏的背光是「使能型」，不能 PWM 调光。
 *   实测（同一块 JC8048W550，背光 PWM 20kHz、8 位）：
 *       100%  -> 正常亮度
 *       40 / 60 / 80%  -> 全黑（一点不亮，不是「变暗」）
 *       10~25% -> 全黑
 *   也就是说 GPIO2 大概率接在背光升压/驱动芯片的 EN 脚上，
 *   20kHz 的方波它跟不上（软启动来不及），只有一直拉高才工作。
 *   所以代码里没有亮度调节：display_init() 里把 GPIO2 拉满就完事，
 *   屏幕永远是最亮的，也就不可能被自己调黑。
 *
 *   要再挑战调光的话，思路是降 PWM 频率（频率越低，驱动芯片越有时间
 *   跟着通断，但可能看到轻微闪烁）：把 display.cpp 里的 BL_LEDC_FREQ
 *   从 20000 改成 500~2000 再试。
 */
#define PIN_LCD_BL          2

/* ---------------------------- ST7262 时序参数 ------------------------
 * 这些值来自官方板级定义，实测可用。如果画面整体偏移/抖动，微调前后沿。
 * ------------------------------------------------------------------- */
#define LCD_HSYNC_POLARITY      0
#define LCD_HSYNC_FRONT_PORCH   8
#define LCD_HSYNC_PULSE_WIDTH   4
#define LCD_HSYNC_BACK_PORCH    8

#define LCD_VSYNC_POLARITY      0
#define LCD_VSYNC_FRONT_PORCH   8
#define LCD_VSYNC_PULSE_WIDTH   4
#define LCD_VSYNC_BACK_PORCH    8

#define LCD_PCLK_ACTIVE_NEG     1        /* 像素时钟下降沿采样 */

/* ---------------------------- 性能 / 稳定性参数 ----------------------
 * 这两项是画面闪烁的关键，别照抄网上别的板子的值。
 *
 * LCD_PREFER_SPEED —— 像素时钟（PCLK）。
 *   ⚠ 必须是 16000000！这是 Arduino_GFX 库里自带的 JC8048W550 板级定义
 *     （examples/PDQgraphicstest/Arduino_GFX_dev_device.h 第 297 行）用的值，
 *     是本项目所有参数里唯一有「同款板子实测可出图」背书的数字。
 *
 *     为什么不能调低：刷新率 = PCLK / ((800+4+8+8) * (480+4+8+8))
 *         16MHz -> 39.0 Hz   （作者实测可用）
 *         12MHz -> 29.3 Hz   （实测白屏！低于面板最低刷新率）
 *     所以画面闪烁不要靠降 PCLK 解决，会直接黑/白屏。
 *
 * LCD_BOUNCE_BUFFER_PX —— DMA 搬运缓冲（单位：像素）。
 *
 *   这是修「画面左右抖动 / 横向错位」的关键参数。
 *
 *   原理：默认（0）时 RGB 的 DMA 直接从 PSRAM 逐行取数据。CPU 在跑 LVGL、
 *   WiFi 在收发时都会占用 PSRAM 带宽，某几行没及时供上，画面就会横向错开
 *   几像素，看起来像在左右动 / 闪。
 *   设成非 0 后，DMA 改从内部 RAM 的搬运缓冲读，由中断提前把数据从 PSRAM
 *   搬过来。内部 RAM 不跟外设抢带宽，抖动就没了。
 *
 *   代价：内部 RAM。ESP-IDF 会分配 RGB_LCD_PANEL_BOUNCE_BUF_NUM(=2) 份，
 *        所以实际占用 = LCD_BOUNCE_BUFFER_PX * 2 * 2 字节。
 *         10 行 = 800*10 像素 = 16KB/份，共 32KB
 *         20 行 = 800*20 像素 = 32KB/份，共 64KB
 *       还抖就往上加（30、40 行）。
 *
 *   ⚠ 约束（来自 ESP-IDF，违反会 ESP_ERROR_CHECK 崩溃重启）：
 *       帧缓冲字节数 % 搬运缓冲字节数 == 0
 *       800*480*2 = 768000
 *       768000 % (800*10*2) = 768000 % 16000 = 0  ✓
 *       取 LCD_WIDTH 的整数倍、且倍数能整除 480 即可（480/16=30 ✓）。
 *
 *  ★ 2025 追加：从 10 行加到 16 行。
 *     实板反馈「偶尔屏幕左边有很轻微的拉丝，断电重启才消失」——
 *     这是典型的 DMA 取数不及时（underrun）：CPU 往 PSRAM 帧缓冲写像素
 *     （分块渲染的 memcpy + cache 写回）时和 RGB DMA 抢带宽，
 *     DMA 某几行没供上，行首就会重复上一行的像素，看起来就是「左边拉丝」。
 *     搬运缓冲越大，DMA 能扛住的突发就越长，拉丝概率越低。
 *     代价是内部 RAM：16 行 = 800*16*2*2 = 51.2KB（10 行是 32KB）。
 *
 *  ★ 2025-11 追加：先加到 24 行（错），再退回 16/12 行（对）—— 关键是 32KB 缓存
 *
 *     现象（用户拍照确认）：大数字左边出现一排 1px 细的短横线，
 *     横线之间的间距**正好等于搬运缓冲的行数**（16 行时 16 行一条，
 *     改成 24 行后就是 24 行一条）。说明这些线是「每块搬运缓冲的开头」出了问题。
 *
 *     原理（看 IDF 的 esp_lcd_panel_rgb.c）：搬运缓冲是两个，DMA 读 A 的
 *     同时中断往 B 里 memcpy；如果那次 memcpy 没在 DMA 读完 A 之前完成，
 *     DMA 就会读到 B 里还没被覆盖的旧内容 —— 也就是每块**开头几个像素**
 *     是上一块的数据。字体竖笔在弯曲处新旧内容不一样，所以只看得到一小段
 *     短线，而且都在**屏幕左边**（行首）。
 *
 *     ⚠⚠ 这里有个坑，也是本项目曾经搞反方向的地方：
 *        IDF 每搬完一块，会调 Cache_Start_DCache_Preload() 把**下一块预读进
 *        L1 数据缓存**。ESP32-S3 的 L1 D-cache 只有 **32KB**
 *        （CONFIG_ESP32S3_DATA_CACHE_SIZE=0x8000，见 sdkconfig）。
 *          12 行 = 19.2KB  预读装得下，留 12.8KB 给别的数据  ✓
 *          16 行 = 25.6KB  勉强装下，只剩 6.4KB            ○（旧值）
 *          24 行 = 38.4KB  **装不下** → 预读不但没用，还把缓存里
 *                           其他东西全冲掉，下一块 memcpy 全落回 PSRAM
 *                           慢读 → 每块都迟到 → 反而更糟       ✗
 *       实测反馈也吻合：24 行那一版「刷机后马上就有」，16 行那版「用久了才
 *       偶尔有」。所以**别再加行数**，要加余量请降像素时钟（见下）。
 *
 *     一次 memcpy 的期限 = 一块的扫描时间 = 行数 × 800px / PCLK。
 *     12 行 @16MHz ≈ 600µs，而 19.2KB 的 PSRAM 拷贝 ≈ 480µs —— 余量 ~120µs；
 *     命中缓存时拷贝只要几十 µs，余量很大。
 *
 *     ⚠ 内部 RAM：12 行 = 800*12*2*2 = 38.4KB（两份，各 19.2KB）。
 *     display_init() 里做了「先试 LCD_BOUNCE_LINES，失败退 12、再退 10」，
 *     内存不够绝不会变砖，串口会打印实际用了几行。
 *
 *     还是偶尔有？**不要动这个数**，去降像素时钟：
 *       串口发 pclk 14（14.55MHz，34Hz，带宽 −9%），跑一天看；
 *       稳了就把 LCD_PREFER_SPEED 改成 14545454 固化。
 *     降 PCLK 是唯一能真正加宽「拷贝期限」的软件手段（12MHz/29Hz 会白屏）。
 */
#define LCD_PREFER_SPEED        16000000

/* 搬运缓冲行数：必须是 LCD_WIDTH 的整数倍、能整除 480，且份数（480/行数）为偶数。
 *   12 行 -> 40 份 ✓    10 行 -> 48 份 ✓    16 行 -> 30 份 ✓
 * ※ 受 32KB L1 数据缓存限制，**不要超过 16 行**，见上面的说明。 */
#define LCD_BOUNCE_LINES        12
#define LCD_BOUNCE_BUFFER_PX    (LCD_WIDTH * LCD_BOUNCE_LINES)

/* ---------------------- 渲染方式（画面闪不闪的关键）------------------
 * 0 = 分块渲染（默认，推荐）
 *     LVGL 先在内部 RAM 的 64KB 小块里画好，再整块 memcpy 进 PSRAM 帧缓冲。
 *     内部 RAM 比 PSRAM 快得多，而且帧缓冲被改写的时间窗口很短 ——
 *     滚动列表、按软键盘这种大面积重绘不会明显撕裂（闪）。
 *     代价：多占 64KB 内部 RAM。
 *
 * 1 = 直接渲染
 *     LVGL 直接往 PSRAM 帧缓冲里画。省 64KB 内部 RAM，但 RGB DMA 正在扫
 *     同一块内存，「一边画一边显示」→ 大面积重绘会明显闪；
 *     而且每次刷新还要把整屏 750KB 写回 PSRAM，和 DMA 抢带宽。
 *     ClockWeather（只刷新时钟数字）用这个没问题，
 *     ClockWeatherSetup 有列表滚动和软键盘，务必用 0。
 *
 * 内部 RAM 不够时分块渲染会自动退回直接渲染，并打印一行日志。
 * ------------------------------------------------------------------ */
#define LCD_DIRECT_RENDER       0

/* ---------------------- RGB DMA 自愈（防止永久花屏）------------------
 * 现象：跑一段时间后随机出现局部花屏 / 拉丝，而且**一直不复原**，
 *       只有按 RST 重启才好，重启后过一阵又会出现。
 *
 * 原因：ESP32-S3 的 RGB GDMA 会「永久失步」(permanent desync)。
 *       ESP-IDF 源码里的原文：
 *         "reset the GDMA channel every VBlank to stop permanent desyncs
 *          from happening."
 *       官方的开关是 CONFIG_LCD_RGB_RESTART_IN_VSYNC，但那是编译期选项，
 *       Arduino 用的 ESP-IDF 是预编译好的，改不了。
 *
 * 对策：改成运行时定期调用 esp_lcd_rgb_panel_restart()。
 *       这个函数只设一个标志，真正的 DMA 复位在下一个 VSYNC 中断里做，
 *       所以不会打断正在输出的这一帧。失步后最多 LCD_DMA_RESTART_INTERVAL_MS
 *       就会自愈，不用再重启板子。
 *
 * ⚠ 启用前必须先给 Arduino_GFX 打一行补丁，把 panel 句柄从 private 放开：
 *      文件： libraries/GFX_Library_for_Arduino/src/databus/Arduino_ESP32RGBPanel.h
 *      找到： private:            （大约第 125 行，_panel_handle 所在区段）
 *      改成： public:
 *   没打补丁会编译报错「'_panel_handle' is private within this context」。
 *   详细步骤见 README 第十节的「让花屏自愈」。
 *
 * 间隔：太长则花屏要等很久才恢复；太短则可能周期性看到轻微撕裂。
 *       5000ms 是折中值。真机上如果发现每 5 秒有一次可察觉的闪动，
 *       把这个值调大到 15000 或 30000。
 * ------------------------------------------------------------------ */
#define LCD_DMA_RESTART_FIX             1
#define LCD_DMA_RESTART_INTERVAL_MS     5000

/* ---------------------------- GT911 电容触摸 -------------------------
 * I2C 挂在 GPIO19(SDA) / GPIO20(SCL)，复位 GPIO38，中断 GPIO18。
 * I2C 地址由复位时 INT 电平决定：INT 拉低 -> 0x5D，拉高 -> 0x14。
 * 本板 INT 默认被下拉，所以通常是 0x5D（程序里会自动探测两个地址）。
 * ------------------------------------------------------------------- */
#define PIN_TOUCH_SDA       19
#define PIN_TOUCH_SCL       20
#define PIN_TOUCH_RST       38
#define PIN_TOUCH_INT       18
#define TOUCH_I2C_FREQ      400000

/* 触摸方向微调：如果触摸位置和手指位置对不上，改这里（0/1 互换）
 *   TOUCH_SWAP_XY  : 交换 X/Y（屏幕被当成竖屏配置时常见）
 *   TOUCH_MIRROR_X : 左右镜像
 *   TOUCH_MIRROR_Y : 上下镜像
 */
#define TOUCH_SWAP_XY       0
#define TOUCH_MIRROR_X      0
#define TOUCH_MIRROR_Y      0
