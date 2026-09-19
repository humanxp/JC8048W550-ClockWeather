/**
 * ============================================================================
 *  weather_icon.h / weather_icon.cpp  —  程序化绘制的天气图标
 *
 *  不依赖任何图片资源：全部用 LVGL 的圆、圆角矩形和折线拼出来，
 *  所以可以任意缩放，也不需要往 Flash 里塞 PNG。
 *
 *  用法：
 *      lv_obj_t *ico = weather_icon_create(parent, WICON_PARTLY, 96, bg_color);
 *      lv_obj_align(ico, LV_ALIGN_TOP_LEFT, x, y);
 *  换天气时直接 lv_obj_del(ico) 再重建即可。
 * ============================================================================
 */
#pragma once

#include <lvgl.h>

#include "weather.h"

/**
 * 在 parent 里创建一个 size x size 的天气图标。
 * bg_color 是图标所在卡片的底色（画月牙时需要用底色「咬」掉一块）。
 */
lv_obj_t *weather_icon_create(lv_obj_t *parent, WeatherIconType type, int size, lv_color_t bg_color);
