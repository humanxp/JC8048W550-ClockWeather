#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>       /* 从内部 RAM 分配分块渲染缓冲 */
#include <esp_lcd_panel_rgb.h>   /* esp_lcd_rgb_panel_restart() 用 */
#include <lvgl.h>
#include <string.h>              /* memcpy：分块渲染要把画好的一块拷进帧缓冲 */

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

/* 分块渲染用的内部 RAM 中转缓冲（只有非 direct_mode 时才分配） */
static lv_color_t *s_draw_buf_mem = nullptr;
static bool        s_direct_mode  = true;

/* 分块渲染缓冲的高度（行数）：800 * 16 * 2 = 25.6KB 内部 RAM。
 *
 * 为什么这么小：这块缓冲和 WiFi/mbedTLS 抢的是同一份「连续内部 RAM」。
 * 内存一紧，轻则 HTTPS 握手失败，重则分配失败写飞 —— 表现就是花屏/重启。
 * 16 行（每帧多分几次 flush，每次都很小）在 240MHz 下滚列表、按软键盘依然跟手。
 * 内存富裕的话可以调到 24（38.4KB）或 40（64KB）。 */
#define LCD_DRAW_BUF_LINES   16

/* 实际用上的搬运缓冲行数（board_pins.h 里的 LCD_BOUNCE_LINES 是首选值，
 * 内存不够时 display_init() 会自动降档，这里记下最终结果给日志用）。 */
static int s_bounce_lines = 0;

/* ------------------------------------------------------------------ */
/*  LVGL 对象                                                          */
/* ------------------------------------------------------------------ */
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

/* 背光用的 LEDC 参数。
 * GPIO2 接的是背光驱动的使能脚，只有「一直拉高」才亮，
 * 所以这里只做初始化并写满占空比，不提供调光接口。 */
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
    ledcWrite(PIN_LCD_BL, 255);          /* 满占空比 = 常亮 */
#else
    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ, BL_LEDC_RES);
    ledcAttachPin(PIN_LCD_BL, BL_LEDC_CHANNEL);
    ledcWrite(BL_LEDC_CHANNEL, 255);
#endif
}

