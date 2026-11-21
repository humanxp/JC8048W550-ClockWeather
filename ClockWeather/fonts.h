/**
 * ============================================================================
 *  fonts.h  —  自造字库的声明
 *
 *  这些字体是用 lv_font_conv 从 Noto Sans SC 生成的，源码就在本目录下：
 *      font_clock_193.c   时钟大数字（0-9、冒号、连字符），211px **粗体**
 *                         ⚠ 必须含连字符 "-"：时间没同步时占位符是 "--:--"
 *
 *                         尺寸推导：粗体字形比常规体宽 7.4%
 *                         （"08:42" = 2.685 × 字号，常规体只要 2.498 × 字号）。
 *                         211px → 566px，加秒数(49)和间距(14)共 630px，
 *                         时钟整行 800px，左右各留 85px。
 *                         想再大要先看 ui.cpp 的 CLOCK_H 够不够（行高 = 0.767 × 字号）。
 *      font_digits_44.c   温度 / 秒数（含空格 . ° - + / %），44px
 *      font_digits_32.c   预报温度数字，32px
 *      font_cjk_22.c      中文正文，22px（GB2312 一级字表 + 常见地名补充字）
 *      font_cjk_32.c      中文标题/天气描述，32px（只含界面用到的字，体积很小）
 *
 *  ⚠ 数字字体里必须包含空格！预报卡片显示的是 "18° / 26°"，
 *    漏掉空格的话斜杠会贴到度数上。tools/fonts/digits_ui.txt 里第一个字符就是空格。
 * ============================================================================
 */
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t font_clock_193;
extern const lv_font_t font_digits_44;
extern const lv_font_t font_digits_32;
extern const lv_font_t font_cjk_22;
extern const lv_font_t font_cjk_32;

#ifdef __cplusplus
}
#endif
