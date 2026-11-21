#include "ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "fonts.h"
#include "weather_icon.h"

/* ------------------------------ 配色 ------------------------------
 * iOS 蓝调（深色）+ Apple HIG 系统色。
 * 背景走 iOS 天气那种「夜空蓝」渐变，卡片是偏蓝的深色，比纯黑更有氛围。
 *     systemBlue  #0A84FF    systemTeal #64D2FF
 *     systemGreen #30D158    systemOrange #FFB340
 * ------------------------------------------------------------------ */
#define COL_BG_TOP      lv_color_hex(0x061020)   /* 夜空最深处 */
#define COL_BG_BOTTOM   lv_color_hex(0x123A66)   /* 往下透出蓝 */

#define COL_CARD        lv_color_hex(0x1C3E63)   /* 卡片渐变上端 */
#define COL_CARD_2      lv_color_hex(0x143050)   /* 卡片渐变下端 */
#define COL_CARD_LINE   lv_color_hex(0x2E5A85)   /* 描边（比卡片略亮） */

#define COL_TEXT        lv_color_hex(0xFFFFFF)
#define COL_TEXT_MID    lv_color_hex(0xC3D9F0)
#define COL_TEXT_DIM    lv_color_hex(0x8FB2D4)

#define COL_ACCENT      lv_color_hex(0x0A84FF)   /* systemBlue */
#define COL_ACCENT_LT   lv_color_hex(0x64D2FF)   /* systemTeal */
#define COL_WARM        lv_color_hex(0xFFB340)   /* 最高温 */
#define COL_COOL        lv_color_hex(0x64D2FF)   /* 最低温 */
#define COL_CITY        lv_color_hex(0xFFFFFF)

#define COL_GREEN       lv_color_hex(0x30D158)
#define COL_AMBER       lv_color_hex(0xFFD60A)
#define COL_RED         lv_color_hex(0xFF453A)
#define COL_BAR_BG      lv_color_hex(0x2A4C73)
#define COL_SHADOW      lv_color_hex(0x000814)

/* ---------------------------- 布局常量 ----------------------------
 * 左右结构：左边时钟区，右边天气卡。
 *
 *   时钟 193px 粗体的 "08:42" = 2.685 × 193 ≈ 518px，
 *   所以时钟区至少 542px（左右各留 12px）。
 *   剩下的给天气卡：558 ~ 784 = 226px。
 *
 *   ⚠ 226px 已经是天气卡的极限下限：最长描述「雷阵雨伴冰雹」在 32px 下
 *     占 192px，加左右留白至少 224px。想让天气卡更宽，就必须把时钟字号降下来。
 *
 *   0    ~ 58    顶栏
 *   58   ~ 352   中间区（左：时钟 542px ／ 右：天气卡 558~784）
 *   356  ~ 468   三天预报（今天 / 明天 / 后天）
 * ------------------------------------------------------------------ */
#define TOPBAR_H        58
#define CLOCK_W         542
#define CARD_X          558
#define CARD_Y          66
#define CARD_W          226
#define CARD_H          280

#define FC_Y            356
#define FC_H            112
#define FC_W            248
#define FC_X0           16
#define FC_GAP          12
#define FC_BAR_W        (FC_W - 32)   /* 温度区间条宽度 */

/* ---------------------------- 控件指针 ---------------------------- */
static lv_obj_t *s_lbl_citydate = nullptr;
static lv_obj_t *s_lbl_status   = nullptr;
static lv_obj_t *s_lbl_time     = nullptr;
static lv_obj_t *s_lbl_sec      = nullptr;
static lv_obj_t *s_bar_sec      = nullptr;
static lv_obj_t *s_card_wx      = nullptr;
static lv_obj_t *s_icon_wx      = nullptr;
static lv_obj_t *s_lbl_temp     = nullptr;
static lv_obj_t *s_lbl_desc     = nullptr;

static lv_obj_t *s_lbl_v_appa   = nullptr;
static lv_obj_t *s_lbl_v_humi   = nullptr;
static lv_obj_t *s_lbl_v_wind   = nullptr;
static lv_obj_t *s_lbl_v_pres   = nullptr;

