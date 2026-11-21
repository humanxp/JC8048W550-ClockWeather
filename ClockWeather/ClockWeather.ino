/**
 * ============================================================================
 *  JC8048W550 联网时钟 + 天气
 *
 *  硬件：ESP32-S3-WROOM-1 N16R8 + 5 寸 800x480 IPS（ST7262 RGB）+ GT911 电容触摸
 *  软件：Arduino + Arduino_GFX + LVGL 8.4 + Open-Meteo（免 API Key）
 *
 *  第一次使用请先改 config.h 里的 WiFi 和城市。
 *  详细的接线 / 库安装 / 烧录参数见同目录 README.md。
 * ============================================================================
 */

#include "config.h"

#include <WiFi.h>

#include "display.h"
#include "net_time.h"
#include "touch_gt911.h"
#include "ui.h"
#include "weather.h"

/* ------------------------------------------------------------------ */
/*  全局状态                                                           */
/* ------------------------------------------------------------------ */
static WeatherData s_weather;
static uint32_t    s_last_clock_ms   = 0;
static uint32_t    s_weather_due_ms = 0;        /* 下次该拉天气的时刻 */
static bool        s_weather_done   = false;    /* 开机后是否已经拉过一次 */
static bool        s_wifi_was_up     = false;
static bool        s_time_was_ok     = false;

#define WEATHER_INTERVAL_MS   ((uint32_t)WEATHER_REFRESH_MIN * 60000UL)
#define WEATHER_RETRY_MS      60000UL           /* 失败后多久重试（1 分钟） */

/* ------------------------------------------------------------------ */
/*  LVGL 日志转发到串口                                                */
/* ------------------------------------------------------------------ */
#if LV_USE_LOG
static void lv_log_cb(const char *buf)
{
#if DEBUG_SERIAL
    Serial.print(buf);
#else
    (void)buf;
#endif
}
#endif

/* ------------------------------------------------------------------ */
/*  拉一次天气                                                         */
/*                                                                     */
/*  重试放在这里而不是 weather.cpp 里，是因为两次重试之间必须让 LVGL    */
/*  继续刷新 —— 否则网络一慢，时钟就会卡住十几秒不动。                  */
/*  每次尝试最长 8 秒，间隔 0.6 秒刷新界面，最坏情况约 27 秒。          */
/* ------------------------------------------------------------------ */
#define WEATHER_MAX_ATTEMPTS   3

