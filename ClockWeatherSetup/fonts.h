/**
 * ============================================================================
 *  fonts.h  —  自造字库的声明（ClockWeatherSetup 版）
 *
 *  这些字体是用 lv_font_conv 从 Noto Sans SC 生成的，源码就在本目录下：
 *      font_clock_290.c   大时钟（满屏那张）的 0-9 : -，290px **粗体**
 *      font_clock_193.c   看板时钟的 0-9 : -，193px **粗体**
 *      font_digits_44.c   温度 / 秒数（含空格 . ° - + / %），44px
 *      font_digits_32.c   预报温度数字，32px
 *      font_cjk_22.c      中文正文，22px（GB2312 一级字表 + 常见地名补充字）
 *      font_cjk_28.c      设置向导正文，28px（只含向导用到的字，体积很小）
 *      font_cjk_32.c      天气描述，32px（只含天气描述用字）
 *      font_cjk_40.c      大时钟页下面的日期，40px
 *                         （只含 0-9 月日周年星期一二三四五六 和「正在同步时间…」）
 *
 *  ⚠ 数字字体里必须包含空格！预报卡片显示的是 "18° / 26°"，
 *    漏掉空格的话斜杠会贴到度数上。tools/fonts/digits_ui.txt 里第一个字符就是空格。
 *
 *  ⚠ font_clock_290 的宽度是这样定出来的（粗体）：
 *        数字 advance = 0.59 × 字号，冒号 = 0.325 × 字号
 *        "HH:MM" 宽度 = (4×0.59 + 0.325) × 字号 = 2.685 × 字号
 *        2.685 × 290 ≈ 779px < 800px（左右各留约 10px，留白后视觉边距约 23px）
 *    再大就会超出屏幕，所以 290 就是「一屏能放下的最大时钟」。
 * ============================================================================
 */
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t font_clock_290;
extern const lv_font_t font_clock_193;
extern const lv_font_t font_digits_44;
extern const lv_font_t font_digits_32;
extern const lv_font_t font_cjk_22;
extern const lv_font_t font_cjk_28;
extern const lv_font_t font_cjk_32;
extern const lv_font_t font_cjk_40;

#ifdef __cplusplus
}
#endif
