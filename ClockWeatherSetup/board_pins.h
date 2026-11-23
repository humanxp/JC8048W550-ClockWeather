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
 *   基线值 16000000 来自 Arduino_GFX 库自带的 JC8048W550 板级定义
 *   （examples/PDQgraphicstest/Arduino_GFX_dev_device.h 第 297 行），是同款板子
 *   实测可出图的数字。
 *
 *   ★ 2025-11 实板结论：**这块屏只认 16MHz，降一档就废**。
 *      试过 14545454Hz（160MHz/11 = 14.55MHz，35.5Hz）：整屏变成蓝/白/绿/红
 *      纯色块一直跳，完全没有图像 —— 和「白屏」是同一类故障（面板不认这个
 *      像素时钟，锁不住画面），只是这块 ST7262 的表现是打色块而不是白屏。
 *      发 `pclk 16` 立刻恢复正常。
 *      ⚠ 所以：**不要靠降 PCLK 找余量**（交接文档里「pclk 14 = 34.1Hz 可用」
 *      那一条对这块板是错的，别照抄）。余量只能从搬运缓冲行数那边赚。
 *      12.00MHz（29.3Hz）更是必然白屏。
 *
 * LCD_BOUNCE_LINES —— DMA 搬运缓冲（bounce buffer）的行数。
 *
 *   这是最影响「屏幕左边一条一条短横线」的参数，也是内部 RAM 的最大开销。
 *
 *   原理：设成非 0 后，RGB DMA 不再直接啃 PSRAM 帧缓冲，而是从**内部 RAM**
 *   的两块搬运缓冲轮流读。中断在 DMA 读 A 的同时往 B 里 memcpy 下一块。
 *   内部 RAM 不和 WiFi / CPU 抢 PSRAM 带宽，所以左右抖动、横向错位会消失。
 *   代价是：DMA 必须一直「跑在 memcpy 前面」——一旦某次 memcpy 没在
 *   DMA 读完当前块之前完成，DMA 就会把 B 里**还没被覆盖的旧内容**打出去，
 *   而旧内容正好是每块开头（= 屏幕左边）几行。这就是那些短横线。
 *
 *   两条硬约束（都是 ESP-IDF 的，违反会断言/崩溃）：
 *
 *   ① 帧缓冲字节数 % 搬运缓冲字节数 == 0
 *        800*480*2 = 768000；768000 % (800*行数*2) == 0  →  480 % 行数 == 0
 *        合法值：1 2 3 4 5 6 8 10 12 15 16 20 24 30 32 40 48 60 …
 *
 *   ② 一块搬运缓冲的「源 + 目的」都要装得进 L1 D-cache
 *      （本板 CONFIG_ESP32S3_DATA_CACHE_SIZE = 0x8000，即 **32KB**，
 *      cache line 32B —— 已在构建产物 sdkconfig.h 里核实过，不是猜的）。
 *
 *      这里最容易算错的一点：IDF 在 memcpy 前会先 Cache_Start_DCache_Preload()
 *      把源区预读进 L1，但 memcpy 的**目的区**（内部 RAM 的搬运缓冲）同样要占
 *      cache line —— 所以真正的工作集是「源 + 目的」= 2 × 一块字节数。
 *          6 行：源 9.6KB + 目的 9.6KB = 19.2KB   ★ 选它
 *          8 行：25.6KB                            ✓ 还剩 6.4KB
 *         10 行：32KB                              ○ 正好塞满，一点富余都没有
 *         12 行：38.4KB                            ✗ 抖！预读进来的行被目的写入挤掉，
 *                                                      每次都要回头重读 PSRAM
 *         16 行：51.2KB                            ✗ 更糟
 *      （v1.8 用 24 行 = 76.8KB，属于最坏的一档。）
 *
 *   ★ 2025-11 实测（串口 bench 命令，逐档测 PSRAM -> 内部 RAM 的热拷贝，
 *     本板只能跑 16MHz，所以期限按 16MHz 算）：
 *       行数  一份字节   期限    热拷贝(平均/最慢)   余量    判决
 *         4     6400      200µs    28 / 124µs       +38%   ✓ 但每帧 120 块
 *         6     9600      300µs    28 /  68µs       +77%   ★ 选它（CPU 也最省）
 *         8    12800      400µs    81 / 354µs       +11%   ○ 可用，余量偏薄
 *        10    16000      500µs    61 / 246µs       +50%   ○ 临界（缓存刚好塞满）
 *        12    19200      600µs   366 / 540µs       +10%   ✗ 缓存抖动
 *        16    25600      800µs   774 / 963µs       -20%   ✗ 超期限
 *      「期限」= 面板扫完这一块的时间；拷贝超过它，每块开头就会出现残影。
 *      6 行的热拷贝只要 28µs，是期限的 1/10 —— 余量最宽，内部 RAM 也只花 19.2KB。
 *
 *   ★ 关于余量：PCLK 不能降（见上），所以余量只能靠「块小一点」来赚。
 *      实测热余量：6 行 +77%、8 行 +11%、4 行 +38%。默认 6 行。
 *      真机上如果还看到线，串口 `bounce 4` 再退一档（会自动重启生效）。
 *
 *   运行时对比（不用重新编译）：
 *        bounce 8       存进 NVS 并重启，开机就按 8 行建面板
 *        bounce auto    清掉设置，回到本文件的默认值
 *        bench          实测每块的 PSRAM 拷贝耗时 vs DMA 期限，直接看余量
 */
