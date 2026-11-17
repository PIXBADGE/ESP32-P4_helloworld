# AKS8 ESP32-P4 Hello World (1.75" AMOLED / ST7801N / MIPI-DSI)
# 基于微雪 ESP32-P4-Platform 的 02_HelloWorld 示例工程结构扩展而来。
# 参考: https://github.com/waveshareteam/ESP32-P4-Platform/tree/main/examples/esp-idf/02_HelloWorld

cmake_minimum_required(VERSION 3.16)

# 微雪原工程在此 include 公共 defaults；本示例是自包含工程，
# 已把所需的芯片配置直接写入 sdkconfig.defaults，故不再依赖微雪仓库路径。
# 如果你把本工程放回微雪仓库的 examples/esp-idf/ 下，可取消下面这行的注释：
# include(${CMAKE_CURRENT_LIST_DIR}/../../../config/esp32p4_local_defaults.cmake)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(AKS8_HelloWorld)
