#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "board_pins.h"     /* LCD_HEIGHT：校验搬运缓冲行数是否整除屏幕高度 */
#include "config.h"

/* NVS 命名空间（最长 15 字符）。和 ClockWeather 天气缓存用的 "clockwx" 分开，互不干扰。 */
#define SETTINGS_NVS_NS   "clksetup"

/* 各字段容量：SSID 标准上限 32 字节，密码上限 64 字节，地区名留得宽一点。 */
#define CAP_SSID      33
#define CAP_PASS      65
#define CAP_REGION    40

static char     s_ssid[CAP_SSID]     = {0};
static char     s_pass[CAP_PASS]     = {0};
static char     s_province[CAP_REGION] = {0};
static char     s_city[CAP_REGION]   = {0};
static float    s_lat = 0.0f;
static float    s_lon = 0.0f;
static uint32_t s_revision = 0;

void copy_utf8(char *dst, size_t cap, const char *src)
{
    if (dst == nullptr || cap == 0) return;
    if (src == nullptr) { dst[0] = '\0'; return; }

    size_t len = strlen(src);
    if (len > cap - 1) {
        len = cap - 1;
        /* 0b10xxxxxx 是 UTF-8 的后续字节，往前退到字符边界 */
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void settings_load()
{
    /* ---- 1. 先铺一层 config.h 的出厂默认值 ---- */
    copy_utf8(s_ssid,     sizeof(s_ssid),     WIFI_SSID);
    copy_utf8(s_pass,     sizeof(s_pass),     WIFI_PASSWORD);
    copy_utf8(s_province, sizeof(s_province), "");
    copy_utf8(s_city,     sizeof(s_city),     CITY_NAME);
    s_lat = CITY_LAT;
    s_lon = CITY_LON;

    /* ---- 2. Flash 里有值就覆盖 ---- */
    Preferences prefs;
    if (prefs.begin(SETTINGS_NVS_NS, true)) {
        String ssid = prefs.getString("ssid", "");
        String pass = prefs.getString("pass", "");
        String prov = prefs.getString("prov", "");
        String city = prefs.getString("city", "");

        if (ssid.length()) copy_utf8(s_ssid, sizeof(s_ssid), ssid.c_str());
        if (pass.length()) copy_utf8(s_pass, sizeof(s_pass), pass.c_str());
        if (prov.length()) copy_utf8(s_province, sizeof(s_province), prov.c_str());
        if (city.length()) copy_utf8(s_city, sizeof(s_city), city.c_str());

        float lat = prefs.getFloat("lat", 0.0f);
        float lon = prefs.getFloat("lon", 0.0f);
        if (lat != 0.0f && lon != 0.0f) {
            s_lat = lat;
            s_lon = lon;
        }

        prefs.end();
    }

    s_revision = 0;

    Serial.printf("[设置] WiFi: %s\n", s_ssid[0] ? s_ssid : "（未配置）");
    Serial.printf("[设置] 地区: %s%s%s  坐标 %.4f, %.4f\n",
                  s_province, s_province[0] ? " · " : "", s_city,
                  (double)s_lat, (double)s_lon);
}

bool settings_has_wifi()
{
    return s_ssid[0] != '\0';
}

bool settings_has_region()
{
    return (s_lat != 0.0f && s_lon != 0.0f);
}

const char *settings_wifi_ssid() { return s_ssid; }
const char *settings_wifi_pass() { return s_pass; }
const char *settings_province()  { return s_province; }
const char *settings_city()      { return s_city; }

const char *settings_city_name()
{
    if (s_city[0])  return s_city;
    if (CITY_NAME[0]) return CITY_NAME;
    return "未设置地区";
}

float settings_lat()      { return s_lat; }
float settings_lon()      { return s_lon; }
const char *settings_timezone() { return TIMEZONE; }
uint32_t settings_revision()    { return s_revision; }

void settings_save_wifi(const char *ssid, const char *pass)
{
    copy_utf8(s_ssid, sizeof(s_ssid), ssid);
    copy_utf8(s_pass, sizeof(s_pass), pass);

    Preferences prefs;
    if (prefs.begin(SETTINGS_NVS_NS, false)) {
        prefs.putString("ssid", s_ssid);
        prefs.putString("pass", s_pass);
        prefs.end();
    }
    s_revision++;
    Serial.printf("[设置] 已保存 WiFi: %s\n", s_ssid);
}

void settings_save_region(const char *province, const char *city, float lat, float lon)
{
    copy_utf8(s_province, sizeof(s_province), province);
    copy_utf8(s_city,     sizeof(s_city),     city);
    s_lat = lat;
    s_lon = lon;

    Preferences prefs;
    if (prefs.begin(SETTINGS_NVS_NS, false)) {
        prefs.putString("prov", s_province);
        prefs.putString("city", s_city);
        prefs.putFloat("lat", s_lat);
        prefs.putFloat("lon", s_lon);
        prefs.end();
    }
    s_revision++;
    Serial.printf("[设置] 已保存地区: %s · %s (%.4f, %.4f)\n",
                  s_province, s_city, (double)s_lat, (double)s_lon);
}

void settings_clear_all()
{
    Preferences prefs;
    if (prefs.begin(SETTINGS_NVS_NS, false)) {
        prefs.clear();
        prefs.end();
    }
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    s_province[0] = '\0';
    s_city[0] = '\0';
    s_lat = 0.0f;
    s_lon = 0.0f;
    s_revision++;
    Serial.println("[设置] 已清空（下次开机会重新进向导）");
}

/* ------------------------------------------------------------------ */
/*  调试旋钮：DMA 搬运缓冲行数                                          */
/*                                                                     */
/*  单独用一次 Preferences::begin 读，不塞进 settings_load()：因为        */
/*  display_init() 在 settings_load() 之前就要用到这个值。              */
/*  读写都是很小的 NVS 操作，开机一次 + 手动命令时才发生，不影响运行。  */
/* ------------------------------------------------------------------ */
int settings_bounce_lines()
{
    /* 语义：-1 = 没存过（用 board_pins.h 的默认值）；0 = 明确要「不用搬运缓冲」 */
    int lines = -1;

    Preferences prefs;
    if (prefs.begin(SETTINGS_NVS_NS, true)) {
        lines = (int)prefs.getInt("bounce", -1);
        prefs.end();
    }

    if (lines < 0) return -1;

    /* 非负值必须合法（整除屏幕高度，否则 IDF 会断言）。
       v2.3 起是 no_fb 模式，搬运缓冲不能为 0（IDF 会拒绝），0 一律当没存过。 */
    if (lines <= 0 || lines > 60 || (LCD_HEIGHT % lines) != 0) {
        if (lines > 0) {
            Serial.printf("[设置] 存下来的搬运缓冲行数 %d 不合法（必须整除 %d），忽略\n",
                          lines, LCD_HEIGHT);
        }
        return -1;
    }
    return lines;
}

void settings_save_bounce_lines(int lines)
{
    /* lines < 0 = 清除这条设置（回到 board_pins.h 的默认值）
       lines = 0 = 明确要「不用搬运缓冲」（DMA 直接读 PSRAM） */
    Preferences prefs;
    if (prefs.begin(SETTINGS_NVS_NS, false)) {
        if (lines < 0) prefs.remove("bounce");
        else           prefs.putInt("bounce", lines);
        prefs.end();
    }
    if (lines < 0) {
        Serial.println("[设置] 搬运缓冲行数 -> 代码默认值（重启后生效）");
    } else {
        Serial.printf("[设置] 搬运缓冲行数 -> %d（重启后生效）\n", lines);
    }
}
