/**
 * ============================================================================
 *  settings.h / settings.cpp  —  用户设置（WiFi + 地区）的 Flash 存储
 *
 *  为什么要有这一层：
 *      ClockWeather 版本的 WiFi 和城市是写死在 config.h 里的，改一次就要
 *      重新编译烧录。这个版本把它们做成运行时可改，存在 ESP32 的 NVS
 *      （Preferences）里，掉电不丢。
 *
 *  优先级：
 *      第一次开机   —— config.h 里的出厂默认值（通常留空）
 *      向导保存之后 —— Flash 里的值覆盖出厂默认值
 *
 *  settings_revision() 会在每次保存时 +1，主循环用它来判断
 *  「用户刚改了 WiFi / 地区，天气要重新拉」。
 * ============================================================================
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 从 Flash 读取设置，并把 config.h 里的默认值合并进来。开机调一次即可。 */
void settings_load();

/** 是否已经配置过 WiFi（SSID 非空）。 */
bool settings_has_wifi();

/** 是否已经有可用的地区坐标（经纬度都非 0）。 */
bool settings_has_region();

const char *settings_wifi_ssid();
const char *settings_wifi_pass();
const char *settings_province();
const char *settings_city();

/** 界面显示用的城市名：优先向导选的市，其次 config.h 的 CITY_NAME。 */
const char *settings_city_name();

float settings_lat();
float settings_lon();
const char *settings_timezone();

/** 设置每次变化都会 +1（主循环用来触发天气刷新）。 */
uint32_t settings_revision();

/** 保存 WiFi（同时写进 RAM 和 Flash）。 */
void settings_save_wifi(const char *ssid, const char *pass);

/** 保存地区（同时写进 RAM 和 Flash）。 */
void settings_save_region(const char *province, const char *city, float lat, float lon);

/** 清空全部设置（恢复出厂 + 下次开机重进向导），一般只在调试时用。 */
void settings_clear_all();

/**
 * UTF-8 安全拷贝：超出容量时按字符边界截断，不会把一个汉字切成两半。
 * （界面文案里到处都要用，所以放这里当公共小工具。）
 */
void copy_utf8(char *dst, size_t cap, const char *src);
