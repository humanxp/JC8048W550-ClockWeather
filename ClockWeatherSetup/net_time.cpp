#include "net_time.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "settings.h"

static bool     s_started          = false;
static bool     s_syncing          = false;
static uint32_t s_sync_started_ms  = 0;
static bool     s_time_ok          = false;
static uint32_t s_last_retry_ms    = 0;

/* 自动重连的目标账号（net_connect_to() 期间会被临时替换） */
static char     s_target_ssid[33]  = {0};
static char     s_target_pass[65]  = {0};

/* 向导正在手动连接时置 true：net_loop() 不要插手重连 */
static bool     s_manual_connect   = false;

/* 扫描结果 */
struct ScanAp {
    char ssid[33];
    int  rssi;
    bool secure;
};
static ScanAp   s_scan[NET_SCAN_MAX];
static int      s_scan_count   = 0;
static bool     s_scan_running = false;
static bool     s_scan_ready   = false;
static bool     s_scan_failed  = false;
static uint32_t s_scan_started_ms = 0;

#define WIFI_RETRY_INTERVAL_MS   10000
#define NTP_RETRY_INTERVAL_MS    20000

/* ------------------------------------------------------------------ */
/*  WiFi                                                               */
/* ------------------------------------------------------------------ */
static void set_target(const char *ssid, const char *pass)
{
    copy_utf8(s_target_ssid, sizeof(s_target_ssid), ssid);
    copy_utf8(s_target_pass, sizeof(s_target_pass), pass);
}

void net_begin()
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    /* 关闭 WiFi 省电：省电模式会让 RGB 屏出现横纹/闪烁 */
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    s_started        = true;
    s_manual_connect = false;
    set_target(settings_wifi_ssid(), settings_wifi_pass());
    s_last_retry_ms  = millis();

    if (s_target_ssid[0] == '\0') {
        Serial.println("[网络] 还没有配置 WiFi，等待设置向导");
        return;
    }

    Serial.printf("[网络] 开始连接 WiFi: %s\n", s_target_ssid);
    WiFi.disconnect();
    WiFi.begin(s_target_ssid, s_target_pass);
}

void net_connect_to(const char *ssid, const char *pass)
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    s_started        = true;
    s_manual_connect = true;
    set_target(ssid, pass);
    s_last_retry_ms  = millis();

    Serial.printf("[网络] 尝试连接: %s\n", s_target_ssid);
    WiFi.disconnect();
    delay(30);
    WiFi.begin(s_target_ssid, s_target_pass);
}

void net_adopt_current()
{
    s_manual_connect = false;
    s_last_retry_ms  = millis();
}

void net_loop()
{
    if (!s_started) return;

    /* 向导正在手动连接：别在这里 disconnect，会把这次尝试打断 */
    if (s_manual_connect) return;
    if (WiFi.status() == WL_CONNECTED) return;
    if (s_target_ssid[0] == '\0') return;

    uint32_t now = millis();
    if (now - s_last_retry_ms < WIFI_RETRY_INTERVAL_MS) return;
    s_last_retry_ms = now;

    Serial.println("[网络] 未连接，重试中…");
    WiFi.disconnect();
    WiFi.begin(s_target_ssid, s_target_pass);
}

bool net_is_connected()
{
    return WiFi.status() == WL_CONNECTED;
}

int net_rssi()
{
    if (!net_is_connected()) return 0;
    return WiFi.RSSI();
}

