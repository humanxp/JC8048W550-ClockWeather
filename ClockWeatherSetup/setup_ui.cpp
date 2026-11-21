#include "setup_ui.h"

#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "fonts.h"
#include "net_time.h"
#include "region_data.h"
#include "settings.h"
#include "touch_gt911.h"
#include "ui.h"

/* ------------------------------ 配色（和 ui.cpp 保持一致）------------------------------ */
#define COL_BG_TOP      lv_color_hex(0x061020)
#define COL_BG_BOTTOM   lv_color_hex(0x123A66)
#define COL_CARD        lv_color_hex(0x1C3E63)
#define COL_CARD_2      lv_color_hex(0x143050)
#define COL_CARD_LINE   lv_color_hex(0x2E5A85)
#define COL_TEXT        lv_color_hex(0xFFFFFF)
#define COL_TEXT_MID    lv_color_hex(0xC3D9F0)
#define COL_TEXT_DIM    lv_color_hex(0x8FB2D4)
#define COL_ACCENT      lv_color_hex(0x0A84FF)
#define COL_ACCENT_LT   lv_color_hex(0x64D2FF)
#define COL_GREEN       lv_color_hex(0x30D158)
#define COL_AMBER       lv_color_hex(0xFFD60A)
#define COL_RED         lv_color_hex(0xFF453A)
#define COL_BAR_BG      lv_color_hex(0x2A4C73)

/* ------------------------------ 布局 ------------------------------ */
#define TOPBAR_H        62
#define BTN_H           44
#define ROW_H           62
#define ROW_GAP         8
#define LIST_W          752
/* WiFi 列表高度：底部那一行只在触摸诊断打开时才占位置 */
#if UI_SHOW_TOUCH_DEBUG
#define WIFI_LIST_H     310
#else
#define WIFI_LIST_H     352
#endif

/* ------------------------------ 状态 ------------------------------ */
enum SetupState {
    SETUP_IDLE = 0,
    SETUP_SCAN,
    SETUP_WIFI_LIST,
    SETUP_PASSWORD,
    SETUP_CONNECTING,
    SETUP_REGION
};

static bool       s_active     = false;
static bool       s_first_run  = false;
static SetupState s_state      = SETUP_IDLE;

static char       s_pending_ssid[33] = {0};
static char       s_pending_pass[65] = {0};
static bool       s_pending_secure   = true;
static bool       s_manual_ssid      = false;
static bool       s_pass_visible     = false;
static uint32_t   s_connect_started_ms = 0;

/* 地区选择 */
static int        s_sel_prov = -1;
static int        s_sel_city = -1;

/* 地区列表的行指针。
   选中某一项时只改这两行的样式，绝不重建整个列表 —— 删除+新建几十个控件
   是最贵、最容易看到「闪」的操作（尤其是省名列表有 34 行）。
   换省时才重建右边的城市列（内容确实变了）。 */
#define REGION_MAX_ROWS  64
static lv_obj_t  *s_prov_row[REGION_MAX_ROWS] = {nullptr};
static int        s_prov_row_n = 0;
static lv_obj_t  *s_city_row[REGION_MAX_ROWS] = {nullptr};
static int        s_city_row_n = 0;

#define CONNECT_TIMEOUT_MS   15000

/* ------------------------------ 控件 ------------------------------ */
static lv_obj_t *s_scr_wifi   = nullptr;
static lv_obj_t *s_wifi_back  = nullptr;
static lv_obj_t *s_wifi_region_btn = nullptr;
static lv_obj_t *s_wifi_sub   = nullptr;
static lv_obj_t *s_wifi_list  = nullptr;

static lv_obj_t *s_scr_pwd    = nullptr;
static lv_obj_t *s_pwd_ssid_lbl = nullptr;
static lv_obj_t *s_pwd_ssid_ta  = nullptr;
static lv_obj_t *s_pwd_ta       = nullptr;
static lv_obj_t *s_pwd_kb       = nullptr;
static lv_obj_t *s_pwd_hint     = nullptr;
static lv_obj_t *s_pwd_connect  = nullptr;
static lv_obj_t *s_pwd_show     = nullptr;

static lv_obj_t *s_scr_region = nullptr;
static lv_obj_t *s_region_sub = nullptr;
static lv_obj_t *s_region_prov_list = nullptr;
static lv_obj_t *s_region_city_list = nullptr;

