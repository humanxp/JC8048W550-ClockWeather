#include "display.h"

#include <Arduino.h>
#include <esp_cpu.h>             /* esp_cpu_get_cycle_count()：ISR 里安全的高精度计时 */
#include <esp_heap_caps.h>
#include <esp_lcd_panel_rgb.h>   /* 直接建 RGB 面板：我们自己填搬运缓冲 */
#include <esp_lcd_panel_ops.h>   /* esp_lcd_panel_reset / esp_lcd_panel_init */
#include <lvgl.h>
#include <string.h>

#include "board_pins.h"
#include "touch_gt911.h"

/*
 * ESP32-S3 的 RGB DMA 绕过 CPU cache，直接读内存。
 * 帧缓冲在 PSRAM 里，所以 CPU 写进去的像素必须先「写回」(write-back) 才能被看到。
 */
#if defined(ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
    #include "esp32s3/rom/cache.h"
    #define HAVE_CACHE_WRITEBACK 1
    #define CACHE_LINE           32     /* CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE */
#else
    #define HAVE_CACHE_WRITEBACK 0
    #define CACHE_LINE           32
#endif

/* ------------------------------------------------------------------ */
/*  面板 / 帧缓冲                                                      */
/*                                                                     */
/*  ★ v2.3 起不再用 Arduino_GFX 建面板，改成直接调 ESP-IDF 的          */
/*    esp_lcd_new_rgb_panel()，原因只有一个：我们要自己接管搬运缓冲的   */
/*    填充（flags.no_fb + on_bounce_empty 回调），才能把「预读」提前    */
/*    到两块以前。IDF 自己在 num_fbs==0 时是不做预读的。                */
/*                                                                     */
/*    顺带的好处：不再需要给 Arduino_GFX 打 _panel_handle 补丁了，      */
/*    面板句柄就是我们自己的。                                          */
/* ------------------------------------------------------------------ */
static esp_lcd_panel_handle_t s_panel_handle = nullptr;
static void  *s_fb       = nullptr;   /* 帧缓冲首地址（PSRAM，我们自己分配） */
static size_t s_fb_bytes = 0;         /* 帧缓冲字节数 */

/* 分块渲染用的内部 RAM 中转缓冲（只有非 direct_mode 时才分配） */
static lv_color_t *s_draw_buf_mem = nullptr;
static bool        s_direct_mode  = true;

/* 分块渲染缓冲的高度（行数）：800 * 8 * 2 = 12.8KB 内部 RAM。 */
#define LCD_DRAW_BUF_LINES   8

/* 搬运缓冲（bounce buffer）参数 */
static int      s_bounce_lines = 0;   /* 实际用了几行（0 = 不用搬运缓冲） */
static uint32_t s_bb_bytes     = 0;   /* 一份搬运缓冲多少字节 */
static int      s_prefetch     = LCD_PREFETCH_BLOCKS;   /* 提前几块做 cache 预读 */
static uint32_t s_pclk_hz      = LCD_PREFER_SPEED;
static uint32_t s_restart_ms   = 0;   /* 手动 DMA 复位间隔，0 = 关 */

/* ------------------------------------------------------------------ */
/*  搬运填充统计（ISR 里只累加，不打印）                                */
/*                                                                     */
/*  这是排查「屏幕左边一条一条」最直接的证据：每次中断把一块从 PSRAM 拷  */
/*  进内部 RAM 花了多久、有没有超过「DMA 读完这一块」的期限。            */
/*  用 CPU 周期计数而不是 micros()：micros() 会拿锁，在 ISR 里不安全。   */
/* ------------------------------------------------------------------ */
static volatile uint32_t s_fill_count         = 0;
static volatile uint32_t s_fill_cycles_total  = 0;
static volatile uint32_t s_fill_cycles_max    = 0;
static volatile uint32_t s_fill_late          = 0;   /* 超过期限的次数 */
static uint32_t          s_fill_deadline_cyc  = 0;   /* 期限（周期数） */

/* flush 统计 */
static uint32_t s_flush_count    = 0;
static uint32_t s_flush_us_total = 0;
static uint32_t s_flush_us_max   = 0;

/* 把一段帧缓冲写回 PSRAM（RGB DMA 绕过 cache，不写回就是花屏/拉丝）。
 * 起止都按 32B cache line 对齐：否则边界那几行可能没被写回。 */
