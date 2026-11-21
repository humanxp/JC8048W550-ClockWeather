/**
 * ============================================================================
 *  setup_ui.h / setup_ui.cpp  —  屏幕上的设置向导
 *
 *  三个页面，全部用 LVGL 控件搭出来，不需要连电脑改代码：
 *
 *    ① 连接 WiFi   扫描附近网络（异步、按信号排序、同名只留最强）
 *                  点一下选中的网络去输密码；也可以手动输入隐藏网络名
 *    ② 输入密码    LVGL 软键盘，密码可显示 / 隐藏，失败能重试
 *    ③ 选择地区    左边省份、右边城市，坐标和时区跟着走
 *
 *  流程有两种入口：
 *      setup_ui_start_wifi(true)    第一次开机（没配置过），走完 WiFi 继续走地区
 *      setup_ui_start_wifi(false)   看板上的「设置」按钮，走完回到看板
 *
 *  扫描和连接都是异步的，用两个 lv_timer 轮询状态，界面不会卡住。
 * ============================================================================
 */
#pragma once

#include <stdbool.h>

/** 建好计时器（在 ui_init() 里调一次）。 */
void setup_ui_init();

/** 打开设置向导的 WiFi 列表页。first_run = 第一次开机。 */
void setup_ui_start_wifi(bool first_run);

/** 打开设置向导的地区选择页。 */
void setup_ui_start_region(bool first_run);

/** 向导是否正显示着（主循环的「5 分钟回大时钟」要避开向导）。 */
bool setup_ui_active();