/* ------------------------------------------------------------------ */
/*  样式小工具                                                         */
/* ------------------------------------------------------------------ */
static void sui_screen(lv_obj_t *scr)
{
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, COL_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(scr, COL_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

static lv_obj_t *sui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

/** 顶栏按钮：胶囊外形，可指定宽度 */
static lv_obj_t *sui_button(lv_obj_t *parent, const char *text, int w, lv_color_t fg, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(b, w, BTN_H);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, COL_CARD, 0);
    lv_obj_set_style_bg_grad_color(b, COL_CARD_2, 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, COL_CARD_LINE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(b, COL_ACCENT_LT, LV_STATE_PRESSED);

    lv_obj_t *l = sui_label(b, &font_cjk_28, fg, text);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

/** 列表行：圆角卡片，点得动 */
static lv_obj_t *sui_row(lv_obj_t *list, int h, bool selected)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, h);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_bg_color(row, selected ? COL_ACCENT : COL_CARD, 0);
    lv_obj_set_style_bg_grad_color(row, selected ? COL_ACCENT : COL_CARD_2, 0);
    lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(row, selected ? LV_OPA_COVER : LV_OPA_90, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, selected ? COL_ACCENT_LT : COL_CARD_LINE, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    /* 按下时整行变蓝：既能给用户反馈，也是排查触摸最直观的依据
       —— 点下去行没变色，就说明按下事件根本没送到这一行。 */
    lv_obj_set_style_bg_color(row, COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(row, COL_ACCENT_LT, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(row, COL_ACCENT_LT, LV_STATE_PRESSED);
    return row;
}

/** 顶栏：标题 + 可选返回按钮；返回按钮的显隐在打开页面时再定 */
static lv_obj_t *sui_topbar(lv_obj_t *scr, const char *title, lv_obj_t **back_out,
                            const char *right_text, int right_w, lv_event_cb_t right_cb)
{
    if (back_out) {
        lv_obj_t *back = sui_button(scr, "返回", 100, COL_TEXT_MID, nullptr);
        lv_obj_align(back, LV_ALIGN_TOP_LEFT, 24, 14);
        *back_out = back;
    }
    lv_obj_t *t = sui_label(scr, &font_cjk_28, COL_TEXT, title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 148, 22);

    if (right_text && right_w > 0) {
        lv_obj_t *r = sui_button(scr, right_text, right_w, COL_ACCENT_LT, right_cb);
        lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -24, 14);
        return r;
    }
    return nullptr;
}

/* ------------------------- 触摸诊断行（调试用） -------------------------
 * 三张向导页的顶栏各放一行小字，显示 GT911 地址 / 按下次数 / 最近坐标。
 * 点了没反应时，这一行能直接区分是「没找到芯片」「坐标不对」还是别的问题。
 * 调好之后把 config.h 的 UI_SHOW_TOUCH_DEBUG 改成 0 就没了。
 * ---------------------------------------------------------------------- */
#if UI_SHOW_TOUCH_DEBUG
static lv_obj_t *s_dbg_lbl[3]    = {nullptr, nullptr, nullptr};
static bool       s_dbg_bottom[3] = {false, false, false};
static int        s_dbg_next     = 0;

/**
 * 加一行触摸诊断。
 * bottom=false：放顶栏中间（标题右边、按钮左边，约 286~528），拆两行写；
 * bottom=true ：放页面底部整条（给顶栏按钮多的情况用，比如 WiFi 列表页），写成一行。
 */
static void sui_add_debug_line(lv_obj_t *scr, bool bottom)
{
    if (s_dbg_next >= 3) return;

    lv_obj_t *l = sui_label(scr, &font_cjk_22, COL_TEXT_DIM, "");
    if (bottom) {
        lv_obj_set_size(l, 752, lv_font_get_line_height(&font_cjk_22));
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 24, 434);
    } else {
        lv_obj_set_width(l, 242);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 286, 4);
    }
    s_dbg_bottom[s_dbg_next] = bottom;
    s_dbg_lbl[s_dbg_next++]  = l;
}

static void sui_update_debug_line()
{
    TouchDiag d;
    touch_diag(&d);

    char one[96];    /* 整条那一版：一行写完 */
    char two[96];    /* 顶栏那一版：拆两行，窄框也放得下 */
    if (d.addr == 0) {
        snprintf(one, sizeof(one), "触摸: 未找到 GT911");
        snprintf(two, sizeof(two), "触摸: 未找到 GT911");
    } else {
        snprintf(one, sizeof(one), "触摸 0x%02X · %u次 · 坐标 %d,%d",
                 (unsigned)d.addr, (unsigned)d.press_count, (int)d.x, (int)d.y);
        snprintf(two, sizeof(two), "触摸 0x%02X · %u次\n坐标 %d, %d",
                 (unsigned)d.addr, (unsigned)d.press_count, (int)d.x, (int)d.y);
    }

    static char last[3][96] = {"", "", ""};
    lv_obj_t *scr = lv_scr_act();
    for (int i = 0; i < 3; i++) {
        if (s_dbg_lbl[i] == nullptr || lv_obj_get_parent(s_dbg_lbl[i]) != scr) continue;
        const char *t = s_dbg_bottom[i] ? one : two;
        if (strcmp(last[i], t) == 0) continue;      /* 没变就别碰 LVGL */
        copy_utf8(last[i], sizeof(last[i]), t);
        lv_label_set_text(s_dbg_lbl[i], t);
    }
}
#else
/* 关掉调试行时给两个空实现，调用处就不用到处写 #if 了 */
static void sui_add_debug_line(lv_obj_t *, bool) {}
static void sui_update_debug_line() {}
#endif

/* ------------------------------------------------------------------ */
/*  通用流程                                                           */
/* ------------------------------------------------------------------ */
static void finish_setup()
{
    s_active = false;
    s_state  = SETUP_IDLE;

    if (s_first_run) {
        s_first_run = false;
        ui_show_idle();          /* 第一次配置完，直接进大时钟 */
    } else {
        ui_show_dashboard();
    }
}

/* ------------------------------------------------------------------ */
/*  WiFi 列表页                                                        */
/* ------------------------------------------------------------------ */
static void open_password(const char *ssid, bool manual, bool secure);
static void ensure_pwd_screen();
static void render_region_lists();
static void render_city_list();
static void start_scan();

static void sui_set_subtitle(lv_obj_t *lbl, const char *text)
{
    if (lbl) lv_label_set_text(lbl, text);
}

/** 信号强度：一根小横条，长度表示格数。
 *  以前画 4 根柱子（4 个控件/行），列表十几行就白白多出几十个控件；
 *  在 LVGL 内存吃紧时这种「每个小装饰都建一个对象」的做法很要命，改成 1 个。 */
static void add_signal_bar(lv_obj_t *parent, int x, int y, int level)
{
    if (level < 0) level = 0;
    if (level > 4) level = 4;

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(bar, 5 + level * 4, 6);          /* 5~21px */
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_color(bar, level >= 3 ? COL_TEXT : COL_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
}

static int rssi_level(int rssi)
{
    if (rssi == 0)   return 0;
    if (rssi > -55)  return 4;
    if (rssi > -65)  return 3;
    if (rssi > -75)  return 2;
    return 1;
}

static void cb_ap_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0) {
        open_password("", true, false);
    } else {
        open_password(net_scan_ssid(idx), false, net_scan_secure(idx));
    }
}

