# apps/rc/CMakeLists.txt — RC per-device Zephyr app (BL-015 step 3).
#
# Mintát ad: apps/estop/CMakeLists.txt — ugyanaz a felépítés, csak az
# RC-specifikus source-okat (rc.c + user_channels.c) húzza be.
cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES
    $ENV{ZEPHYR_BASE}/../modules/lib/micro_ros_zephyr_module/modules/libmicroros
    $ENV{W6100_MODULE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(w6100_pico_bridge_rc)

# Közös réteg
add_subdirectory(
    $ENV{COMMON_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/common)

target_sources(app PRIVATE
    src/user_channels.c
    src/rc.c
)

target_include_directories(app PRIVATE src)