int net_rssi_level()
{
    int r = net_rssi();
    if (r == 0)   return 0;
    if (r > -55)  return 4;
    if (r > -65)  return 3;
    if (r > -75)  return 2;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  扫描                                                               */
/* ------------------------------------------------------------------ */
void net_scan_clear()
{
    WiFi.scanDelete();
    s_scan_count   = 0;
    s_scan_running = false;
    s_scan_ready   = false;
    s_scan_failed  = false;
}

void net_scan_start()
{
    if (s_scan_running) return;

    net_scan_clear();
    Serial.println("[网络] 开始扫描附近的 WiFi…");

    /* 第二个参数 false = 不显示隐藏网络；第一个参数 true = 异步，不阻塞 */
    int ret = WiFi.scanNetworks(true, false);
    if (ret == WIFI_SCAN_FAILED) {
        s_scan_failed = true;
        s_scan_ready  = true;
        Serial.println("[网络] 扫描启动失败");
        return;
    }
    s_scan_running = true;
    s_scan_ready   = false;
    s_scan_started_ms = millis();
}

/** 把 WiFi 的原始结果去重（同名留最强）、按信号从强到弱排序 */
static void collect_scan_results()
{
    int n = WiFi.scanComplete();
    s_scan_count = 0;

    for (int i = 0; i < n && i < 64; i++) {
        String name = WiFi.SSID(i);
        if (name.length() == 0) continue;

        int  rssi   = WiFi.RSSI(i);
        bool secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

        /* 同一个 SSID 可能出现多个热点（mesh / 双频），只留信号最强的 */
        int found = -1;
        for (int k = 0; k < s_scan_count; k++) {
            if (name.equals(s_scan[k].ssid)) { found = k; break; }
        }
        if (found >= 0) {
            if (rssi > s_scan[found].rssi) {
                s_scan[found].rssi   = rssi;
                s_scan[found].secure = secure;
            }
            continue;
        }
        if (s_scan_count >= NET_SCAN_MAX) break;

        copy_utf8(s_scan[s_scan_count].ssid, sizeof(s_scan[0].ssid), name.c_str());
        s_scan[s_scan_count].rssi   = rssi;
        s_scan[s_scan_count].secure = secure;
        s_scan_count++;
    }

    /* 简单选择排序：数量最多 24，开销可以忽略 */
    for (int i = 0; i < s_scan_count - 1; i++) {
        int best = i;
        for (int k = i + 1; k < s_scan_count; k++) {
            if (s_scan[k].rssi > s_scan[best].rssi) best = k;
        }
        if (best != i) {
            ScanAp tmp = s_scan[i];
            s_scan[i]  = s_scan[best];
            s_scan[best] = tmp;
        }
    }

    Serial.printf("[网络] 扫描完成，共 %d 个网络\n", s_scan_count);
}

bool net_scan_running()
{
    if (!s_scan_running) return false;

    /* 刚启动的头 0.5 秒别急着判失败：异步扫描的状态要过一会儿才置起来，
       这期间 scanComplete() 可能短暂返回 -2，会误报「扫描失败」。 */
    if (millis() - s_scan_started_ms < 500) return true;

    int r = WiFi.scanComplete();
    if (r == WIFI_SCAN_RUNNING) return true;      /* 还在扫，状态不变 */

    /* 扫完了（r >= 0）或者失败了（WIFI_SCAN_FAILED = -2） */
    s_scan_running = false;
    s_scan_ready   = true;
    if (r == WIFI_SCAN_FAILED) {
        s_scan_failed = true;
        Serial.println("[网络] 扫描失败");
    } else {
        collect_scan_results();
    }
    return false;
}

bool net_scan_ready()   { return s_scan_ready; }
bool net_scan_failed()  { return s_scan_failed; }
int  net_scan_count()   { return s_scan_count; }

/* 只读：给主循环判断「现在在扫描，先别拉天气」用。
   注意别用 net_scan_running() 代替 —— 那个会顺手收扫描结果。 */
bool net_scan_busy()    { return s_scan_running; }

const char *net_scan_ssid(int index)
{
    if (index < 0 || index >= s_scan_count) return "";
    return s_scan[index].ssid;
}

int net_scan_rssi(int index)
{
    if (index < 0 || index >= s_scan_count) return 0;
    return s_scan[index].rssi;
}

bool net_scan_secure(int index)
{
    if (index < 0 || index >= s_scan_count) return false;
    return s_scan[index].secure;
}

/* ------------------------------------------------------------------ */
/*  NTP                                                                */
/* ------------------------------------------------------------------ */
void time_begin_sync()
{
    Serial.printf("[时间] 启动 NTP 授时，时区 %s\n", TIMEZONE);
    /* configTzTime 启动 SNTP 后台任务，不阻塞 */
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    s_syncing         = true;
    s_sync_started_ms = millis();
}

bool time_is_valid()
{
    if (s_time_ok) return true;

    /* 2020-01-01 之后认为是有效时间（未同步时是 1970 年） */
    if (time(nullptr) > 1577836800) {
        s_time_ok = true;
        Serial.println("[时间] NTP 同步成功");
        return true;
    }

    if (s_syncing && (millis() - s_sync_started_ms) > NTP_RETRY_INTERVAL_MS) {
        s_syncing = false;
        time_begin_sync();
    }
    return false;
}

bool time_now(struct tm *out)
{
    if (!time_is_valid()) return false;
    return getLocalTime(out, 0);
}
