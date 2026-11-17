/*
 * AKS8 ESP32-P4 Hello World 示例
 *
 * 基于微雪 ESP32-P4-Platform 的 02_HelloWorld 示例扩展：
 *   1) 保留串口打印芯片信息 / Flash / Heap / 倒计时重启
 *   2) 新增 1.75" AMOLED (ST7801N, MIPI-DSI 466x466) 初始化
 *   3) 屏幕上显示 Hello World + RGB 彩条
 *   4) 电容触摸：点击屏幕时在串口打印坐标
 *
 * 硬件: AKS8 (ESP32-P4, 16MB Flash, PSRAM)
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"

#include "st7801n.h"
#include "touch.h"
#include "lcd_gfx.h"

static const char *TAG = "aks8_hello";

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_lcd_io = NULL;

/* --------------------------------------------------------------------------
 * 显示演示：先彩条，再 Hello World 文本
 * -------------------------------------------------------------------------- */
static void display_demo(void)
{
    /* 1) RGB 彩条，验证屏幕点亮、方向与色彩 */
    ESP_LOGI(TAG, "Draw color bars");
    gfx_draw_color_bars(s_panel, AKS8_LCD_WIDTH, AKS8_LCD_HEIGHT);
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* 2) Hello World 文本页（居中显示几行） */
    ESP_LOGI(TAG, "Draw Hello World text");
    gfx_fill_screen(s_panel, AKS8_LCD_WIDTH, AKS8_LCD_HEIGHT, GFX_BLACK);

    gfx_draw_string(s_panel, 60, 150, "Hello World!", GFX_CYAN, GFX_BLACK, 3);
    gfx_draw_string(s_panel, 78, 210, "ESP32-P4", GFX_YELLOW, GFX_BLACK, 2);
    gfx_draw_string(s_panel, 78, 250, "ST7801N 466x466", GFX_GREEN, GFX_BLACK, 2);
    gfx_draw_string(s_panel, 78, 300, "Touch the screen", GFX_WHITE, GFX_BLACK, 1);
}

/* --------------------------------------------------------------------------
 * 触摸任务：周期性读取触摸坐标并打印
 * -------------------------------------------------------------------------- */
static void touch_task(void *arg)
{
    aks8_touch_data_t tp;
    bool last_pressed = false;

    while (1) {
        if (aks8_touch_read(&tp) == ESP_OK && tp.pressed && !last_pressed) {
            /* 仅在按下瞬间打印一次，避免刷屏 */
            ESP_LOGI(TAG, "Touch: points=%u  P0=(%u,%u)",
                     tp.num_points, tp.point[0].x, tp.point[0].y);

            /* 触摸时在屏幕上画一个小方块作为视觉反馈 */
            int x = tp.point[0].x;
            int y = tp.point[0].y;
            int rx = x - 6;
            int ry = y - 6;
            if (rx < 0) rx = 0;
            if (ry < 0) ry = 0;
            if (x > 0 && y > 0) {
                gfx_fill_rect(s_panel, rx, ry, 12, 12, GFX_RED);
            }
        }
        last_pressed = tp.pressed;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    printf("Hello world!\n");

    /* ---------- 以下为微雪 HelloWorld 原示例内容：打印芯片信息 ---------- */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }
    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    /* ---------- AKS8 扩展：初始化 1.75" AMOLED ---------- */
    ESP_LOGI(TAG, "Initializing AKS8 1.75inch AMOLED (ST7801N MIPI-DSI)...");
    esp_err_t ret = aks8_st7801n_init(&s_panel, &s_lcd_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD init FAILED: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LCD init OK");
        display_demo();

        /* 初始化触摸并启动触摸任务（触摸不可用不影响主流程） */
        if (aks8_touch_init() == ESP_OK) {
            xTaskCreate(touch_task, "aks8_tp", 4096, NULL, 3, NULL);
        } else {
            ESP_LOGW(TAG, "Touch init skipped (no controller detected)");
        }
    }

    /* ---------- 微雪 HelloWorld 原示例：倒计时重启 ----------
     * 注意：本示例为便于观察屏幕效果，默认【不自动重启】。
     * 如需恢复微雪原行为，把下面 AKS8_AUTO_RESTART 定义为 1。 */
#define AKS8_AUTO_RESTART 0
#if AKS8_AUTO_RESTART
    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
#else
    ESP_LOGI(TAG, "Demo running. (Auto-restart disabled; set AKS8_AUTO_RESTART=1 to enable)")
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}
