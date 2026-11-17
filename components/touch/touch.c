/*
 * AKS8 电容触摸屏驱动（I2C，CST 系列兼容）
 *
 * 支持自动探测以下常见触摸控制器地址：
 *   0x15 (CST9217 / CST816 常见), 0x1A (CST820), 0x38 (FT6236/GT911 替代地址)
 * 坐标寄存器兼容 CST 系列 0x01 起始的 Report 格式：
 *   [0]=gesture, [1]=手指1状态(高2位事件/低2位触点编号),
 *   [2..3]=X, [4..5]=Y；0x02 寄存器给出有效触点数。
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "touch.h"

static const char *TAG = "aks8_tp";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t  s_tp_dev = NULL;
static uint8_t                  s_tp_addr = 0;

/* 常见触摸 IC I2C 地址（7-bit） */
static const uint8_t s_tp_candidates[] = {
    0x15, 0x1A, 0x38, 0x5D, 0x14,
};

static esp_err_t tp_reset(void)
{
    gpio_config_t rst_conf = {
        .pin_bit_mask = 1ULL << AKS8_PIN_TP_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_conf));

    gpio_set_level(AKS8_PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(AKS8_PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    return ESP_OK;
}

static esp_err_t tp_i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,                 /* -1 = 自动选择空闲 I2C 端口 */
        .scl_io_num = AKS8_PIN_TP_SCL,
        .sda_io_num = AKS8_PIN_TP_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    /* 先用 probe 探测候选地址，确认应答后再挂载设备 */
    for (size_t i = 0; i < sizeof(s_tp_candidates); i++) {
        if (i2c_master_probe(s_i2c_bus, s_tp_candidates[i], 100) == ESP_OK) {
            i2c_device_config_t dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = s_tp_candidates[i],
                .scl_speed_hz = AKS8_TP_I2C_FREQ_HZ,
            };
            if (i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_tp_dev) == ESP_OK) {
                s_tp_addr = s_tp_candidates[i];
                ESP_LOGI(TAG, "Touch controller found at I2C addr 0x%02X", s_tp_addr);
                return ESP_OK;
            }
        }
    }
    ESP_LOGW(TAG, "No touch controller detected; touch functions disabled");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t aks8_touch_init(void)
{
    ESP_ERROR_CHECK(tp_reset());
    esp_err_t ret = tp_i2c_init();
    if (ret != ESP_OK) {
        return ret; /* 触摸不可用不影响主程序 */
    }
    return ESP_OK;
}

esp_err_t aks8_touch_read(aks8_touch_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(data, 0, sizeof(*data));
    if (s_tp_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 读取从 0x01 开始的 6 字节触摸报告 */
    uint8_t reg = 0x01;
    uint8_t buf[8] = {0};
    esp_err_t ret = i2c_master_transmit_receive(s_tp_dev, &reg, 1, buf, sizeof(buf),
                                                pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t points = buf[1] & 0x0F; /* 0x02 寄存器：有效触点数 */
    data->num_points = points;
    data->pressed = (points > 0);

    if (points >= 1) {
        data->point[0].x = ((uint16_t)(buf[3] & 0x0F) << 8) | buf[2];
        data->point[0].y = ((uint16_t)(buf[5] & 0x0F) << 8) | buf[4];
    }
    if (points >= 2) {
        /* 两点触摸：CST 在 0x07~0x0B，本例仅做基础解析 */
        data->point[1].x = data->point[0].x;
        data->point[1].y = data->point[0].y;
    }
    return ESP_OK;
}
