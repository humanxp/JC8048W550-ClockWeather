#include "ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "fonts.h"
#include "settings.h"
#include "setup_ui.h"
#include "weather_icon.h"

/* ------------------------------ 配色 ------------------------------
 * iOS 蓝调（深色）+ Apple HIG 系统色。
 * 背景走 iOS 天气那种「夜空蓝」渐变，卡片是偏蓝的深色，比纯黑更有氛围。
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
#define COL_CITY        lv_color_hex(0xFFFFFF)

#define COL_GREEN       lv_color_hex(0x30D158)
#define COL_AMBER       lv_color_hex(0xFFD60A)
#define COL_RED         lv_color_hex(0xFF453A)
#define COL_BAR_BG      lv_color_hex(0x2A4C73)
#define COL_SHADOW      lv_color_hex(0x000814)

/* ---------------------------- 看板布局常量 ----------------------------
 * 左右结构：左边时钟区，右边天气卡。
 *
 *   时钟 193px 粗体的 "08:42" ≈ 519px，所以时钟区 542px（左右各留 12px）。
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
#define FC_POP_LBL_W    60                            /* 右边「100%」标签的宽度（实测 56.8px） */
#define FC_BAR_W        (FC_W - 32 - FC_POP_LBL_W - 8) /* 降水概率条宽度 = 148px */

/* --------------------------- 大时钟屏布局 ---------------------------
 * 屏幕竖向分配（每段之间都留了 15px 以上，别再互相碰）：
 *    18  ~  44   右上角「已联网」状态行
 *    62  ~ 274   290px 大时钟（行高 224，居中对齐后上移 72）
 *   297  ~ 335   日期（40px 字，行高 38）
 *   352  ~ 468   底部天气条：当前温度 / 描述 + 体感 / 湿度 / 风速 / 气压
 * ------------------------------------------------------------------ */
#define IDLE_TIME_Y     (-72)
#define IDLE_DATE_Y     (76)
#define IDLE_WX_X       24
#define IDLE_WX_Y       352
#define IDLE_WX_W       752
#define IDLE_WX_H       116

/* ---------------------------- 控件指针 ---------------------------- */
/* 大时钟屏 */
static lv_obj_t *s_idle_scr      = nullptr;
static lv_obj_t *s_idle_time     = nullptr;
static lv_obj_t *s_idle_date     = nullptr;
static lv_obj_t *s_idle_dot      = nullptr;
static lv_obj_t *s_idle_status   = nullptr;   /* 右上角状态文字（错误信息也显示在这） */
static lv_obj_t *s_refresh_btn   = nullptr;   /* 看板顶栏的「刷新天气」按钮 */
static bool      s_weather_refresh_req = false;
static lv_obj_t *s_idle_wx_card  = nullptr;   /* 底部天气条（拿到天气才显示） */
static lv_obj_t *s_idle_wx_icon  = nullptr;
static lv_obj_t *s_idle_wx_temp  = nullptr;
static lv_obj_t *s_idle_wx_desc  = nullptr;
static lv_obj_t *s_idle_wx_v[4]  = {nullptr, nullptr, nullptr, nullptr};   /* 体感/湿度/风速/气压 */

/* 看板屏 */
static lv_obj_t *s_dash_scr    = nullptr;
static lv_obj_t *s_lbl_citydate = nullptr;
static lv_obj_t *s_lbl_status   = nullptr;
static lv_obj_t *s_dot_status   = nullptr;
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
static lv_obj_t *s_fc_hi[3]     = {nullptr, nullptr, nullptr};   /* 最高温 */
static lv_obj_t *s_fc_sep[3]    = {nullptr, nullptr, nullptr};   /* 中间的斜杠 */
static lv_obj_t *s_fc_lo[3]     = {nullptr, nullptr, nullptr};   /* 最低温 */
static lv_obj_t *s_fc_pop_bar[3] = {nullptr, nullptr, nullptr};  /* 降水概率条 */
static lv_obj_t *s_fc_pop_lbl[3] = {nullptr, nullptr, nullptr};  /* 右边的「60%」 */

/* ---------------------------- 状态 ---------------------------- */
enum UiMode { UI_MODE_IDLE = 0, UI_MODE_DASHBOARD };
static UiMode   s_mode          = UI_MODE_IDLE;
static uint32_t s_dash_enter_ms = 0;

