/*
 * ST7801N 1.75" AMOLED MIPI-DSI 显示屏驱动（AKS8 硬件）
 *
 * 屏幕模组: AFE03-S31FMA-1H (钜硕电子), 驱动 IC: Sitronix ST7801N
 * 接口: MIPI DSI, 1 clock lane + 1 data lane, 最高 550Mbps
 *
 * 初始化所需的屏内时序/Gamma/分辨率由模组 OTP 固化，
 * 主机侧仅需发送 SLEEP OUT / 亮度 / 像素格式 / TE / DISPLAY ON 等 DCS 命令，
 * 并配置匹配的 DPI 时序。
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * 硬件引脚映射（来自 AKS8 原理图）
 *   MIPI DSI 时钟/数据为 ESP32-P4 专用 DPHY 引脚，驱动内部绑定，无需 GPIO 号
 * -------------------------------------------------------------------------- */
#define AKS8_PIN_LCD_RST        23   /* LCD_RST  -> ESP32-P4 GPIO23 */
#define AKS8_PIN_LCD_TE         17   /* LCD_TE   -> ESP32-P4 GPIO17 */
#define AKS8_PIN_LCD_POW_EN     49   /* LCD_POW_EN -> ESP32-P4 GPIO49 (P-MOS, 低电平使能 3V3) */
#define AKS8_LCD_POW_EN_ACTIVE  0    /* 低电平打开 LCD_3V3 (AO3401 P-MOS) */

/* --------------------------------------------------------------------------
 * 屏幕参数（466 x 466，RGB565）
 * 如实际分辨率不同，只需修改下面 4 个宏即可
 * -------------------------------------------------------------------------- */
#define AKS8_LCD_WIDTH          466
#define AKS8_LCD_HEIGHT         466

#define AKS8_LCD_HSYNC          2    /* 水平同步脉宽 (pixel clock) */
#define AKS8_LCD_HBP            10   /* 水平后肩 */
#define AKS8_LCD_HFP            10   /* 水平前肩 */
#define AKS8_LCD_VSYNC          2    /* 垂直同步脉宽 (line) */
#define AKS8_LCD_VBP            10   /* 垂直后肩 */
#define AKS8_LCD_VFP            10   /* 垂直前肩 */

/* DPI 像素时钟: (466+2+10+10)*(466+2+10+10)*60Hz ≈ 14.3MHz，取 15MHz */
#define AKS8_MIPI_DPI_CLK_MHZ   15
/* DSI 单 lane 数据率: 15MHz * 16bpp ≈ 240Mbps，ST7801N 上限 550Mbps */
#define AKS8_MIPI_LANE_MBPS     500

/* --------------------------------------------------------------------------
 * DPHY 供电：2.5V
 * 原理图 VDD_MIPI_DPHY 由 ESP32-P4 内部 LDO_VO3 提供 (ESP_LDO_VO3)
 * -------------------------------------------------------------------------- */
#define AKS8_MIPI_PHY_LDO_CHAN      3
#define AKS8_MIPI_PHY_LDO_VOLTAGE   2500

/**
 * @brief 初始化 AKS8 的 1.75" AMOLED 屏幕
 *
 * 完成: LCD 电源使能 -> 复位 -> MIPI DSI 总线 -> DBI 控制 IO ->
 *       发送 ST7801N 初始化 DCS 命令 -> 创建 DPI panel -> 打开显示
 *
 * @param[out] ret_panel  返回 panel 句柄，供 esp_lcd_panel_draw_bitmap() 使用
 * @param[out] ret_io     返回 DBI IO 句柄（可选，传 NULL 则不输出）
 * @return ESP_OK 成功，其它为错误码
 */
esp_err_t aks8_st7801n_init(esp_lcd_panel_handle_t *ret_panel,
                            esp_lcd_panel_io_handle_t *ret_io);

/**
 * @brief 读取并打印 ST7801N 的 ID（DAh/DBh/DCh），用于调试排错
 */
esp_err_t aks8_st7801n_read_id(esp_lcd_panel_io_handle_t io);

/**
 * @brief 设置屏幕亮度（通过 0x51 DCS 命令，范围 0~255）
 */
esp_err_t aks8_st7801n_set_brightness(esp_lcd_panel_io_handle_t io, uint8_t brightness);

#ifdef __cplusplus
}
#endif
