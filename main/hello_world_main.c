/*
 * ESP32-P4 Hello World —— UART 打印验证版
 *
 * 用途：先在微雪 ESP32-P4-ETH 开发板上把"打印"跑通，确认打印口。
 * 日志从 UART0 输出 —— 对应开发板的 Type-C 口（板载 CH343 USB 转串口），
 * 波特率 115200。4PIN USB 口是 OTG，不是打印口。
 *
 * 打印验证阶段默认不开屏幕；确认打印 OK 后，把 AKS8_ENABLE_LCD 改成 1，
 * 即可恢复 AKS8 的 1.75" AMOLED / 触摸演示。
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"

/* 在 ESP32-P4-ETH 开发板上验证打印时保持 0；接上 AKS8 屏后改为 1 */
#define AKS8_ENABLE_LCD 0

#if AKS8_ENABLE_LCD
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "st7801n.h"
#include "touch.h"
#include "lcd_gfx.h"
#endif

static const char *TAG = "aks8_hello";

#if AKS8_ENABLE_LCD
static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_lcd_io = NULL;

static void board_lcd_power_on_first(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << AKS8_PIN_LCD_POW_EN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(AKS8_PIN_LCD_POW_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(AKS8_PIN_LCD_POW_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "Early LCD power ON: GPIO%d = 0", AKS8_PIN_LCD_POW_EN);
}

static void display_demo(void)
{
    ESP_LOGI(TAG, "Draw color bars");
    gfx_draw_color_bars(s_panel, AKS8_LCD_WIDTH, AKS8_LCD_HEIGHT);
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "Draw Hello World text");
    gfx_fill_screen(s_panel, AKS8_LCD_WIDTH, AKS8_LCD_HEIGHT, GFX_BLACK);
    gfx_draw_string(s_panel, 60, 150, "Hello World!", GFX_CYAN, GFX_BLACK, 3);
    gfx_draw_string(s_panel, 78, 210, "ESP32-P4", GFX_YELLOW, GFX_BLACK, 2);
    gfx_draw_string(s_panel, 78, 250, "ST7801N 466x466", GFX_GREEN, GFX_BLACK, 2);
    gfx_draw_string(s_panel, 78, 300, "Touch the screen", GFX_WHITE, GFX_BLACK, 1);
}

static void touch_task(void *arg)
{
    aks8_touch_data_t tp;
    bool last_pressed = false;
    while (1) {
        if (aks8_touch_read(&tp) == ESP_OK && tp.pressed && !last_pressed) {
            ESP_LOGI(TAG, "Touch: points=%u  P0=(%u,%u)",
                     tp.num_points, tp.point[0].x, tp.point[0].y);
        }
        last_pressed = tp.pressed;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif /* AKS8_ENABLE_LCD */

/* 持续打印心跳：只要串口助手打开正确 COM 口，2 秒内一定能看到输出 */
static void heartbeat_task(void *arg)
{
    int n = 0;
    while (1) {
        ESP_LOGI(TAG, "heartbeat %d  |  UART0 console OK (Type-C / CH343, 115200)", n++);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    /* 开机立刻打印，不依赖任何外设初始化结果 */
    printf("\n================================================\n");
    printf("Hello world!\n");
    printf("AKS8 ESP32-P4 HelloWorld (UART print test)\n");
    printf("Console: UART0 -> Type-C (CH343) @ 115200\n");
    printf("================================================\n");

    /* ---------- 微雪 HelloWorld 原示例：打印芯片信息 ---------- */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s)\n", CONFIG_IDF_TARGET, chip_info.cores);

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed\n");
    } else {
        printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
               (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    }
    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    /* 启动心跳打印任务 */
    xTaskCreate(heartbeat_task, "heartbeat", 2048, NULL, 2, NULL);

#if AKS8_ENABLE_LCD
    /* ---------- AKS8 扩展：初始化 1.75" AMOLED ---------- */
    board_lcd_power_on_first();
    ESP_LOGI(TAG, "Initializing AKS8 1.75inch AMOLED (ST7801N MIPI-DSI)...");
    esp_err_t ret = aks8_st7801n_init(&s_panel, &s_lcd_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD init FAILED: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LCD init OK");
        display_demo();
        if (aks8_touch_init() == ESP_OK) {
            xTaskCreate(touch_task, "aks8_tp", 4096, NULL, 3, NULL);
        } else {
            ESP_LOGW(TAG, "Touch init skipped (no controller detected)");
        }
    }
#endif

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