static char s_city[24]        = "";
static char s_status_text[64] = "启动中";
static char s_update_text[16] = "";
static UiStatus s_status      = UI_STATUS_CONNECTING;
static int  s_wday            = -1;

/* 各标签上次设置过的内容，用于「内容没变就不重绘」（见 label_set_if_changed） */
static char s_last_time_idle[16]  = "";
static char s_last_time_dash[16]  = "";
static char s_last_idle_date[64]  = "";
static char s_last_idle_temp[12]  = "";
static char s_last_idle_desc[24]  = "";
static char s_last_idle_v[4][16]  = {"", "", "", ""};
static char s_last_citydate[128]  = "";
static char s_last_sec[8]         = "";
static char s_last_status[128]    = "";
static char s_last_fc_day[3][16]  = {"", "", ""};
static char s_last_fc_hi[3][12]   = {"", "", ""};
static char s_last_fc_lo[3][12]   = {"", "", ""};
static char s_last_fc_pop[3][8]   = {"", "", ""};
static lv_color_t s_last_status_color = COL_TEXT_DIM;
static int  s_last_bar_sec        = -1;

/* ------------------------------------------------------------------ */
/*  小工具                                                             */
/* ------------------------------------------------------------------ */

/**
 * 只在文字真的变了才更新标签 —— 这个函数是修「时间区域一直闪」的关键。
 *
 * ⚠ 绝对不要无条件调用 lv_label_set_text()！
 *   LVGL v8.4 的实现第一行就是 lv_obj_invalidate(obj)，在任何内容比较之前，
 *   所以哪怕文字一模一样也会让整块标签重绘。
 *   时钟本来每 200ms 刷新一次，无条件调用就等于让大钟每秒重绘 5 次，
 *   屏幕上就是时间区域一直在闪。
 */
