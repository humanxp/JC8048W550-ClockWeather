/**
 * ============================================================================
 *  weather.h / weather.cpp  —  Open-Meteo 天气数据
 *
 *  Open-Meteo 免 API Key、免注册、免费商用，非常适合个人项目：
 *      https://open-meteo.com/
 *
 *  天气代码使用 WMO 标准（0=晴，3=阴，61=小雨，95=雷阵雨 …）。
 * ============================================================================
 */
#pragma once

#include <Arduino.h>
#include <stdbool.h>

#define WEATHER_FORECAST_DAYS   3   /* 今天 + 明天 + 后天 */

/** 天气图标类型（由天气代码 + 昼夜推导出来） */
enum WeatherIconType {
    WICON_SUN = 0,      /* 晴（白天） */
    WICON_MOON,         /* 晴（夜间） */
    WICON_PARTLY,       /* 多云（白天） */
    WICON_PARTLY_NIGHT, /* 多云（夜间） */
    WICON_CLOUD,        /* 阴 */
    WICON_FOG,          /* 雾/霾 */
    WICON_DRIZZLE,      /* 毛毛雨 */
    WICON_RAIN,         /* 雨 */
    WICON_SLEET,        /* 雨夹雪/冻雨 */
    WICON_SNOW,         /* 雪 */
    WICON_THUNDER,      /* 雷阵雨 */
    WICON_UNKNOWN
};

struct WeatherDay {
    int   code;
    float tmax;
    float tmin;
    bool  valid;
};

struct WeatherData {
    bool  valid;            /* 是否拿到过有效数据 */
    float temp;             /* 气温 °C */
    float apparent;         /* 体感温度 °C */
    float humidity;         /* 相对湿度 % */
    float wind;             /* 风速 m/s */
    float pressure;         /* 地面气压 hPa */
    int   code;             /* 当前天气代码 */
    bool  is_day;           /* 是否白天 */
    WeatherDay days[WEATHER_FORECAST_DAYS];
    uint32_t fetched_ms;    /* 本次拉取时刻（millis） */
};

/** 拉取一次天气。失败时 err 里会带上原因。阻塞约 1~3 秒。 */
bool weather_fetch(WeatherData *out, String *err);

/** 天气代码 -> 中文描述 */
const char *weather_text(int code);

/** 天气代码 + 昼夜 -> 图标类型 */
WeatherIconType weather_icon_type(int code, bool is_day);
