# Újraindítási prompt — W6100 Bridge v2.0

## Állapot újraindítás előtt

A v2.0 teljes implementáció elkészült és commitolva van:
- Commit: `90935b9` — "feat: implement v2.0 bridge architecture (phases 0-6)"
- Branch: main

## Mit kell csinálni újraindítás után

**Egyetlen feladat: `make build` futtatása**

```bash
cd /Users/m2mini/Dev/W6100_EVB_Pico_Zephyr_MicroROS
make build
```

Ez a library rebuild (~45-60 perc Docker-ben). Első futás mert:
1. `prj.conf`-ban új entity limitek (PUBLISHERS=20, SUBSCRIBERS=16, SERVERS=8)
2. `libmicroros.mk`-ban `std_srvs/COLCON_IGNORE` eltávolítva → std_srvs engedélyezve

## A build után elvárt viselkedés

A build logban keresendő:
```
[micro-ROS] RMW_UXRCE_MAX_PUBLISHERS=20
[micro-ROS] RMW_UXRCE_MAX_SUBSCRIPTIONS=16
[micro-ROS] RMW_UXRCE_MAX_SERVICES=8
```

A build végén:
```
[100%] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:      ...
             RAM:      ...
```

RAM: max ~230 KB / 264 KB legyen. Ha több → jelzés kell.

## Ha a build sikertelen

Legvalószínűbb hibák és megoldások:

### 1. Fordítási hiba az új fájlokban
Nézd meg pontosan melyik fájl/sor → javítás.

### 2. `rclc_parameter_server_init_with_option` not found
A rclc_parameter könyvtár nem fordult be. Ellenőrizni:
```
workspace/modules/lib/micro_ros_zephyr_module/modules/libmicroros/colcon.meta
```
Szerepel-e benne az rclc csomag.

### 3. `std_srvs` típusok nem találhatók
A `libmicroros.mk` módosítás nem vette hatását. Ellenőrizni:
```
workspace/modules/lib/micro_ros_zephyr_module/modules/libmicroros/libmicroros.mk
```
A `std_srvs/COLCON_IGNORE` sor valóban hiányzik-e (102. sor környéke).

### 4. `sys_heap_runtime_stats_get` hiba
Ha előkerül: a diagnostics.c-ből ki van hagyva a heap monitoring (döntés alapján).
Ez NEM szerepel a kódban, tehát nem fordulhat elő.

### 5. RAM overflow (> 264 KB)
A heap 128 KB maradt (döntés). Ha mégis overflow: először
`CONFIG_HEAP_MEM_POOL_SIZE` csökkentése jöhet szóba.

## Összefoglalás az elvégzett munkáról

### Módosított fájlok
- `app/prj.conf` — entity limitek, ATOMIC_OPERATIONS_BUILTIN
- `libmicroros.mk` — std_srvs engedélyezve
- `app/src/bridge/channel.h` — channel_state_t, irq_capable
- `app/src/bridge/channel_manager.h/.c` — state API, IRQ, perform_channel_publish()
- `app/src/config/config.h/.c` — channel_params_save/load, json_get_int
- `app/src/main.c` — 1ms loop, diagnostics/param/service lifecycle
- `app/boards/w5500_evb_pico.overlay` — GP14/GP15 alias placeholder

### Új fájlok
- `app/src/bridge/diagnostics.h/.c`
- `app/src/bridge/param_server.h/.c`
- `app/src/bridge/service_manager.h/.c`
- `app/src/drivers/drv_gpio.h/.c`
- `app/src/drivers/drv_adc.h/.c`
- `app/src/drivers/drv_imu.h`
