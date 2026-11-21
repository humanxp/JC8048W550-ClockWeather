#include "touch_gt911.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_pins.h"
#include "config.h"

/* ---------------- GT911 寄存器 ---------------- */
#define GT911_REG_CONFIG_VERSION   0x8047
#define GT911_REG_CONFIG_XMAX      0x8048   /* 2 字节，小端 */
#define GT911_REG_CONFIG_YMAX      0x804A   /* 2 字节，小端 */
#define GT911_REG_PRODUCT_ID       0x8140   /* 4 字节 ASCII */
#define GT911_REG_STATUS           0x814E
/* ⚠ 触摸点记录的起始地址是 0x814F，不是 0x8150！
 *
 * 一个触摸点占 8 字节，从 0x814F 开始：
 *      0x814F        track id
 *      0x8150~0x8151 X 坐标（小端）
 *      0x8152~0x8153 Y 坐标（小端）
 *      0x8154~0x8155 触摸面积
 *      0x8156        保留
 * 下面解析时用 p[1..2] 当 X、p[3..4] 当 Y，所以基准必须取 0x814F。
 *
 * 曾经这里写成 0x8150，于是 X 读成了「X 高字节 + Y 低字节」、
 * Y 读成了「Y 高字节 + 面积低字节」，算出来都是几千的数，
 * 再乘屏幕尺寸就被 clamp 到右下角 —— 现象就是：
 * 触摸能按下（次数在涨），但坐标永远是 799,479，点哪儿都没反应。
 *
 * 交叉核对（两个都从 0x814E 的 status 之后、即 0x814F 开始读）：
 *   esp-bsp  esp_lcd_touch_gt911.c : 读 0x814E+1，x = buf[3]<<8|buf[2]
 *   LovyanGFX Touch_GT911.cpp      : x = data[1]（data 指向 0x814F）
 */
#define GT911_REG_POINT1           0x814F   /* 每个触摸点 8 字节（含 track id） */

/* 连续 1.5 秒没有新数据就认为手指已抬起 */
#define TOUCH_STALE_MS             1500

static uint8_t  s_addr        = 0;      /* 0 = 未探测到 */
static uint16_t s_cfg_max_x   = LCD_WIDTH;
static uint16_t s_cfg_max_y   = LCD_HEIGHT;
static bool     s_swap_xy     = false;  /* 触摸芯片按竖屏配置时需要交换 */
static int32_t  s_last_x      = 0;
static int32_t  s_last_y      = 0;
static bool     s_last_touch  = false;
static uint32_t s_last_seen_ms = 0;

/* 诊断计数（见 touch_gt911.h 的 TouchDiag） */
static uint32_t s_press_count = 0;
static uint32_t s_read_fail   = 0;

