/**
 * ============================================================================
 *  JC8048W550 联网时钟 + 天气（带屏幕设置向导的版本）
 *
 *  和 ClockWeather 的区别：
 *    · WiFi 和地区都能在屏幕上设置（扫 WiFi / 输密码 / 选省份城市），
 *      存进 Flash，不用改 config.h 重新烧录
 *    · 平时屏幕上是「满屏大时钟」；点一下才切到「时间 + 天气」看板，
 *      看板停留 5 分钟（config.h 的 IDLE_RETURN_MIN）自动回到大时钟
 *
 *  硬件：ESP32-S3-WROOM-1 N16R8 + 5 寸 800x480 IPS（ST7262 RGB）+ GT911 电容触摸
 *  软件：Arduino + ESP-IDF LCD 驱动（v2.3 起不再用 Arduino_GFX）+ LVGL 8.4
 *        + Open-Meteo（免 API Key）
 *
 *  详细的接线 / 库安装 / 烧录参数见同目录 README.md。
 * ============================================================================
 */

#include "config.h"

#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "display.h"
#include "diag.h"
#include "net_time.h"
#include "settings.h"
#include "setup_ui.h"
#include "touch_gt911.h"
#include "ui.h"
#include "weather.h"

/* ------------------------------------------------------------------ */
/*  全局状态                                                           */
/* ------------------------------------------------------------------ */
static WeatherData s_weather;
static uint32_t    s_last_clock_ms    = 0;
static uint32_t    s_weather_due_ms   = 0;        /* 下次该拉天气的时刻 */
static bool        s_weather_done     = false;    /* 开机后是否已经拉过一次 */
static bool        s_wifi_was_up      = false;
static bool        s_time_was_ok      = false;
static uint32_t    s_settings_rev     = 0;        /* 用来看用户有没有改设置 */

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
/*  天气：放到后台任务里拉，绝不卡住界面                                */
/*                                                                     */
/*  为什么不能像以前那样「主循环里直接拉 + 重试几次」：                  */
/*    weather_fetch() 里的 DNS 和 HTTPS 全是阻塞调用。网络慢或者不通时  */
/*    一次能卡十几秒，连着重试三次就是大半分钟。这段时间主循环停转，     */
/*    LVGL 不刷新、触摸不响应 —— 现象就是：                             */
/*        大时钟停在某个时间不动，点屏幕毫无反应。                      */
/*                                                                     */
/*    ⚠ 这个坑在设置向导里看不出来：向导开着时本来就跳过天气，           */
/*      所以向导很跟手；一旦配置完切到大时钟，天气开始拉，界面就卡住。   */
/*                                                                     */
/*  现在改成：主循环只负责「启动任务」和「取结果」，LVGL 只在主循环里    */
/*  被调用；后台拉取期间界面照常刷新、照常响应触摸。                    */
/* ------------------------------------------------------------------ */
#define WEATHER_TASK_STACK   16384
#define WEATHER_TASK_PRIO    1
#define WEATHER_TASK_CORE    0        /* WiFi 协议栈在 core 0，就近跑 */
#define WEATHER_MAX_ATTEMPTS 3        /* 后台重试不卡界面，可以多试几次 */

static SemaphoreHandle_t s_wx_lock    = nullptr;
static WeatherData       s_wx_data;               /* 后台任务写、主循环读 */
static String            s_wx_err;
static volatile bool     s_wx_running = false;
static volatile bool     s_wx_done    = false;
static volatile bool     s_wx_ok      = false;