static void rebuild_wifi_list()
{
    if (s_wifi_list == nullptr) return;
    lv_obj_clean(s_wifi_list);

    char sub[96];

    if (net_scan_failed()) {
        sui_set_subtitle(s_wifi_sub, "扫描失败，请确认路由器开着，点右上角重新扫描");
        return;
    }

    int n = net_scan_count();
    if (n == 0) {
        sui_set_subtitle(s_wifi_sub, "没发现网络，请确认路由器开着，点右上角重新扫描");
    } else {
        snprintf(sub, sizeof(sub), "找到 %d 个网络，点击即可连接", n);
        sui_set_subtitle(s_wifi_sub, sub);
    }

    /* --- 每个网络一行 --- */
    for (int i = 0; i < n; i++) {
        const char *ssid = net_scan_ssid(i);
        bool secure = net_scan_secure(i);
        bool saved  = (strcmp(ssid, settings_wifi_ssid()) == 0);

        lv_obj_t *row = sui_row(s_wifi_list, ROW_H, false);
        add_signal_bar(row, 18, 28, rssi_level(net_scan_rssi(i)));

        if (secure) {
            lv_obj_t *lk = sui_label(row, &font_cjk_22, COL_TEXT_DIM, "加密");
            lv_obj_align(lk, LV_ALIGN_LEFT_MID, 52, 0);
        }

        /* SSID 是用户数据，可能是中文 —— 用 22px 全字库，避免中文 WiFi 名变方框 */
        lv_obj_t *nm = sui_label(row, &font_cjk_22, COL_TEXT, ssid);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_set_width(nm, 520);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 108, 0);

        if (saved) {
            lv_obj_t *sv = sui_label(row, &font_cjk_22, COL_GREEN, "已保存");
            lv_obj_align(sv, LV_ALIGN_RIGHT_MID, -52, 0);
        }

        lv_obj_t *arw = sui_label(row, &font_cjk_28, COL_TEXT_DIM, ">");
        lv_obj_align(arw, LV_ALIGN_RIGHT_MID, -20, 0);

        lv_obj_add_event_cb(row, cb_ap_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    /* --- 最后一行：手动输入隐藏网络 --- */
    {
        lv_obj_t *row = sui_row(s_wifi_list, ROW_H, false);
        lv_obj_t *plus = sui_label(row, &font_cjk_28, COL_ACCENT_LT, "+");
        lv_obj_align(plus, LV_ALIGN_LEFT_MID, 20, 0);

        lv_obj_t *nm = sui_label(row, &font_cjk_22, COL_TEXT_MID, "手动输入网络名（隐藏的 WiFi）");
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 58, 0);

        lv_obj_add_event_cb(row, cb_ap_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)(-1));
    }

    ui_log_mem("WiFi 列表");
}

