# ESP-Mosaico Literature Clock: normal application installed alongside Vibe Mode.
# Lives at <esp-mosaico-vibe>/projects/literature_clock. Based on the official
# hello_world template, with the GSP demo UI replaced by an LVGL clock.
cmake_minimum_required(VERSION 3.16)
set(MOSAICO_UTILS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-utils")
set(MOSAICO_BSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-bsp")
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.application.defaults")
endif()
list(APPEND EXTRA_COMPONENT_DIRS
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_app_recovery")
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/mosaico_idf_project.cmake")
project(literature_clock VERSION 1.0.1)
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
