# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 添加lvgl v8.3版本组件
set(EXTRA_COMPONENT_DIRS 
    "./components/lvgl"
    "./components/lvgl_esp32_drivers"
    )

add_compile_options(-fdiagnostics-color=always)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(master)