#define LCD_PREFER_SPEED        16000000

/* 搬运缓冲行数：480 % 16 == 0 ✓，一份 25.6KB（两份 51.2KB 内部 RAM）。
 *
 * ★ 2025-11 最终取值（用 stats 扫出来的，见下面「实测」）：
 *   短线（数字弧边上那一小段 16px 凸起）的成因是**每块搬运缓冲的边界**上
 *   LCD FIFO 里残留着 16 个像素，于是边界那一行整体左移约 16px。所以
 *   **块越大、边界越少、短线越稀**：
 *        6 行 → 每帧 80 个边界（短线最密，但现在默认不是它）
 *       16 行 → 每帧 30 个边界  ★ 现默认
 *       24 行 → 每帧 20 个边界，但掉进缓存抖动（填充平均 572µs，会超期限）
 *   16 行是「短线最稀 + 填充从不超期限」的折中点。
 *
 *   运行时可以用串口现场对比：bounce 6 / 12 / 16 / 20 / 0（会自动重启），
 *   然后 stats 看填充超期限比例、肉眼看短线密度。 */
#define LCD_BOUNCE_LINES        16
#define LCD_BOUNCE_BUFFER_PX    (LCD_WIDTH * LCD_BOUNCE_LINES)

/* ---------------------- 搬运缓冲的 cache 预读提前几块 -----------------
 * 只在本工程「自己接管搬运缓冲填充」（display.cpp 的 no_fb + on_bounce_empty）
 * 时才有效。IDF 原生做法固定提前 1 块，且 num_fbs==0 时干脆不预读。
 *
 * 实测（16MHz / 6 行 = 一块 9600 字节，期限 300µs，每 15 秒约 5.4 万次填充）：
 *     提前 0 块：平均 90µs，超期限 309 次 = 0.57%
 *     提前 1 块：平均 73µs，超期限   1 次 = 0.002%   ★ 默认
 *     提前 2 块：平均 61µs，超期限  13 次 = 0.024%
 * 「超期限」= 中断里拷贝一块的时间超过 DMA 读完这一块的期限，超了就会出现
 * 「每块开头几个像素是旧内容」——也就是屏幕左边那些 1px 短线。
 * 1 块最好：2 块要同时占两份（19.2KB）L1，反而会被 LVGL 的 flush 挤掉一部分。
 * 运行时可用串口 prefetch 0/1/2 现场对比，stats 看比例。
 * -------------------------------------------------------------------- */
#define LCD_PREFETCH_BLOCKS     1

