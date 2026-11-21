/**
 * ============================================================================
 *  net_time.h / net_time.cpp  —  WiFi 连接 + 扫描 + NTP 网络授时
 *
 *  比 ClockWeather 版多出来的部分：
 *      - 连接用的账号密码来自 settings（设置向导填的），不再是编译期常量
 *      - 异步扫描附近的 WiFi，供设置向导的列表页用
 *      - net_connect_to()：向导里尝试连接某个网络（不改 Flash 里的存档）
 *
 *  所有操作都是非阻塞的：扫描和连接都在后台跑，LVGL 照常刷新，
 *  向导用一个 lv_timer 轮询状态（见 setup_ui.cpp）。
 * ============================================================================
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* 扫描结果最多缓存这么多个网络（按信号从强到弱，同名只留最强的那个）。
 * 每多一行 WiFi 就要多占一份 LVGL 内存（一行好几个控件），
 * 家用环境十几个绰绰有余，别调太大。 */
#define NET_SCAN_MAX   16

/** 用已保存的设置启动 WiFi 连接（立即返回，不阻塞）。 */
void net_begin();

/** 主循环里反复调用：处理掉线重连。 */
void net_loop();

/** 当前是否已连接 WiFi。 */
bool net_is_connected();

/** 信号强度 dBm，未连接时返回 0。 */
int net_rssi();

/** 信号强度转成 0~4 格。 */
int net_rssi_level();

/* ---------------------------- 扫描 ---------------------------- */

/** 开始一次异步扫描（已在扫描中会直接返回）。 */
void net_scan_start();

/** 扫描是否还在进行中。 */
bool net_scan_running();

/** 扫描是否已经结束（无论成功还是失败）。 */
bool net_scan_ready();

/**
 * 扫描是否正在进行中（只读，不改任何状态）。
 * 扫描时 WiFi 会吃掉一大块内存，别的网络请求最好避一避。
 */
bool net_scan_busy();

/** 本次扫描是否失败。 */
bool net_scan_failed();

/** 扫描到的网络数量（已去重、已按信号排序）。 */
int net_scan_count();

const char *net_scan_ssid(int index);
int         net_scan_rssi(int index);
bool        net_scan_secure(int index);

/** 丢弃扫描结果并释放 WiFi 内部缓存。 */
void net_scan_clear();

/* ------------------------- 指定账号连接 ------------------------- */

/**
 * 尝试连接指定网络（不会写入 Flash）。
 * 之后用 net_is_connected() 轮询；失败想恢复原设置就再调 net_begin()。
 * 调用期间 net_loop() 的自动重连会暂停，避免把这次尝试打断。
 */
void net_connect_to(const char *ssid, const char *pass);

/**
 * 连接成功后调用：把 net_connect_to() 临时的「手动模式」交还给后台，
 * 之后掉线会自动重连，而且不用重新连一次。
 */
void net_adopt_current();

/* ---------------------------- NTP ---------------------------- */

/** 启动 NTP 授时（内部会自己重试）。 */
void time_begin_sync();

/** 时间是否已经同步过（能取到 2020 年以后的时间）。 */
bool time_is_valid();

/** 取本地时间。成功返回 true。 */
bool time_now(struct tm *out);
