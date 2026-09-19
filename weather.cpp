#include "weather.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>              /* WiFi.hostByName() 做 DNS 诊断用 */
#include <WiFiClientSecure.h>

#include "config.h"
#include "net_time.h"

/* ================================================================== */
/*  极简 JSON 取值工具                                                 */
/*                                                                     */
/*  Open-Meteo 返回的结构固定且扁平，为了少装一个 ArduinoJson 依赖，    */
/*  这里用「按 key 精确定位 + 解析数字」的办法取值。                    */
/*  因为搜索的是带冒号的 `"key":`，所以 "temperature_2m" 不会误匹配到   */
/*  "temperature_2m_max"。                                             */
/* ================================================================== */

/** 定位 "key":{...} 或 "key":[...] 的内容区间 [start, end) */
static bool block_range(const String &s, const char *key, int *start, int *end)
{
    String pat = String("\"") + key + "\":";
    int i = s.indexOf(pat);
    if (i < 0) return false;

    i += pat.length();
    while (i < (int)s.length() && s[i] == ' ') i++;
    if (i >= (int)s.length()) return false;

    char open  = s[i];
    char close = (open == '{') ? '}' : ']';
    int e = s.indexOf(close, i + 1);
    if (e < 0) return false;

    *start = i + 1;
    *end   = e;
    return true;
}

/** 在 [from, to) 中取 "key": <number> */
static bool json_number(const String &s, int from, int to, const char *key, float *out)
{
    String pat = String("\"") + key + "\":";
    int i = s.indexOf(pat, from);
    if (i < 0 || i >= to) return false;

    i += pat.length();
    if (i >= (int)s.length()) return false;

    const char *p = s.c_str() + i;
    while (*p == ' ') p++;
    if (*p == 'n' || *p == 'N') return false;   /* null */

    char *endp = nullptr;
    float v = strtof(p, &endp);
    if (endp == p) return false;

    *out = v;
    return true;
}

/** 在 [from, to) 中取 "key":[a,b,c]，返回取到的个数 */
static int json_number_array(const String &s, int from, int to, const char *key, float *out, int max_n)
{
    String pat = String("\"") + key + "\":[";
    int i = s.indexOf(pat, from);
    if (i < 0 || i >= to) return -1;

    i += pat.length();
    const char *p = s.c_str() + i;
    const char *limit = s.c_str() + to;

    int n = 0;
    while (n < max_n && p < limit) {
        while (p < limit && (*p == ' ' || *p == ',')) p++;
        if (p >= limit || *p == ']') break;
        if (*p == 'n') {                       /* null */
            out[n++] = NAN;
            while (p < limit && *p != ',' && *p != ']') p++;
            continue;
        }
        char *endp = nullptr;
        float v = strtof(p, &endp);
        if (endp == p) break;
        out[n++] = v;
        p = endp;
    }
    return n;
}

/* ================================================================== */
/*  URL 百分号编码                                                     */
/*                                                                     */
/*  ⚠ 不能把中文原样塞进 URL！实测 Open-Meteo 的地理编码接口对原始     */
/*    UTF-8 字节返回 403 Forbidden，百分号编码后才是 200。             */
/*    （曾经这里只做了 replace(" ", "+")，漏了中文。）                 */
/* ================================================================== */
static String url_encode(const String &s)
{
    /* ⚠ 变量名不能叫 HEX！Arduino 的 Print.h 里有 #define HEX 16
       （就是 Serial.print(x, HEX) 用的那个），会把数组名替换成 16[]。 */
    static const char hex_digits[] = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() * 3);

    for (size_t i = 0; i < s.length(); i++) {
        uint8_t c = (uint8_t)s[i];
        /* unreserved 字符直接保留，其余（含所有中文、空格）全部编码 */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex_digits[c >> 4];
            out += hex_digits[c & 0x0F];
        }
    }
    return out;
}

