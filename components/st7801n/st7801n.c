/*
 * ST7801N 1.75" AMOLED MIPI-DSI 显示屏驱动（AKS8 硬件）
 *
 * 初始化命令严格对应硬件资料中的「簡代碼_251126」：
 *   mipi.write 0x05 0x11        -> DCS 0x11 (SLPOUT), 无参数
 *   delay 100
 *   mipi.write 0x29 0x53 0x20   -> DCS 0x53 (Write CTRL Display), 参数 0x20
 *   mipi.write 0x29 0x51 0xFF   -> DCS 0x51 (Write Brightness),    参数 0xFF
 *   mipi.write 0x29 0x3A 0x55   -> DCS 0x3A (COLMOD RGB565),       参数 0x55
 *   mipi.write 0x29 0x35 0x00   -> DCS 0x35 (TE ON),               参数 0x00
 *   mipi.write 0x05 0x29        -> DCS 0x29 (DISPON), 无参数
 * （简码中的 0x05/0x29 是 MIPI 数据包类型：短写无参 / 带参写入）
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"

#include "st7801n.h"

static const char *TAG = "st7801n";

/* ST7801N DCS 命令定义 */
#define ST7801N_CMD_NOP        0x00
#define ST7801N_CMD_SWRESET    0x01
#define ST7801N_CMD_SLPIN      0x10
#define ST7801N_CMD_SLPOUT     0x11
#define ST7801N_CMD_DISPOFF    0x28
#define ST7801N_CMD_DISPON     0x29
#define ST7801N_CMD_CASET      0x2A
#define ST7801N_CMD_RASET      0x2B
#define ST7801N_CMD_RAMWR      0x2C
#define ST7801N_CMD_TEON       0x35
#define ST7801N_CMD_COLMOD     0x3A
#define ST7801N_CMD_WDBV       0x51   /* Write Display Brightness */
#define ST7801N_CMD_WCTRLD     0x53   /* Write CTRL Display */
#define ST7801N_CMD_RDDID1     0xDA
#define ST7801N_CMD_RDDID2     0xDB
#define ST7801N_CMD_RDDID3     0xDC

/* 缓存 panel IO 句柄，供亮度/ID 等接口复用 */
static esp_lcd_panel_io_handle_t s_io = NULL;

/* --------------------------------------------------------------------------
 * 板级电源与复位
 * -------------------------------------------------------------------------- */

/* MIPI DPHY 供电：原理图 VDD_MIPI_DPHY <- ESP32-P4 内部 LDO_VO3 (2.5V) */
static void board_enable_dsi_phy_power(void)
{
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t cfg = {
        .chan_id = AKS8_MIPI_PHY_LDO_CHAN,
        .voltage_mv = AKS8_MIPI_PHY_LDO_VOLTAGE,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&cfg, &ldo));
    ESP_LOGI(TAG, "MIPI DPHY powered by LDO_CH%d @ %dmV",
             AKS8_MIPI_PHY_LDO_CHAN, AKS8_MIPI_PHY_LDO_VOLTAGE);
}

/* LCD 电源使能：GPIO49 通过 AO3401 (P-MOS) 控制 LCD_3V3，低电平导通 */
static void board_lcd_power_on(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << AKS8_PIN_LCD_POW_EN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(AKS8_PIN_LCD_POW_EN, AKS8_LCD_POW_EN_ACTIVE);
    ESP_LOGI(TAG, "LCD_3V3 power enabled (GPIO%d = %d)",
             AKS8_PIN_LCD_POW_EN, AKS8_LCD_POW_EN_ACTIVE);
    vTaskDelay(pdMS_TO_TICKS(20));
}

