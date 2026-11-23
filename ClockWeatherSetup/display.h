/**
 * ============================================================================
 *  display.h / display.cpp  —  RGB 屏驱动 + LVGL 移植
 *
 *  做法：直接用 ESP-IDF 的 esp_lcd_new_rgb_panel() 驱动 ST7262（v2.3 起不再
 *        经 Arduino_GFX），帧缓冲放 PSRAM（800*480*2 = 750KB），我们自己
 *        分配；DMA 搬运缓冲的填充也由我们自己接管（no_fb + on_bounce_empty）。
 *
 *  为什么要自己接管搬运缓冲：
 *      IDF 默认每次填充只对「下一块」做 cache 预读，而一块的时限只有 300µs
 *      （6 行 @16MHz），冷读 9.6KB 实测约 340µs —— 差一口气，于是每块开头
 *      几个像素是上一块的旧内容，就是屏幕左边那些 1px 短线。
 *      自己接管后可以把预读提前到 1~2 块（运行时用 prefetch 命令 A/B），
 *      并且能把每次填充的耗时/超期限次数直接统计出来（stats 命令）。
 *
 *  LVGL 渲染方式：分块渲染（LCD_DIRECT_RENDER=0），内部 RAM 8 行缓冲，
 *  画完一块再 memcpy 进帧缓冲并把这几行写回 cache。
 * ============================================================================
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 运行期统计（display_get_stats 取出，串口 stats 命令打印） */
typedef struct {
    int      bounce_lines;      /* 实际用的搬运缓冲行数（0 = 没启用搬运缓冲） */
    int      prefetch;          /* 预读提前几块 */
    uint32_t pclk_hz;           /* 当前像素时钟（可能被 pclk 命令改过） */
    uint32_t restart_ms;        /* 手动 DMA 复位间隔，0 = 关闭 */

    uint32_t flush_count;       /* 累计 flush 次数 */
    uint32_t flush_us_total;    /* flush 累计耗时（微秒） */
    uint32_t flush_us_max;      /* 单次 flush 最长耗时（微秒） */

    /* ★ 搬运缓冲填充（中断里实测）：这是判断「左边短线」最直接的指标 */
    uint32_t fill_count;        /* 累计填充次数 */
    uint32_t fill_late;         /* 超过 DMA 期限的次数（>0 就可能出现短线） */
    uint32_t fill_us_avg;       /* 平均填充耗时 */
    uint32_t fill_us_max;       /* 最长填充耗时 */
    uint32_t fill_deadline_us;  /* 期限：DMA 读完这一块需要多久 */
} DisplayStats;

/** 一次 PSRAM→内部 RAM 拷贝的实测结果（display_bench_psram，参考基线用） */
typedef struct {
    int      lines;             /* 这次测的是几行的块 */
    uint32_t bytes;             /* 一块多少字节 */
    uint32_t deadline_us;       /* DMA 读完这一块的期限（受像素时钟限制） */

    /* 热：源区在 L1 缓存里（预读生效时的正常情形） */
    uint32_t warm_avg_us;
    uint32_t warm_max_us;
    int32_t  margin_warm_pct;

    /* 冷：每次强制失效缓存，必须从 PSRAM 读（预读没赶上时的情形） */
    uint32_t cold_avg_us;
    uint32_t cold_max_us;
    int32_t  margin_cold_pct;
} DisplayBench;

/**
 * 初始化屏幕、背光和 LVGL 显示/输入驱动。成功返回 true。
 *
 * prefer_bounce_lines：搬运缓冲行数。
 *   负数 = 用 board_pins.h 的 LCD_BOUNCE_LINES（NVS 里没存过时就是这个）；
 *   0    = 不用搬运缓冲，DMA 直接读 PSRAM（会左右抖动，只用于对照实验）；
 *   >0   = 行数，必须整除 480，否则自动退回默认值。
 *   内存不够时自动降档，串口会打印实际用了几行。
 */
bool display_init(int prefer_bounce_lines);

/** 主循环里调用，等价于 lv_timer_handler()。 */
void display_tick();

/**
 * 运行时改像素时钟（诊断用，不写 Flash，重启恢复默认值）。
 * ⚠ 本板实测只认 16MHz：14.55MHz 会整屏蓝/白/绿/红纯色块（面板锁不住画面）。
 * 所以这个接口只用于「救助」和「验证降频会怎样」。
 */
bool display_set_pclk(uint32_t hz);

/**
 * 运行时改手动 DMA 复位间隔（毫秒，0 = 关闭）。
 * 本板驱动已在 VSYNC 里自动复位，默认就是关闭。
 */
bool display_set_restart_interval(uint32_t ms);

/**
 * ★ 搬运缓冲预读提前几块（0/1/2，默认 2）。
 * 0 = 不预读（等于把 IDF 的行为也关掉）；1 = 和 IDF 原行为一样；
 * 2 = 提前两块（600µs），给冷读足够时间落进 L1。
 * 改完请用 stats 看「填充超期限」比例。
 */
bool display_set_prefetch(int blocks);

/** 当前预读提前几块。 */
int display_get_prefetch();

/**
 * 实测「把一块搬运缓冲从 PSRAM 拷进内部 RAM」要多久（参考基线）。
 * 注意：真实填充耗时已经在中断里统计了，直接看 stats 更准。
 */
bool display_bench_psram(int lines, int loops, DisplayBench *out);

/** 取运行期统计。 */
void display_get_stats(DisplayStats *out);

/** 清零统计（不影响画面）。 */
void display_reset_stats();

/* 背光不对外提供接口：这块屏的背光是使能型（只有全亮，调低直接全黑），
   由 display_init() 固定拉满，见 board_pins.h 里的实板结论。 */