static void cb_wifi_back(lv_event_t *e)
{
    LV_UNUSED(e);
    finish_setup();
}

static void cb_rescan(lv_event_t *e)
{
    LV_UNUSED(e);
    start_scan();
}

/** 从设置里直接跳到地区页（不改 WiFi，只改地区时用） */
static void cb_region_shortcut(lv_event_t *e)
{
    LV_UNUSED(e);
    setup_ui_start_region(false);
}

static void ensure_wifi_screen()
{
    if (s_scr_wifi) return;

    s_scr_wifi = lv_obj_create(nullptr);
    sui_screen(s_scr_wifi);

    sui_topbar(s_scr_wifi, "连接 WiFi", &s_wifi_back, "重新扫描", 148, cb_rescan);
    /* 诊断行只在 UI_SHOW_TOUCH_DEBUG 时出现，它占的是页面最下面一条 */
    sui_add_debug_line(s_scr_wifi, true);

    /* 「地区」快捷入口，摆在「重新扫描」左边 */
    s_wifi_region_btn = sui_button(s_scr_wifi, "地区", 88, COL_TEXT_MID, cb_region_shortcut);
    lv_obj_align(s_wifi_region_btn, LV_ALIGN_TOP_RIGHT, -180, 14);

    lv_obj_add_event_cb(s_wifi_back, cb_wifi_back, LV_EVENT_CLICKED, nullptr);

    s_wifi_sub = sui_label(s_scr_wifi, &font_cjk_22, COL_TEXT_DIM, "正在扫描附近的网络…");
    lv_obj_align(s_wifi_sub, LV_ALIGN_TOP_LEFT, 24, 84);

    s_wifi_list = lv_obj_create(s_scr_wifi);
    lv_obj_remove_style_all(s_wifi_list);
    lv_obj_set_size(s_wifi_list, LIST_W, WIFI_LIST_H);
    lv_obj_set_pos(s_wifi_list, 24, 112);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_wifi_list, ROW_GAP, 0);
    lv_obj_set_style_pad_all(s_wifi_list, 0, 0);
    lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
}

static void start_scan()
{
    s_state = SETUP_SCAN;
    if (s_wifi_list) lv_obj_clean(s_wifi_list);
    sui_set_subtitle(s_wifi_sub, "正在扫描附近的网络…");
    net_scan_start();
}

/* ------------------------------------------------------------------ */
/*  密码页                                                             */
/* ------------------------------------------------------------------ */
static void sui_set_hint(lv_obj_t *lbl, lv_color_t color, const char *text)
{
    if (lbl == nullptr) return;
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
}

static void cb_pwd_back(lv_event_t *e)
{
    LV_UNUSED(e);

    /* 连接尝试中按返回 = 放弃这次尝试，恢复原来的账号 */
    if (s_state == SETUP_CONNECTING) net_begin();

    s_state = SETUP_WIFI_LIST;
    if (s_scr_wifi) lv_scr_load(s_scr_wifi);
}

static void cb_show_toggle(lv_event_t *e)
{
    LV_UNUSED(e);
    s_pass_visible = !s_pass_visible;
    if (s_pwd_ta) lv_textarea_set_password_mode(s_pwd_ta, !s_pass_visible);
    if (s_pwd_show) {
        lv_obj_t *l = lv_obj_get_child(s_pwd_show, 0);
        if (l) lv_label_set_text(l, s_pass_visible ? "隐藏" : "显示");
    }
}

static void cb_ssid_ta_focused(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_pwd_kb && s_pwd_ssid_ta) lv_keyboard_set_textarea(s_pwd_kb, s_pwd_ssid_ta);
}

static void cb_pwd_ta_focused(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_pwd_kb && s_pwd_ta) lv_keyboard_set_textarea(s_pwd_kb, s_pwd_ta);
}

static void do_connect();

static void cb_pwd_connect(lv_event_t *e)
{
    LV_UNUSED(e);
    do_connect();
}

static void cb_kb_ready(lv_event_t *e)
{
    LV_UNUSED(e);
    do_connect();
}