static inline void fb_writeback(const void *addr, size_t bytes)
{
#if HAVE_CACHE_WRITEBACK
    uint32_t a = (uint32_t)addr & ~(uint32_t)(CACHE_LINE - 1);
    uint32_t e = ((uint32_t)addr + (uint32_t)bytes + (CACHE_LINE - 1)) & ~(uint32_t)(CACHE_LINE - 1);
    if (e > a) Cache_WriteBack_Addr(a, e - a);
#else
    (void)addr;
    (void)bytes;
#endif
}

/* ------------------------------------------------------------------ */
/*  自己的搬运缓冲填充（DMA EOF 中断里被调用）                          */
/*                                                                     */
/*  IDF 的默认做法（num_fbs>0 时）是在这里 memcpy 一块，然后对「下一块」 */
/*  发 Cache_Start_DCache_Preload()。也就是预读只提前一块 —— 一块的时限 */
/*  只有 300µs（6 行 @16MHz），而一次冷读 9.6KB 实测约 340µs，正好差一点 */
/*  ，于是每块开头几个像素是上一块的旧内容（屏幕左边那些 1px 短线）。    */
/*                                                                     */
/*  我们把预读提前到 s_prefetch 块（默认 2 块 = 600µs），给它足够时间落   */
/*  进 L1；同时把每次填充耗时统计下来（stats 命令看「超期限」的比例）。  */
/* ------------------------------------------------------------------ */
static IRAM_ATTR bool bounce_fill_cb(esp_lcd_panel_handle_t panel, void *buf,
                                     int pos_px, int len_bytes, void *ctx)
{
    (void)panel;
    (void)ctx;

    if (pos_px < 0 || len_bytes <= 0) return false;

    const uint8_t *src = (const uint8_t *)s_fb + (size_t)pos_px * 2;
    const size_t   len = (size_t)len_bytes;
    if (src + len > (const uint8_t *)s_fb + s_fb_bytes) return false;

    uint32_t c0 = esp_cpu_get_cycle_count();
    memcpy(buf, src, len);
    uint32_t cyc = esp_cpu_get_cycle_count() - c0;

    /* volatile 变量用 ++ 会被 C++20 判为 deprecated，这里显式赋值 */
    s_fill_count = s_fill_count + 1;
    s_fill_cycles_total = s_fill_cycles_total + cyc;
    if (cyc > s_fill_cycles_max) s_fill_cycles_max = cyc;
    if (s_fill_deadline_cyc && cyc > s_fill_deadline_cyc) s_fill_late = s_fill_late + 1;

#if HAVE_CACHE_WRITEBACK
    /* 提前 s_prefetch 块做预读：等真要用这块时，缓存已经装好了。
       超过帧缓冲末尾就回卷到 0（和 IDF 的 wrap 行为一致）。 */
    if (s_prefetch > 0) {
        size_t ahead = (size_t)s_prefetch * len;
        size_t off   = (size_t)pos_px * 2 + ahead;
        if (off + len > s_fb_bytes) off = 0;
        Cache_Start_DCache_Preload((uint32_t)((const uint8_t *)s_fb + off), (uint32_t)len, 0);
    }
#endif

    return false;   /* 不需要 yield */
}

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
/*  分块渲染：LVGL 先在内部 RAM 的小缓冲里画好一块，再由这里 memcpy 进  */
/*  帧缓冲，然后把这几行写回 cache。                                    */
/*                                                                     */
/*  为什么逐块写回而不是整屏：大面积重绘时整屏写回会和 RGB DMA 抢 PSRAM  */
/*  带宽，反而把画面拖花。                                              */
/* ------------------------------------------------------------------ */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px_map)
{
    if (s_fb == nullptr) {
        lv_disp_flush_ready(drv);
        return;
    }

    const uint32_t t0 = micros();

    if (s_direct_mode) {
        /* 像素已经在帧缓冲里了。整屏写回最省心（这个模式本来就少用）。 */
        fb_writeback(s_fb, s_fb_bytes);
    } else {
        const int32_t w = area->x2 - area->x1 + 1;
        const int32_t h = area->y2 - area->y1 + 1;

        if (w > 0 && h > 0 &&
            area->x1 >= 0 && area->y1 >= 0 &&
            area->x2 < LCD_WIDTH && area->y2 < LCD_HEIGHT) {
            lv_color_t *dst = (lv_color_t *)s_fb + (size_t)area->y1 * LCD_WIDTH + area->x1;

            if (w == LCD_WIDTH) {
                /* 整行宽的块在帧缓冲里是连续的：一次 memcpy + 一次写回，
                   比按行循环（480 行 = 480 次写回）省一半以上的 CPU 时间。 */
                const size_t bytes = (size_t)w * (size_t)h * sizeof(lv_color_t);
                memcpy(dst, px_map, bytes);
                fb_writeback(dst, bytes);
            } else {
                const lv_color_t *src = px_map;
                const size_t row_bytes = (size_t)w * sizeof(lv_color_t);

                for (int32_t y = 0; y < h; y++) {
                    memcpy(dst, src, row_bytes);
                    fb_writeback(dst, row_bytes);   /* 相邻行不连续，只能逐行写回 */
                    dst += LCD_WIDTH;
                    src += w;
                }
            }
        }
    }

    const uint32_t dt = micros() - t0;
    s_flush_count++;
    s_flush_us_total += dt;
    if (dt > s_flush_us_max) s_flush_us_max = dt;

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
/*  建面板                                                             */
/* ------------------------------------------------------------------ */
static bool bounce_lines_ok(int lines)
{
    if (lines < 0 || lines > 60) return false;
    if (lines == 0) return true;
    return (LCD_HEIGHT % lines) == 0;
}

/* 用 IDF 直接建 RGB 面板，bounce 行数为 lines（0 = 不要搬运缓冲）。
 *
 * 引脚/时序全部来自 board_pins.h，和原来 Arduino_GFX 里那份配置逐项一致：
 *   data_gpio_nums 的顺序是 B0..B4, G0..G5, R0..R4（对应 useBigEndian=false）
 *   hsync/vsync 极性为 0 → idle 为低；PCLK 下降沿采样。 */
static bool panel_create(int lines, size_t burst)
{
    /* 帧缓冲由我们自己分配（no_fb 模式下驱动不分配）：PSRAM + 64B 对齐。
       它不参与 DMA 直接读取（DMA 只读内部 RAM 的搬运缓冲），所以只要
       cache line 对齐即可。 */
    if (s_fb == nullptr) {
        s_fb = heap_caps_aligned_calloc(64, 1, (size_t)LCD_WIDTH * LCD_HEIGHT * 2,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_fb == nullptr) {
            Serial.println("[显示] PSRAM 帧缓冲分配失败（请确认 PSRAM=OPI）");
            return false;
        }
        s_fb_bytes = (size_t)LCD_WIDTH * LCD_HEIGHT * 2;
        memset(s_fb, 0, s_fb_bytes);
        fb_writeback(s_fb, s_fb_bytes);
    }

    esp_lcd_rgb_panel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.clk_src    = LCD_CLK_SRC_DEFAULT;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;

    cfg.timings.pclk_hz            = s_pclk_hz;
    cfg.timings.h_res              = LCD_WIDTH;
    cfg.timings.v_res              = LCD_HEIGHT;
    cfg.timings.hsync_pulse_width  = LCD_HSYNC_PULSE_WIDTH;
    cfg.timings.hsync_back_porch   = LCD_HSYNC_BACK_PORCH;
    cfg.timings.hsync_front_porch  = LCD_HSYNC_FRONT_PORCH;
    cfg.timings.vsync_pulse_width  = LCD_VSYNC_PULSE_WIDTH;
    cfg.timings.vsync_back_porch   = LCD_VSYNC_BACK_PORCH;
    cfg.timings.vsync_front_porch  = LCD_VSYNC_FRONT_PORCH;
    cfg.timings.flags.hsync_idle_low = (LCD_HSYNC_POLARITY == 0);
    cfg.timings.flags.vsync_idle_low = (LCD_VSYNC_POLARITY == 0);
    cfg.timings.flags.de_idle_high   = 0;
    cfg.timings.flags.pclk_active_neg = LCD_PCLK_ACTIVE_NEG;
    cfg.timings.flags.pclk_idle_high  = 0;

    cfg.num_fbs               = 0;    /* no_fb 模式下必须为 0 */
    cfg.bounce_buffer_size_px = (size_t)LCD_WIDTH * (size_t)lines;
    cfg.dma_burst_size        = burst;   /* 见 board_pins.h 的说明 */

    cfg.hsync_gpio_num = PIN_LCD_HSYNC;
    cfg.vsync_gpio_num = PIN_LCD_VSYNC;
    cfg.de_gpio_num    = PIN_LCD_DE;
    cfg.pclk_gpio_num  = PIN_LCD_PCLK;
    cfg.disp_gpio_num  = -1;

    /* 数据线顺序：B0..B4, G0..G5, R0..R4（Arduino_GFX useBigEndian=false 的排法） */
    cfg.data_gpio_nums[0]  = PIN_LCD_B0;
    cfg.data_gpio_nums[1]  = PIN_LCD_B1;
    cfg.data_gpio_nums[2]  = PIN_LCD_B2;
    cfg.data_gpio_nums[3]  = PIN_LCD_B3;
    cfg.data_gpio_nums[4]  = PIN_LCD_B4;
    cfg.data_gpio_nums[5]  = PIN_LCD_G0;
    cfg.data_gpio_nums[6]  = PIN_LCD_G1;
    cfg.data_gpio_nums[7]  = PIN_LCD_G2;
    cfg.data_gpio_nums[8]  = PIN_LCD_G3;
    cfg.data_gpio_nums[9]  = PIN_LCD_G4;
    cfg.data_gpio_nums[10] = PIN_LCD_G5;
    cfg.data_gpio_nums[11] = PIN_LCD_R0;
    cfg.data_gpio_nums[12] = PIN_LCD_R1;
    cfg.data_gpio_nums[13] = PIN_LCD_R2;
    cfg.data_gpio_nums[14] = PIN_LCD_R3;
    cfg.data_gpio_nums[15] = PIN_LCD_R4;

    cfg.flags.disp_active_low    = 1;
    cfg.flags.refresh_on_demand  = 0;
    cfg.flags.fb_in_psram        = 1;    /* 虽然 no_fb，标记上更贴近实情 */
    cfg.flags.double_fb          = 0;
    cfg.flags.no_fb              = 1;    /* ★ 驱动不管帧缓冲，由我们在回调里填搬运缓冲 */
    cfg.flags.bb_invalidate_cache = 0;   /* 别让 ISR 去失效我们的缓存行 */

    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel_handle);
    if (err != ESP_OK) {
        Serial.printf("[显示] esp_lcd_new_rgb_panel 失败：%s（lines=%d, burst=%u）\n",
                      esp_err_to_name(err), lines, (unsigned)burst);
        s_panel_handle = nullptr;
        return false;
    }

    /* 必须在使用前注册：esp_lcd_panel_init() 里就会 fill 前两块搬运缓冲 */
    esp_lcd_rgb_panel_event_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.on_bounce_empty = bounce_fill_cb;
    err = esp_lcd_rgb_panel_register_event_callbacks(s_panel_handle, &cbs, nullptr);
    if (err != ESP_OK) {
        esp_lcd_panel_del(s_panel_handle);
        s_panel_handle = nullptr;
        return false;
    }

    /* 记下期限：一块 = bb_bytes 字节，面板每拍取 2 字节 → 期限 = bb_bytes/(2*PCLK) */
    s_bb_bytes = (size_t)LCD_WIDTH * (size_t)lines * 2;
    s_fill_deadline_cyc = (uint32_t)((uint64_t)s_bb_bytes * 1000000ULL *
                                     (uint64_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ /
                                     (2ULL * (uint64_t)s_pclk_hz));

    err = esp_lcd_panel_reset(s_panel_handle);
    if (err == ESP_OK) err = esp_lcd_panel_init(s_panel_handle);
    if (err != ESP_OK) {
        esp_lcd_panel_del(s_panel_handle);
        s_panel_handle = nullptr;
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/*  初始化                                                             */
/* ------------------------------------------------------------------ */
bool display_init(int prefer_bounce_lines)
{
    /* 首选值来自 NVS（-1 = 没存过 / 不合法，用 board_pins.h 的默认）。
       内存不够就按「更小一档」重试：6 → 4 → 3 …，绝不让板子卡在重启循环里。 */
    int pref = prefer_bounce_lines;
    if (pref < 0 || !bounce_lines_ok(pref)) {
        if (pref >= 0) {
            Serial.printf("[显示] 搬运缓冲 %d 行不合法（必须整除 %d），改用默认 %d 行\n",
                          pref, LCD_HEIGHT, (int)LCD_BOUNCE_LINES);
        }
        pref = LCD_BOUNCE_LINES;
    }

    static const int k_smaller[] = { 12, 10, 8, 6, 4, 3 };
    int cand[8];
    int cand_n = 0;
    cand[cand_n++] = pref;
    for (int i = 0; i < (int)(sizeof(k_smaller) / sizeof(k_smaller[0])) && cand_n < 8; i++) {
        if (k_smaller[i] < pref) cand[cand_n++] = k_smaller[i];
    }

    /* burst 也要能降档：本板实测 256 会 ESP_ERR_NOT_SUPPORTED，直接进重启循环
       就太蠢了。所以每个行数档都先试首选 burst，失败再退 64（IDF 默认）。 */
    static const size_t k_bursts[] = { LCD_DMA_BURST_SIZE, 64 };

    int lines_used = -1;
    size_t burst_used = 0;

    for (int i = 0; i < cand_n && lines_used < 0; i++) {
        for (int b = 0; b < (int)(sizeof(k_bursts) / sizeof(k_bursts[0])); b++) {
            if (b > 0 && k_bursts[b] == k_bursts[0]) break;   /* 首选就是 64 时别试两遍 */
            if (panel_create(cand[i], k_bursts[b])) {
                lines_used = cand[i];
                burst_used = k_bursts[b];
                break;
            }
        }
        if (lines_used < 0) {
            Serial.printf("[显示] 搬运缓冲 %d 行建面板失败，退一档重试\n", cand[i]);
        }
    }

    if (lines_used < 0) {
        Serial.println("[显示] RGB 面板建不起来（帧缓冲/搬运缓冲都没申请到内存）");
        return false;
    }
    s_bounce_lines = lines_used;

    Serial.printf("[显示] DMA 搬运缓冲 %d 行（每份 %u 字节，两份共 %u 字节内部 RAM，"
                  "首选 %d 行，L1 D-cache 32KB），burst %u\n",
                  s_bounce_lines,
                  (unsigned)((size_t)LCD_WIDTH * s_bounce_lines * 2),
                  (unsigned)((size_t)LCD_WIDTH * s_bounce_lines * 2 * 2),
                  (int)pref, (unsigned)burst_used);
    Serial.printf("[显示] 像素时钟 %u Hz（约 %.1f Hz 刷新）\n",
                  (unsigned)s_pclk_hz,
                  (double)s_pclk_hz /
                      ((LCD_WIDTH + LCD_HSYNC_PULSE_WIDTH + LCD_HSYNC_BACK_PORCH + LCD_HSYNC_FRONT_PORCH) *
                       (double)(LCD_HEIGHT + LCD_VSYNC_PULSE_WIDTH + LCD_VSYNC_BACK_PORCH + LCD_VSYNC_FRONT_PORCH)));
    Serial.printf("[显示] 搬运填充：自己接管（no_fb + on_bounce_empty），预读提前 %d 块\n",
                  s_prefetch);
#if defined(CONFIG_LCD_RGB_RESTART_IN_VSYNC) && CONFIG_LCD_RGB_RESTART_IN_VSYNC
    Serial.println("[显示] DMA 复位：驱动每帧在 VSYNC 里自动做（CONFIG_LCD_RGB_RESTART_IN_VSYNC=1）");
#endif

    /* ---- 背光（一次拉满，之后不再动） ---- */
    bl_init();

    /* ---- LVGL ---- */
    lv_init();

    Serial.printf("[显示] 帧缓冲 @ %p (%u 字节)，PSRAM 剩余 %u 字节\n",
                  s_fb, (unsigned)s_fb_bytes, (unsigned)ESP.getFreePsram());

    /* ---- 渲染缓冲 ----
       分块渲染：LVGL 在内部 RAM 的小块里画，画完再拷进帧缓冲。
       内部 RAM 不够时自动退回直接渲染（能用，但大面积重绘会闪）。 */
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = LCD_WIDTH;
    s_disp_drv.ver_res  = LCD_HEIGHT;
    s_disp_drv.flush_cb = disp_flush_cb;

#if LCD_DIRECT_RENDER
    (void)s_draw_buf_mem;
    s_direct_mode = true;
    lv_disp_draw_buf_init(&s_draw_buf, s_fb, nullptr, LCD_WIDTH * LCD_HEIGHT);
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
        lv_disp_draw_buf_init(&s_draw_buf, s_fb, nullptr, LCD_WIDTH * LCD_HEIGHT);
        Serial.println("[显示] 内部 RAM 不足，退回直接渲染（大面积重绘会闪）");
    }
#endif

    s_disp_drv.draw_buf    = &s_draw_buf;
    s_disp_drv.direct_mode = s_direct_mode;
    lv_disp_drv_register(&s_disp_drv);

    /* ---- 触摸输入设备 ---- */
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
/*  **2025-11 实板打回：这块 ST7262 只认 16MHz** —— 降到 14.55MHz       */
/*  （160MHz/11）整屏就变成蓝/白/绿/红纯色块一直跳，完全没有图像。      */
/*  所以这个接口现在只用于「验证降频会怎样」和「出故障时救回来」：      */
/*      pclk 16   -> 回到 16MHz（本板唯一正常的值，也是默认）          */
/*      pclk 14   -> 实验用，出问题立刻发 pclk 16                      */
/*  只影响本次运行，重启就回到 board_pins.h 的 LCD_PREFER_SPEED。      */
/* ------------------------------------------------------------------ */
bool display_set_pclk(uint32_t hz)
{
    if (s_panel_handle == nullptr) return false;
    if (hz < 8000000 || hz > 24000000) return false;   /* 超出这个范围别乱试 */

    esp_err_t err = esp_lcd_rgb_panel_set_pclk(s_panel_handle, hz);
    if (err != ESP_OK) return false;

    /* 实际上是下一个 VSYNC 里按 160MHz 分频取最接近值，这里先按分频算出来打在
       日志里，方便对照「降了多少带宽、掉到多少帧」。只是提示，不是精确读数。 */
    uint32_t div = (uint32_t)(160000000.0f / (float)hz + 0.5f);
    if (div == 0) div = 1;
    uint32_t actual = 160000000u / div;
    s_pclk_hz = actual;

    /* 期限随 PCLK 变，同步更新 */
    if (s_bb_bytes) {
        s_fill_deadline_cyc = (uint32_t)((uint64_t)s_bb_bytes * 1000000ULL *
                                         (uint64_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ /
                                         (2ULL * (uint64_t)s_pclk_hz));
    }

    const double ht = LCD_WIDTH + LCD_HSYNC_PULSE_WIDTH + LCD_HSYNC_BACK_PORCH + LCD_HSYNC_FRONT_PORCH;
    const double vt = LCD_HEIGHT + LCD_VSYNC_PULSE_WIDTH + LCD_VSYNC_BACK_PORCH + LCD_VSYNC_FRONT_PORCH;
    Serial.printf("[显示] 像素时钟请求 %u Hz -> 实际约 %u Hz（%.1f Hz 刷新，带宽 %.0f%%）\n",
                  (unsigned)hz, (unsigned)actual,
                  (double)actual / (ht * vt),
                  100.0 * (double)actual / (double)LCD_PREFER_SPEED);
    return true;
}

/* 手动 DMA 复位间隔（0 = 关闭）。本板驱动已在 VSYNC 里自动复位，默认关闭。 */
bool display_set_restart_interval(uint32_t ms)
{
    if (ms > 600000u) return false;
    s_restart_ms = ms;
    if (ms == 0) {
        Serial.println("[显示] 手动 DMA 复位已关闭（驱动仍在 VSYNC 里自动复位）");
    } else {
        Serial.printf("[显示] 手动 DMA 复位间隔 -> %u ms\n", (unsigned)ms);
    }
    return true;
}

/** 预读提前几块（0/1/2）。这是本轮排查的核心旋钮，运行时就能 A/B。 */
bool display_set_prefetch(int blocks)
{
    if (blocks < 0 || blocks > 2) return false;
    s_prefetch = blocks;
    Serial.printf("[显示] 搬运缓冲预读提前 %d 块（%d × %u 字节）\n",
                  blocks, blocks, (unsigned)s_bb_bytes);
    return true;
}

int display_get_prefetch()
{
    return s_prefetch;
}

void display_get_stats(DisplayStats *out)
{
    if (out == nullptr) return;
    out->bounce_lines   = s_bounce_lines;
    out->pclk_hz        = s_pclk_hz;
    out->restart_ms     = s_restart_ms;
    out->prefetch       = s_prefetch;
    out->flush_count    = s_flush_count;
    out->flush_us_total = s_flush_us_total;
    out->flush_us_max   = s_flush_us_max;

    /* 周期数换算成微秒（CPU 240MHz） */
    const uint32_t mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    out->fill_count    = s_fill_count;
    out->fill_late     = s_fill_late;
    out->fill_us_avg   = s_fill_count ? (s_fill_cycles_total / s_fill_count) / mhz : 0;
    out->fill_us_max   = s_fill_cycles_max / mhz;
    out->fill_deadline_us = s_fill_deadline_cyc / mhz;
}

void display_reset_stats()
{
    s_flush_count    = 0;
    s_flush_us_total = 0;
    s_flush_us_max   = 0;
    s_fill_count     = 0;
    s_fill_cycles_total = 0;
    s_fill_cycles_max   = 0;
    s_fill_late         = 0;
}

/**
 * 实测「PSRAM -> 内部 RAM」搬一块要多久，和 DMA 读完这一块的期限比。
 * （只作为参考基线：现在真正的填充耗时由 bounce_fill_cb 直接统计，见 stats。）
 */
bool display_bench_psram(int lines, int loops, DisplayBench *out)
{
    if (out == nullptr || s_fb == nullptr) return false;
    if (!bounce_lines_ok(lines) || lines == 0) return false;

    const size_t bytes = (size_t)LCD_WIDTH * (size_t)lines * sizeof(lv_color_t);

    uint8_t *dst = (uint8_t *)s_draw_buf_mem;
    bool    heap_dst = false;
    if (dst == nullptr || (size_t)LCD_WIDTH * LCD_DRAW_BUF_LINES * sizeof(lv_color_t) < bytes) {
        dst = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (dst == nullptr) return false;
        heap_dst = true;
    }

    if (loops < 1)   loops = 1;
    if (loops > 100) loops = 100;

    const uint8_t *src = (const uint8_t *)s_fb;

    /* ---- 热：连续读同一块，源区留在 L1 里 ---- */
    uint32_t warm_sum = 0, warm_max = 0;
    for (int i = 0; i < loops; i++) {
        uint32_t t0 = micros();
        memcpy(dst, src, bytes);
        uint32_t dt = micros() - t0;
        warm_sum += dt;
        if (dt > warm_max) warm_max = dt;
    }

    /* ---- 冷：每次先把源区踢出 cache，强制从 PSRAM 读 ---- */
    uint32_t cold_sum = 0, cold_max = 0;
    for (int i = 0; i < loops; i++) {
#if HAVE_CACHE_WRITEBACK
        if (!s_direct_mode) {
            Cache_Invalidate_Addr((uint32_t)src & ~(uint32_t)(CACHE_LINE - 1),
                                  (uint32_t)bytes + CACHE_LINE);
        }
#endif
        uint32_t t0 = micros();
        memcpy(dst, src, bytes);
        uint32_t dt = micros() - t0;
        cold_sum += dt;
        if (dt > cold_max) cold_max = dt;
    }

    if (heap_dst) heap_caps_free(dst);

    out->lines         = lines;
    out->bytes         = (uint32_t)bytes;
    out->deadline_us   = (uint32_t)(((double)bytes * 1000000.0) / (2.0 * (double)s_pclk_hz));
    out->warm_avg_us   = warm_sum / (uint32_t)loops;
    out->warm_max_us   = warm_max;
    out->cold_avg_us   = cold_sum / (uint32_t)loops;
    out->cold_max_us   = cold_max;

    const int64_t dl = (int64_t)out->deadline_us;
    out->margin_warm_pct = (dl > 0) ? (int32_t)((dl - (int64_t)warm_max) * 100 / dl) : 0;
    out->margin_cold_pct = (dl > 0) ? (int32_t)((dl - (int64_t)cold_max) * 100 / dl) : 0;
    return true;
}

/* ------------------------------------------------------------------ */
/*  RGB GDMA 复位                                                      */
/*                                                                     */
/*  IDF 驱动在 CONFIG_LCD_RGB_RESTART_IN_VSYNC=1 时每帧自己复位，所以    */
/*  s_restart_ms 默认 0（关闭）。这里只保留手动兜底，用于对照实验。      */
/* ------------------------------------------------------------------ */
void display_tick()
{
    lv_timer_handler();

    if (s_restart_ms == 0) return;

    static uint32_t s_last_restart_ms = 0;
    uint32_t now = millis();

    if (s_panel_handle != nullptr && now - s_last_restart_ms >= s_restart_ms) {
        s_last_restart_ms = now;
        esp_lcd_rgb_panel_restart(s_panel_handle);
    }
}

/* 背光在 display_init() 里一次拉满，之后不再动 —— 这块屏没法调光。 */
