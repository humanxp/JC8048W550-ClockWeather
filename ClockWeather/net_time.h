/**
 * ============================================================================
 *  net_time.h / net_time.cpp  —  WiFi 连接 + NTP 网络授时
 *
 *  全部是非阻塞的：WiFi 连接和 NTP 同步都在后台进行，
 *  界面在联网过程中依然可以正常刷新（时钟会先显示 --:--）。
 * ============================================================================
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/** 启动 WiFi 连接（立即返回，不阻塞）。 */
void net_begin();

/** 主循环里反复调用：处理掉线重连。 */
void net_loop();

/** 当前是否已连接 WiFi。 */
bool net_is_connected();

/** 信号强度 dBm，未连接时返回 0。 */
int net_rssi();

/** 信号强度转成 0~4 格。 */
int net_rssi_level();

/** 启动 NTP 授时（内部会自己重试）。 */
void time_begin_sync();

/** 时间是否已经同步过（能取到 2020 年以后的时间）。 */
bool time_is_valid();

/** 取本地时间。成功返回 true。 */
bool time_now(struct tm *out);
