#include "weather_icon.h"

#include <math.h>

/* ------------------------------ 配色 ------------------------------ */
#define C_SUN          lv_color_hex(0xFFC93C)
#define C_SUN_GLOW     lv_color_hex(0xFFD75E)
#define C_CLOUD        lv_color_hex(0xE9F1FB)
#define C_CLOUD_GRAY   lv_color_hex(0xA8B8CE)
#define C_MOON         lv_color_hex(0xF3EAC9)
#define C_RAIN         lv_color_hex(0x4FA8F5)
#define C_SNOW         lv_color_hex(0xDCEBFF)
#define C_BOLT         lv_color_hex(0xFFD400)
#define C_FOG          lv_color_hex(0xB9C6D8)
#define C_STAR         lv_color_hex(0xFFF3C4)

/* ------------------------------------------------------------------ */
/*  基础图元                                                           */
/* ------------------------------------------------------------------ */

/** 删除时释放 lv_line 的坐标数组，避免每次刷新天气都漏内存 */
static void line_delete_cb(lv_event_t *e)
{
    void *p = lv_event_get_user_data(e);
    if (p) lv_mem_free(p);
}

/** 以 (cx, cy) 为圆心、直径 d 画一个实心圆 */
static lv_obj_t *add_circle(lv_obj_t *parent, int cx, int cy, int d, lv_color_t color, lv_opa_t opa)
{
    if (d < 1) d = 1;
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    return o;
}

/** 画一个圆角矩形 */
static lv_obj_t *add_round_rect(lv_obj_t *parent, int x, int y, int w, int h, int radius, lv_color_t color)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

/** 画一条线段 */
static lv_obj_t *add_seg(lv_obj_t *parent, int x1, int y1, int x2, int y2, lv_color_t color, int width)
{
    lv_point_t *pts = (lv_point_t *)lv_mem_alloc(sizeof(lv_point_t) * 2);
    if (pts == nullptr) return nullptr;
    pts[0].x = (lv_coord_t)x1; pts[0].y = (lv_coord_t)y1;
    pts[1].x = (lv_coord_t)x2; pts[1].y = (lv_coord_t)y2;

    lv_obj_t *l = lv_line_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_add_event_cb(l, line_delete_cb, LV_EVENT_DELETE, pts);
    return l;
}

/* ------------------------------------------------------------------ */
/*  云朵 / 太阳 / 月亮                                                 */
/* ------------------------------------------------------------------ */

/**
 * 云朵。
 *   k  : 缩放（相对图标边长）
 *   ox,oy : 在图标内的偏移（同样是 0~1 的比例）
 */
static void draw_cloud(lv_obj_t *parent, int S, lv_color_t color, float k, float ox, float oy)
{
    /* 把比例换算成像素 */
    #define PX(v)  ((int)((v) * S))

    int cx1 = PX(ox + 0.42f * k), cy1 = PX(oy + 0.44f * k);
    int cx2 = PX(ox + 0.21f * k), cy2 = PX(oy + 0.55f * k);
    int cx3 = PX(ox + 0.64f * k), cy3 = PX(oy + 0.52f * k);

    add_circle(parent, cx1, cy1, PX(0.40f * k), color, LV_OPA_COVER);
    add_circle(parent, cx2, cy2, PX(0.26f * k), color, LV_OPA_COVER);
    add_circle(parent, cx3, cy3, PX(0.32f * k), color, LV_OPA_COVER);

    int bw = PX(0.78f * k);
    int bh = PX(0.20f * k);
    add_round_rect(parent, PX(ox + 0.09f * k), PX(oy + 0.51f * k), bw, bh, bh / 2, color);

    #undef PX
}