/** 后台任务：只做网络，绝不碰 LVGL。 */
static void weather_task(void *arg)
{
    (void)arg;

    WeatherData d;
    String err;
    bool ok = false;

    /* 失败就再试 —— 现在跑在后台任务里，重试期间界面照常刷新，
       不像以前那样会把主循环卡住。 */
    for (int attempt = 1; attempt <= WEATHER_MAX_ATTEMPTS && !ok; attempt++) {
        ok = weather_fetch(&d, &err);
        if (!ok) {
            Serial.printf("[天气] 第 %d/%d 次失败: %s\n",
                          attempt, WEATHER_MAX_ATTEMPTS, err.c_str());
            if (attempt < WEATHER_MAX_ATTEMPTS) vTaskDelay(pdMS_TO_TICKS(800));
        }
    }

    if (s_wx_lock != nullptr && xSemaphoreTake(s_wx_lock, portMAX_DELAY) == pdTRUE) {
        if (ok) s_wx_data = d;
        s_wx_err  = err;
        s_wx_ok   = ok;
        s_wx_done = true;              /* 数据写完了才置这个标志 */
        xSemaphoreGive(s_wx_lock);
    }

    /* 栈还剩多少：太小的话 TLS 握手会把这块栈写穿，
       表现就是跑一会儿随机卡死/重启。低于 2KB 就该调大 WEATHER_TASK_STACK。 */
    Serial.printf("[天气] 任务栈剩余 %u 字节\n",
                  (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));

    s_wx_running = false;
    vTaskDelete(nullptr);
}

/** 启动一次后台拉取。返回 false 表示任务没起来。 */
static bool weather_start_fetch()
{
    s_wx_done    = false;
    s_wx_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(weather_task, "weather",
                                             WEATHER_TASK_STACK, nullptr,
                                             WEATHER_TASK_PRIO, nullptr,
                                             WEATHER_TASK_CORE);
    if (ret != pdPASS) {
        s_wx_running = false;
        return false;
    }

    ui_set_status(UI_STATUS_CONNECTING, "获取天气");
    return true;
}

/** 后台拉完了就把结果搬到界面上（LVGL 只在这里被调用）。 */
static void weather_apply_result(uint32_t now)
{
    if (!s_wx_done) return;
    s_wx_done = false;

    bool   ok = false;
    String err;

    if (s_wx_lock != nullptr && xSemaphoreTake(s_wx_lock, portMAX_DELAY) == pdTRUE) {
        ok = s_wx_ok;
        err = s_wx_err;
        if (ok) s_weather = s_wx_data;
        xSemaphoreGive(s_wx_lock);
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
        Serial.printf("[天气] 拉取成功: %.1f°C %s\n",
                      s_weather.temp, weather_text(s_weather.code));
    } else {
        Serial.printf("[天气] 拉取失败: %s\n", err.c_str());
        ui_set_status(UI_STATUS_ERROR, err.c_str());
    }

    /* 成功就按正常间隔（默认 15 分钟）；失败就 1 分钟后重试 */
    s_weather_due_ms = now + (ok ? WEATHER_INTERVAL_MS : WEATHER_RETRY_MS);

    /* 「刷新天气」按钮的文字恢复 */
    ui_set_weather_refreshing(false);
}

/* ------------------------------------------------------------------ */
/*  setup                                                              */
/* ------------------------------------------------------------------ */
/*  复位原因：排查「花屏 + 重启」最关键的一行                          */
/*                                                                     */
/*  花屏重启有两种完全不同的原因，靠这一行区分：                        */
/*    PANIC / WDT  -> 程序崩了（多半是内存不够 / 栈溢出），看上面的     */
/*                    Guru Meditation backtrace                        */
/*    BROWNOUT     -> 供电不足（USB 线/口太弱，WiFi 发射瞬间掉压）      */
/* ------------------------------------------------------------------ */
static const char *reset_reason_text(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:   return "上电（正常开机）";
        case ESP_RST_EXT:       return "外部复位脚（按了 RST）";
        case ESP_RST_SW:        return "软件重启（ESP.restart）";
        case ESP_RST_PANIC:     return "★程序崩溃 panic（看上面 Guru Meditation 的 backtrace）";
        case ESP_RST_INT_WDT:   return "★中断看门狗（中断里卡太久）";
        case ESP_RST_TASK_WDT:  return "★任务看门狗（某个任务卡太久）";
        case ESP_RST_WDT:       return "★看门狗复位";
        case ESP_RST_DEEPSLEEP: return "深睡唤醒";
        case ESP_RST_BROWNOUT:  return "★★供电不足 brownout（换 USB 线/换口/换电源）";
        case ESP_RST_SDIO:      return "SDIO 复位";
        default:                return "未知";
    }
}

