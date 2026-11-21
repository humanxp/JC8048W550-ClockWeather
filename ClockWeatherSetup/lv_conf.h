/**
 * ============================================================================
 *  lv_conf.h  —  LVGL 8.4 配置文件（本项目精简版）
 *
 *  ★ 安装位置很关键 ★
 *  这个文件不能放在本工程目录里，必须放到 Arduino 的 libraries 目录下，
 *  和 lvgl 文件夹同级，LVGL 才能找到它：
 *
 *      Windows : C:\Users\你的用户名\Documents\Arduino\libraries\lv_conf.h
 *      macOS   : ~/Documents/Arduino/libraries/lv_conf.h
 *      Linux   : ~/Arduino/libraries/lv_conf.h
 *
 *  也就是：
 *      Arduino/libraries/
 *          ├── lvgl/          <- 库管理器装的 LVGL
 *          ├── lv_conf.h      <- 就是这个文件
 *          ├── Arduino_GFX_Library/
 *          └── ...
 *
 *  注意：必须装 LVGL 8.4.x（不是 9.x）！本工程用的是 v8 API。
 * ============================================================================
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   颜色与显存
 *====================*/

/* 16 位色（RGB565），和 ST7262 的 RGB565 并口一致 */
#define LV_COLOR_DEPTH 16

/* 如果屏幕颜色明显发蓝/发黄（红蓝颠倒），把下面这行改成 1 */
#define LV_COLOR_16_SWAP 0

/* 屏幕透明效果：不用，省性能 */
#define LV_COLOR_SCREEN_TRANSP 0

#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

/*====================
   内存
 *====================*/

/* 用 LVGL 自带的内存池（放在内部 RAM）。
   这个版本控件很多，全都在这个池子里：
       大时钟屏 + 看板屏（含天气图标）+ 设置向导三页
       （WiFi 列表最多十几行、软键盘、34 个省 + 一列城市）
   实测「多操作几下再回大时钟」会卡死，就是池子不够 —— 分块渲染/向导列表
   反复重建时一旦分配失败，控件拿到的就是 NULL，随后就是跑飞或卡住。
   所以这里给到 112KB。别乱调小；调小后先看串口打印的 [LVGL] 用了多少。 */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (112U * 1024U)

#define LV_MEM_BUF_MAX_NUM 16
#define LV_MEMCPY_MEMSET_STD 0

/*====================
   HAL 设置
 *====================*/

/* 刷新周期。面板本身是 16MHz PCLK ≈ 39Hz（26ms 一帧），
   原来设 16ms（60fps）等于画了面板也来不及显示，纯属白写 PSRAM、
   和 RGB DMA 抢带宽 —— 左侧拉丝就是这么来的。
   设成 25ms 跟面板对齐：同样流畅，但 PSRAM 写入量少三分之一。 */
#define LV_DISP_DEF_REFR_PERIOD 25
#define LV_INDEV_DEF_READ_PERIOD 30  /* 触摸每 30ms 读一次 */

/* 用 Arduino 的 millis() 作为 LVGL 的心跳，不用手动 lv_tick_inc */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/*====================
   功能开关
 *====================*/

#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 0
    #define LV_LOG_USE_TIMESTAMP 0
    #define LV_LOG_TRACE_MEM 0
    #define LV_LOG_TRACE_TIMER 0
    #define LV_LOG_TRACE_INDEV 0
    #define LV_LOG_TRACE_DISP_REFR 0
    #define LV_LOG_TRACE_EVENT 0
    #define LV_LOG_TRACE_OBJ_CREATE 0
    #define LV_LOG_TRACE_LAYOUT 0
    #define LV_LOG_TRACE_ANIM 0
#endif

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

#define LV_USE_USER_DATA 1

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0
#define LV_SPRINTF_CUSTOM 0
#define LV_SPRINTF_USE_FLOAT 1

/*====================
   字体
 *====================*/

#define LV_FONT_MONTSERRAT_14 1   /* LVGL 需要一个默认字体，保留最小的 */
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1   /* 设置向导的软键盘 / 输入框用 */
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_SIMSUN_16_CJK 0

#define LV_FONT_DEFAULT &lv_font_montserrat_24
#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_FONT_SUBPX 0
#define LV_USE_FONT_PLACEHOLDER 1

/*====================
   控件：关掉用不到的，省 Flash
 *====================*/

#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 0
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 0
#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 1
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/*====================
   布局与主题
 *====================*/

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1
    #define LV_THEME_DEFAULT_GROW 1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif

#define LV_USE_THEME_BASIC 0
#define LV_USE_THEME_MONO 0

#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_RLOTTIE 0
#define LV_USE_SNAPSHOT 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0
#define LV_USE_EXAMPLES 0

/*====================
   其它
 *====================*/

#define LV_USE_ANIM 1
#define LV_USE_SHADOW 1
#define LV_USE_GROUP 1
#define LV_USE_GPU 0
#define LV_USE_OBJ_ID 0
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_EXTRA 1

#endif /* LV_CONF_H */