static lv_obj_t *s_fc_card[3]   = {nullptr, nullptr, nullptr};
static lv_obj_t *s_fc_day[3]    = {nullptr, nullptr, nullptr};
static lv_obj_t *s_fc_icon[3]   = {nullptr, nullptr, nullptr};
static lv_obj_t *s_fc_hi[3]     = {nullptr, nullptr, nullptr};   /* 最高温（暖色） */
static lv_obj_t *s_fc_sep[3]    = {nullptr, nullptr, nullptr};   /* 中间的斜杠 */
static lv_obj_t *s_fc_lo[3]     = {nullptr, nullptr, nullptr};   /* 最低温（冷色） */
static lv_obj_t *s_fc_bar[3]    = {nullptr, nullptr, nullptr};   /* 温度区间条 */

static lv_obj_t *s_dot_status   = nullptr;                       /* 顶栏状态圆点 */

/* ---------------------------- 状态缓存 ---------------------------- */
static char s_city[24]        = CITY_NAME;
static char s_status_text[64] = "启动中";
static char s_update_text[16] = "";
static UiStatus s_status      = UI_STATUS_CONNECTING;
static int  s_wday            = 0;

/* 各标签上次设置过的内容，用于「内容没变就不重绘」（见 label_set_if_changed） */
static char s_last_time[16]      = "";
static char s_last_sec[8]        = "";
static char s_last_status[128]   = "";
static char s_last_citydate[128] = "";
static char s_last_fc_day[3][16] = {"", "", ""};
static char s_last_fc_hi[3][12]  = {"", "", ""};
static char s_last_fc_lo[3][12]  = {"", "", ""};
static lv_color_t s_last_status_color = COL_TEXT_DIM;
static int  s_last_bar_sec       = -1;

/* ------------------------------------------------------------------ */
/*  小工具                                                             */
/* ------------------------------------------------------------------ */

/**
 * 安全的字符串拷贝：超长时按 UTF-8 字符边界截断。
 * 直接用 snprintf 截断中文会把一个汉字切成两半，屏幕上出现乱码方块。
 */
