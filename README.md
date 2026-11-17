# AKS8 ESP32-P4 Hello World —— 1.75" AMOLED 示例

基于微雪 [ESP32-P4-Platform / 02_HelloWorld](https://github.com/waveshareteam/ESP32-P4-Platform/tree/main/examples/esp-idf/02_HelloWorld) 示例扩展，适配 **AKS8 硬件（ESP32-P4 + 1.75 寸 AMOLED / ST7801N / MIPI-DSI 466×466 + 电容触摸）**。

程序上电后：
1. 串口打印芯片信息（保留微雪 HelloWorld 原内容）；
2. 初始化 AMOLED 屏，先显示 RGB 彩条，再显示 `Hello World!` 文本；
3. 触摸屏幕时，串口打印触摸坐标并在屏幕上画方块反馈。

---

## 1. 硬件连接（来自 AKS8 原理图）

### 1.1 显示屏（AFE03-S31FMA-1H，ST7801N）

| 屏幕信号 | ESP32-P4 | 说明 |
|---|---|---|
| `DSI_MIPI_CLKP / CLKN` | 专用 MIPI DPHY 引脚 | 时钟 lane（驱动内部绑定，无需 GPIO 号） |
| `DSI_MIPI_D0P / D0N` | 专用 MIPI DPHY 引脚 | 数据 lane 0（单 lane） |
| `LCD_RST` | **GPIO23** | 复位，低有效 |
| `LCD_TE` | **GPIO17** | Tearing Effect（本例 DPI 连续刷新，仅预留观测） |
| `LCD_POW_EN` | **GPIO49** | 经 AO3401 (P-MOS) 控制 `LCD_3V3`，**低电平上电** |
| `VDD_MIPI_DPHY` | 内部 LDO_VO3 | 2.5V，驱动中通过 `esp_ldo` 使能 |

### 1.2 触摸屏（I2C）

| 触摸信号 | ESP32-P4 |
|---|---|
| `TP_RST` | **GPIO3** |
| `TP_INT` | **GPIO4** |
| `I2C1_SDA` | **GPIO5** |
| `I2C1_SCL` | **GPIO6** |

> 触摸 IC 集成在屏模组内，驱动会**自动探测**常见地址（0x15 / 0x1A / 0x38 / 0x5D / 0x14），无需手动指定型号。若你的模组触摸 IC 不同或地址不在列表，可在 `components/touch/touch.c` 的 `s_tp_candidates[]` 中补充。

### 1.3 电源 / 其它（原理图参考）

- Flash：BY25Q64ASSIG（8MB，原理图）— sdkconfig 按 16MB 配置，若实际 8MB 请改为 `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`。
- PSRAM：启用 XIP，显存从 PSRAM 分配。
- 充电 / 电量 / 音频 / IMU / NFC / SD 等外设本示例未使用。

---

## 2. 屏幕关键参数与可调项

所有屏幕参数集中在 `components/st7801n/include/st7801n.h`，如实际屏幕不同，改这几个宏即可：

```c
#define AKS8_LCD_WIDTH          466
#define AKS8_LCD_HEIGHT         466
#define AKS8_MIPI_DPI_CLK_MHZ   15     // DPI 像素时钟
#define AKS8_MIPI_LANE_MBPS     500    // 单 lane 数据率（ST7801N 上限 550）
```

初始化 DCS 序列在 `components/st7801n/st7801n.c` 的 `st7801n_send_init_commands()`，严格对应硬件资料《簡代碼_251126》：

```
SLPOUT(11h) → delay → CTRL(53h=20h) → 亮度(51h=FFh) → COLMOD(3Ah=55h RGB565)
→ TE(35h=00h) → [DPI 启动] → DISPON(29h)
```

> 该屏的时序、Gamma、分辨率由模组 OTP 固化，主机只需发出上述命令并配置匹配的 DPI 时序。
> 程序启动时会通过 `RDDID1/2/3 (DAh/DBh/DCh)` 读取并打印屏 ID，便于调试核对。

---

## 3. 编译与烧录

需 ESP-IDF **v5.4 或以上**（推荐 v5.4，与微雪示例一致）。

```bash
cd AKS8_HelloWorld
idf.py set-target esp32p4
idf.py menuconfig        # 可选：确认 Flash 大小、PSRAM 等
idf.py build
idf.py -p PORT flash monitor
```

`PORT` 替换为实际串口，例如 Windows 的 `COM3`、Linux 的 `/dev/ttyUSB0`。

如果在微雪仓库环境内使用，可把本工程放到 `examples/esp-idf/` 下，并取消顶层 `CMakeLists.txt` 中公共 defaults 的 include 注释。

---

## 4. 预期输出

串口（示例）：

```
Hello world!
This is esp32p4 chip with 2 CPU core(s), ...
silicon revision v3.0, 16MB external flash
Minimum free heap size: xxxxx bytes
I (xxx) st7801n: ST7801N ID: RDDID1=0x.. RDDID2=0x.. RDDID3=0x..
I (xxx) st7801n: MIPI DSI bus created (1 lane, 500 Mbps)
I (xxx) st7801n: ST7801N DCS init sequence sent
I (xxx) st7801n: DPI panel created (466x466 RGB565 @ 15MHz)
I (xxx) st7801n: ST7801N display ON
I (xxx) aks8_hello: LCD init OK
I (xxx) aks8_hello: Draw color bars
I (xxx) aks8_hello: Draw Hello World text
I (xxx) aks8_tp: Touch controller found at I2C addr 0x15
I (xxx) aks8_hello: Touch: points=1  P0=(233,233)
```

屏幕：先滚动显示彩条约 1.5 秒，随后显示 `Hello World!` 文本页。

---

## 5. 常见问题

- **屏幕不亮 / 花屏**
  1. 确认 `LCD_POW_EN=GPIO49` 电平极性（AO3401 低电平导通）；
  2. 确认分辨率/时序与屏 OTP 一致（试调整 `AKS8_LCD_*` 与 DPI 时钟）；
  3. 确认 LDO_VO3（2.5V MIPI DPHY）供电正常；
  4. 查看串口打印的屏 ID 是否与 ST7801N 一致。
- **颜色 / 方向不对**：改 `st7801n.c` 中 `COLMOD` 参数（0x55=RGB565）或 `dpi_cfg` 的 `in_color_format`；方向可通过彩条判断。
- **触摸无反应**：确认触摸 IC 地址是否在 `s_tp_candidates[]`；用 I2C 扫描工具确认总线。
- **编译报 MIPI DSI API 未定义**：确认 target 为 `esp32p4` 且 IDF ≥ v5.3（MIPI DSI 自 v5.3 引入）。

---

## 6. 工程结构

```
AKS8_HelloWorld/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── hello_world_main.c      # 主程序（HelloWorld + 显示 + 触摸）
│   ├── lcd_gfx.c / .h          # 轻量 RGB565 绘图 + 5x7 字库
│   └── idf_component.yml       # (可选) 组件依赖清单
├── components/
│   ├── st7801n/                # ST7801N MIPI-DSI 屏驱动
│   └── touch/                  # I2C 电容触摸驱动
└── README.md
```