/** 太阳：光晕 + 本体 + 8 根光芒 */
static void draw_sun(lv_obj_t *parent, int S, float ccx, float ccy, float out_r)
{
    int cx = (int)(ccx * S);
    int cy = (int)(ccy * S);

    int core_d = (int)(out_r * 0.52f * S);   /* 本体直径 */
    int glow_d = (int)(out_r * 1.70f * S);   /* 光晕直径 */
    int r_in   = (int)(out_r * 0.66f * S);   /* 光芒起点半径 */
    int r_out  = (int)(out_r * 1.00f * S);   /* 光芒终点半径 */
    int lw     = (int)(S * 0.045f);
    if (lw < 2) lw = 2;

    add_circle(parent, cx, cy, glow_d, C_SUN_GLOW, LV_OPA_20);
    add_circle(parent, cx, cy, core_d, C_SUN, LV_OPA_COVER);

    for (int i = 0; i < 8; i++) {
        float a = (float)i * (float)M_PI / 4.0f;
        int x1 = cx + (int)(cosf(a) * r_in);
        int y1 = cy + (int)(sinf(a) * r_in);
        int x2 = cx + (int)(cosf(a) * r_out);
        int y2 = cy + (int)(sinf(a) * r_out);
        add_seg(parent, x1, y1, x2, y2, C_SUN, lw);
    }
}

/** 月牙：用底色圆咬掉一块 */
static void draw_moon(lv_obj_t *parent, int S, lv_color_t bg)
{
    int d = (int)(S * 0.56f);
    int cx = (int)(S * 0.46f);
    int cy = (int)(S * 0.50f);

    add_circle(parent, cx, cy, d, C_MOON, LV_OPA_COVER);
    add_circle(parent, cx + (int)(S * 0.20f), cy - (int)(S * 0.14f), (int)(S * 0.52f), bg, LV_OPA_COVER);

    /* 两颗小星星，让夜空不空 */
    add_circle(parent, (int)(S * 0.80f), (int)(S * 0.20f), (int)(S * 0.075f), C_STAR, LV_OPA_COVER);
    add_circle(parent, (int)(S * 0.88f), (int)(S * 0.40f), (int)(S * 0.050f), C_STAR, LV_OPA_70);
}

/* ------------------------------------------------------------------ */
/*  雨 / 雪 / 雷电                                                     */
/* ------------------------------------------------------------------ */
static void draw_rain(lv_obj_t *parent, int S, int drops)
{
    int lw = (int)(S * 0.050f);
    if (lw < 2) lw = 2;

    for (int i = 0; i < drops; i++) {
        float x = 0.28f + 0.17f * i;
        add_seg(parent,
                (int)(x * S),          (int)(0.70f * S),
                (int)((x - 0.06f) * S), (int)(0.90f * S),
                C_RAIN, lw);
    }
}

static void draw_snow(lv_obj_t *parent, int S)
{
    add_circle(parent, (int)(0.28f * S), (int)(0.78f * S), (int)(0.10f * S), C_SNOW, LV_OPA_COVER);
    add_circle(parent, (int)(0.47f * S), (int)(0.72f * S), (int)(0.09f * S), C_SNOW, LV_OPA_COVER);
    add_circle(parent, (int)(0.66f * S), (int)(0.78f * S), (int)(0.10f * S), C_SNOW, LV_OPA_COVER);
    add_circle(parent, (int)(0.55f * S), (int)(0.90f * S), (int)(0.08f * S), C_SNOW, LV_OPA_COVER);
}