static void label_set_if_changed(lv_obj_t *lbl, char *cache, size_t cap, const char *text)
{
    if (lbl == nullptr || text == nullptr) return;
    if (strcmp(cache, text) == 0) return;      /* 内容没变，直接返回，不碰 LVGL */
    copy_utf8(cache, cap, text);
    lv_label_set_text(lbl, text);
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

/** 两张主屏共用的背景：夜空蓝纵向渐变 */
static void style_screen(lv_obj_t *scr)
{
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, COL_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(scr, COL_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
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

/**
 * 递归清掉 CLICKABLE 标记。
 *
 * 大时钟屏靠「点哪儿都切到看板」工作，而这条路径要求点击能一路穿到最底下那层
 * 透明背景对象。LVGL 的基础对象默认是可点的，底部天气条里的图标又是用好几个
 * 基础对象拼出来的，所以创建完统一清一遍，保证怎么点都能触发切换。
 */
static void make_pass_through(lv_obj_t *o)
{
    if (o == nullptr) return;
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    uint32_t n = lv_obj_get_child_cnt(o);
    for (uint32_t i = 0; i < n; i++) make_pass_through(lv_obj_get_child(o, i));
}

/** 顶栏用的胶囊按钮（大时钟 / 设置 / 刷新天气） */
static lv_obj_t *make_top_button(lv_obj_t *parent, const char *text, int w, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(b, w, 40);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, COL_CARD, 0);
    lv_obj_set_style_bg_grad_color(b, COL_CARD_2, 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, COL_CARD_LINE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);

    lv_obj_t *l = make_label(b, &font_cjk_22, COL_TEXT_MID, text);
    lv_obj_center(l);

    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

/* ------------------------------------------------------------------ */
/*  日期 / 状态                                                        */
/* ------------------------------------------------------------------ */

/** 只填日期部分，成功返回 true；时间没同步时填占位符并返回 false。 */
static bool format_date(char *buf, size_t n)
{
    struct tm t;
    if (getLocalTime(&t, 0) && (time(nullptr) > 1577836800)) {
        static const char *wk[] = {"日", "一", "二", "三", "四", "五", "六"};
        /* 用「周X」而不是「星期X」：看板顶栏要一行放下
           「宁波市 · 9月20日 周六」，写「星期六」会在 220px 里折到第二行 */
        snprintf(buf, n, "%d月%d日 周%s", t.tm_mon + 1, t.tm_mday, wk[t.tm_wday % 7]);
        return true;
    }
    snprintf(buf, n, "正在同步时间…");
    return false;
}

/** 重画大时钟屏下面的日期 */
static void refresh_idle_date()
{
    if (s_idle_date == nullptr) return;
    char datebuf[48];
    format_date(datebuf, sizeof(datebuf));
    label_set_if_changed(s_idle_date, s_last_idle_date, sizeof(s_last_idle_date), datebuf);
}

/** 重画看板左上角「城市 · 日期」 */
static void refresh_citydate()
{
    if (s_lbl_citydate == nullptr) return;

    char datebuf[48];
    format_date(datebuf, sizeof(datebuf));

    char buf[128];
    /* 城市和日期之间只留一个空格，不用「·」：
       font_cjk_22 的「·」是全角（22px），去掉后双位月份也能一行放下 */
    snprintf(buf, sizeof(buf), "%s%s %s", s_city, UI_TIMEZONE_LABEL, datebuf);
    label_set_if_changed(s_lbl_citydate, s_last_citydate, sizeof(s_last_citydate), buf);
}

/** 重画顶栏右上角的状态文字（两张屏的点颜色一起更新） */
static void refresh_status_label()
{
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

    /* 大时钟屏只有一个点 */
    if (s_idle_dot) lv_obj_set_style_bg_color(s_idle_dot, c, 0);
    if (s_dot_status) lv_obj_set_style_bg_color(s_dot_status, c, 0);

    if (s_lbl_status == nullptr) return;

    /* 文字和颜色都没变就什么都不做（设样式同样会触发重绘） */
    if (strcmp(s_last_status, buf) == 0 && s_last_status_color.full == c.full) return;

    copy_utf8(s_last_status, sizeof(s_last_status), buf);
    s_last_status_color = c;

    if (s_lbl_status != nullptr) {
        lv_label_set_text(s_lbl_status, buf);
        lv_obj_set_style_text_color(s_lbl_status, c, 0);
    }
    /* 大时钟屏右上角也显示同一份状态（出错时就是靠它看出原因） */
    if (s_idle_status != nullptr) {
        lv_label_set_text(s_idle_status, buf);
        lv_obj_set_style_text_color(s_idle_status, c, 0);
    }
}

/* ------------------------------------------------------------------ */
/*  事件回调                                                           */
/* ------------------------------------------------------------------ */
static void cb_idle_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
#if DEBUG_SERIAL
    Serial.println("[界面] 大时钟被点 -> 切看板");
#endif
    ui_show_dashboard();
}

/** 看板顶栏「刷新天气」：只打个标记，真正拉取交给主循环去启动 */
static void cb_refresh_weather(lv_event_t *e)
{
    LV_UNUSED(e);
    s_weather_refresh_req = true;
#if DEBUG_SERIAL
    Serial.println("[界面] 点「刷新天气」");
#endif
}

static void cb_dash_to_idle(lv_event_t *e)
{
    LV_UNUSED(e);
#if DEBUG_SERIAL
    Serial.println("[界面] 看板点「大时钟」-> 切回大时钟");
#endif
    ui_show_idle();
}

static void cb_open_settings(lv_event_t *e)
{
    LV_UNUSED(e);
    setup_ui_start_wifi(false);
}

/* ------------------------------------------------------------------ */
/*  大时钟屏                                                           */
/* ------------------------------------------------------------------ */
static void create_idle_screen()
{
    s_idle_scr = lv_obj_create(nullptr);
    style_screen(s_idle_scr);

    /* 整屏可点：点一下切到看板。
       先铺一层透明可点背景，再把文字放上去 —— 标签本身不接收点击，
       点击会穿到背景上，所以点哪儿都管用。 */
    lv_obj_t *hit = lv_obj_create(s_idle_scr);
    lv_obj_remove_style_all(hit);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hit, 800, 480);
    lv_obj_set_pos(hit, 0, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, cb_idle_clicked, LV_EVENT_CLICKED, nullptr);

    s_idle_time = make_label(s_idle_scr, &font_clock_290, COL_TEXT, "--:--");
    lv_obj_align(s_idle_time, LV_ALIGN_CENTER, 0, IDLE_TIME_Y);

    s_idle_date = make_label(s_idle_scr, &font_cjk_40, COL_TEXT_MID, "");
    lv_obj_align(s_idle_date, LV_ALIGN_CENTER, 0, IDLE_DATE_Y);

    /* ---------------- 最下面：当前详细天气 ----------------
       图标 + 温度 + 描述，右边竖线隔开四列：体感 / 湿度 / 风速 / 气压。
       还没拿到天气之前整条隐藏，免得开局挂着一排 "--"。 */
    s_idle_wx_card = lv_obj_create(s_idle_scr);
    style_card(s_idle_wx_card, 20);
    lv_obj_set_size(s_idle_wx_card, IDLE_WX_W, IDLE_WX_H);
    lv_obj_set_pos(s_idle_wx_card, IDLE_WX_X, IDLE_WX_Y);
    lv_obj_add_flag(s_idle_wx_card, LV_OBJ_FLAG_HIDDEN);

    s_idle_wx_icon = weather_icon_create(s_idle_wx_card, WICON_UNKNOWN, 56, COL_CARD);
    lv_obj_align(s_idle_wx_icon, LV_ALIGN_LEFT_MID, 16, 0);

    s_idle_wx_temp = make_label(s_idle_wx_card, &font_digits_44, COL_TEXT, "--°");
    lv_obj_align(s_idle_wx_temp, LV_ALIGN_LEFT_MID, 80, -22);

    s_idle_wx_desc = make_label(s_idle_wx_card, &font_cjk_22, COL_TEXT_MID, "加载中");
    lv_obj_set_width(s_idle_wx_desc, 160);
    lv_label_set_long_mode(s_idle_wx_desc, LV_LABEL_LONG_DOT);
    lv_obj_align(s_idle_wx_desc, LV_ALIGN_LEFT_MID, 80, 34);

    /* 竖分隔线 */
    lv_obj_t *vline = make_hline(s_idle_wx_card, 1, COL_CARD_LINE, LV_OPA_70);
    lv_obj_set_size(vline, 1, 76);
    lv_obj_align(vline, LV_ALIGN_LEFT_MID, 252, 0);

    /* 四列要素：上面小字标签、下面数值 */
    static const char *const wx_names[4] = { "体感", "湿度", "风速", "气压" };
    for (int i = 0; i < 4; i++) {
        int x = 268 + i * 121;

        lv_obj_t *nm = make_label(s_idle_wx_card, &font_cjk_22, COL_TEXT_DIM, wx_names[i]);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, x, -26);

        s_idle_wx_v[i] = make_label(s_idle_wx_card, &font_cjk_22, COL_TEXT, "--");
        lv_obj_align(s_idle_wx_v[i], LV_ALIGN_LEFT_MID, x, 22);
    }

    /* 整条天气 + 里面的图标都不能吃点击，否则点那块就切不到看板了 */
    make_pass_through(s_idle_wx_card);

    /* ---------------- 右上角：联网状态圆点 + 文字 ----------------
       大时钟页保持干净（不放按钮，整屏只有时间、日期、底部天气）。
       「刷新天气」按钮在看板那一页的顶栏；出错原因这里也能看到。 */
    lv_obj_t *row_status = lv_obj_create(s_idle_scr);
    lv_obj_remove_style_all(row_status);
    lv_obj_clear_flag(row_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row_status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_status, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row_status, 8, 0);
    lv_obj_align(row_status, LV_ALIGN_TOP_RIGHT, -20, 18);

    s_idle_dot = lv_obj_create(row_status);
    lv_obj_remove_style_all(s_idle_dot);
    lv_obj_clear_flag(s_idle_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_idle_dot, 10, 10);
    lv_obj_set_style_radius(s_idle_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_idle_dot, COL_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(s_idle_dot, LV_OPA_COVER, 0);

    s_idle_status = make_label(row_status, &font_cjk_22, COL_TEXT_DIM, "");
    /* 大时钟页没有按钮，整条宽度都可以给状态/报错文字用（能显示完整原因）。
       高度同样锁一行，超长时打点而不是换行 */
    lv_obj_set_size(s_idle_status, 600, lv_font_get_line_height(&font_cjk_22));
    lv_label_set_long_mode(s_idle_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_idle_status, LV_TEXT_ALIGN_RIGHT, 0);
}

/* ------------------------------------------------------------------ */
/*  看板屏                                                             */
/* ------------------------------------------------------------------ */
static void create_dashboard_screen()
{
    s_dash_scr = lv_obj_create(nullptr);
    style_screen(s_dash_scr);

    /* ---------------- 顶栏 ----------------
       横向预算（800px）：左边「城市 日期」，中间状态，右边三个按钮。
         城市日期 26~246（220，固定一行高）
         状态行   254~524（圆点10 + 间距8 + 文字252）
         按钮     532~784（刷新天气96 + 大时钟76 + 设置64，间距各 8）

       ⚠ 两个坑：
       1) 城市日期必须用「固定宽度 + 固定一行高」。LVGL 的 LONG_DOT 只在
          高度被限死时才会打点；高度自适应的话它直接换行，
          于是「周日」就掉到第二行去了（实测 227px vs 220px 就会折）。
       2) 日期用「周X」不用「星期X」，城市和日期之间也不加「·」——
          font_cjk_22 里「·」是全角 22px，「宁波市 · 12月28日 周六」要 240px，
          去掉后只要 213px，220px 就放得下双位月份了。 */
    s_lbl_citydate = make_label(s_dash_scr, &font_cjk_22, COL_CITY, "");
    lv_obj_set_size(s_lbl_citydate, 220, lv_font_get_line_height(&font_cjk_22));
    lv_label_set_long_mode(s_lbl_citydate, LV_LABEL_LONG_DOT);
    lv_obj_align(s_lbl_citydate, LV_ALIGN_TOP_LEFT, 26, 16);

    /* 右上角三个按钮（从右往左：设置 / 大时钟 / 刷新天气） */
    lv_obj_t *btn_settings = make_top_button(s_dash_scr, "设置", 64, cb_open_settings);
    lv_obj_align(btn_settings, LV_ALIGN_TOP_RIGHT, -16, 9);

    lv_obj_t *btn_idle = make_top_button(s_dash_scr, "大时钟", 76, cb_dash_to_idle);
    lv_obj_align(btn_idle, LV_ALIGN_TOP_RIGHT, -88, 9);

    /* 「刷新天气」放在看板这一页（大时钟页保持干净，只有时间和天气） */
    s_refresh_btn = make_top_button(s_dash_scr, "刷新天气", 96, cb_refresh_weather);
    lv_obj_align(s_refresh_btn, LV_ALIGN_TOP_RIGHT, -172, 9);

    /* 状态圆点 + 文字（在按钮左边） */
    lv_obj_t *row_status = lv_obj_create(s_dash_scr);
    lv_obj_remove_style_all(row_status);
    lv_obj_clear_flag(row_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row_status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_status, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row_status, 8, 0);
    lv_obj_align(row_status, LV_ALIGN_TOP_RIGHT, -276, 18);

    s_dot_status = lv_obj_create(row_status);
    lv_obj_remove_style_all(s_dot_status);
    lv_obj_set_size(s_dot_status, 10, 10);
    lv_obj_set_style_radius(s_dot_status, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot_status, COL_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(s_dot_status, LV_OPA_COVER, 0);

    /* 252px：够显示「天气连不上：connection refused」的大半句；
       完整那句在大时钟页（600px）和串口里 */
    s_lbl_status = make_label(row_status, &font_cjk_22, COL_TEXT_MID, "");
    lv_obj_set_size(s_lbl_status, 252, lv_font_get_line_height(&font_cjk_22));
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_lbl_status, LV_TEXT_ALIGN_RIGHT, 0);

    /* 顶栏下方一条极淡的分隔线 */
    lv_obj_t *top_line = make_hline(s_dash_scr, 800, COL_CARD_LINE, LV_OPA_50);
    lv_obj_set_pos(top_line, 0, TOPBAR_H - 2);

    /* ---------------- 时钟区（左侧）----------------
       纵向 flex：上面是 193px 粗体时间，下面一行是秒进度条 + 秒数。 */
    lv_obj_t *cont_clock = lv_obj_create(s_dash_scr);
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
    s_card_wx = lv_obj_create(s_dash_scr);
    style_card(s_card_wx, 22);
    lv_obj_set_size(s_card_wx, CARD_W, CARD_H);
    lv_obj_set_pos(s_card_wx, CARD_X, CARD_Y);

    /* 图标背后一团柔光，做视觉焦点（先创建 = 在最底层） */
    lv_obj_t *glow = lv_obj_create(s_card_wx);
    lv_obj_remove_style_all(glow);
    lv_obj_set_size(glow, 76, 76);
    lv_obj_align(glow, LV_ALIGN_TOP_LEFT, 10, 16);
    lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(glow, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(glow, LV_OPA_20, 0);

    s_icon_wx = weather_icon_create(s_card_wx, WICON_UNKNOWN, 68, COL_CARD);
    lv_obj_align(s_icon_wx, LV_ALIGN_TOP_LEFT, 14, 20);

    s_lbl_temp = make_label(s_card_wx, &font_digits_44, COL_TEXT, "--°");
    lv_obj_align(s_lbl_temp, LV_ALIGN_TOP_LEFT, 96, 30);

    s_lbl_desc = make_label(s_card_wx, &font_cjk_32, COL_TEXT_MID, "加载中");
    lv_obj_align(s_lbl_desc, LV_ALIGN_TOP_LEFT, 14, 110);

    lv_obj_t *wx_sep = make_hline(s_card_wx, CARD_W - 32, COL_CARD_LINE, LV_OPA_70);
    lv_obj_align(wx_sep, LV_ALIGN_TOP_LEFT, 16, 150);

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

        if (i < 3) {
            lv_obj_t *sep = make_hline(s_card_wx, CARD_W - 32, COL_CARD_LINE, LV_OPA_70);
            lv_obj_align(sep, LV_ALIGN_TOP_LEFT, 16, 158 + (i + 1) * 30 - 3);
        }
    }

    /* ---------------- 三天预报 ---------------- */
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = lv_obj_create(s_dash_scr);
        style_card(c, 20);
        lv_obj_set_size(c, FC_W, FC_H);
        lv_obj_set_pos(c, FC_X0 + i * (FC_W + FC_GAP), FC_Y);
        s_fc_card[i] = c;

        s_fc_day[i] = make_label(c, &font_cjk_22, COL_TEXT, "--");
        lv_obj_align(s_fc_day[i], LV_ALIGN_TOP_LEFT, 16, 10);

        s_fc_icon[i] = weather_icon_create(c, WICON_UNKNOWN, 52, COL_CARD);
        lv_obj_align(s_fc_icon[i], LV_ALIGN_TOP_RIGHT, -12, 8);

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

        /* 降水概率条：灰蓝轨道 = 0~100% 的固定刻度（三张卡刻度一样，能横着比），
           上面那条按概率从左边填满，右边再用字写一遍精确值。 */
        lv_obj_t *track = lv_obj_create(c);
        lv_obj_remove_style_all(track);
        lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(track, FC_BAR_W, 6);
        lv_obj_set_pos(track, 16, 92);
        lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(track, COL_BAR_BG, 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(track, 0, 0);

        s_fc_pop_bar[i] = lv_obj_create(track);
        lv_obj_remove_style_all(s_fc_pop_bar[i]);
        lv_obj_clear_flag(s_fc_pop_bar[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_fc_pop_bar[i], 0, 6);      /* 宽度由 update 按概率算 */
        lv_obj_set_pos(s_fc_pop_bar[i], 0, 0);
        lv_obj_set_style_radius(s_fc_pop_bar[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_fc_pop_bar[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_fc_pop_bar[i], COL_TEXT_DIM, 0);

        s_fc_pop_lbl[i] = make_label(c, &font_cjk_22, COL_TEXT_DIM, "--");
        lv_obj_align(s_fc_pop_lbl[i], LV_ALIGN_TOP_RIGHT, -16, 82);
    }
}

/* ------------------------------------------------------------------ */
/*  初始化                                                             */
/* ------------------------------------------------------------------ */
void ui_init()
{
    copy_utf8(s_city, sizeof(s_city), settings_city_name());

    create_idle_screen();
    create_dashboard_screen();

    refresh_idle_date();
    refresh_citydate();
    refresh_status_label();

    /* 默认（平时）是大时钟屏 */
    lv_scr_load(s_idle_scr);
    s_mode = UI_MODE_IDLE;

    /* 设置向导的轮询计时器 */
    setup_ui_init();

    ui_log_mem("ui_init 完成");
}

/* ------------------------------------------------------------------ */
/*  主屏切换                                                           */
/* ------------------------------------------------------------------ */
void ui_show_idle()
{
    if (s_idle_scr == nullptr) return;
    lv_scr_load(s_idle_scr);
    s_mode = UI_MODE_IDLE;
    ui_log_mem("-> 大时钟");
}

void ui_show_dashboard()
{
    if (s_dash_scr == nullptr) return;
    lv_scr_load(s_dash_scr);
    s_mode          = UI_MODE_DASHBOARD;
    s_dash_enter_ms = millis();      /* 重新开始计时 */
    ui_log_mem("-> 看板");
}

bool ui_is_dashboard()
{
    return s_mode == UI_MODE_DASHBOARD;
}

void ui_poll()
{
    if (s_mode != UI_MODE_DASHBOARD) return;
    if (setup_ui_active()) return;                     /* 向导开着就先别抢屏 */

    uint32_t timeout = (uint32_t)IDLE_RETURN_MIN * 60000UL;
    if ((uint32_t)(millis() - s_dash_enter_ms) >= timeout) {
        Serial.printf("[界面] 看板停留超过 %d 分钟，回到大时钟\n", IDLE_RETURN_MIN);
        ui_show_idle();
    }
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
    label_set_if_changed(s_idle_time, s_last_time_idle, sizeof(s_last_time_idle), buf);
    label_set_if_changed(s_lbl_time,  s_last_time_dash, sizeof(s_last_time_dash), buf);

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
        refresh_idle_date();
        refresh_citydate();
    }
}

/* ------------------------------------------------------------------ */
/*  天气刷新（只作用于看板）                                            */
/* ------------------------------------------------------------------ */
void ui_update_weather(const WeatherData *w)
{
    if (w == nullptr || !w->valid) return;
    if (s_lbl_temp == nullptr) return;

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
    for (int i = 0; i < 3; i++) {
        int di = i;
        if (di >= WEATHER_FORECAST_DAYS || !w->days[di].valid) {
            lv_obj_add_flag(s_fc_card[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_fc_card[i], LV_OBJ_FLAG_HIDDEN);

        static const char *day_name[3] = {"今天", "明天", "后天"};
        label_set_if_changed(s_fc_day[i], s_last_fc_day[i], sizeof(s_last_fc_day[i]), day_name[i]);

        if (s_fc_icon[i]) {
            lv_obj_del(s_fc_icon[i]);
            s_fc_icon[i] = nullptr;
        }
        s_fc_icon[i] = weather_icon_create(s_fc_card[i],
                                           weather_icon_type(w->days[di].code, true),
                                           52, COL_CARD);
        lv_obj_align(s_fc_icon[i], LV_ALIGN_TOP_RIGHT, -12, 8);

        snprintf(buf, sizeof(buf), "%.0f°", w->days[di].tmax);
        label_set_if_changed(s_fc_hi[i], s_last_fc_hi[i], sizeof(s_last_fc_hi[i]), buf);
        snprintf(buf, sizeof(buf), "%.0f°", w->days[di].tmin);
        label_set_if_changed(s_fc_lo[i], s_last_fc_lo[i], sizeof(s_last_fc_lo[i]), buf);

        /* --- 降水概率条 ---
           pop < 0 表示接口这次没给这个字段：轨道留着，画"--"。
           概率越高颜色越深（<25% 灰蓝＝基本没雨，25~60% 浅蓝，>60% 亮蓝）。 */
        float pop = w->days[di].pop;
        if (pop < 0) {
            lv_obj_set_size(s_fc_pop_bar[i], 0, 6);
            label_set_if_changed(s_fc_pop_lbl[i], s_last_fc_pop[i], sizeof(s_last_fc_pop[i]), "--");
            lv_obj_set_style_text_color(s_fc_pop_lbl[i], COL_TEXT_DIM, 0);
        } else {
            if (pop > 100) pop = 100;
            int bw = (int)(pop / 100.0f * (float)FC_BAR_W + 0.5f);
            if (bw < 6 && pop > 0) bw = 6;      /* 1% 也留一小截，别看起来像没有 */
            lv_obj_set_size(s_fc_pop_bar[i], bw, 6);

            lv_color_t c = (pop < 25)  ? COL_TEXT_DIM
                         : (pop <= 60) ? COL_ACCENT_LT
                                       : COL_ACCENT;
            lv_obj_set_style_bg_color(s_fc_pop_bar[i], c, 0);

            snprintf(buf, sizeof(buf), "%.0f%%", pop);
            label_set_if_changed(s_fc_pop_lbl[i], s_last_fc_pop[i], sizeof(s_last_fc_pop[i]), buf);
            lv_obj_set_style_text_color(s_fc_pop_lbl[i], c, 0);
        }
    }

    /* --- 大时钟屏最下面那条「当前详细天气」 --- */
    if (s_idle_wx_card != nullptr) {
        fmt_num(buf, sizeof(buf), "%.1f°", w->temp);
        label_set_if_changed(s_idle_wx_temp, s_last_idle_temp, sizeof(s_last_idle_temp), buf);
        label_set_if_changed(s_idle_wx_desc, s_last_idle_desc, sizeof(s_last_idle_desc),
                             weather_text(w->code));

        if (s_idle_wx_icon) {
            lv_obj_del(s_idle_wx_icon);
            s_idle_wx_icon = nullptr;
        }
        s_idle_wx_icon = weather_icon_create(s_idle_wx_card,
                                             weather_icon_type(w->code, w->is_day),
                                             56, COL_CARD);
        lv_obj_align(s_idle_wx_icon, LV_ALIGN_LEFT_MID, 16, 0);
        make_pass_through(s_idle_wx_icon);       /* 点图标也要能切到看板 */

        static const char *const val_fmt[4] = { "%.1f°C", "%.0f%%", "%.1fm/s", "%.0fhPa" };
        const float val[4] = { w->apparent, w->humidity, w->wind, w->pressure };
        for (int i = 0; i < 4; i++) {
            fmt_num(buf, sizeof(buf), val_fmt[i], val[i]);
            label_set_if_changed(s_idle_wx_v[i], s_last_idle_v[i], sizeof(s_last_idle_v[i]), buf);
        }

        lv_obj_clear_flag(s_idle_wx_card, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ------------------------------------------------------------------ */
/*  状态与杂项                                                         */
/* ------------------------------------------------------------------ */
void ui_set_status(UiStatus st, const char *text)
{
    s_status = st;
    if (text) {
        copy_utf8(s_status_text, sizeof(s_status_text), text);
    }
    refresh_status_label();
}

void ui_set_updated(const char *hhmm)
{
    if (hhmm) {
        copy_utf8(s_update_text, sizeof(s_update_text), hhmm);
    } else {
        s_update_text[0] = '\0';
    }
    refresh_status_label();
}

void ui_set_city(const char *city)
{
    if (city && city[0]) {
        copy_utf8(s_city, sizeof(s_city), city);
        refresh_citydate();
    }
}

bool ui_take_weather_refresh_request()
{
    if (!s_weather_refresh_req) return false;
    s_weather_refresh_req = false;
    return true;
}

void ui_set_weather_refreshing(bool refreshing)
{
    if (s_refresh_btn == nullptr) return;
    lv_obj_t *l = lv_obj_get_child(s_refresh_btn, 0);
    if (l != nullptr) lv_label_set_text(l, refreshing ? "刷新中…" : "刷新天气");
}

void ui_log_mem(const char *tag)
{
#if DEBUG_SERIAL
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    Serial.printf("[LVGL] %-16s 已用 %u/%u 字节  最大空闲块 %u  碎片 %u%%\n",
                  tag ? tag : "",
                  (unsigned)(m.total_size - m.free_size),
                  (unsigned)m.total_size,
                  (unsigned)m.free_biggest_size,
                  (unsigned)m.frag_pct);
#else
    (void)tag;
#endif
}
