# common/CMakeLists.txt — közös réteg minden per-device app-nak (BL-015 step 1).
#
# Beemelési minta az apps/<device>/CMakeLists.txt-ből:
#
#     add_subdirectory(
#         $ENV{COMMON_DIR}
#         ${CMAKE_CURRENT_BINARY_DIR}/common)
#
# A user/ réteg NEM közös (csak a user/user_channels.h interface header):
# minden device a saját apps/<device>/src/-jében definiálja a
# user_register_channels() implementációt és a csatorna-objektumokat.
#
# Build mód: NINCS önálló zephyr_library — minden közös .c fájlt a
# `app` Zephyr default target-hez fűzünk (target_sources(app PRIVATE ...)),
# mint a régi app/-ban. Indok: a libmicroros és littlefs header-eket csak
# az `app` target kapja meg automatikusan a Zephyr build kontextusból; egy
# új zephyr_library_named(...) target nem örökli ezeket, és minden közös
# include-ot (rcl/rcl.h, lfs.h, stb.) manuálisan kéne be propagálni.

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config/config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shell/shell_cmd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bridge/channel_manager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bridge/diagnostics.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bridge/param_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bridge/service_manager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/drv_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/drv_adc.c
)

# BL-020 Step 2 — PWM input driver kiválasztása Kconfig-alapon.
# CONFIG_RC_PWM_DRIVER_PIO=y  → deterministic RP2040 PIO driver (RC app).
# CONFIG_RC_PWM_DRIVER_PIO=n  → GPIO EDGE_BOTH IRQ fallback driver (default,
#   estop/pedal app is ezt kapja — sem a Kconfig szimbólumot nem definiálják,
#   sem a DT-ben nincs `nowlab,pwm-input-pio` node).
if(CONFIG_RC_PWM_DRIVER_PIO)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/drv_pwm_in_pio.c)
else()
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/drv_pwm_in.c)
endif()

# A modulonkénti header-ek a src/<modul>/ mellett laknak (channel.h,
# config.h, drv_*.h, user/user_channels.h ...). Hozzáadjuk az `app` target
# include path-jaihoz, így a per-device user_channels.c is megtalálja őket
# (#include "bridge/...", "user/user_channels.h", stb.).
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