static bool do_weather_update()
{
    Serial.printf("[内存] 更新前  空闲堆 %u  最大连续堆 %u  PSRAM %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)ESP.getFreePsram());

    ui_set_status(UI_STATUS_CONNECTING, "获取天气");
    lv_timer_handler();          /* 先把「获取天气」画到屏幕上 */
    delay(20);

    String err;
    bool ok = false;

    for (int attempt = 1; attempt <= WEATHER_MAX_ATTEMPTS && !ok; attempt++) {
        ok = weather_fetch(&s_weather, &err);

        if (!ok && attempt < WEATHER_MAX_ATTEMPTS) {
            Serial.printf("[天气] 第 %d/%d 次失败: %s（0.6 秒后重试）\n",
                          attempt, WEATHER_MAX_ATTEMPTS, err.c_str());
            ui_set_status(UI_STATUS_ERROR, err.c_str());
            /* 重试前让 LVGL 跑一会儿，时钟继续走 */
            for (int i = 0; i < 60; i++) {
                lv_timer_handler();
                delay(10);
            }
        }
    }

    if (ok) {
        ui_update_weather(&s_weather);

        struct tm t;
        char hhmm[8] = "";
        if (getLocalTime(&t, 0)) {
            snprintf(hhmm, sizeof(hhmm), "%02d:%02d", t.tm_hour, t.tm_min);
        }
        ui_set_updated(hhmm);
        ui_set_status(UI_STATUS_ONLINE, "已联网");
    } else {
        Serial.printf("[天气] 拉取失败: %s\n", err.c_str());
        ui_set_status(UI_STATUS_ERROR, err.c_str());
    }

    Serial.printf("[内存] 更新后  空闲堆 %u  最大连续堆 %u  PSRAM %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)ESP.getFreePsram());

    lv_timer_handler();
    return ok;
}

/* ------------------------------------------------------------------ */
/*  setup                                                              */
/* ------------------------------------------------------------------ */
void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("==========================================");
    Serial.println("  JC8048W550  联网时钟 + 天气");
    Serial.println("  ESP32-S3 + ST7262 800x480 + GT911");
    Serial.println("==========================================");

    /* ---------------------------------------------------------------
       先单独拦住「PSRAM 没开」这一种情况。

       为什么必须拦在这里：800x480 的 RGB 屏需要 750KB 帧缓冲，
       ESP32-S3 内部 RAM 只有 512KB，所以帧缓冲必须放 PSRAM。
       PSRAM 没开的话，Arduino_GFX 内部的 esp_lcd_new_rgb_panel() 会走
       ESP_ERROR_CHECK 直接 abort —— 它根本不会把失败返回给我们，
       用户看到的就是一串看不懂的 Backtrace + 无限重启。
       这里提前判断，把话说清楚，然后停住不重启（否则日志会被刷走）。
       --------------------------------------------------------------- */
    if (!psramFound() || ESP.getPsramSize() < (800 * 480 * 2)) {
        Serial.println();
        Serial.println("####################################################");
        Serial.println("#                                                  #");
        Serial.println("#   没有检测到可用的 PSRAM，屏幕无法工作           #");
        Serial.println("#                                                  #");
        Serial.println("#   800x480 的 RGB 屏需要 750KB 帧缓冲，           #");
        Serial.println("#   而芯片内部 RAM 只有 512KB，必须放到 PSRAM。    #");
        Serial.println("#                                                  #");
        Serial.println("#   解决办法（Arduino IDE）：                      #");
        Serial.println("#     工具 -> PSRAM      选 OPI PSRAM              #");
        Serial.println("#     工具 -> Flash Mode 选 QIO 80MHz              #");
        Serial.println("#   改完必须重新点「上传」，只按 RST 不生效。      #");
        Serial.println("#                                                  #");
        Serial.printf("#   当前 PSRAM 大小: %u 字节\n", (unsigned)ESP.getPsramSize());
        Serial.println("#                                                  #");
        Serial.println("####################################################");
        Serial.println();
        /* 停在这里，不重启：让上面这段话留在串口里不被刷掉 */
        while (true) {
            delay(5000);
            Serial.println("[等待] 请设置 PSRAM = OPI PSRAM 并重新上传");
        }
    }

    Serial.printf("PSRAM 总计 %u 字节，可用 %u 字节\n",
                  (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());

    /* 1. 屏幕 + LVGL */
    if (!display_init()) {
        Serial.println("屏幕初始化失败，5 秒后重启重试…");
        delay(5000);
        ESP.restart();
    }

#if LV_USE_LOG
    lv_log_register_print_cb(lv_log_cb);
#endif

    /* 2. 界面（先画出来，联网过程中也能看到画面） */
    ui_init();
    ui_set_city(CITY_NAME);
    ui_set_status(UI_STATUS_CONNECTING, "连接 WiFi");
    lv_timer_handler();

    /* 3. 触摸 */
    if (!touch_init()) {
        Serial.println("触摸初始化失败，界面仍会正常显示，但无法点击");
    }
    lv_timer_handler();

    /* 4. 联网（非阻塞，立刻返回） */
    net_begin();

    Serial.println("初始化完成，进入主循环");
}

/* ------------------------------------------------------------------ */
/*  loop                                                               */
/* ------------------------------------------------------------------ */
void loop()
{
    /* ---- 1. 让 LVGL 干活（刷新画面、处理触摸） ---- */
    display_tick();

    uint32_t now = millis();

    /* ---- 2. 网络状态变化 ---- */
    bool wifi_up = net_is_connected();
    if (wifi_up != s_wifi_was_up) {
        s_wifi_was_up = wifi_up;
        if (wifi_up) {
            Serial.printf("[网络] 已连接，IP %s，信号 %d dBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            ui_set_status(UI_STATUS_CONNECTING, "同步时间");
            time_begin_sync();
            s_weather_done = false;      /* 联网后立刻拉一次天气 */
        } else {
            Serial.println("[网络] 连接断开");
            ui_set_status(UI_STATUS_OFFLINE, "未联网");
        }
    }
    net_loop();

    /* ---- 3. 时间 ---- */
    bool time_ok = time_is_valid();
    if (time_ok && !s_time_was_ok) {
        s_time_was_ok = true;
        ui_set_status(UI_STATUS_CONNECTING, "已联网");
        /* 时间一到就先把时钟画出来，不用等天气 */
        struct tm t;
        if (getLocalTime(&t, 0)) ui_update_time(&t);
    }

    if (now - s_last_clock_ms >= CLOCK_TICK_MS) {
        s_last_clock_ms = now;
        struct tm t;
        if (getLocalTime(&t, 0) && time(nullptr) > 1577836800) {
            ui_update_time(&t);
        }
    }

    /* ---- 4. 天气 ---- */
    if (wifi_up && time_ok) {
        if (!s_weather_done) {
            s_weather_done   = true;
            s_weather_due_ms = now;          /* 刚联网，立刻拉一次 */
        }
        /* 用有符号差值比较，毫秒计数器回绕也不会出错 */
        if ((int32_t)(now - s_weather_due_ms) >= 0) {
            bool ok = do_weather_update();
            /* 成功就按正常间隔（默认 15 分钟）；
               失败就 1 分钟后重试 —— 不用干等一整个刷新周期。 */
            s_weather_due_ms = now + (ok ? WEATHER_INTERVAL_MS : WEATHER_RETRY_MS);
        }
    }

    /* ---- 5. 给系统留点时间（IDLE / WiFi 任务） ---- */
    delay(3);
}
