/**
 * ============================================================================
 *  display.h / display.cpp  —  RGB 屏驱动 + LVGL 移植
 *
 *  做法：用 Arduino_GFX 的 Arduino_ESP32RGBPanel 驱动 ST7262，
 *        帧缓冲放在 PSRAM 里（800*480*2 = 750KB）。
 *
 *  LVGL 渲染方式有两种，由 board_pins.h 的 LCD_DIRECT_RENDER 选：
 *
 *    ① 分块渲染（默认）：LVGL 先在内部 RAM 的 64KB 小块里画好，
 *       再整块 memcpy 进 PSRAM 帧缓冲。内部 RAM 快，且帧缓冲被改写的时间
 *       窗口很短 —— 滚动列表 / 软键盘这种大面积重绘不会明显撕裂。
 *
 *    ② 直接渲染 (direct_mode)：LVGL 直接往帧缓冲里画，零拷贝、省 64KB
 *       内部 RAM，但 RGB DMA 正在扫同一块内存，大面积重绘会明显闪；
 *       而且每次刷新都要整屏写回 PSRAM。ClockWeather（只刷时钟数字）用它没问题，
 *       ClockWeatherSetup 有交互，必须用 ①。
 * ============================================================================
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** 初始化屏幕、背光和 LVGL 显示/输入驱动。成功返回 true。 */
bool display_init();

/** 主循环里调用，等价于 lv_timer_handler()。 */
void display_tick();

/**
 * 运行时改像素时钟（诊断用，不写 Flash，重启恢复默认值）。
 * 降一点能降低 PSRAM 带宽压力、缓解「行首一条一条」的拉丝；
 * 但降过头面板会白屏 —— 白屏时串口里再发 pclk 16 就能回来。
 * 超出 8~24MHz 或者面板还没建好时返回 false。
 */
bool display_set_pclk(uint32_t hz);

/* 背光不对外提供接口：这块屏的背光是使能型（只有全亮，调低直接全黑），
   由 display_init() 固定拉满，见 board_pins.h 里的实板结论。 */