/* ------------------------------------------------------------------ */
/*  底层 I2C 读写                                                      */
/* ------------------------------------------------------------------ */
static bool gt911_read(uint16_t reg, uint8_t *buf, uint16_t len)
{
    if (s_addr == 0) return false;

    Wire.beginTransmission(s_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;

    uint16_t got = Wire.requestFrom((uint8_t)s_addr, (uint8_t)len);
    if (got != len) {
        while (Wire.available()) Wire.read();   /* 清空残包 */
        return false;
    }
    for (uint16_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
    return true;
}

static bool gt911_write8(uint16_t reg, uint8_t value)
{
    if (s_addr == 0) return false;

    Wire.beginTransmission(s_addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool probe_address(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

/* ------------------------------------------------------------------ */
/*  坐标映射                                                           */
/* ------------------------------------------------------------------ */
static int32_t scale_coord(uint16_t raw, uint16_t raw_max, int32_t screen_max)
{
    if (raw_max == 0) return (int32_t)raw;
    int32_t v = (int32_t)((uint32_t)raw * (uint32_t)screen_max / (uint32_t)raw_max);
    if (v < 0) v = 0;
    if (v > screen_max) v = screen_max;
    return v;
}

/* ------------------------------------------------------------------ */
/*  初始化                                                             */
/* ------------------------------------------------------------------ */
bool touch_init()
{
    /* --- 1. 复位时序，同时锁存 I2C 地址 --- */
    pinMode(PIN_TOUCH_INT, OUTPUT);
    pinMode(PIN_TOUCH_RST, OUTPUT);

    digitalWrite(PIN_TOUCH_INT, LOW);   /* INT 拉低 -> 地址 0x5D */
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(12);

    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(60);                          /* 等 GT911 完成初始化并锁存地址 */

    pinMode(PIN_TOUCH_INT, INPUT);      /* 释放 INT，之后当普通输入用 */
    delay(60);

    /* --- 2. 启动 I2C 并探测地址 --- */
    Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, TOUCH_I2C_FREQ);
    Wire.setTimeOut(50);

    if (probe_address(0x5D)) {
        s_addr = 0x5D;
    } else if (probe_address(0x14)) {
        s_addr = 0x14;
    } else {
        s_addr = 0;
        Serial.println("[触摸] 未找到 GT911（0x5D / 0x14 都没有应答）");
        return false;
    }

    /* --- 3. 读产品 ID 和配置分辨率 --- */
    uint8_t id[4] = {0};
    if (gt911_read(GT911_REG_PRODUCT_ID, id, 4)) {
        Serial.printf("[触摸] GT911 地址 0x%02X，产品 ID: %c%c%c%c\n",
                      s_addr, id[0], id[1], id[2], id[3]);
    } else {
        Serial.printf("[触摸] GT911 地址 0x%02X（产品 ID 读取失败，继续尝试）\n", s_addr);
    }

    uint8_t cfg[4] = {0};
    if (gt911_read(GT911_REG_CONFIG_XMAX, cfg, 4)) {
        s_cfg_max_x = (uint16_t)(cfg[0] | (cfg[1] << 8));
        s_cfg_max_y = (uint16_t)(cfg[2] | (cfg[3] << 8));
    }
    /* 配置异常时按屏幕分辨率处理，避免除零或全屏偏移 */
    if (s_cfg_max_x < 100 || s_cfg_max_x > 4096) s_cfg_max_x = LCD_WIDTH;
    if (s_cfg_max_y < 100 || s_cfg_max_y > 4096) s_cfg_max_y = LCD_HEIGHT;
    Serial.printf("[触摸] 芯片配置分辨率 %ux%u\n", s_cfg_max_x, s_cfg_max_y);

    /* 如果芯片内部按竖屏 480x800 配置，就需要交换 XY */
    s_swap_xy = (s_cfg_max_x == LCD_HEIGHT && s_cfg_max_y == LCD_WIDTH);
#if TOUCH_SWAP_XY
    s_swap_xy = !s_swap_xy;
#endif

    /* 清一次状态，丢弃复位期间的脏数据 */
    gt911_write8(GT911_REG_STATUS, 0);

    s_press_count = 0;
    s_read_fail   = 0;
    return true;
}

/* ------------------------------------------------------------------ */
/*  读取触摸                                                           */
/* ------------------------------------------------------------------ */
bool touch_read(int32_t *x, int32_t *y)
{
    if (s_addr == 0) return false;

    uint8_t status = 0;
    if (!gt911_read(GT911_REG_STATUS, &status, 1)) {
        s_read_fail++;
        /* I2C 读失败：维持上一状态，但超时后强制松开，避免卡住 */
        if (s_last_touch && (millis() - s_last_seen_ms) > TOUCH_STALE_MS) {
            s_last_touch = false;
        }
        if (s_last_touch) { *x = s_last_x; *y = s_last_y; }
        return s_last_touch;
    }

    bool have_new = (status & 0x80) != 0;
    uint8_t points = status & 0x0F;

    if (have_new) {
        if (points > 0) {
            uint8_t p[8] = {0};
            if (gt911_read(GT911_REG_POINT1, p, 8)) {
                uint16_t raw_x = (uint16_t)(p[1] | (p[2] << 8));
                uint16_t raw_y = (uint16_t)(p[3] | (p[4] << 8));

                if (s_swap_xy) {
                    uint16_t t = raw_x; raw_x = raw_y; raw_y = t;
                }

                int32_t sx = scale_coord(raw_x, s_cfg_max_x, LCD_WIDTH  - 1);
                int32_t sy = scale_coord(raw_y, s_cfg_max_y, LCD_HEIGHT - 1);

#if TOUCH_MIRROR_X
                sx = (LCD_WIDTH  - 1) - sx;
#endif
#if TOUCH_MIRROR_Y
                sy = (LCD_HEIGHT - 1) - sy;
#endif

                s_last_x = sx;
                s_last_y = sy;
                if (!s_last_touch) {
                    s_press_count++;      /* 新的一次按下（不是按住时的重复采样） */
#if DEBUG_SERIAL
                    Serial.printf("[触摸] 按下 (%d, %d)\n", (int)sx, (int)sy);
#endif
                }
                s_last_touch  = true;
                s_last_seen_ms = millis();
            }
        } else {
            /* 点数为 0：手指全部抬起 */
#if DEBUG_SERIAL
            if (s_last_touch) {
                Serial.printf("[触摸] 抬起 (%d, %d)\n", (int)s_last_x, (int)s_last_y);
            }
#endif
            s_last_touch = false;
        }
        /* 必须写 0 清标志，否则芯片不再上报 */
        gt911_write8(GT911_REG_STATUS, 0);
    } else if (s_last_touch && (millis() - s_last_seen_ms) > TOUCH_STALE_MS) {
#if DEBUG_SERIAL
        Serial.println("[触摸] 抬起（1.5 秒没有新数据，超时判定）");
#endif
        s_last_touch = false;
    }

    if (s_last_touch) {
        *x = s_last_x;
        *y = s_last_y;
    }
    return s_last_touch;
}

uint8_t touch_addr()
{
    return s_addr;
}

void touch_diag(TouchDiag *out)
{
    if (out == nullptr) return;
    out->addr        = s_addr;
    out->pressed     = s_last_touch;
    out->x           = s_last_x;
    out->y           = s_last_y;
    out->press_count = s_press_count;
    out->read_fail   = s_read_fail;
    out->raw_max_x   = s_cfg_max_x;
    out->raw_max_y   = s_cfg_max_y;
    out->swap_xy     = s_swap_xy;
}