/* ------------------------------------------------------------------ */
/*  串口诊断命令（只在 DEBUG_SERIAL=1 时编译）                          */
/*                                                                     */
/*  v2.0 起全部搬到 diag.cpp 里，这里只留一句调用。为什么搬走：         */
/*  排「屏幕左边一条一条」要反复做对照实验，命令从 1 条涨到 7 条        */
/*  （bench / stats / pclk / bounce / restart / reboot / help），        */
/*  塞在主循环里既不好读，也不好改。                                     */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("==========================================");
    Serial.println("  JC8048W550  联网时钟 + 天气（设置向导版）");
    Serial.println("  ESP32-S3 + ST7262 800x480 + GT911");
    Serial.println("==========================================");

    Serial.printf("[启动] 固件 %s\n", FIRMWARE_TAG);
    Serial.printf("[启动] 复位原因: %s\n", reset_reason_text(esp_reset_reason()));
    Serial.printf("[启动] 空闲堆 %u  最大连续堆 %u  PSRAM %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)ESP.getFreePsram());

    /* ---------------------------------------------------------------
     *  先单独拦住「PSRAM 没开」这一种情况。
     *
     *  为什么必须拦在这里：800x480 的 RGB 屏需要 750KB 帧缓冲，
     *  ESP32-S3 内部 RAM 只有 512KB，所以帧缓冲必须放 PSRAM。
     *  PSRAM 没开的话，建面板/分配帧缓冲会失败（v2.3 起 display_init()
     *  会把 esp_err_t 打出来，但用户多半看不懂），随后就是无限重启。
     *  这里提前判断，把话说清楚，然后停住不重启（否则日志会被刷走）。
     * --------------------------------------------------------------- */
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
        while (true) {
            delay(5000);
            Serial.println("[等待] 请设置 PSRAM = OPI PSRAM 并重新上传");
        }
    }

    Serial.printf("PSRAM 总计 %u 字节，可用 %u 字节\n",
                  (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());

    /* 0. 天气后台任务用的互斥量（保护任务和主循环之间的数据传递） */
    s_wx_lock = xSemaphoreCreateMutex();
    if (s_wx_lock == nullptr) {
        Serial.println("[天气] 互斥量创建失败，本机将不更新天气");
    }

    /* 1. 屏幕 + LVGL
         搬运缓冲行数先问 NVS（串口 bounce 命令写进去的），没有就用
         board_pins.h 的 LCD_BOUNCE_LINES。必须在建面板之前定下来。 */
    if (!display_init(settings_bounce_lines())) {
        Serial.println("屏幕初始化失败，5 秒后重启重试…");
        delay(5000);
        ESP.restart();
    }

#if LV_USE_LOG
    lv_log_register_print_cb(lv_log_cb);
#endif

    /* 2. 读设置（WiFi / 地区），必须在 ui_init() 之前 —— 界面要用城市名 */
    settings_load();

    /* 3. 界面（先画出来，联网过程中也能看到画面） */
    ui_init();
    ui_set_city(settings_city_name());

    /* 4. 触摸 */
    if (!touch_init()) {
        Serial.println("触摸初始化失败，界面仍会正常显示，但无法点击");
    }
    lv_timer_handler();

    /* 5. 决定开机进哪儿：
          没配 WiFi   -> 设置向导（WiFi 列表）
          没配地区    -> 设置向导（地区），同时后台先连 WiFi
          都配好了    -> 大时钟，后台连 WiFi 拉天气            */
    if (!settings_has_wifi()) {
        ui_set_status(UI_STATUS_OFFLINE, "请先设置 WiFi");
        lv_timer_handler();
        setup_ui_start_wifi(true);
    } else if (!settings_has_region()) {
        ui_set_status(UI_STATUS_CONNECTING, "连接 WiFi");
        net_begin();
        lv_timer_handler();
        setup_ui_start_region(true);
    } else {
        ui_show_idle();
        ui_set_status(UI_STATUS_CONNECTING, "连接 WiFi");
        net_begin();
    }

    Serial.println("初始化完成，进入主循环");

#if DEBUG_SERIAL
    diag_print_help();
#endif
}

