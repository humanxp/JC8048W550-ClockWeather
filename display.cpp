#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#include "board_pins.h"
#include "touch_gt911.h"

/*
 * ESP32-S3 的 RGB DMA 绕过 CPU cache，直接读 PSRAM 里的帧缓冲。
 * 所以 CPU 写进帧缓冲的像素必须先「写回」(write-back) 到 PSRAM，DMA 才看得到。
 *
 * Arduino_GFX 自己的绘图函数（fillScreen / draw16bitRGBBitmap …）内部会调
 * _dirty() -> Cache_WriteBack_Addr() 做这件事；但 direct_mode 下是 LVGL
 * 直接往帧缓冲写内存，绕过了它。不补这一步，屏幕就会出现花屏、拉丝。
 */
#if defined(ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
    #include "esp32s3/rom/cache.h"
    #define HAVE_CACHE_WRITEBACK 1
#else
    #define HAVE_CACHE_WRITEBACK 0
#endif

/* ------------------------------------------------------------------ */
/*  Arduino_GFX 对象                                                   */
/* ------------------------------------------------------------------ */
static Arduino_ESP32RGBPanel *s_panel = nullptr;
static Arduino_RGB_Display *s_gfx = nullptr;
static void  *s_fb       = nullptr;   /* 帧缓冲首地址（cache 写回要用） */
static size_t s_fb_bytes = 0;         /* 帧缓冲字节数 */

/* ------------------------------------------------------------------ */
/*  LVGL 对象                                                          */
/* ------------------------------------------------------------------ */
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

/* 背光用的 LEDC 参数 */
#define BL_LEDC_CHANNEL   0
#define BL_LEDC_FREQ      20000
#define BL_LEDC_RES       8      /* 8 位 -> 0~255 */

/*
 * arduino-esp32 3.x 改了 LEDC 的 API：
 *   2.x:  ledcSetup(ch, freq, res) + ledcAttachPin(pin, ch) + ledcWrite(ch, duty)
 *   3.x:  ledcAttach(pin, freq, res)                        + ledcWrite(pin, duty)
 * 下面用版本宏把两套都兼容掉。
 */
static void bl_init()
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_LCD_BL, BL_LEDC_FREQ, BL_LEDC_RES);
#else
    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ, BL_LEDC_RES);
    ledcAttachPin(PIN_LCD_BL, BL_LEDC_CHANNEL);
#endif
}

static void bl_write(uint32_t duty)
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PIN_LCD_BL, duty);
#else
    ledcWrite(BL_LEDC_CHANNEL, duty);
#endif
}

/* ------------------------------------------------------------------ */
/*  LVGL 显示刷新回调                                                  */
/*                                                                     */
/*  direct_mode 下 LVGL 已经把图像画进了 s_gfx 的帧缓冲里，而这块缓冲   */
/*  由 RGB 外设持续扫描输出到屏幕，所以这里不用拷贝像素。               */
/*                                                                     */
/*  但必须做一件事：把 CPU cache 里的改动写回 PSRAM。                   */
/*  LVGL 是直接写内存的，不会经过 Arduino_GFX 的 _dirty()，             */
/*  漏了这一步屏幕就会花屏、拉丝（尤其是经常重绘的区域）。              */
/* ------------------------------------------------------------------ */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px_map)
{
    LV_UNUSED(area);
    LV_UNUSED(px_map);

#if HAVE_CACHE_WRITEBACK
    /* 整屏写回最省心。本界面每秒只刷新一两次（数字变化时才重绘），
       750KB 的写回开销在这个刷新率下可以忽略。
       相比按 area 精确写回，这样不会因为 LVGL 版本对 area 语义的差异而出错。 */
    if (s_fb && s_fb_bytes) {
        Cache_WriteBack_Addr((uint32_t)s_fb, (uint32_t)s_fb_bytes);
    }
#endif

    lv_disp_flush_ready(drv);
}

