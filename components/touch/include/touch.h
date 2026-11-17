/*
 * AKS8 电容触摸屏驱动（I2C，CST 系列兼容）
 *
 * 原理图连接：
 *   TP_RST  -> ESP32-P4 GPIO3
 *   TP_INT  -> ESP32-P4 GPIO4
 *   I2C1_SDA -> ESP32-P4 GPIO5
 *   I2C1_SCL -> ESP32-P4 GPIO6
 *
 * 屏模组集成的触摸 IC 多为 CST9217 / CST820 / CST816 系列（地址 0x15/0x1A/0x38），
 * 本驱动在上电时自动探测地址，并读取标准坐标寄存器。
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AKS8_PIN_TP_RST     3
#define AKS8_PIN_TP_INT     4
#define AKS8_PIN_TP_SDA     5
#define AKS8_PIN_TP_SCL     6
#define AKS8_TP_I2C_FREQ_HZ 100000

typedef struct {
    uint16_t x;
    uint16_t y;
} aks8_tp_point_t;

typedef struct {
    bool pressed;        /* 是否有触摸 */
    uint8_t num_points;  /* 触摸点数 */
    aks8_tp_point_t point[2];
} aks8_touch_data_t;

/**
 * @brief 初始化触摸（创建 I2C 主机、复位触摸 IC、自动探测地址）
 * @return ESP_OK 成功
 */
esp_err_t aks8_touch_init(void);

/**
 * @brief 读取当前触摸数据（非阻塞）
 */
esp_err_t aks8_touch_read(aks8_touch_data_t *data);

#ifdef __cplusplus
}
#endif