/* ------------------------------------------------------------------ */
/*  loop                                                               */
/* ------------------------------------------------------------------ */
void loop()
{
    /* ---- 1. 让 LVGL 干活（刷新画面、处理触摸、向导计时器） ---- */
    display_tick();

    uint32_t now = millis();

    /* ---- 2. 用户在设置向导里改了东西吗？ ---- */
    if (settings_revision() != s_settings_rev) {
        s_settings_rev = settings_revision();
        ui_set_city(settings_city_name());
        s_weather_done = false;      /* 地区/WiFi 变了，天气要重拉 */
    }

    /* ---- 3. 网络状态变化 ---- */
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

    /* ---- 4. 时间 ---- */
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

    /* ---- 5. 天气：启动后台拉取 / 收取结果，全程不阻塞界面 ----
              （要联网 + 时间 + 地区都齐了才拉；向导开着时也照常拉，
                因为现在拉取在后台，不会顶住向导界面） */
    /* 大时钟页那个「刷新天气」按钮 */
    if (ui_take_weather_refresh_request()) {
        if (!wifi_up)                    ui_set_status(UI_STATUS_OFFLINE, "未联网，刷新不了");
        else if (!time_ok)               ui_set_status(UI_STATUS_CONNECTING, "正在同步时间");
        else if (!settings_has_region()) ui_set_status(UI_STATUS_ERROR, "还没设置地区");
        else if (s_wx_running)           ui_set_status(UI_STATUS_CONNECTING, "正在刷新…");
        else                             s_weather_due_ms = now;   /* 立刻到期 */
    }

    /* 扫描 WiFi 时内存最紧 —— 实测空闲堆从 50KB 掉到 32KB、
       最大连续块只剩 8KB，这时候去拉天气基本注定失败（TLS 更是握不上）。
       所以扫描期间先不拉，等扫完再拉。 */
    bool scanning = net_scan_busy();

    if (wifi_up && time_ok && settings_has_region() && s_wx_lock != nullptr && !scanning) {
        if (!s_weather_done) {
            s_weather_done   = true;
            s_weather_due_ms = now;          /* 条件一满足，立刻拉一次 */
        }

        /* 用有符号差值比较，毫秒计数器回绕也不会出错 */
        bool due = ((int32_t)(now - s_weather_due_ms) >= 0);

        if (due && !s_wx_running && !s_wx_done) {
            if (weather_start_fetch()) {
                ui_set_weather_refreshing(true);
            } else {
                Serial.println("[天气] 后台任务创建失败，1 分钟后再试");
                s_weather_due_ms = now + WEATHER_RETRY_MS;
            }
        }
    }

    /* 后台拉完了就把结果搬到界面（只有这里碰 LVGL） */
    weather_apply_result(now);

    /* ---- 6. 看板停留超时就回大时钟 ---- */
    ui_poll();

#if DEBUG_SERIAL
    /* ---- 6.5 串口诊断命令 ----
       串口监视器 115200 敲 help 看全部命令（bench / stats / pclk / bounce /
       restart / reboot）。命令实现在 diag.cpp，不写 Flash（bounce 除外，
       它本来就要存 NVS 才能开机生效）。 */
    diag_poll();
#endif

#if DEBUG_SERIAL
    /* ---- 7. 心跳日志 ----
       排查「卡死」最有用的一条：它每 10 秒打一次，一旦界面卡住，日志会
       停在哪一行、当时的内存是多少，一眼就能看出来。发布时可以关掉。 */
    {
        static uint32_t s_last_beat_ms = 0;
        if ((uint32_t)(now - s_last_beat_ms) >= 10000) {
            s_last_beat_ms = now;
            lv_mem_monitor_t m;
            lv_mem_monitor(&m);
            Serial.printf("[心跳] 空闲堆 %u  最大连续堆 %u  主循环栈剩 %u  LVGL 已用 %u/%u  最大空闲块 %u  天气:%s\n",
                          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                          (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)),
                          (unsigned)(m.total_size - m.free_size), (unsigned)m.total_size,
                          (unsigned)m.free_biggest_size,
                          s_wx_running ? "拉取中" : (s_wx_done ? "待应用" : "空闲"));
        }
    }
#endif

    /* ---- 8. 给系统留点时间（IDLE / WiFi 任务） ---- */
    delay(3);
}
