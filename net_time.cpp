#include "net_time.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"

static bool     s_started          = false;
static bool     s_syncing          = false;
static uint32_t s_sync_started_ms  = 0;
static bool     s_time_ok          = false;
static uint32_t s_last_retry_ms    = 0;

#define WIFI_RETRY_INTERVAL_MS   10000
#define NTP_RETRY_INTERVAL_MS    20000

/* ------------------------------------------------------------------ */
/*  WiFi                                                               */
/* ------------------------------------------------------------------ */
void net_begin()
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    /* 关闭 WiFi 省电：省电模式会让 RGB 屏出现横纹/闪烁 */
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    Serial.printf("[网络] 开始连接 WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    s_started       = true;
    s_last_retry_ms = millis();
}

void net_loop()
{
    if (!s_started) return;
    if (WiFi.status() == WL_CONNECTED) return;

    uint32_t now = millis();
    if (now - s_last_retry_ms < WIFI_RETRY_INTERVAL_MS) return;
    s_last_retry_ms = now;

    Serial.println("[网络] 未连接，重试中…");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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