static void copy_safe(char *dst, size_t cap, const char *src)
{
    if (dst == nullptr || cap == 0) return;
    if (src == nullptr) { dst[0] = '\0'; return; }

    size_t len = strlen(src);
    if (len > cap - 1) {
        len = cap - 1;
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/** 数字转文字，NAN 显示成 "--" */
static void fmt_num(char *buf, size_t n, const char *fmt, float v)
{
    if (isnan(v)) {
        snprintf(buf, n, "--");
    } else {
        snprintf(buf, n, fmt, v);
    }
}

/**
 * 只在文字真的变了才更新标签 —— 这个函数是修「时间区域一直闪」的关键。
 *
 * ⚠ 绝对不要无条件调用 lv_label_set_text()！
 *   LVGL v8.4 的实现第一行就是 lv_obj_invalidate(obj)，在任何内容比较之前，
 *   所以哪怕文字一模一样也会让整块标签重绘。
 *   时钟本来每 200ms 刷新一次，无条件调用就等于让 190px 的大钟每秒重绘 5 次，
 *   屏幕上就是时间区域一直在闪。
 *
 * 用法：给每个频繁更新的标签配一个 cache 数组，记住上次设过的内容。
 */
static void label_set_if_changed(lv_obj_t *lbl, char *cache, size_t cap, const char *text)
{
    if (lbl == nullptr || text == nullptr) return;
    if (strcmp(cache, text) == 0) return;      /* 内容没变，直接返回，不碰 LVGL */
    copy_safe(cache, cap, text);
    lv_label_set_text(lbl, text);
}

static void style_card(lv_obj_t *o, int radius)
{
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    /* 立体感 = 纵向微渐变（上浅下深）+ 一圈柔和投影。
       软件渲染，投影别开太大（18px / 40% 是实测不卡的量级）。 */
    lv_obj_set_style_bg_color(o, COL_CARD, 0);
    lv_obj_set_style_bg_grad_color(o, COL_CARD_2, 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, COL_CARD_LINE, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 18, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_40, 0);
    lv_obj_set_style_shadow_color(o, COL_SHADOW, 0);
    lv_obj_set_style_shadow_ofs_y(o, 5, 0);
}

/** 一条 1px 的分隔线（颜色和透明度单独给） */
static lv_obj_t *make_hline(lv_obj_t *parent, int w, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, w, 1);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

/** 重画顶栏右上角的状态文字 */
static void refresh_status_label()
{
    if (s_lbl_status == nullptr) return;

    char buf[128];
    if (s_update_text[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s · 更新 %s", s_status_text, s_update_text);
    } else {
        snprintf(buf, sizeof(buf), "%s", s_status_text);
    }

    lv_color_t c = COL_TEXT_DIM;
    switch (s_status) {
        case UI_STATUS_ONLINE:     c = COL_GREEN; break;
        case UI_STATUS_CONNECTING: c = COL_AMBER; break;
        case UI_STATUS_OFFLINE:
        case UI_STATUS_ERROR:      c = COL_RED;   break;
    }

    /* 文字和颜色都没变就什么都不做（设样式同样会触发重绘） */
    if (strcmp(s_last_status, buf) == 0 && s_last_status_color.full == c.full) return;

    copy_safe(s_last_status, sizeof(s_last_status), buf);
    s_last_status_color = c;
    lv_label_set_text(s_lbl_status, buf);
    lv_obj_set_style_text_color(s_lbl_status, c, 0);
    /* 圆点和文字同色，一眼能看出联网状态 */
    if (s_dot_status) lv_obj_set_style_bg_color(s_dot_status, c, 0);
}

/** 重画左上角「城市 · 日期」 */
static void refresh_citydate()
{
    if (s_lbl_citydate == nullptr) return;

    /* 时间还没同步时，日期先用占位符，避免显示 1970 年 */
    struct tm t;
    char datebuf[48];
    if (getLocalTime(&t, 0) && (time(nullptr) > 1577836800)) {
        static const char *wk[] = {"日", "一", "二", "三", "四", "五", "六"};
        snprintf(datebuf, sizeof(datebuf), "%d月%d日 星期%s",
                 t.tm_mon + 1, t.tm_mday, wk[t.tm_wday % 7]);
    } else {
        snprintf(datebuf, sizeof(datebuf), "正在同步时间…");
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "%s%s · %s", s_city, UI_TIMEZONE_LABEL, datebuf);
    label_set_if_changed(s_lbl_citydate, s_last_citydate, sizeof(s_last_citydate), buf);
}

/* ------------------------------------------------------------------ */
/*  创建界面                                                           */
/* ------------------------------------------------------------------ */
void ui_init()
{
    /* ---------------- 根背景 ---------------- */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, COL_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(scr, COL_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ---------------- 顶栏 ---------------- */
    /* 两个标签都限宽 + 超长显示省略号：状态栏要显示错误信息，
       不加限制的话长中文会把左边的城市名压掉。 */
    s_lbl_citydate = make_label(scr, &font_cjk_22, COL_CITY, "");
    lv_obj_set_width(s_lbl_citydate, 356);
    lv_label_set_long_mode(s_lbl_citydate, LV_LABEL_LONG_DOT);
    lv_obj_align(s_lbl_citydate, LV_ALIGN_TOP_LEFT, 26, 16);

    /* 右上角：状态圆点 + 文字，用 flex 让它们自动挨在一起靠右 */
    lv_obj_t *row_status = lv_obj_create(scr);
    lv_obj_remove_style_all(row_status);
    lv_obj_clear_flag(row_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row_status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_status, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row_status, 8, 0);
    lv_obj_align(row_status, LV_ALIGN_TOP_RIGHT, -26, 18);

    s_dot_status = lv_obj_create(row_status);
    lv_obj_remove_style_all(s_dot_status);
    lv_obj_set_size(s_dot_status, 10, 10);
    lv_obj_set_style_radius(s_dot_status, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot_status, COL_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(s_dot_status, LV_OPA_COVER, 0);

    s_lbl_status = make_label(row_status, &font_cjk_22, COL_TEXT_MID, "");
    lv_obj_set_width(s_lbl_status, 320);
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_lbl_status, LV_TEXT_ALIGN_RIGHT, 0);

    /* 顶栏下方一条极淡的分隔线，把标题区和内容区分开 */
    lv_obj_t *top_line = make_hline(scr, 800, COL_CARD_LINE, LV_OPA_50);
    lv_obj_set_pos(top_line, 0, TOPBAR_H - 2);

    /* ---------------- 时钟区（左侧）----------------
       纵向 flex：上面是 193px 粗体时间，下面一行是秒进度条 + 秒数。
       不写死纵坐标，交给 flex 居中，改字号也不会跑偏。 */
    lv_obj_t *cont_clock = lv_obj_create(scr);
    lv_obj_remove_style_all(cont_clock);
    lv_obj_clear_flag(cont_clock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cont_clock, CLOCK_W, 480 - TOPBAR_H - FC_H - 16);
    lv_obj_set_pos(cont_clock, 0, TOPBAR_H);
    lv_obj_set_flex_flow(cont_clock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont_clock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont_clock, 18, 0);

    s_lbl_time = make_label(cont_clock, &font_clock_193, COL_TEXT, "--:--");

#if UI_SHOW_SECONDS
    lv_obj_t *row_sec = lv_obj_create(cont_clock);
    lv_obj_remove_style_all(row_sec);
    lv_obj_clear_flag(row_sec, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row_sec, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row_sec, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_sec, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row_sec, 14, 0);

    /* 秒进度条 + 秒数并排 */
    s_bar_sec = lv_bar_create(row_sec);
    lv_obj_remove_style_all(s_bar_sec);
    lv_obj_set_size(s_bar_sec, 360, 10);
    lv_obj_set_style_radius(s_bar_sec, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_bar_sec, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(s_bar_sec, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_bar_sec, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(s_bar_sec, COL_ACCENT_LT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(s_bar_sec, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar_sec, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_sec, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar_sec, 0, 59);

    s_lbl_sec = make_label(row_sec, &font_digits_44, COL_ACCENT, "00");
#else
    s_lbl_sec = nullptr;
#endif

    /* ---------------- 天气卡片（右侧竖卡）----------------
       卡片只有 226px 宽（是极限值）：最长描述「雷阵雨伴冰雹」在 32px 下
       占 192px，所以图标缩到 76px、温度起点左移到 x=94，才塞得下。 */
    s_card_wx = lv_obj_create(scr);
    style_card(s_card_wx, 22);
    lv_obj_set_size(s_card_wx, CARD_W, CARD_H);
    lv_obj_set_pos(s_card_wx, CARD_X, CARD_Y);

    /* 图标背后一团柔光，做视觉焦点（先创建 = 在最底层） */
    lv_obj_t *glow = lv_obj_create(s_card_wx);
    lv_obj_remove_style_all(glow);
    lv_obj_set_size(glow, 76, 76);
    lv_obj_align(glow, LV_ALIGN_TOP_LEFT, 10, 16);   /* 右边缘 x=86 */
    lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(glow, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(glow, LV_OPA_20, 0);

    s_icon_wx = weather_icon_create(s_card_wx, WICON_UNKNOWN, 68, COL_CARD);
    lv_obj_align(s_icon_wx, LV_ALIGN_TOP_LEFT, 14, 20);

    s_lbl_temp = make_label(s_card_wx, &font_digits_44, COL_TEXT, "--°");
    lv_obj_align(s_lbl_temp, LV_ALIGN_TOP_LEFT, 96, 30);   /* 右边缘最多 213，卡内可用 216 */

    s_lbl_desc = make_label(s_card_wx, &font_cjk_32, COL_TEXT_MID, "加载中");
    lv_obj_align(s_lbl_desc, LV_ALIGN_TOP_LEFT, 14, 110);

    /* 分隔线，把「图标 + 温度 + 描述」和下面的四项要素分开 */
    lv_obj_t *wx_sep = make_hline(s_card_wx, CARD_W - 32, COL_CARD_LINE, LV_OPA_70);
    lv_obj_align(wx_sep, LV_ALIGN_TOP_LEFT, 16, 150);

    /* 四行要素：左边标签、右边数值（iOS 分组列表样式） */
    struct { const char *name; lv_obj_t **slot; } rows[4] = {
        { "体感", &s_lbl_v_appa },
        { "湿度", &s_lbl_v_humi },
        { "风速", &s_lbl_v_wind },
        { "气压", &s_lbl_v_pres },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *nm = make_label(s_card_wx, &font_cjk_22, COL_TEXT_DIM, rows[i].name);
        lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 16, 158 + i * 30);

        *rows[i].slot = make_label(s_card_wx, &font_cjk_22, COL_TEXT, "--");
        lv_obj_align(*rows[i].slot, LV_ALIGN_TOP_RIGHT, -16, 158 + i * 30);

        /* 行与行之间一条内缩的细分隔线 */
        if (i < 3) {
            lv_obj_t *sep = make_hline(s_card_wx, CARD_W - 32, COL_CARD_LINE, LV_OPA_70);
            lv_obj_align(sep, LV_ALIGN_TOP_LEFT, 16, 158 + (i + 1) * 30 - 3);
        }
    }

    /* ---------------- 三天预报 ---------------- */
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = lv_obj_create(scr);
        style_card(c, 20);
        lv_obj_set_size(c, FC_W, FC_H);
        lv_obj_set_pos(c, FC_X0 + i * (FC_W + FC_GAP), FC_Y);
        s_fc_card[i] = c;

        s_fc_day[i] = make_label(c, &font_cjk_22, COL_TEXT, "--");
        lv_obj_align(s_fc_day[i], LV_ALIGN_TOP_LEFT, 16, 10);

        s_fc_icon[i] = weather_icon_create(c, WICON_UNKNOWN, 52, COL_CARD);
        lv_obj_align(s_fc_icon[i], LV_ALIGN_TOP_RIGHT, -12, 8);

        /* 照 iOS 天气的做法：最高温用白色（主），最低温用灰色（次），
           彩色留给下面那根区间条，整体更干净。 */
        lv_obj_t *row_t = lv_obj_create(c);
        lv_obj_remove_style_all(row_t);
        lv_obj_clear_flag(row_t, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(row_t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row_t, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row_t, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(row_t, 6, 0);
        lv_obj_align(row_t, LV_ALIGN_TOP_LEFT, 16, 46);

        s_fc_hi[i]  = make_label(row_t, &font_digits_32, COL_TEXT, "--°");
        s_fc_sep[i] = make_label(row_t, &font_digits_32, COL_TEXT_DIM, "/");
        s_fc_lo[i]  = make_label(row_t, &font_digits_32, COL_TEXT_DIM, "--°");

        /* 温度区间条：底槽 + 一根按当日最低~最高定位的渐变条 */
        lv_obj_t *track = lv_obj_create(c);
        lv_obj_remove_style_all(track);
        lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(track, FC_BAR_W, 6);
        lv_obj_set_pos(track, 16, 90);
        lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(track, COL_BAR_BG, 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(track, 0, 0);

        s_fc_bar[i] = lv_obj_create(track);
        lv_obj_remove_style_all(s_fc_bar[i]);
        lv_obj_clear_flag(s_fc_bar[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_fc_bar[i], 60, 6);
        lv_obj_set_pos(s_fc_bar[i], 0, 0);
        lv_obj_set_style_radius(s_fc_bar[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_fc_bar[i], COL_COOL, 0);
        lv_obj_set_style_bg_grad_color(s_fc_bar[i], COL_WARM, 0);
        lv_obj_set_style_bg_grad_dir(s_fc_bar[i], LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(s_fc_bar[i], LV_OPA_COVER, 0);
    }

    refresh_citydate();
    refresh_status_label();
}

/* ------------------------------------------------------------------ */
/*  时间刷新                                                           */
/* ------------------------------------------------------------------ */
void ui_update_time(const struct tm *t)
{
    if (t == nullptr) return;

    char buf[24];

#if USE_24H_FORMAT
    snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
#else
    int h = t->tm_hour % 12;
    if (h == 0) h = 12;
    snprintf(buf, sizeof(buf), "%d:%02d", h, t->tm_min);
#endif
    /* 大钟只在分钟真正变化时才重绘（见 label_set_if_changed 的说明） */
    label_set_if_changed(s_lbl_time, s_last_time, sizeof(s_last_time), buf);

#if UI_SHOW_SECONDS
    snprintf(buf, sizeof(buf), "%02d", t->tm_sec);
    label_set_if_changed(s_lbl_sec, s_last_sec, sizeof(s_last_sec), buf);
    /* 秒进度条同理：值没变就别碰它 */
    if (s_bar_sec && t->tm_sec != s_last_bar_sec) {
        s_last_bar_sec = t->tm_sec;
        lv_bar_set_value(s_bar_sec, t->tm_sec, LV_ANIM_OFF);
    }
#endif

    /* 日期只在跨天时需要重画，这里简单判断星期是否变化 */
    if (t->tm_wday != s_wday) {
        s_wday = t->tm_wday;
        refresh_citydate();
    }
}

/* ------------------------------------------------------------------ */
/*  天气刷新                                                           */
/* ------------------------------------------------------------------ */
void ui_update_weather(const WeatherData *w)
{
    if (w == nullptr || !w->valid) return;

    char buf[48];

    /* --- 当前温度与描述 --- */
    fmt_num(buf, sizeof(buf), "%.1f°", w->temp);
    lv_label_set_text(s_lbl_temp, buf);
    lv_label_set_text(s_lbl_desc, weather_text(w->code));

    /* --- 当前天气图标（重建） --- */
    if (s_icon_wx) {
        lv_obj_del(s_icon_wx);
        s_icon_wx = nullptr;
    }
    s_icon_wx = weather_icon_create(s_card_wx, weather_icon_type(w->code, w->is_day), 68, COL_CARD);
    lv_obj_align(s_icon_wx, LV_ALIGN_TOP_LEFT, 14, 20);

    /* --- 四项要素 --- */
    fmt_num(buf, sizeof(buf), "%.1f°C", w->apparent);
    lv_label_set_text(s_lbl_v_appa, buf);

    fmt_num(buf, sizeof(buf), "%.0f%%", w->humidity);
    lv_label_set_text(s_lbl_v_humi, buf);

    fmt_num(buf, sizeof(buf), "%.1fm/s", w->wind);
    lv_label_set_text(s_lbl_v_wind, buf);

    fmt_num(buf, sizeof(buf), "%.0fhPa", w->pressure);
    lv_label_set_text(s_lbl_v_pres, buf);

    /* --- 三天预报：days[0]=今天、days[1]=明天、days[2]=后天 --- */
    /* 先算出这三天整体的最低/最高，作为温度区间条的刻度基准 */
    float span_min = 1000.0f, span_max = -1000.0f;
    for (int i = 0; i < 3; i++) {
        int di = i;
        if (di < WEATHER_FORECAST_DAYS && w->days[di].valid) {
            if (w->days[di].tmin < span_min) span_min = w->days[di].tmin;
            if (w->days[di].tmax > span_max) span_max = w->days[di].tmax;
        }
    }
    float span = span_max - span_min;
    if (span < 1.0f) span = 1.0f;      /* 防止除零 */

    for (int i = 0; i < 3; i++) {
        int di = i;                           /* 0=今天 1=明天 2=后天 */
        if (di >= WEATHER_FORECAST_DAYS || !w->days[di].valid) {
            lv_obj_add_flag(s_fc_card[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_fc_card[i], LV_OBJ_FLAG_HIDDEN);

        /* 今天 / 明天 / 后天 */
        static const char *day_name[3] = {"今天", "明天", "后天"};
        const char *name = (i < 3) ? day_name[i] : "--";
        label_set_if_changed(s_fc_day[i], s_last_fc_day[i], sizeof(s_last_fc_day[i]), name);

        /* 换图标 */
        if (s_fc_icon[i]) {
            lv_obj_del(s_fc_icon[i]);
            s_fc_icon[i] = nullptr;
        }
        s_fc_icon[i] = weather_icon_create(s_fc_card[i],
                                           weather_icon_type(w->days[di].code, true),
                                           52, COL_CARD);
        lv_obj_align(s_fc_icon[i], LV_ALIGN_TOP_RIGHT, -12, 8);

        /* 最高温暖色、最低温冷色 */
        snprintf(buf, sizeof(buf), "%.0f°", w->days[di].tmax);
        label_set_if_changed(s_fc_hi[i], s_last_fc_hi[i], sizeof(s_last_fc_hi[i]), buf);
        snprintf(buf, sizeof(buf), "%.0f°", w->days[di].tmin);
        label_set_if_changed(s_fc_lo[i], s_last_fc_lo[i], sizeof(s_last_fc_lo[i]), buf);

        /* 温度区间条：按 最低~最高 在整段跨度里的位置和长度来摆 */
        int x  = (int)((w->days[di].tmin - span_min) / span * (float)FC_BAR_W);
        int bw = (int)((w->days[di].tmax - w->days[di].tmin) / span * (float)FC_BAR_W);
        if (bw < 10) bw = 10;
        if (x < 0) x = 0;
        if (x + bw > FC_BAR_W) x = FC_BAR_W - bw;
        if (x < 0) x = 0;
        lv_obj_set_pos(s_fc_bar[i], x, 0);
        lv_obj_set_size(s_fc_bar[i], bw, 6);
    }
}

/* ------------------------------------------------------------------ */
/*  状态与杂项                                                         */
/* ------------------------------------------------------------------ */
void ui_set_status(UiStatus st, const char *text)
{
    s_status = st;
    if (text) {
        copy_safe(s_status_text, sizeof(s_status_text), text);
    }
    refresh_status_label();
}

void ui_set_updated(const char *hhmm)
{
    if (hhmm) {
        copy_safe(s_update_text, sizeof(s_update_text), hhmm);
    } else {
        s_update_text[0] = '\0';
    }
    refresh_status_label();
}

void ui_set_city(const char *city)
{
    if (city && city[0]) {
        copy_safe(s_city, sizeof(s_city), city);
        refresh_citydate();
    }
}