/* ------------------------------------------------------------------ */
/*  LVGL 触摸读取回调                                                  */
/* ------------------------------------------------------------------ */
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    LV_UNUSED(drv);

    int32_t x = 0;
    int32_t y = 0;

    if (touch_read(&x, &y)) {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ------------------------------------------------------------------ */
/*  初始化                                                             */
/* ------------------------------------------------------------------ */
bool display_init()
{
    /* ---- 1. RGB 面板 ---- */
    /* 参数顺序：DE, VSYNC, HSYNC, PCLK, R0..R4, G0..G5, B0..B4,
                 水平(极性,前肩,脉宽,后肩), 垂直(极性,前肩,脉宽,后肩),
                 PCLK下降沿采样, 期望速率, 是否大端,
                 de_idle_high, pclk_idle_high, DMA搬运缓冲大小(像素)
       最后那个 bounce buffer 很重要：不设的话 DMA 直接从 PSRAM 读，
       PSRAM 一忙画面就闪烁。详见 board_pins.h 里的说明。 */
    s_panel = new Arduino_ESP32RGBPanel(
        PIN_LCD_DE, PIN_LCD_VSYNC, PIN_LCD_HSYNC, PIN_LCD_PCLK,
        PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
        PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
        PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
        LCD_HSYNC_POLARITY, LCD_HSYNC_FRONT_PORCH, LCD_HSYNC_PULSE_WIDTH, LCD_HSYNC_BACK_PORCH,
        LCD_VSYNC_POLARITY, LCD_VSYNC_FRONT_PORCH, LCD_VSYNC_PULSE_WIDTH, LCD_VSYNC_BACK_PORCH,
        LCD_PCLK_ACTIVE_NEG, LCD_PREFER_SPEED, false /* useBigEndian */,
        0 /* de_idle_high */, 0 /* pclk_idle_high */, LCD_BOUNCE_BUFFER_PX);

    /* rotation=0, auto_flush=true：RGB 外设持续把帧缓冲刷到屏幕 */
    s_gfx = new Arduino_RGB_Display(LCD_WIDTH, LCD_HEIGHT, s_panel, 0 /* rotation */, true /* auto_flush */);

    if (!s_gfx->begin()) {
        Serial.println("[显示] gfx->begin() 失败！");
        return false;
    }
    s_gfx->fillScreen(RGB565_BLACK);

    /* ---- 2. 背光 ---- */
    bl_init();
    bl_write(255);   /* 全亮 */

    /* ---- 3. LVGL ---- */
    lv_init();

    lv_color_t *fb = (lv_color_t *)s_gfx->getFramebuffer();
    if (fb == nullptr) {
        Serial.println("[显示] 帧缓冲分配失败，请检查 PSRAM 是否已开启 (OPI PSRAM)");
        return false;
    }
    /* 记下来给 flush 回调做 cache 写回用 */
    s_fb       = (void *)fb;
    s_fb_bytes = (size_t)LCD_WIDTH * LCD_HEIGHT * 2;

    Serial.printf("[显示] 帧缓冲 @ %p (%u 字节)，PSRAM 剩余 %u 字节\n",
                  (void *)fb, (unsigned)s_fb_bytes, (unsigned)ESP.getFreePsram());

    /* 缓冲大小 = 整屏像素。direct_mode 要求缓冲至少覆盖一屏。 */
    lv_disp_draw_buf_init(&s_draw_buf, fb, nullptr, LCD_WIDTH * LCD_HEIGHT);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res     = LCD_WIDTH;
    s_disp_drv.ver_res     = LCD_HEIGHT;
    s_disp_drv.flush_cb    = disp_flush_cb;
    s_disp_drv.draw_buf    = &s_draw_buf;
    s_disp_drv.direct_mode = true;
    lv_disp_drv_register(&s_disp_drv);

    /* ---- 4. 触摸输入设备 ---- */
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&s_indev_drv);

    Serial.println("[显示] ST7262 800x480 初始化完成");
    return true;
}

void display_tick()
{
    lv_timer_handler();
}

void display_set_brightness(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    bl_write((uint32_t)(percent * 255 / 100));
}
