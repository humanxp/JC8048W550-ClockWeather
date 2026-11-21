/**
 * ============================================================================
 *  ScreenTest —— 屏幕隔离测试（排查白屏/黑屏/花屏专用）
 *
 *  这个程序不依赖 LVGL，也不依赖主工程的任何文件，
 *  只做三件事：开背光 -> 初始化 RGB 面板 -> 循环填充纯色。
 *
 *  用法：
 *    1. Arduino IDE 新建工程，把本文件内容全部粘进去
 *    2. 另存为 ScreenTest（文件夹名要一致）
 *    3. 开发板设置和主程序完全一样：
 *         ESP32S3 Dev Module / OPI PSRAM / 16MB /
 *         16M Flash (3MB APP/9.9MB FATFS) / USB CDC On Boot = Disabled
 *    4. 上传，看屏幕能不能红绿蓝黄白黑循环变色
 *
 *  ★ 如果不变色，就改下面两个开关做二分排查 ★
 * ============================================================================
 */

#include <Arduino_GFX_Library.h>

#define PIN_LCD_BL   2      /* 背光 */

/* ---------------------------------------------------------------------------
 *  测试开关 1：DMA 搬运缓冲（像素数）
 *      0            = Arduino_GFX 默认，不启用（先试这个）
 *      800 * 20     = 20 行，约 32KB 内部 RAM
 *      800 * 40     = 40 行，约 64KB
 * ------------------------------------------------------------------------ */
#define TEST_BOUNCE_PX      0

/* ---------------------------------------------------------------------------
 *  测试开关 2：像素时钟（Hz）
 *      12000000 = Arduino_GFX 对 OPI PSRAM 的默认值
 *      16000000 = 网上常见的激进值
 *       9000000 = 保守值
 * ------------------------------------------------------------------------ */
#define TEST_PCLK_HZ        12000000

/* ---------------------------------------------------------------------------
 *  测试开关 3：RGB 数据位序
 *      false = 常规（默认）
 *      true  = 大端，颜色明显不对时试
 * ------------------------------------------------------------------------ */
#define TEST_BIG_ENDIAN     false


Arduino_ESP32RGBPanel *panel = new Arduino_ESP32RGBPanel(
    40, 41, 39, 42,                    /* DE, VSYNC, HSYNC, PCLK */
    45, 48, 47, 21, 14,                /* R0-R4 */
    5, 6, 7, 15, 16, 4,                /* G0-G5 */
    8, 3, 46, 9, 1,                    /* B0-B4 */
    0, 8, 4, 8,                        /* hsync: 极性,前肩,脉宽,后肩 */
    0, 8, 4, 8,                        /* vsync: 极性,前肩,脉宽,后肩 */
    1, TEST_PCLK_HZ, TEST_BIG_ENDIAN,
    0, 0, TEST_BOUNCE_PX);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, panel, 0, true);


void setup()
{
    Serial.begin(115200);
    delay(800);

    Serial.println();
    Serial.println("========== 屏幕隔离测试 ==========");
    Serial.printf("PSRAM        : %u 字节 %s\n",
                  (unsigned)ESP.getPsramSize(),
                  psramFound() ? "" : "  <-- 没检测到！必须设成 OPI PSRAM");
    Serial.printf("PCLK         : %d Hz\n", TEST_PCLK_HZ);
    Serial.printf("DMA 搬运缓冲 : %d 像素 (%d 字节)\n", TEST_BOUNCE_PX, TEST_BOUNCE_PX * 2);
    Serial.printf("大端模式     : %s\n", TEST_BIG_ENDIAN ? "是" : "否");
    Serial.println("----------------------------------");

    /* 先开背光，这样即使后面失败也能区分「背光问题」和「数据问题」 */
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);
    Serial.println("[1] 背光 GPIO2 = HIGH（屏幕此刻应该微微发亮/发白）");

    if (!gfx->begin()) {
        Serial.println("[2] gfx->begin() 失败！");
        return;
    }
    Serial.println("[2] gfx->begin() 成功");

    uint16_t *fb = gfx->getFramebuffer();
    Serial.printf("[3] 帧缓冲地址 : %p %s\n", (void *)fb, fb ? "" : " <-- 为空！");
    if (!fb) return;

    Serial.println("[4] 开始循环填色");
    Serial.println("==================================");
}

void loop()
{
    static const uint16_t cols[] = {
        RGB565_RED, RGB565_GREEN, RGB565_BLUE,
        RGB565_WHITE, RGB565_BLACK, RGB565_YELLOW
    };
    static const char *names[] = {
        "红", "绿", "蓝", "白", "黑", "黄"
    };
    static uint8_t i = 0;

    uint8_t k = i % 6;
    gfx->fillScreen(cols[k]);
    Serial.printf("填充 %s (0x%04X)\n", names[k], cols[k]);
    i++;
    delay(1200);
}
