#include "weather.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>              /* WiFi.hostByName() 做 DNS 诊断用 */
#include <WiFiClientSecure.h>

#include "config.h"
#include "net_time.h"
#include "settings.h"

/* 天气接口域名：默认 Open-Meteo。config.h 里可以改（见那边的说明）。 */
#ifndef WEATHER_HOST
#define WEATHER_HOST "api.open-meteo.com"
#endif

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
/*  发一次 GET                                                         */
/*                                                                     */
/*  use_tls = true  -> HTTPS（WiFiClientSecure，需要 mbedTLS 的连续内存）*/
/*  use_tls = false -> 明文 HTTP（WiFiClient，80 端口，完全不碰 TLS）    */
/*                                                                     */
/*  为什么留一条明文的路：TLS 握手要一次拿到十几 KB「连续」内部 RAM，   */
/*  拿不到就直接失败，而 HTTPClient 只回一个 -1；而且不少网络对 443     */
/*  比对 80 更苛刻。天气数据不敏感，明文完全够用。                      */
/* ================================================================== */
static bool http_get(const String &path, bool use_tls, String *body, String *err)
{
    String url = String(use_tls ? "https://" : "http://") + WEATHER_HOST + path;

    int code = 0;

    /* ⚠⚠ 声明顺序是雷区：必须先声明 client、后声明 HTTPClient。
     *
     *   C++ 的析构顺序和声明顺序相反 —— client 在前，http 就在前析构，
     *   这时 client 还活着。
     *
     *   反过来（http 先声明）会怎样：HTTPClient::end() 只在
     *   connected() 为真时才把 _client 置空；而服务器发完响应常常直接关连接，
     *   此时 connected() 为假，_client 不会被清掉。接着 client 先析构，
     *   ~HTTPClient() 又对这块已经销毁的对象调用 _client->stop() ——
     *   use-after-free，lwIP 里就是 pbuf 双重释放：
     *       assert failed: pbuf_free ... (pbuf_free: p->ref > 0)
     *   实测症状：天气拉取成功约 300ms 后崩溃重启（花屏 + Rebooting）。
     *
     *   所以下面 TLS 分支和明文分支都是「client 行在前，HTTPClient 行在后」，
     *   千万不要为了"整洁"把它俩换个位置。 */
    if (use_tls) {
        WiFiClientSecure client;
        client.setInsecure();               /* 个人项目不做证书校验，省 Flash 和内存 */
        client.setTimeout(8);
        client.setHandshakeTimeout(8000);   /* 卡住就尽快失败，好让上层重试 */

        HTTPClient http;
        http.setConnectTimeout(8000);
        http.setTimeout(8000);

        if (!http.begin(client, url)) { *err = "http.begin 失败"; return false; }
        code = http.GET();
        if (code == HTTP_CODE_OK) *body = http.getString();
        else                      *err  = String("TLS ") + http.errorToString(code);
        http.end();
    } else {
        WiFiClient client;
        client.setTimeout(8);

        HTTPClient http;
        http.setConnectTimeout(8000);
        http.setTimeout(8000);

        if (!http.begin(client, url)) { *err = "http.begin 失败"; return false; }
        code = http.GET();
        if (code == HTTP_CODE_OK) *body = http.getString();
        else                      *err  = String("HTTP ") + http.errorToString(code);
        http.end();
    }

    return code == HTTP_CODE_OK;
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

    /* 坐标来自设置向导选的地区，存在 Flash 里（见 settings.cpp）。
       没有地区就直接告诉用户去设置，不再走地理编码接口。 */
    if (!settings_has_region()) {
        *err = "还没有设置地区";
        return false;
    }
    float lat = settings_lat();
    float lon = settings_lon();

    String path = String("/v1/forecast?latitude=") + String(lat, 4) +
                 "&longitude=" + String(lon, 4) +
                 "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
                 "is_day,weather_code,surface_pressure,wind_speed_10m" +
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
                 "precipitation_probability_max" +
                 "&timezone=auto&forecast_days=" + String(WEATHER_FORECAST_DAYS) +
                 "&wind_speed_unit=ms";

    /* ---- 先单独做一次 DNS 解析并打印结果 ----
       HTTPClient 失败时只会返回一个 HTTP -1（连接被拒），
       DNS 解析不了 和 网络连不通 都报同一个码，没法排查。
       这里先把域名解析出来，日志里就能一眼区分。 */
    IPAddress host_ip;
    if (!WiFi.hostByName(WEATHER_HOST, host_ip)) {
        *err = "DNS 解析失败（域名解析不出 IP）";
        Serial.printf("[天气] DNS 解析失败  空闲堆 %u  最大连续堆 %u\n",
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        return false;
    }
    /* 最大连续堆(HeapMaxAlloc)是决定 TLS 能否握手的关键：
       mbedTLS 要一次性分配约 16~40KB 的连续内部 RAM，
       碎片化或总量不足时连接就会失败，而 HTTPClient 只报一个 -1。 */
    Serial.printf("[天气] %s -> %s  空闲堆 %u  最大连续堆 %u\n",
                  WEATHER_HOST, host_ip.toString().c_str(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

    /* ---- 决定走不走 TLS ----
       TLS 握手要一次拿到十几 KB「连续」内部内存。实测这台机器界面跑起来后
       「最大连续堆」只有 25KB 左右，扫描 WiFi 时更是掉到 8KB —— 根本握不上，
       而硬试一次还会把堆搞得更碎、白等十几秒。
       所以默认（config.h 的 WEATHER_TRY_TLS_FIRST = 0）直接走明文 HTTP：
       不走 mbedTLS，只要几 KB 内存，国内网络对 80 端口也更宽松。
       天气数据不敏感，够用。 */
    String body;
    String err_detail;
    bool ok = false;

#if WEATHER_TRY_TLS_FIRST
    if (ESP.getMaxAllocHeap() >= 20000) {
        ok = http_get(path, true, &body, &err_detail);
        if (!ok) {
            Serial.printf("[天气] HTTPS 失败（%s），改走明文 HTTP\n", err_detail.c_str());
            body = "";
        }
    } else {
        Serial.printf("[天气] 最大连续堆只有 %u 字节，TLS 握不上，直接走明文 HTTP\n",
                      (unsigned)ESP.getMaxAllocHeap());
    }
#endif

    if (!ok && !http_get(path, false, &body, &err_detail)) {
        *err = String("天气连不上：") + err_detail;
        Serial.printf("[天气] HTTP 失败（%s）  空闲堆 %u  最大连续堆 %u\n",
                      err_detail.c_str(),
                      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
        return false;
    }

    /* 每次都把内存状况打出来：连续跑几小时看趋势，
       就能判断「连不上」是不是堆碎片化造成的。 */
    Serial.printf("[天气] 拿到 %u 字节  空闲堆 %u  最大连续堆 %u\n",
                  (unsigned)body.length(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

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
        out->days[i].pop   = -1.0f;      /* -1 = 未知（见下面 daily 解析） */
    }

    if (block_range(body, "daily", &ds, &de)) {
        float codes[WEATHER_FORECAST_DAYS];
        float tmax[WEATHER_FORECAST_DAYS];
        float tmin[WEATHER_FORECAST_DAYS];
        float pop[WEATHER_FORECAST_DAYS];

        int nc = json_number_array(body, ds, de, "weather_code",       codes, WEATHER_FORECAST_DAYS);
        int nx = json_number_array(body, ds, de, "temperature_2m_max", tmax,  WEATHER_FORECAST_DAYS);
        int nn = json_number_array(body, ds, de, "temperature_2m_min", tmin,  WEATHER_FORECAST_DAYS);

        /* 降水概率是「额外」字段：有的模式/参数下接口不给，或者给 null。
           所以它单独解析，缺了也只是不画概率条（pop = -1），
           绝不能让温度那几项跟着一起作废。 */
        int np = json_number_array(body, ds, de, "precipitation_probability_max",
                                   pop, WEATHER_FORECAST_DAYS);

        int n = nc;
        if (nx < n) n = nx;
        if (nn < n) n = nn;

        for (int i = 0; i < n; i++) {
            out->days[i].code  = (int)codes[i];
            out->days[i].tmax  = tmax[i];
            out->days[i].tmin  = tmin[i];
            out->days[i].pop   = (i < np && !isnan(pop[i])) ? pop[i] : -1.0f;
            out->days[i].valid = true;
        }
    }

    out->fetched_ms = millis();

    Serial.printf("[天气] %.1f°C 体感%.1f° 湿度%.0f%% 风%.1fm/s 气压%.0fhPa 代码%d %s\n",
                  out->temp, out->apparent, out->humidity, out->wind,
                  out->pressure, out->code, weather_text(out->code));
    if (out->days[0].valid) {
        /* pop 为 -1 表示接口没给这个字段，会照原样打成 -1% */
        Serial.printf("[天气] 三天降水概率: %.0f%% / %.0f%% / %.0f%%\n",
                      out->days[0].pop, out->days[1].pop, out->days[2].pop);
    }
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