/* ------------------------------------------------------------------ */
/*  LVGL 显示刷新回调                                                  */
/*                                                                     */
/*  两种渲染方式（由 board_pins.h 的 LCD_DIRECT_RENDER 选）：            */
/*                                                                     */
/*  ① 分块渲染（默认，direct_mode = false）                             */
/*     LVGL 先在内部 RAM 的小缓冲里画好一块，再由这里 memcpy 进 PSRAM   */
/*     帧缓冲。内部 RAM 比 PSRAM 快得多；更关键的是，帧缓冲被改写的时间 */
/*     窗口很短，滚动列表 / 按软键盘这种大面积重绘不会明显撕裂。        */
/*                                                                     */
/*  ② 直接渲染（direct_mode = true）                                    */
/*     LVGL 直接往 PSRAM 帧缓冲里画。省 64KB 内部 RAM，但 RGB DMA 正在  */
/*     扫同一块内存，大面积重绘就是「一边画一边显示」——明显闪；         */
/*     而且每次刷新都要把整屏 750KB 写回 PSRAM，和 DMA 抢带宽。         */
/*     只适合时钟这种小面积、低频次刷新的界面。                         */
/*                                                                     */
/*  两种方式都必须把 CPU cache 写回 PSRAM —— RGB DMA 绕过 cache 直接   */
/*  读 PSRAM，不写回就是花屏 / 拉丝。                                  */
/* ------------------------------------------------------------------ */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px_map)
{
    if (s_fb == nullptr) {
        lv_disp_flush_ready(drv);
        return;
    }

    if (s_direct_mode) {
        /* 像素已经在帧缓冲里了。整屏写回最省心：
           这个模式本来只用于小面积、低频次刷新，750KB 的开销可以忽略。 */
#if HAVE_CACHE_WRITEBACK
        Cache_WriteBack_Addr((uint32_t)s_fb, (uint32_t)s_fb_bytes);
#endif
        lv_disp_flush_ready(drv);
        return;
    }

    /* 分块渲染：把这一块从 LVGL 的缓冲拷进帧缓冲，逐行写回 cache。
       逐行写回而不是整屏写回，是因为大面积重绘时整屏写回会和 RGB DMA
       抢 PSRAM 带宽，反而把画面拖花。 */
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;

    if (w > 0 && h > 0 &&
        area->x1 >= 0 && area->y1 >= 0 &&
        area->x2 < LCD_WIDTH && area->y2 < LCD_HEIGHT) {
        lv_color_t       *dst = (lv_color_t *)s_fb + (size_t)area->y1 * LCD_WIDTH + area->x1;
        const lv_color_t *src = px_map;

        for (int32_t y = 0; y < h; y++) {
            memcpy(dst, src, (size_t)w * sizeof(lv_color_t));
#if HAVE_CACHE_WRITEBACK
            Cache_WriteBack_Addr((uint32_t)dst, (uint32_t)((size_t)w * sizeof(lv_color_t)));
#endif
            dst += LCD_WIDTH;
            src += w;
        }
    }

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

    /* 搬运缓冲按「大 -> 小」试：每块 800*行数*2 字节，两份（内部 RAM）。
       12 行约 38.4KB、10 行 32KB、8 行 25.6KB。
       内存不够时 esp_lcd_new_rgb_panel() 会返回 NULL，绝不能让板子卡在
       开机重启循环里，所以失败就自动降一档。
       ⚠ 这三档都是「预读装得进 32KB L1 缓存」的尺寸；别往上加，
         见 board_pins.h 里 LCD_BOUNCE_LINES 的说明。 */
    static const int k_bounce_lines[] = { LCD_BOUNCE_LINES, 10, 8 };
    const int k_bounce_tries = (int)(sizeof(k_bounce_lines) / sizeof(k_bounce_lines[0]));
    int lines_used = 0;

    for (int i = 0; i < k_bounce_tries; i++) {
        int lines = k_bounce_lines[i];

        s_panel = new Arduino_ESP32RGBPanel(
            PIN_LCD_DE, PIN_LCD_VSYNC, PIN_LCD_HSYNC, PIN_LCD_PCLK,
            PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
            PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
            PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
            LCD_HSYNC_POLARITY, LCD_HSYNC_FRONT_PORCH, LCD_HSYNC_PULSE_WIDTH, LCD_HSYNC_BACK_PORCH,
            LCD_VSYNC_POLARITY, LCD_VSYNC_FRONT_PORCH, LCD_VSYNC_PULSE_WIDTH, LCD_VSYNC_BACK_PORCH,
            LCD_PCLK_ACTIVE_NEG, LCD_PREFER_SPEED, false /* useBigEndian */,
            0 /* de_idle_high */, 0 /* pclk_idle_high */,
            (size_t)LCD_WIDTH * (size_t)lines);

        /* rotation=0, auto_flush=true：RGB 外设持续把帧缓冲刷到屏幕 */
        s_gfx = new Arduino_RGB_Display(LCD_WIDTH, LCD_HEIGHT, s_panel, 0 /* rotation */, true /* auto_flush */);

        if (s_gfx->begin()) {
            lines_used = lines;
            break;
        }

        /* begin() 在 getFrameBuffer() 返回 NULL 时失败，也就是面板/搬运缓冲
           没申请到内存。IDF 在失败路径里已经把内存还回去了，这里把两个
           C++ 对象删掉重来即可。 */
        Serial.printf("[显示] 搬运缓冲 %d 行申请失败（内部 RAM 不够），退一档重试\n", lines);
        delete s_gfx;
        s_gfx = nullptr;
        delete s_panel;
        s_panel = nullptr;
    }

    if (lines_used == 0) {
        Serial.println("[显示] gfx->begin() 失败！面板和搬运缓冲都没建起来");
        return false;
    }
    s_bounce_lines = lines_used;
    s_gfx->fillScreen(RGB565_BLACK);

    /* 屏幕稳定性的三个关键参数，开机打出来备查：
       —— 出现「左侧拉丝 / 一条一条」时先看这里的搬运缓冲实际用了几行
          （每块的第一行出问题，就是这一块 memcpy 没赶上 DMA）；
       —— 出现「花屏一直不复原」时看自愈是不是「有效」。 */
    Serial.printf("[显示] DMA 搬运缓冲 %d 行（%u 字节内部 RAM，首选 %d 行）\n",
                  s_bounce_lines,
                  (unsigned)((size_t)LCD_WIDTH * s_bounce_lines * 2 * 2),
                  (int)LCD_BOUNCE_LINES);
    Serial.printf("[显示] 像素时钟 %u Hz（约 %.1f Hz 刷新）\n",
                  (unsigned)LCD_PREFER_SPEED,
                  (double)LCD_PREFER_SPEED /
                      ((LCD_WIDTH + LCD_HSYNC_PULSE_WIDTH + LCD_HSYNC_BACK_PORCH + LCD_HSYNC_FRONT_PORCH) *
                       (double)(LCD_HEIGHT + LCD_VSYNC_PULSE_WIDTH + LCD_VSYNC_BACK_PORCH + LCD_VSYNC_FRONT_PORCH)));
#if LCD_DMA_RESTART_FIX
    Serial.printf("[显示] 花屏自愈：每 %d ms 复位一次 DMA，panel 句柄 %s\n",
                  LCD_DMA_RESTART_INTERVAL_MS,
                  (s_panel != nullptr && s_panel->_panel_handle != nullptr)
                      ? "有效" : "无效（检查 Arduino_GFX 补丁）");
#else
    Serial.println("[显示] 花屏自愈：已关闭（LCD_DMA_RESTART_FIX=0）");
#endif

    /* ---- 2. 背光（一次拉满，之后不再动） ---- */
    bl_init();

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

    /* ---- 渲染缓冲 ----
       默认用「分块渲染」：LVGL 在内部 RAM 的 64KB 小块里画，画完再拷进帧缓冲。
       内部 RAM 快得多，而且帧缓冲被改写的时间窗口很短，滚动列表 / 软键盘
       这种大面积重绘不会闪。内部 RAM 不够时自动退回直接渲染（能用，但会闪）。 */
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = LCD_WIDTH;
    s_disp_drv.ver_res  = LCD_HEIGHT;
    s_disp_drv.flush_cb = disp_flush_cb;

#if LCD_DIRECT_RENDER
    (void)s_draw_buf_mem;    /* 只有分块渲染才用它，这里显式标注一下免得报警告 */
    s_direct_mode = true;
    lv_disp_draw_buf_init(&s_draw_buf, fb, nullptr, LCD_WIDTH * LCD_HEIGHT);
    Serial.println("[显示] 渲染方式：直接渲染（LVGL 直接画进 PSRAM 帧缓冲）");
#else
    s_draw_buf_mem = (lv_color_t *)heap_caps_malloc(
        (size_t)LCD_WIDTH * LCD_DRAW_BUF_LINES * sizeof(lv_color_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (s_draw_buf_mem != nullptr) {
        s_direct_mode = false;
        lv_disp_draw_buf_init(&s_draw_buf, s_draw_buf_mem, nullptr,
                              LCD_WIDTH * LCD_DRAW_BUF_LINES);
        Serial.printf("[显示] 渲染方式：分块渲染，%d 行缓冲（%u 字节内部 RAM）\n",
                      LCD_DRAW_BUF_LINES,
                      (unsigned)((size_t)LCD_WIDTH * LCD_DRAW_BUF_LINES * sizeof(lv_color_t)));
    } else {
        s_direct_mode = true;
        lv_disp_draw_buf_init(&s_draw_buf, fb, nullptr, LCD_WIDTH * LCD_HEIGHT);
        Serial.println("[显示] 内部 RAM 不足，退回直接渲染（大面积重绘会闪）");
    }
#endif

    s_disp_drv.draw_buf    = &s_draw_buf;
    s_disp_drv.direct_mode = s_direct_mode;
    lv_disp_drv_register(&s_disp_drv);

    /* ---- 4. 触摸输入设备 ---- */
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&s_indev_drv);

    Serial.println("[显示] ST7262 800x480 初始化完成");
    return true;
}

/* ------------------------------------------------------------------ */
/*  运行时改像素时钟（诊断用）                                          */
/*                                                                     */
/*  为什么需要它：PSRAM 的读带宽是「面板连续取数 + CPU 写帧缓冲」共用  */
/*  的，像素时钟越低，面板每秒要的字节越少，搬运缓冲的余量就越大。      */
/*  但 PCLK 同时决定刷新率，降到面板不认的值会直接白屏 —— 而这是编译期 */
/*  常量，白屏了就只能重新烧录。所以留一个串口命令现场试：             */
/*      pclk 14   -> 14MHz 左右（驱动按 160MHz 分频取最接近值）        */
/*      pclk 16   -> 回到默认                                         */
/*  只影响本次运行，重启就回到 board_pins.h 的 LCD_PREFER_SPEED。      */
/* ------------------------------------------------------------------ */
bool display_set_pclk(uint32_t hz)
{
    if (s_panel == nullptr || s_panel->_panel_handle == nullptr) return false;
    if (hz < 8000000 || hz > 24000000) return false;   /* 超出这个范围别乱试 */

    esp_err_t err = esp_lcd_rgb_panel_set_pclk(s_panel->_panel_handle, hz);
    if (err != ESP_OK) return false;

    /* 实际上是下一个 VSYNC 里按 160MHz 分频取最接近值，这里先按分频算出来打在
       日志里，方便对照「降了多少带宽、掉到多少帧」。只是提示，不是精确读数。 */
    uint32_t div = (uint32_t)(160000000.0f / (float)hz + 0.5f);
    if (div == 0) div = 1;
    uint32_t actual = 160000000u / div;
    const double ht = LCD_WIDTH + LCD_HSYNC_PULSE_WIDTH + LCD_HSYNC_BACK_PORCH + LCD_HSYNC_FRONT_PORCH;
    const double vt = LCD_HEIGHT + LCD_VSYNC_PULSE_WIDTH + LCD_VSYNC_BACK_PORCH + LCD_VSYNC_FRONT_PORCH;
    Serial.printf("[显示] 像素时钟请求 %u Hz -> 实际约 %u Hz（%.1f Hz 刷新，带宽 %.0f%%）\n",
                  (unsigned)hz, (unsigned)actual,
                  (double)actual / (ht * vt),
                  100.0 * (double)actual / (double)LCD_PREFER_SPEED);
    return true;
}

/* ------------------------------------------------------------------ */
/*  RGB GDMA 自愈                                                      */
/*                                                                     */
/*  ESP32-S3 的 RGB GDMA 会「永久失步」，表现为随机出现局部花屏/拉丝， */
/*  而且一直不复原、只有重启才好。ESP-IDF 官方给的开关是编译期的       */
/*  CONFIG_LCD_RGB_RESTART_IN_VSYNC，Arduino 改不了，所以这里改成      */
/*  运行时定期调 esp_lcd_rgb_panel_restart() 让它自愈。                */
/*                                                                     */
/*  这个函数只设一个内部标志，真正的复位在下一个 VSYNC 中断里做，      */
/*  所以不会打断当前正在输出的这一帧。                                 */
/* ------------------------------------------------------------------ */
void display_tick()
{
    lv_timer_handler();

#if LCD_DMA_RESTART_FIX
    static uint32_t s_last_restart_ms = 0;
    uint32_t now = millis();

    if (s_panel != nullptr &&
        now - s_last_restart_ms >= (uint32_t)LCD_DMA_RESTART_INTERVAL_MS) {
        s_last_restart_ms = now;
        /* _panel_handle 需要先给 Arduino_GFX 打补丁放开权限，
           见 board_pins.h 里 LCD_DMA_RESTART_FIX 的说明 */
        if (s_panel->_panel_handle != nullptr) {
            esp_lcd_rgb_panel_restart(s_panel->_panel_handle);
        }
    }
#endif
}

/* 背光在 display_init() 里一次拉满，之后不再动 —— 这块屏没法调光。 */
