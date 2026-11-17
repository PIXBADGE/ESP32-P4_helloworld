/*
 * 轻量 RGB565 图形绘制（无第三方依赖，用于 HelloWorld 示例）
 * 通过 esp_lcd_panel_draw_bitmap 写入 DPI panel 的 framebuffer。
 */
#pragma once

#include <stdint.h>
#include "esp_lcd_panel_ops.h"

/* 常用 RGB565 颜色 */
#define GFX_BLACK   0x0000
#define GFX_WHITE   0xFFFF
#define GFX_RED     0xF800
#define GFX_GREEN   0x07E0
#define GFX_BLUE    0x001F
#define GFX_YELLOW  0xFFE0
#define GFX_CYAN    0x07FF
#define GFX_MAGENTA 0xF81F

/* 将 8bit R/G/B 打包为 RGB565 */
#define GFX_RGB565(r, g, b) (((uint16_t)(r) & 0xF8) << 8 | \
                             ((uint16_t)(g) & 0xFC) << 3 | \
                             ((uint16_t)(b) >> 3))

/**
 * @brief 用纯色填充整个屏幕
 */
void gfx_fill_screen(esp_lcd_panel_handle_t panel, int w, int h, uint16_t color);

/**
 * @brief 填充矩形区域 (x,y,w,h)
 */
void gfx_fill_rect(esp_lcd_panel_handle_t panel, int x, int y, int w, int h, uint16_t color);

/**
 * @brief 绘制垂直彩条（红/绿/蓝/黄/青/品红/白），用于验证色彩与方向
 */
void gfx_draw_color_bars(esp_lcd_panel_handle_t panel, int w, int h);

/**
 * @brief 在 (x,y) 处绘制一个缩放的 ASCII 字符（5x7 点阵）
 * @param scale 放大倍数（1=5x7, 2=10x14 ...）
 */
void gfx_draw_char(esp_lcd_panel_handle_t panel, int x, int y, char c,
                   uint16_t fg, uint16_t bg, int scale);

/**
 * @brief 绘制 ASCII 字符串
 */
void gfx_draw_string(esp_lcd_panel_handle_t panel, int x, int y, const char *str,
                     uint16_t fg, uint16_t bg, int scale);
