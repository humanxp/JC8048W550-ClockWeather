#include "diag.h"

#include <Arduino.h>
#include <string.h>

#include "board_pins.h"
#include "config.h"
#include "display.h"
#include "settings.h"

/* 命令缓冲区：最长 32 字符（够放 "restart 15000" 这种），静默 200ms 断句 */
#define DIAG_CMD_MAX   32
#define DIAG_IDLE_MS   200

static String   s_cmd;
static uint32_t s_cmd_ms = 0;

/* ------------------------------------------------------------------ */
/*  小工具                                                             */
/* ------------------------------------------------------------------ */
static void print_mem()
{
    Serial.printf("    内存：空闲堆 %u  最大连续堆 %u  PSRAM 剩 %u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)ESP.getFreePsram());
}

/** 取 "pclk 14" 里 "pclk" 后面那段（已去空格） */
static String arg_of(const String &cmd, const char *name)
{
    int n = (int)strlen(name);
    if (cmd.length() <= n) return "";
    String rest = cmd.substring(n);
    rest.trim();
    return rest;
}

/* ------------------------------------------------------------------ */
/*  bench：把「余量够不够」变成数字                                     */
/*                                                                     */
/*  这是整个诊断里最有用的命令。横线的直接原因是「中断里的 PSRAM→内部   */
/*  RAM 拷贝没赶上 DMA 读完当前块」。期限只跟像素时钟和块大小有关：     */
/*        期限(µs) = 块字节数 / (2 × PCLK(Hz)) × 1e6                    */
/*  而拷贝耗时是实测的。余量 = (期限 - 最慢一次) / 期限。               */
/*  同一块屏幕、同一份 WiFi 流量下，余量越大，越不容易看到横线。        */
/* ------------------------------------------------------------------ */
static void cmd_bench()
{
    DisplayStats st;
    display_get_stats(&st);

    Serial.println("[基准] 测「PSRAM -> 内部 RAM」搬一块的耗时；期限 = 一块字节数 / (2×PCLK)");
    Serial.printf("[基准] 当前像素时钟 %u Hz，搬运缓冲 %d 行\n",
                  (unsigned)st.pclk_hz, st.bounce_lines);
    Serial.println("[基准] 行数  一块字节   期限µs |  热: 平均/最慢  余量 |  冷: 平均/最慢  余量");
    print_mem();

    /* 合法的块大小里，能放进渲染缓冲（16 行 = 25.6KB）的那些 */
    static const int k_lines[] = { 4, 6, 8, 10, 12, 16 };
    bool any = false;

    for (int i = 0; i < (int)(sizeof(k_lines) / sizeof(k_lines[0])); i++) {
        DisplayBench b;
        if (!display_bench_psram(k_lines[i], 16, &b)) continue;
        any = true;

        Serial.printf("[基准] %4d %9u %7u | %6u/%6u %+5d%% | %6u/%6u %+5d%%%s\n",
                      b.lines, (unsigned)b.bytes, (unsigned)b.deadline_us,
                      (unsigned)b.warm_avg_us, (unsigned)b.warm_max_us, (int)b.margin_warm_pct,
                      (unsigned)b.cold_avg_us, (unsigned)b.cold_max_us, (int)b.margin_cold_pct,
                      b.margin_cold_pct < 0 ? "  ←冷读超期限" : "");
    }

    if (!any) {
        Serial.println("[基准] 测不了：内部 RAM 里没有可用的目标缓冲（显示没初始化？）");
        return;
    }

    Serial.println("[基准] 读法：");
    Serial.println("[基准]   热余量 = 平时（缓存命中/预读生效）；为负说明连平时都追不上，"
                   "一定会有横线");
    Serial.println("[基准]   冷余量 = WiFi 抢带宽/满屏重绘后的最坏情况；为负就是"
                   "「偶尔闪一下」的来源");
    Serial.println("[基准]   想让余量变大：把行数调小一档（6 → 4），块小、单次拷贝更短");
    Serial.println("[基准]   ⚠ 不要靠降 PCLK：本板低于 16MHz 会整屏色块（见 board_pins.h）");
    Serial.println("[基准]   ⚠ 冷余量那一列恒为负是物理上限：16MHz 下这块屏要 PSRAM 持续供 32MB/s，"
                   "实测冷读只有 ~30MB/s，比值 1.07，改块大小改不掉（见 README）");
    Serial.println("[基准] 天气刷新/滚列表时再敲一次 bench，那才是真正的最坏情况。");
}

/* ------------------------------------------------------------------ */
/*  stats：flush 统计 + 搬运填充统计 + 内存                             */
/* ------------------------------------------------------------------ */
static void cmd_stats()
{
    DisplayStats st;
    display_get_stats(&st);

    const uint32_t avg = st.flush_count ? (st.flush_us_total / st.flush_count) : 0;

    Serial.printf("[统计] 开机以来 flush %u 次，累计 %u µs，平均 %u µs，最长 %u µs\n",
                  (unsigned)st.flush_count, (unsigned)st.flush_us_total,
                  (unsigned)avg, (unsigned)st.flush_us_max);
    Serial.printf("[统计] 搬运缓冲 %d 行，预读提前 %d 块，像素时钟 %u Hz，手动 DMA 复位 %u ms%s\n",
                  st.bounce_lines, st.prefetch, (unsigned)st.pclk_hz, (unsigned)st.restart_ms,
                  st.restart_ms == 0 ? "（关闭）" : "");

    /* ★ 核心指标：中断里把一块从 PSRAM 拷进内部 RAM 花了多久，有没有超期限 */
    const double late_pct = st.fill_count
        ? (100.0 * (double)st.fill_late / (double)st.fill_count) : 0.0;
    Serial.printf("[统计] 搬运填充 %u 次，平均 %u µs，最长 %u µs（期限 %u µs），"
                  "超期限 %u 次 = %.2f%%%s\n",
                  (unsigned)st.fill_count, (unsigned)st.fill_us_avg, (unsigned)st.fill_us_max,
                  (unsigned)st.fill_deadline_us, (unsigned)st.fill_late, late_pct,
                  st.fill_late == 0 ? "  ✓ 一次都没超"
                                    : (late_pct < 0.05 ? "  ✓ 基本可忽略"
                                                       : "  ← 这就是「左边短线」的来源"));
    Serial.println("[统计] 判读：超期限比例 0% = 干净；<1% = 偶尔闪一下；"
                   ">5% = 明显能看到短线。用 prefetch 0/1/2 对比这个比例。");

    print_mem();
}

/* ------------------------------------------------------------------ */
/*  各条命令                                                           */
/* ------------------------------------------------------------------ */
static void cmd_pclk(const String &rest)
{
    float mhz = rest.toFloat();
    if (mhz < 8.0f || mhz > 24.0f) {
        Serial.println("[命令] 用法：pclk 16（本板只有 16MHz 能正常出图）");
        return;
    }
    uint32_t hz = (uint32_t)(mhz * 1000000.0f + 0.5f);
    if (display_set_pclk(hz)) {
        Serial.printf("[命令] 像素时钟 -> 请求 %.2f MHz（驱动按 160MHz 分频取最接近值）\n",
                      (double)mhz);
        if (hz < 16000000u) {
            Serial.println("[命令] ⚠ 本板实测：低于 16MHz 面板锁不住画面"
                           "（整屏蓝/白/绿/红色块乱跳），发 pclk 16 立刻恢复");
        }
    } else {
        Serial.println("[命令] 改像素时钟失败（面板没建好，或超出 8~24MHz）");
    }
}

static void cmd_bounce(const String &rest)
{
    if (rest.length() == 0) {
        int saved = settings_bounce_lines();
        if (saved < 0) {
            Serial.printf("[命令] 没存过，用代码默认 %d 行\n", (int)LCD_BOUNCE_LINES);
        } else {
            Serial.printf("[命令] 当前存的是 %d 行%s\n", saved,
                          saved == 0 ? "（不用搬运缓冲，DMA 直接读 PSRAM）" : "");
        }
        Serial.println("[命令] 用法：bounce 12 / bounce 16 / bounce 0（不用搬运缓冲）"
                       "/ bounce auto（清除，回代码默认）");
        return;
    }

    int lines;
    if (rest == "auto" || rest == "clear" || rest == "default") {
        lines = -1;                       /* 清除这条 NVS 记录 */
    } else {
        lines = rest.toInt();
        if (lines == 0) {
            Serial.println("[命令] v2.3 起用 no_fb 模式，搬运缓冲是**必须**的"
                           "（IDF 会拒绝 no_fb + 无搬运缓冲）。");
            Serial.println("[命令] 想看「不用搬运缓冲」的效果（会左右抖）只能烧旧固件；"
                           "现在最小用 bounce 3。");
            return;
        }
        if (lines < 0 || lines > 60 || (LCD_HEIGHT % lines) != 0) {
            Serial.printf("[命令] %d 行不合法：必须能整除 %d（3/4/6/8/12/16/20/24…），"
                          "auto = 回默认\n",
                          lines, LCD_HEIGHT);
            return;
        }
        if (lines > 20) {
            Serial.printf("[命令] ⚠ %d 行 = 每份 %u 字节，超过 20 行填充余量只剩几个百分点，"
                          "可能反而超期限（见 board_pins.h）\n",
                          lines, (unsigned)(LCD_WIDTH * lines * 2));
        }
    }

    settings_save_bounce_lines(lines);
    Serial.println("[命令] 1.5 秒后重启，开机日志里会打印实际用了几行");
    Serial.flush();
    delay(1500);
    ESP.restart();
}

static void cmd_restart(const String &rest)
{
    int ms = rest.toInt();
    if (rest.length() == 0 || ms < 0) {
        Serial.println("[命令] 用法：restart 15000（毫秒，0 = 关闭手动复位）");
        return;
    }
    if (!display_set_restart_interval((uint32_t)ms)) {
        Serial.println("[命令] 间隔太大（上限 600000ms）");
    }
}

/* ★ 预读提前几块：本轮排查「左边短线」的核心旋钮 */
static void cmd_prefetch(const String &rest)
{
    if (rest.length() == 0) {
        Serial.printf("[命令] 当前预读提前 %d 块；用法：prefetch 0 / 1 / 2\n",
                      display_get_prefetch());
        return;
    }
    int n = rest.toInt();
    if (!display_set_prefetch(n)) {
        Serial.println("[命令] 只能是 0 / 1 / 2（0=不预读，1=和 IDF 一样，2=提前两块）");
        return;
    }
    Serial.println("[命令] 现在敲 stats 看「超期限」比例；等一分钟再敲一次更准");
}

/* ------------------------------------------------------------------ */
/*  命令分发                                                           */
/* ------------------------------------------------------------------ */
static void handle(const String &raw)
{
    String cmd = raw;
    cmd.trim();
    cmd.toLowerCase();
    if (cmd.length() == 0) return;

    if (cmd == "help" || cmd == "?") {
        diag_print_help();
    } else if (cmd == "bench") {
        cmd_bench();
    } else if (cmd == "stats") {
        cmd_stats();
    } else if (cmd.startsWith("pclk")) {
        cmd_pclk(arg_of(cmd, "pclk"));
    } else if (cmd.startsWith("bounce")) {
        cmd_bounce(arg_of(cmd, "bounce"));
    } else if (cmd.startsWith("restart")) {
        cmd_restart(arg_of(cmd, "restart"));
    } else if (cmd.startsWith("prefetch")) {
        cmd_prefetch(arg_of(cmd, "prefetch"));
    } else if (cmd == "reboot") {
        Serial.println("[命令] 重启…");
        Serial.flush();
        delay(100);
        ESP.restart();
    } else {
        Serial.printf("[命令] 不认识：%s（敲 help 看列表）\n", cmd.c_str());
    }
}

/* ------------------------------------------------------------------ */
/*  收串口                                                             */
/* ------------------------------------------------------------------ */
void diag_poll()
{
    bool done = false;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        s_cmd_ms = millis();

        if (c == '\n' || c == '\r') {
            if (s_cmd.length() > 0) {
                handle(s_cmd);
                s_cmd = "";
            }
            done = true;
        } else if (c >= 0x20 && c < 0x7F && s_cmd.length() < DIAG_CMD_MAX) {
            s_cmd += c;      /* 只收可见 ASCII：串口监视器回显/杂波不会污染命令 */
        }
    }

    /* 行尾符设成「无」时不会来 \r\n：静默 200ms 就当一条命令结束 */
    if (!done && s_cmd.length() > 0 && (uint32_t)(millis() - s_cmd_ms) > DIAG_IDLE_MS) {
        handle(s_cmd);
        s_cmd = "";
    }
}

void diag_print_help()
{
    Serial.println("[命令] 串口诊断（115200）");
    Serial.println("[命令]   help            列出这些命令");
    Serial.println("[命令]   bench           ★ 实测 PSRAM 拷贝余量（横线问题的核心指标）");
    Serial.println("[命令]   stats           flush 耗时 + 搬运填充「超期限」比例（核心指标）");
    Serial.println("[命令]   prefetch 2      搬运缓冲预读提前几块（0/1/2，默认 2）");
    Serial.println("[命令]   pclk 14         降像素时钟（⚠ 本板低于 16MHz 会整屏色块，只做实验）");
    Serial.println("[命令]   pclk 16         回 16MHz（本板唯一能正常出图的值，也是默认）");
    Serial.println("[命令]   bounce 16       搬运缓冲行数（越大短线越稀），存 NVS 并自动重启");
    Serial.println("[命令]   bounce auto     清除设置，回到 board_pins.h 的默认值（16 行）");
    Serial.println("[命令]   restart 0       关闭手动 DMA 复位（本板驱动本来就在 VSYNC 自愈）");
    Serial.println("[命令]   reboot          软件重启");
    Serial.println("[命令] 提示：行尾符设「无」也行，静默 200ms 自动断句。");
}
