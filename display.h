/**
 * ============================================================================
 *  display.h / display.cpp  —  RGB 屏驱动 + LVGL 移植
 *
 *  做法：用 Arduino_GFX 的 Arduino_ESP32RGBPanel 驱动 ST7262，
 *        帧缓冲放在 PSRAM 里（800*480*2 = 750KB）。
 *
 *  LVGL 使用「直接渲染模式」(direct_mode)：
 *      LVGL 直接往面板帧缓冲里画，画完不用 memcpy，零拷贝，帧率最高。
 *      代价是绘制过程中屏幕可能出现轻微撕裂；对秒级刷新的时钟来说基本看不出来。
 * ============================================================================
 */
#pragma once

#include <stdbool.h>

/** 初始化屏幕、背光和 LVGL 显示/输入驱动。成功返回 true。 */
bool display_init();

/** 主循环里调用，等价于 lv_timer_handler()。 */
void display_tick();

/** 屏幕亮度调节：0~100（用 LEDC 调 GPIO2 的占空比）。 */
void display_set_brightness(int percent);