/* ================================================================== */
/*  坐标：没有填经纬度时，用城市名走一次地理编码并缓存到 Flash          */
/* ================================================================== */
static bool resolve_coordinates(float *lat, float *lon, String *display_name, String *err)
{
    if (CITY_LAT != 0.0f || CITY_LON != 0.0f) {
        *lat = CITY_LAT;
        *lon = CITY_LON;
        *display_name = CITY_NAME;
        return true;
    }

    /* --- 先看 Flash 里有没有缓存 --- */
    Preferences prefs;
    prefs.begin("clockwx", true);
    float clat = prefs.getFloat("lat", 0.0f);
    float clon = prefs.getFloat("lon", 0.0f);
    String cname = prefs.getString("name", "");
    prefs.end();

    if (clat != 0.0f && clon != 0.0f) {
        *lat = clat;
        *lon = clon;
        *display_name = (cname.length() ? cname : String(CITY_NAME));
        Serial.printf("[天气] 使用缓存坐标 %.4f, %.4f\n", clat, clon);
        return true;
    }

    /* --- 走地理编码接口查一次 --- */
    Serial.printf("[天气] 用城市名查询坐标: %s\n", CITY_NAME);

    /* 中文必须做百分号编码，否则接口返回 403 */
    String url = String("https://geocoding-api.open-meteo.com/v1/search?name=") +
                 url_encode(CITY_NAME) +
                 "&count=1&language=zh&format=json";

    WiFiClientSecure client;
    client.setInsecure();               /* 个人项目不做证书校验，省 Flash 和内存 */
    client.setTimeout(10);

    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    if (!http.begin(client, url)) {
        *err = "地理编码：无法建立连接";
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        *err = String("地理编码 HTTP ") + code;
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    int rs = 0, re = 0;
    if (!block_range(body, "results", &rs, &re)) {
        *err = String("没查到「") + CITY_NAME + "」，请改成手动填经纬度";
        return false;
    }
    /* results 是数组，元素是对象，用对象区间取坐标 */
    int os = body.indexOf('{', rs);
    if (os < 0 || os > re) {
        *err = String("没查到「") + CITY_NAME + "」，请改成手动填经纬度";
        return false;
    }

    float la = 0, lo = 0;
    if (!json_number(body, os, re, "latitude", &la) ||
        !json_number(body, os, re, "longitude", &lo)) {
        *err = "地理编码返回内容无法解析";
        return false;
    }

    *lat = la;
    *lon = lo;
    *display_name = CITY_NAME;

    Preferences w;
    w.begin("clockwx", false);
    w.putFloat("lat", la);
    w.putFloat("lon", lo);
    w.putString("name", CITY_NAME);
    w.end();

    Serial.printf("[天气] 坐标解析为 %.4f, %.4f（已缓存）\n", la, lo);
    return true;
}

/* ================================================================== */
/*  拉取天气                                                           */
/* ================================================================== */
bool weather_fetch(WeatherData *out, String *err)
{
    if (!net_is_connected()) {
        *err = "网络未连接";
        return false;
    }

    float lat = 0, lon = 0;
    String display;
    if (!resolve_coordinates(&lat, &lon, &display, err)) {
        return false;
    }

    String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(lat, 4) +
                 "&longitude=" + String(lon, 4) +
                 "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
                 "is_day,weather_code,surface_pressure,wind_speed_10m" +
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min" +
                 "&timezone=auto&forecast_days=" + String(WEATHER_FORECAST_DAYS) +
                 "&wind_speed_unit=ms";

    /* ---- 先单独做一次 DNS 解析并打印结果 ----
       HTTPClient 失败时只会返回一个 HTTP -1（连接被拒），
       DNS 解析不了 和 网络连不通 都报同一个码，没法排查。
       这里先把域名解析出来，日志里就能一眼区分。 */
    IPAddress host_ip;
    if (!WiFi.hostByName("api.open-meteo.com", host_ip)) {
        *err = "DNS 解析失败（域名解析不出 IP）";
        Serial.printf("[天气] DNS 解析失败  空闲堆 %u  最大连续堆 %u\n",
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        return false;
    }
    /* 最大连续堆(HeapMaxAlloc)是决定 TLS 能否握手的关键：
       mbedTLS 要一次性分配约 16~40KB 的连续内部 RAM，
       碎片化或总量不足时连接就会失败，而 HTTPClient 只报一个 -1。 */
    Serial.printf("[天气] api.open-meteo.com -> %s  空闲堆 %u  最大连续堆 %u\n",
                  host_ip.toString().c_str(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

    /* ---- 单次 HTTPS 请求 ----
       重试放在主程序里做（ClockWeather.ino 的 do_weather_update），
       因为重试之间有 1~2 秒空档，那段空档必须让 LVGL 继续刷新，
       否则时钟会卡住不动 —— weather.cpp 这层不依赖 LVGL，做不了这件事。 */
    String body;
    int code = 0;

    {
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(8);

        HTTPClient http;
        http.setConnectTimeout(8000);
        http.setTimeout(8000);

        if (!http.begin(client, url)) {
            *err = "http.begin 失败";
        } else {
            code = http.GET();
            if (code == HTTP_CODE_OK) {
                body = http.getString();
            }
            http.end();
        }
        /* client 在这里析构，会 stop() 并释放 mbedTLS 的缓冲，
           避免把 TLS 的大块内存一直占着、加剧堆碎片 */
    }

    /* 每次都把内存状况打出来：连续跑几小时看趋势，
       就能判断「连不上」是不是堆碎片化造成的。 */
    Serial.printf("[天气] HTTP %d  空闲堆 %u  最大连续堆 %u\n",
                  code, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

    if (code != HTTP_CODE_OK) {
        if (code == -1) {
            /* -1 = HTTPC_ERROR_CONNECTION_REFUSED：TCP/TLS 都没连上。
               常见原因：TLS 要不到连续内存、路由器/热点连接数用满、信号太弱。 */
            *err = "天气接口连不上（HTTP -1）";
        } else {
            *err = String("天气接口 HTTP ") + code;
        }
        return false;
    }

    if (body.length() < 50) {
        *err = "天气接口返回内容为空";
        return false;
    }

    /* ---- current 区块 ---- */
    int cs = 0, ce = 0;
    if (!block_range(body, "current", &cs, &ce)) {
        /* Open-Meteo 出错时返回 {"error":true,"reason":"..."}。
           把 reason 提出来显示，比只报「没有 current 字段」好排查得多。
           注意 reason 是字符串值，不能套用 block_range（那是给 {} [] 用的）。 */
        int ri = body.indexOf("\"reason\":");
        if (ri >= 0) {
            ri += 9;                                  /* strlen("\"reason\":") */
            while (ri < (int)body.length() && body[ri] == ' ') ri++;
            if (ri < (int)body.length() && body[ri] == '"') {
                int endq = body.indexOf('"', ri + 1);
                if (endq > ri + 1) {
                    *err = body.substring(ri + 1, endq);
                    return false;
                }
            }
        }
        *err = "返回内容里没有 current 字段";
        return false;
    }

    float v = 0;
    out->valid = true;

    out->temp     = json_number(body, cs, ce, "temperature_2m",        &v) ? v : NAN;
    out->apparent = json_number(body, cs, ce, "apparent_temperature",  &v) ? v : NAN;
    out->humidity = json_number(body, cs, ce, "relative_humidity_2m",  &v) ? v : NAN;
    out->wind     = json_number(body, cs, ce, "wind_speed_10m",        &v) ? v : NAN;
    out->pressure = json_number(body, cs, ce, "surface_pressure",      &v) ? v : NAN;
    out->is_day   = json_number(body, cs, ce, "is_day",                &v) ? (v > 0.5f) : true;

    float wcode = 0;
    out->code = json_number(body, cs, ce, "weather_code", &wcode) ? (int)wcode : -1;

    /* ---- daily 区块 ---- */
    int ds = 0, de = 0;
    for (int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
        out->days[i].valid = false;
    }

    if (block_range(body, "daily", &ds, &de)) {
        float codes[WEATHER_FORECAST_DAYS];
        float tmax[WEATHER_FORECAST_DAYS];
        float tmin[WEATHER_FORECAST_DAYS];

        int nc = json_number_array(body, ds, de, "weather_code",       codes, WEATHER_FORECAST_DAYS);
        int nx = json_number_array(body, ds, de, "temperature_2m_max", tmax,  WEATHER_FORECAST_DAYS);
        int nn = json_number_array(body, ds, de, "temperature_2m_min", tmin,  WEATHER_FORECAST_DAYS);

        int n = nc;
        if (nx < n) n = nx;
        if (nn < n) n = nn;

        for (int i = 0; i < n; i++) {
            out->days[i].code  = (int)codes[i];
            out->days[i].tmax  = tmax[i];
            out->days[i].tmin  = tmin[i];
            out->days[i].valid = true;
        }
    }

    out->fetched_ms = millis();

    Serial.printf("[天气] %.1f°C 体感%.1f° 湿度%.0f%% 风%.1fm/s 气压%.0fhPa 代码%d %s\n",
                  out->temp, out->apparent, out->humidity, out->wind,
                  out->pressure, out->code, weather_text(out->code));
    return true;
}

/* ================================================================== */
/*  WMO 天气代码 -> 中文                                               */
/* ================================================================== */
const char *weather_text(int code)
{
    switch (code) {
        case 0:              return "晴";
        case 1:              return "晴间多云";
        case 2:              return "多云";
        case 3:              return "阴";
        case 45:             return "有雾";
        case 48:             return "雾凇";
        case 51:             return "毛毛雨";
        case 53:             return "小雨";
        case 55:             return "中雨";
        case 56: case 57:    return "冻雨";
        case 61:             return "小雨";
        case 63:             return "中雨";
        case 65:             return "大雨";
        case 66: case 67:    return "冻雨";
        case 71:             return "小雪";
        case 73:             return "中雪";
        case 75:             return "大雪";
        case 77:             return "米雪";
        case 80:             return "阵雨";
        case 81:             return "强阵雨";
        case 82:             return "暴雨";
        case 85:             return "阵雪";
        case 86:             return "强阵雪";
        case 95:             return "雷阵雨";
        case 96: case 99:    return "雷阵雨伴冰雹";
        default:             return "未知";
    }
}

WeatherIconType weather_icon_type(int code, bool is_day)
{
    switch (code) {
        case 0:              return is_day ? WICON_SUN : WICON_MOON;
        case 1: case 2:      return is_day ? WICON_PARTLY : WICON_PARTLY_NIGHT;
        case 3:              return WICON_CLOUD;
        case 45: case 48:    return WICON_FOG;
        case 51: case 53:    return WICON_DRIZZLE;
        case 55: case 61: case 63: case 65: case 80: case 81: case 82:
                             return WICON_RAIN;
        case 56: case 57: case 66: case 67:
                             return WICON_SLEET;
        case 71: case 73: case 75: case 77: case 85: case 86:
                             return WICON_SNOW;
        case 95: case 96: case 99:
                             return WICON_THUNDER;
        default:             return WICON_UNKNOWN;
    }
}
