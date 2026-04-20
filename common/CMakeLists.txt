# common/CMakeLists.txt — közös réteg minden per-device app-nak (BL-015 step 1).
#
# Beemelési minta az apps/<device>/CMakeLists.txt-ből:
#
#     add_subdirectory(
#         ${CMAKE_CURRENT_SOURCE_DIR}/../../common
#         ${CMAKE_CURRENT_BINARY_DIR}/common)
#
# A user/ réteg NEM közös: minden device a saját apps/<device>/src/-jében
# definiálja a register_if_enabled() hívásokat és a csatorna-implementációkat.
#
# BL-015 step 1 állapot: ez a fájl még NINCS hívva sehonnan — csak akkor
# aktivizálódik, amikor az első apps/<device>/ létrejön (step 2). A régi app/
# build érintetlen.

zephyr_library_named(w6100_bridge_common)

zephyr_library_sources(
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
)

# A modulonkénti header-ek a src/<modul>/ mellett laknak (channel.h,
# config.h, drv_*.h, ...). Globálisan elérhetővé tesszük, hogy a per-device
# user_channels.c is megtalálja őket — ugyanaz a használati minta, mint a
# régi app/-ban a `target_include_directories(app PRIVATE src)`.
zephyr_include_directories(src)