static void do_connect()
{
    if (s_state == SETUP_CONNECTING) return;

    char ssid[33] = {0};
    char pass[65] = {0};

    if (s_manual_ssid) {
        if (s_pwd_ssid_ta) copy_utf8(ssid, sizeof(ssid), lv_textarea_get_text(s_pwd_ssid_ta));
    } else {
        copy_utf8(ssid, sizeof(ssid), s_pending_ssid);
    }
    if (s_pwd_ta) copy_utf8(pass, sizeof(pass), lv_textarea_get_text(s_pwd_ta));

    if (ssid[0] == '\0') {
        sui_set_hint(s_pwd_hint, COL_RED, "请先输入网络名");
        return;
    }
    if (s_pending_secure && pass[0] != '\0' && strlen(pass) < 8) {
        sui_set_hint(s_pwd_hint, COL_AMBER, "密码一般是 8 位以上，仍将尝试连接");
    } else {
        sui_set_hint(s_pwd_hint, COL_AMBER, "正在连接…");
    }

    copy_utf8(s_pending_ssid, sizeof(s_pending_ssid), ssid);
    copy_utf8(s_pending_pass, sizeof(s_pending_pass), pass);

    if (s_pwd_connect) lv_obj_add_state(s_pwd_connect, LV_STATE_DISABLED);

    net_connect_to(s_pending_ssid, s_pending_pass);
    s_connect_started_ms = millis();
    s_state = SETUP_CONNECTING;
}

static void open_password(const char *ssid, bool manual, bool secure)
{
    ensure_pwd_screen();
    if (s_scr_pwd == nullptr) return;

    s_manual_ssid    = manual;
    s_pending_secure = secure;
    s_pass_visible   = false;

    copy_utf8(s_pending_ssid, sizeof(s_pending_ssid), ssid);
    s_pending_pass[0] = '\0';

    /* 网络名：普通网络用标签显示，隐藏网络用输入框 */
    if (manual) {
        lv_obj_clear_flag(s_pwd_ssid_ta, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pwd_ssid_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(s_pwd_ssid_ta, "");
    } else {
        lv_obj_add_flag(s_pwd_ssid_ta, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pwd_ssid_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_pwd_ssid_lbl, s_pending_ssid);
    }

    lv_textarea_set_text(s_pwd_ta, "");
    lv_textarea_set_password_mode(s_pwd_ta, true);
    if (s_pwd_show) {
        lv_obj_t *l = lv_obj_get_child(s_pwd_show, 0);
        if (l) lv_label_set_text(l, "显示");
        lv_obj_clear_state(s_pwd_show, LV_STATE_DISABLED);
    }
    if (s_pwd_connect) lv_obj_clear_state(s_pwd_connect, LV_STATE_DISABLED);

    if (!secure) {
        sui_set_hint(s_pwd_hint, COL_TEXT_DIM, "开放式网络，密码不用填，直接点「连接」");
        if (s_pwd_ta) lv_obj_add_state(s_pwd_ta, LV_STATE_DISABLED);
        if (s_pwd_show) lv_obj_add_state(s_pwd_show, LV_STATE_DISABLED);
    } else {
        sui_set_hint(s_pwd_hint, COL_TEXT_DIM, "密码区分大小写");
        if (s_pwd_ta) lv_obj_clear_state(s_pwd_ta, LV_STATE_DISABLED);
    }

    /* 键盘指到该输的那个框上 */
    if (s_pwd_kb) {
        lv_keyboard_set_textarea(s_pwd_kb, manual ? s_pwd_ssid_ta : s_pwd_ta);
    }

    s_state = SETUP_PASSWORD;
    lv_scr_load(s_scr_pwd);
    ui_log_mem("密码页");
}

/* ------------------------------------------------------------------ */
/*  地区页                                                             */
/* ------------------------------------------------------------------ */
/** 更新「已选：省 · 市」那行字 */
static void refresh_region_sub()
{
    char buf[112];
    if (s_sel_prov >= 0 && s_sel_city >= 0) {
        const RegionProvince *p = &g_region_provinces[s_sel_prov];
        if (s_sel_city < (int)p->city_count) {
            snprintf(buf, sizeof(buf), "已选：%s · %s", p->name, p->cities[s_sel_city].name);
        } else {
            snprintf(buf, sizeof(buf), "已选：%s", p->name);
        }
    } else {
        snprintf(buf, sizeof(buf), "请先选省份，再选城市");
    }
    sui_set_subtitle(s_region_sub, buf);
}

/* 行高亮：只改这一行的样式，不重建列表 */
static void sui_row_set_selected(lv_obj_t *row, bool sel)
{
    if (row == nullptr) return;
    lv_obj_set_style_bg_color(row, sel ? COL_ACCENT : COL_CARD, 0);
    lv_obj_set_style_bg_grad_color(row, sel ? COL_ACCENT : COL_CARD_2, 0);
    lv_obj_set_style_bg_opa(row, sel ? LV_OPA_COVER : LV_OPA_90, 0);
    lv_obj_set_style_border_color(row, sel ? COL_ACCENT_LT : COL_CARD_LINE, 0);
}

static void cb_prov_clicked(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= (int)g_region_province_count) return;
    if (i == s_sel_prov) return;                 /* 点的就是当前省，什么都不用做 */

    /* 只改两行的高亮，不重建省份列（34 行重建一次就是一次全屏级重绘，会闪） */
    if (s_sel_prov >= 0 && s_sel_prov < s_prov_row_n) {
        sui_row_set_selected(s_prov_row[s_sel_prov], false);
    }
    s_sel_prov = i;
    if (i < s_prov_row_n) sui_row_set_selected(s_prov_row[i], true);

    s_sel_city = 0;                              /* 默认选中该省第一个城市 */
    refresh_region_sub();
    render_city_list();                          /* 城市内容变了，必须重建 */

    if (s_region_city_list) lv_obj_scroll_to_y(s_region_city_list, 0, LV_ANIM_OFF);
}

