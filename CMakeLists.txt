# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# NOTE: this name is baked into every built image's esp_app_desc_t.project_name
# and is what components/app_ota checks before installing an update (see
# FIRMWARE_PROJECT_NAME in components/app_ota/app_ota.c) - don't rename it
# without updating that constant too.
project(wt32_smalltv)