static void draw_bolt(lv_obj_t *parent, int S)
{
    lv_point_t *pts = (lv_point_t *)lv_mem_alloc(sizeof(lv_point_t) * 7);
    if (pts == nullptr) return;

    /* 闪电的折线形状。
       注意最低点不能取 1.00：线段有宽度，会向两端各外扩半个线宽，
       贴到容器底边会被父对象裁掉，所以留 0.03 的余量。 */
    const float shape[7][2] = {
        {0.54f, 0.58f}, {0.40f, 0.80f}, {0.50f, 0.80f},
        {0.42f, 0.97f}, {0.64f, 0.74f}, {0.53f, 0.74f}, {0.62f, 0.58f}
    };
    for (int i = 0; i < 7; i++) {
        pts[i].x = (lv_coord_t)(shape[i][0] * S);
        pts[i].y = (lv_coord_t)(shape[i][1] * S);
    }

    lv_obj_t *l = lv_line_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_line_set_points(l, pts, 7);
    lv_obj_set_style_line_width(l, (int)(S * 0.055f) < 2 ? 2 : (int)(S * 0.055f), 0);
    lv_obj_set_style_line_color(l, C_BOLT, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_add_event_cb(l, line_delete_cb, LV_EVENT_DELETE, pts);
}

/* ------------------------------------------------------------------ */
/*  对外入口                                                           */
/* ------------------------------------------------------------------ */
lv_obj_t *weather_icon_create(lv_obj_t *parent, WeatherIconType type, int size, lv_color_t bg_color)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, size, size);

    const int S = size;

    switch (type) {
        case WICON_SUN:
            draw_sun(box, S, 0.5f, 0.5f, 0.42f);
            break;

        case WICON_MOON:
            draw_moon(box, S, bg_color);
            break;

        case WICON_PARTLY:
            draw_sun(box, S, 0.36f, 0.34f, 0.31f);
            draw_cloud(box, S, C_CLOUD, 0.76f, 0.20f, 0.30f);
            break;

        case WICON_PARTLY_NIGHT:
            draw_moon(box, S, bg_color);
            draw_cloud(box, S, C_CLOUD, 0.70f, 0.26f, 0.34f);
            break;

        case WICON_CLOUD:
            draw_cloud(box, S, C_CLOUD_GRAY, 0.92f, 0.04f, 0.16f);
            draw_cloud(box, S, C_CLOUD, 0.70f, 0.16f, 0.24f);
            break;

        case WICON_FOG:
            draw_cloud(box, S, C_CLOUD_GRAY, 0.78f, 0.11f, 0.02f);
            add_round_rect(box, (int)(0.10f * S), (int)(0.66f * S), (int)(0.66f * S), (int)(0.09f * S), (int)(0.045f * S), C_FOG);
            add_round_rect(box, (int)(0.20f * S), (int)(0.79f * S), (int)(0.70f * S), (int)(0.09f * S), (int)(0.045f * S), C_FOG);
            add_round_rect(box, (int)(0.13f * S), (int)(0.92f * S), (int)(0.52f * S), (int)(0.08f * S), (int)(0.04f * S), C_FOG);
            break;

        case WICON_DRIZZLE:
            draw_cloud(box, S, C_CLOUD, 0.86f, 0.07f, 0.00f);
            draw_rain(box, S, 2);
            add_circle(box, (int)(0.70f * S), (int)(0.82f * S), (int)(0.07f * S), C_RAIN, LV_OPA_COVER);
            break;

        case WICON_RAIN:
            draw_cloud(box, S, C_CLOUD, 0.86f, 0.07f, 0.00f);
            draw_rain(box, S, 3);
            break;

        case WICON_SLEET:
            draw_cloud(box, S, C_CLOUD, 0.86f, 0.07f, 0.00f);
            add_seg(box, (int)(0.32f * S), (int)(0.70f * S), (int)(0.26f * S), (int)(0.90f * S), C_RAIN, (int)(S * 0.05f));
            add_circle(box, (int)(0.52f * S), (int)(0.80f * S), (int)(0.09f * S), C_SNOW, LV_OPA_COVER);
            add_circle(box, (int)(0.68f * S), (int)(0.88f * S), (int)(0.08f * S), C_SNOW, LV_OPA_COVER);
            break;

        case WICON_SNOW:
            draw_cloud(box, S, C_CLOUD, 0.86f, 0.07f, 0.00f);
            draw_snow(box, S);
            break;

        case WICON_THUNDER:
            draw_cloud(box, S, C_CLOUD, 0.86f, 0.07f, 0.00f);
            draw_bolt(box, S);
            break;

        case WICON_UNKNOWN:
        default:
            draw_cloud(box, S, C_CLOUD_GRAY, 0.86f, 0.07f, 0.14f);
            add_circle(box, (int)(0.38f * S), (int)(0.56f * S), (int)(0.08f * S), C_CLOUD_GRAY, LV_OPA_COVER);
            add_circle(box, (int)(0.55f * S), (int)(0.56f * S), (int)(0.08f * S), C_CLOUD_GRAY, LV_OPA_COVER);
            add_circle(box, (int)(0.72f * S), (int)(0.56f * S), (int)(0.08f * S), C_CLOUD_GRAY, LV_OPA_COVER);
            break;
    }

    return box;
}
