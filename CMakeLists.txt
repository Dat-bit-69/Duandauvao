# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS 
    "E:/esp-idf-lib/components/esp_idf_lib_helpers"
    "E:/esp-idf-lib/components/i2cdev"
    "E:/esp-idf-lib/components/bh1750"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(main)