static void cb_city_clicked(lv_event_t *e)
{
    int j = (int)(intptr_t)lv_event_get_user_data(e);
    if (j == s_sel_city) return;

    /* 点城市只改两行高亮，什么都不重建，所以完全不闪 */
    if (s_sel_city >= 0 && s_sel_city < s_city_row_n) {
        sui_row_set_selected(s_city_row[s_sel_city], false);
    }
    s_sel_city = j;
    if (j >= 0 && j < s_city_row_n) sui_row_set_selected(s_city_row[j], true);

    refresh_region_sub();
}

static void cb_region_back(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_first_run) {
        /* 第一次配置时，地区页的上一步是 WiFi */
        s_state = SETUP_WIFI_LIST;
        if (s_scr_wifi) lv_scr_load(s_scr_wifi);
    } else {
        finish_setup();
    }
}

static void cb_region_finish(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_sel_prov < 0 || s_sel_prov >= (int)g_region_province_count) return;

    const RegionProvince *p = &g_region_provinces[s_sel_prov];
    if (s_sel_city < 0 || s_sel_city >= (int)p->city_count) return;

    const RegionCity *c = &p->cities[s_sel_city];
    settings_save_region(p->name, c->name, c->lat, c->lon);
    finish_setup();
}

/** 只重建右边的城市列（换省时必须重建：内容确实变了） */
static void render_city_list()
{
    if (s_region_city_list == nullptr) return;

    lv_obj_clean(s_region_city_list);
    s_city_row_n = 0;

    if (s_sel_prov < 0 || s_sel_prov >= (int)g_region_province_count) return;

    const RegionProvince *p = &g_region_provinces[s_sel_prov];
    for (uint16_t j = 0; j < p->city_count && j < REGION_MAX_ROWS; j++) {
        lv_obj_t *row = sui_row(s_region_city_list, 46, (int)j == s_sel_city);

        /* 城市名来自 region_data.cpp —— 是「数据」，不是源码里的字符串字面量，
           而 28px 那套字库是按源码字面量裁出来的，不含这些地名。
           所以地名统一用 22px 的全字库（3906 字，含全国省市名）。 */
        lv_obj_t *nm = sui_label(row, &font_cjk_22, COL_TEXT, p->cities[j].name);
        lv_obj_set_width(nm, 460);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 14, 0);

        lv_obj_add_event_cb(row, cb_city_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)j);
        s_city_row[j] = row;
        s_city_row_n  = j + 1;
    }
}

/** 首次进入地区页：两列都建出来 */
static void render_region_lists()
{
    if (s_region_prov_list == nullptr || s_region_city_list == nullptr) return;

    lv_obj_clean(s_region_prov_list);
    s_prov_row_n = 0;

    for (uint16_t i = 0; i < g_region_province_count && i < REGION_MAX_ROWS; i++) {
        lv_obj_t *row = sui_row(s_region_prov_list, 46, (int)i == s_sel_prov);

        /* 省名同理用全字库：像「新疆维吾尔自治区」8 个字，
           28px 在 250px 宽的列里放不下，22px 刚好 */
        lv_obj_t *nm = sui_label(row, &font_cjk_22, COL_TEXT, g_region_provinces[i].name);
        lv_obj_set_width(nm, 222);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 14, 0);

        lv_obj_add_event_cb(row, cb_prov_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_prov_row[i] = row;
        s_prov_row_n  = i + 1;
    }

    render_city_list();
    ui_log_mem("地区列表");
}