/* LCD 硬件复位：GPIO23，低有效 */
static void board_lcd_reset(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << AKS8_PIN_LCD_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_set_level(AKS8_PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(AKS8_PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(AKS8_PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120)); /* 等待屏内 OTP 加载完成 */
    ESP_LOGI(TAG, "LCD hardware reset done (GPIO%d)", AKS8_PIN_LCD_RST);
}

/* TE 引脚：原理图 LCD_TE -> GPIO17；DPI video mode 由主机连续刷新，
 * 此处配置为输入仅作预留/观测，不参与显示数据流。 */
static void board_te_pin_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << AKS8_PIN_LCD_TE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

/* --------------------------------------------------------------------------
 * DCS 命令发送
 * -------------------------------------------------------------------------- */

esp_err_t aks8_st7801n_set_brightness(esp_lcd_panel_io_handle_t io, uint8_t brightness)
{
    if (io == NULL) {
        io = s_io;
    }
    if (io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t param = brightness;
    return esp_lcd_panel_io_tx_param(io, ST7801N_CMD_WDBV, &param, 1);
}

esp_err_t aks8_st7801n_read_id(esp_lcd_panel_io_handle_t io)
{
    if (io == NULL) {
        io = s_io;
    }
    if (io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t id1 = 0, id2 = 0, id3 = 0;
    esp_err_t ret;
    ret = esp_lcd_panel_io_rx_param(io, ST7801N_CMD_RDDID1, &id1, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_panel_io_rx_param(io, ST7801N_CMD_RDDID2, &id2, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_panel_io_rx_param(io, ST7801N_CMD_RDDID3, &id3, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "ST7801N ID: RDDID1=0x%02X RDDID2=0x%02X RDDID3=0x%02X", id1, id2, id3);
    return ESP_OK;
}

/* 发送屏初始化 DCS 序列（不含 DISPON，DISPON 在 DPI panel 就绪后发） */
static void st7801n_send_init_commands(esp_lcd_panel_io_handle_t io)
{
    /* 0x11 Sleep Out */
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, ST7801N_CMD_SLPOUT, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(120)); /* 简码 delay 100，留足余量 */

    /* 0x53 Write CTRL Display = 0x20 */
    uint8_t ctrl = 0x20;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, ST7801N_CMD_WCTRLD, &ctrl, 1));

    /* 0x51 Brightness = 0xFF */
    uint8_t brightness = 0xFF;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, ST7801N_CMD_WDBV, &brightness, 1));

    /* 0x3A COLMOD = 0x55 (RGB565 / 16bit) */
    uint8_t colmod = 0x55;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, ST7801N_CMD_COLMOD, &colmod, 1));

    /* 0x35 Tearing Effect ON = 0x00 */
    uint8_t te = 0x00;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, ST7801N_CMD_TEON, &te, 1));

    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "ST7801N DCS init sequence sent");
}

/* --------------------------------------------------------------------------
 * 对外初始化入口
 * -------------------------------------------------------------------------- */

esp_err_t aks8_st7801n_init(esp_lcd_panel_handle_t *ret_panel,
                            esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;

    /* 1) 板级电源 / 复位 */
    board_lcd_power_on();
    board_enable_dsi_phy_power();
    board_lcd_reset();
    board_te_pin_init();

    /* 2) 创建 MIPI DSI 总线（1 data lane） */
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = 1,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = AKS8_MIPI_LANE_MBPS,
    };
    ret = esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_dsi_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "MIPI DSI bus created (1 lane, %d Mbps)", AKS8_MIPI_LANE_MBPS);

    /* 3) 创建 DBI 控制 IO，用于发送 DCS 命令 */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_dbi failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_io = io;

    /* 4) 发送初始化 DCS 命令（含 SLEEP OUT） */
    st7801n_send_init_commands(io);

    /* 5) 读取屏 ID（唤醒后读取，调试用，失败不阻断） */
    aks8_st7801n_read_id(io);

    /* 6) 创建 DPI panel（RGB565，1 framebuffer，内部 PSRAM） */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = AKS8_MIPI_DPI_CLK_MHZ,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
        /* v5.x 的 dpi 配置结构体含 pixel_format 字段（v6.x 已移除） */
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
#endif
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = AKS8_LCD_WIDTH,
            .v_size = AKS8_LCD_HEIGHT,
            .hsync_pulse_width = AKS8_LCD_HSYNC,
            .hsync_back_porch = AKS8_LCD_HBP,
            .hsync_front_porch = AKS8_LCD_HFP,
            .vsync_pulse_width = AKS8_LCD_VSYNC,
            .vsync_back_porch = AKS8_LCD_VBP,
            .vsync_front_porch = AKS8_LCD_VFP,
        },
    };
    ret = esp_lcd_new_panel_dpi(dsi_bus, &dpi_cfg, &panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_dpi failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "DPI panel created (%dx%d RGB565 @ %dMHz)",
             AKS8_LCD_WIDTH, AKS8_LCD_HEIGHT, AKS8_MIPI_DPI_CLK_MHZ);

    /* 7) 启动 panel（通用 DPI panel 的 init 完成 DPI 扫描启动） */
    /* 硬件复位已在前面 board_lcd_reset() 完成；部分 IDF 版本的通用 DPI
     * panel 不支持 esp_lcd_panel_reset()，故此处仅作容错调用。 */
    ret = esp_lcd_panel_reset(panel);
    if (ret != ESP_OK && ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "panel reset failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    vTaskDelay(pdMS_TO_TICKS(30));

    /* 8) 最后打开显示（对应简码最后一条 0x05 0x29） */
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, ST7801N_CMD_DISPON, NULL, 0));
    ESP_LOGI(TAG, "ST7801N display ON");

    if (ret_panel) {
        *ret_panel = panel;
    }
    if (ret_io) {
        *ret_io = io;
    }
    return ESP_OK;
}
