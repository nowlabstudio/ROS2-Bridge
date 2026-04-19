cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES
    $ENV{ZEPHYR_BASE}/../modules/lib/micro_ros_zephyr_module/modules/libmicroros
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/w6100_driver)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(w6100_pico_bridge)

target_sources(app PRIVATE
    src/main.c
    src/config/config.c
    src/shell/shell_cmd.c
    src/bridge/channel_manager.c
    src/bridge/diagnostics.c
    src/bridge/param_server.c
    src/bridge/service_manager.c
    src/drivers/drv_gpio.c
    src/drivers/drv_adc.c
    src/drivers/drv_pwm_in.c
    src/user/user_channels.c
    src/user/test_channels.c
    src/user/estop.c
    src/user/rc.c
)

target_include_directories(app PRIVATE src)