static void ensure_region_screen()
{
    if (s_scr_region) return;

    s_scr_region = lv_obj_create(nullptr);
    sui_screen(s_scr_region);

    lv_obj_t *back = nullptr;
    sui_topbar(s_scr_region, "选择地区", &back, "完成", 100, cb_region_finish);
    lv_obj_add_event_cb(back, cb_region_back, LV_EVENT_CLICKED, nullptr);
    sui_add_debug_line(s_scr_region, false);

    s_region_sub = sui_label(s_scr_region, &font_cjk_22, COL_TEXT_DIM, "");
    lv_obj_align(s_region_sub, LV_ALIGN_TOP_LEFT, 24, 84);

    s_region_prov_list = lv_obj_create(s_scr_region);
    lv_obj_remove_style_all(s_region_prov_list);
    lv_obj_set_size(s_region_prov_list, 250, 340);
    lv_obj_set_pos(s_region_prov_list, 24, 116);
    lv_obj_set_flex_flow(s_region_prov_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_region_prov_list, 6, 0);
    lv_obj_set_scrollbar_mode(s_region_prov_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(s_region_prov_list, LV_DIR_VER);

    s_region_city_list = lv_obj_create(s_scr_region);
    lv_obj_remove_style_all(s_region_city_list);
    lv_obj_set_size(s_region_city_list, 486, 340);
    lv_obj_set_pos(s_region_city_list, 290, 116);
    lv_obj_set_flex_flow(s_region_city_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_region_city_list, 6, 0);
    lv_obj_set_scrollbar_mode(s_region_city_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(s_region_city_list, LV_DIR_VER);
}

/* ------------------------------------------------------------------ */
/*  密码页创建（被 open_password 调用，提前声明）                       */
/* ------------------------------------------------------------------ */
static void ensure_pwd_screen()
{
    if (s_scr_pwd) return;

    s_scr_pwd = lv_obj_create(nullptr);
    sui_screen(s_scr_pwd);

    lv_obj_t *back = nullptr;
    s_pwd_connect = sui_topbar(s_scr_pwd, "输入密码", &back, "连接", 100, cb_pwd_connect);
    lv_obj_add_event_cb(back, cb_pwd_back, LV_EVENT_CLICKED, nullptr);
    sui_add_debug_line(s_scr_pwd, false);

    /* 网络名 */
    lv_obj_t *lbl_net = sui_label(s_scr_pwd, &font_cjk_22, COL_TEXT_DIM, "网络");
    lv_obj_align(lbl_net, LV_ALIGN_TOP_LEFT, 24, 96);

    /* SSID 是用户数据（可能是中文），用 22px 全字库，避免方框 */
    s_pwd_ssid_lbl = sui_label(s_scr_pwd, &font_cjk_22, COL_TEXT, "");
    lv_label_set_long_mode(s_pwd_ssid_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_pwd_ssid_lbl, 600);
    lv_obj_align(s_pwd_ssid_lbl, LV_ALIGN_TOP_LEFT, 110, 94);

    s_pwd_ssid_ta = lv_textarea_create(s_scr_pwd);
    lv_textarea_set_one_line(s_pwd_ssid_ta, true);
    lv_obj_set_size(s_pwd_ssid_ta, 460, 50);
    lv_obj_align(s_pwd_ssid_ta, LV_ALIGN_TOP_LEFT, 110, 86);
    lv_obj_add_flag(s_pwd_ssid_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_pwd_ssid_ta, cb_ssid_ta_focused, LV_EVENT_FOCUSED, nullptr);

    /* 密码 */
    lv_obj_t *lbl_pwd = sui_label(s_scr_pwd, &font_cjk_22, COL_TEXT_DIM, "密码");
    lv_obj_align(lbl_pwd, LV_ALIGN_TOP_LEFT, 24, 158);

    s_pwd_ta = lv_textarea_create(s_scr_pwd);
    lv_textarea_set_one_line(s_pwd_ta, true);
    lv_textarea_set_password_mode(s_pwd_ta, true);
    lv_obj_set_size(s_pwd_ta, 460, 50);
    lv_obj_align(s_pwd_ta, LV_ALIGN_TOP_LEFT, 110, 148);
    lv_obj_add_event_cb(s_pwd_ta, cb_pwd_ta_focused, LV_EVENT_FOCUSED, nullptr);

    s_pwd_show = sui_button(s_scr_pwd, "显示", 92, COL_TEXT_MID, cb_show_toggle);
    lv_obj_align(s_pwd_show, LV_ALIGN_TOP_LEFT, 584, 148);

    s_pwd_hint = sui_label(s_scr_pwd, &font_cjk_22, COL_TEXT_DIM, "");
    lv_obj_set_width(s_pwd_hint, 752);
    lv_label_set_long_mode(s_pwd_hint, LV_LABEL_LONG_DOT);
    lv_obj_align(s_pwd_hint, LV_ALIGN_TOP_LEFT, 24, 208);

    /* 软键盘 */
    s_pwd_kb = lv_keyboard_create(s_scr_pwd);
    lv_obj_set_size(s_pwd_kb, 760, 232);
    lv_obj_align(s_pwd_kb, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_keyboard_set_textarea(s_pwd_kb, s_pwd_ta);
    lv_obj_add_event_cb(s_pwd_kb, cb_kb_ready, LV_EVENT_READY, nullptr);

    /* 键盘的按键样式：深色卡片 + 白字，和整体风格统一 */
    lv_obj_set_style_bg_color(s_pwd_kb, COL_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_color(s_pwd_kb, COL_CARD_2, LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_dir(s_pwd_kb, LV_GRAD_DIR_VER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_pwd_kb, COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_pwd_kb, COL_CARD_LINE, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_pwd_kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_pwd_kb, 8, LV_PART_ITEMS);
}

/* ------------------------------------------------------------------ */
/*  连接结果                                                           */
/* ------------------------------------------------------------------ */
static void on_connect_ok()
{
    settings_save_wifi(s_pending_ssid, s_pending_pass);
    net_adopt_current();                       /* 让后台接管自动重连 */
    ui_set_status(UI_STATUS_CONNECTING, "同步时间");
    /* NTP 由主循环在「WiFi 刚连上」时统一启动，这里不用重复调 */

    Serial.printf("[向导] WiFi 连接成功: %s\n", s_pending_ssid);

    if (!settings_has_region()) {
        setup_ui_start_region(s_first_run);
    } else {
        finish_setup();
    }
}

static void on_connect_fail(const char *msg)
{
    Serial.printf("[向导] %s（%s）\n", msg, s_pending_ssid);
    sui_set_hint(s_pwd_hint, COL_RED, msg);
    if (s_pwd_connect) lv_obj_clear_state(s_pwd_connect, LV_STATE_DISABLED);
    s_state = SETUP_PASSWORD;

    /* 恢复原来的账号，后台继续正常重连 */
    net_begin();
}

/* ------------------------------------------------------------------ */
/*  轮询计时器                                                         */
/* ------------------------------------------------------------------ */
static void setup_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!s_active) return;

    /* 顶栏那行触摸诊断（GT911 地址 / 按下次数 / 最近坐标） */
    sui_update_debug_line();

    if (s_state == SETUP_SCAN) {
        if (!net_scan_running()) {
            rebuild_wifi_list();
            s_state = SETUP_WIFI_LIST;
        }
    } else if (s_state == SETUP_CONNECTING) {
        if (net_is_connected()) {
            on_connect_ok();
        } else if ((uint32_t)(millis() - s_connect_started_ms) > CONNECT_TIMEOUT_MS) {
            on_connect_fail("连接失败：密码不对或信号太弱");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  对外接口                                                           */
/* ------------------------------------------------------------------ */
void setup_ui_init()
{
    lv_timer_create(setup_tick, 250, nullptr);
}

void setup_ui_start_wifi(bool first_run)
{
    s_first_run = first_run;
    s_active    = true;

    ensure_wifi_screen();
    ensure_pwd_screen();
    ensure_region_screen();

    /* 第一次开机不给「返回」（还没配置完，退回去就没事干了），
       也不给「地区」快捷入口（得先把 WiFi 配好） */
    if (s_wifi_back) {
        if (first_run) lv_obj_add_flag(s_wifi_back, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_clear_flag(s_wifi_back, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wifi_region_btn) {
        if (first_run) lv_obj_add_flag(s_wifi_region_btn, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_clear_flag(s_wifi_region_btn, LV_OBJ_FLAG_HIDDEN);
    }

    lv_scr_load(s_scr_wifi);
    start_scan();
}

void setup_ui_start_region(bool first_run)
{
    s_first_run = first_run;
    s_active    = true;

    ensure_region_screen();

    /* 用已保存的地区做预选；没保存过就默认第一个省的第一个市 */
    int pi = region_find_province(settings_province());
    if (pi < 0) pi = 0;
    s_sel_prov = pi;

    int ci = region_find_city(pi, settings_city());
    if (ci < 0) ci = 0;
    s_sel_city = ci;

    render_region_lists();
    refresh_region_sub();

    s_state = SETUP_REGION;
    lv_scr_load(s_scr_region);
}

bool setup_ui_active()
{
    return s_active;
}
