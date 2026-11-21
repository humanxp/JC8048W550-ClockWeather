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
 *       取 LCD_WIDTH 的整数倍、且倍数能整除 480 即可（480/10=48 ✓）。
 */
#define LCD_PREFER_SPEED        16000000
#define LCD_BOUNCE_BUFFER_PX    (LCD_WIDTH * 10)   /* 10 行，共约 32KB 内部 RAM */

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
