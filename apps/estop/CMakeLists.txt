# apps/estop/CMakeLists.txt — E_STOP per-device Zephyr app (BL-015 step 2).
#
# Felépítés:
#   1. ZEPHYR_EXTRA_MODULES — közös micro-ROS lib + repo-szintű w6100 modul.
#      A w6100 modul host-oldalon a repo gyökerében van (modules/w6100_driver/),
#      konténerben a Makefile mountolja /workdir/modules-ra és exportálja a
#      W6100_MODULE_DIR env-et — így a CMakeLists nem függ host vs. konténer
#      relatív útvonalak különbségétől (régen ${CMAKE_CURRENT_SOURCE_DIR}/
#      modules/w6100_driver volt, mert a modul az app/ alatt élt).
#   2. find_package(Zephyr) — standard Zephyr build init.
#   3. add_subdirectory(common) — beemeli a közös réteget (main.c, bridge/,
#      drivers/, config/, shell/) mint zephyr_library_named(w6100_bridge_common).
#      Útvonal: COMMON_DIR env (Makefile állítja /workdir/common-ra), így
#      egyszerűen váltható, ha a közös réteg helye módosul.
#   4. target_sources(app PRIVATE src/...) — E_STOP-specifikus source-ok
#      (estop.c + user_channels.c, csak az estop csatornát regisztrálja).
#   5. include path: src/ — hogy a saját estop.h elérhető legyen; a közös
#      header-eket a common/CMakeLists.txt zephyr_include_directories(src)-je
#      teszi globálissá.
cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES
    $ENV{ZEPHYR_BASE}/../modules/lib/micro_ros_zephyr_module/modules/libmicroros
    $ENV{W6100_MODULE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(w6100_pico_bridge_estop)

# Közös réteg (zephyr_library_named: w6100_bridge_common)
add_subdirectory(
    $ENV{COMMON_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/common)

target_sources(app PRIVATE
    src/user_channels.c
    src/estop.c
)

target_include_directories(app PRIVATE src)