/* ---------------------- DMA 突发长度（dma_burst_size）------------------
 * GDMA 每次向 PSRAM/内部 RAM 要多少字节。默认 64（IDF 默认值）。
 *
 * ★ 2025-11 实板结论：屏幕左边那些短线，不是"拷贝太慢"，而是**每块搬运缓冲
 *   的边界上，LCD FIFO 里还残留着上一行的 16 个像素**，于是下一行开头会重复
 *   一点点（放大看就是数字弧边上每隔「块行数」一小段 16px 长的凸起）。
 *   → 块越大边界越少、短线越少；最彻底的是**根本不用搬运缓冲**（bounce 0），
 *     但那时 DMA 直读 PSRAM，PSRAM 一忙就整行位移（左右抖动）。
 *   → 所以治"抖"的办法是把每次读的粒度做大：burst 256 让 GDMA 一次拿 256B，
 *     减少请求次数 = 减少 FIFO 断流机会。
 *   合法值一般 16/32/64/128/256；本板实测 **256 不被 GDMA 接受**
 *   （esp_lcd_new_rgb_panel 返回 ESP_ERR_NOT_SUPPORTED），
 *   display_init() 会自动退到 64，不会变砖。
 */
#define LCD_DMA_BURST_SIZE      128

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
 * 结论先说：**本板不需要这个功能，默认关闭。**
 *
 * 背景：ESP32-S3 的 RGB GDMA 会「永久失步」(permanent desync)，表现为随机
 *   局部花屏 / 拉丝且一直不复原。ESP-IDF 源码里的注释是：
 *       "reset the GDMA channel every VBlank to stop permanent desyncs
 *        from happening."
 *   对应的开关就是 CONFIG_LCD_RGB_RESTART_IN_VSYNC。
 *
 * ★ 2025-11 核实：这个开关在 arduino-esp32 3.3.11 的预编译库里**已经是开的**。
 *   实际构建用的变体（PSRAM=opi + FlashMode=qio → qio_opi）的
 *   include/sdkconfig.h 里就是：
 *       #define CONFIG_LCD_RGB_RESTART_IN_VSYNC 1
 *   也就是说驱动**每一帧的 VSYNC 都会自己复位一次 GDMA**，早就把失步救了。
 *   v1.7 加的那套「每 5 秒手动 esp_lcd_rgb_panel_restart()」是白干的 ——
 *   而且每次手动 restart 都可能在帧中间插一次 DMA 复位，本身就是
 *   「偶尔闪一下 / 左边一条线」的可疑来源之一，所以现在默认关掉。
 *
 * 下面仍保留手动兜底代码：万一以后换成没开这个选项的 IDF 构建，
 * 把 LCD_DMA_RESTART_FIX 手动定成 1 就能恢复（编译时自动判定，见下）。
 *
 * ⚠ 一旦真的打开（fix=1，或串口 restart 命令 > 0），才需要给 Arduino_GFX
 *   打补丁把 _panel_handle 从 private 放开（display_set_pclk 也依赖它）：
 *      文件： libraries/GFX_Library_for_Arduino/src/databus/Arduino_ESP32RGBPanel.h
 *      找到： private:            （_panel_handle 所在区段，约第 125 行）
 *      改成： public:
 *   没打补丁会编译报错「'_panel_handle' is private within this context」。
 * ------------------------------------------------------------------ */
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

#ifndef LCD_DMA_RESTART_FIX
#  if defined(CONFIG_LCD_RGB_RESTART_IN_VSYNC) && CONFIG_LCD_RGB_RESTART_IN_VSYNC
#    define LCD_DMA_RESTART_FIX         0    /* 驱动每帧自己在 VSYNC 里复位，不用我们管 */
#  else
#    define LCD_DMA_RESTART_FIX         1    /* 老 IDF 构建才需要手动兜底 */
#  endif
#endif

/* 手动兜底的复位间隔（只在 LCD_DMA_RESTART_FIX=1 或串口 restart 命令下生效）。
 * 5000ms 是折中值；真机上发现周期性闪动就调大，或串口发 restart 0 关掉。 */
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
