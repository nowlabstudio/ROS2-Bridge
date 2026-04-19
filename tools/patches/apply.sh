#!/usr/bin/env bash
# =============================================================================
# tools/patches/apply.sh
# =============================================================================
#
# Idempotens patch-alkalmazó a west-managelt `workspace/` fákra. A jazzy
# branch HEAD-je két ismert hibát hordoz, amit a build előtt orvosolni kell:
#
#  1. micro_ros_zephyr_module UDP transport header régi Zephyr POSIX layoutot
#     vár (<posix/sys/socket.h>). A modern Zephyr-ben (v3.1+) a path
#     <zephyr/posix/sys/socket.h>. A serial transportok már conditionalt
#     használnak, de a UDP header lemaradt. (ERRATA ERR-027)
#
#  2. libmicroros.mk `touch std_srvs/COLCON_IGNORE` kizárja a std_srvs
#     csomagot. A mi firmware-ünk SetBool/Trigger szolgáltatásokat használ,
#     így kell. (ERRATA ERR-028)
#
# A korábbi Patch 3/4/5 (W5500 driver W6100-kompatibilitási hack) megszűnt:
# a W6100-at natív out-of-tree driverrel hajtjuk (app/modules/w6100_driver/),
# a W5500 driver érintetlen marad. (ERRATA ERR-030 lezárva)
#
# Mindkét patch a west update után alkalmazandó a `make build` előtt.
# A Makefile `build` target automatikusan függ az `apply-patches`-től.
#
# Használat:
#   bash tools/patches/apply.sh <workspace-dir>
#     alapértelmezett: ./workspace
#
# A script idempotens: már patchelt állapotot felismer, hiba nélkül kilép.
# =============================================================================

set -euo pipefail

WS="${1:-workspace}"

if [ ! -d "$WS" ]; then
    echo "[apply.sh] ERROR: workspace directory not found: $WS" >&2
    echo "[apply.sh] Run 'make workspace-init' first." >&2
    exit 1
fi

echo "[apply.sh] Applying patches to $WS ..."

# -----------------------------------------------------------------------------
# Patch 1 — microros_transports.h (UDP): ZEPHYR_VERSION_CODE conditional
# -----------------------------------------------------------------------------
F1="$WS/modules/lib/micro_ros_zephyr_module/modules/libmicroros/microros_transports/udp/microros_transports.h"

python3 - "$F1" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()

OLD = '''#include <sys/types.h>
#include <posix/sys/socket.h>
#include <posix/poll.h>
'''
NEW = '''#include <sys/types.h>
#include <version.h>

#if ZEPHYR_VERSION_CODE >= ZEPHYR_VERSION(3,1,0)
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/poll.h>
#else
#include <posix/sys/socket.h>
#include <posix/poll.h>
#endif
'''

if OLD in s:
    open(p, 'w').write(s.replace(OLD, NEW))
    print(f'[apply.sh] [Patch 1] applied: {p}')
elif NEW in s:
    print(f'[apply.sh] [Patch 1] already applied: {p}')
else:
    print(f'[apply.sh] [Patch 1] ERROR: pattern not found, manual inspection needed', file=sys.stderr)
    print(f'  File: {p}', file=sys.stderr)
    sys.exit(1)
PY

# -----------------------------------------------------------------------------
# Patch 2 — libmicroros.mk: enable std_srvs (rm -f helyett touch COLCON_IGNORE)
# -----------------------------------------------------------------------------
F2="$WS/modules/lib/micro_ros_zephyr_module/modules/libmicroros/libmicroros.mk"

python3 - "$F2" <<'PY'
import sys, re
p = sys.argv[1]
s = open(p).read()

# Original line (tab-indented): `\ttouch src/common_interfaces/std_srvs/COLCON_IGNORE; \\\n`
# Patched line: `\trm -f src/common_interfaces/std_srvs/COLCON_IGNORE; \\\n`

OLD_RE = re.compile(r'\ttouch src/common_interfaces/std_srvs/COLCON_IGNORE; \\\n')
NEW    = '\trm -f src/common_interfaces/std_srvs/COLCON_IGNORE; \\\n'

if OLD_RE.search(s):
    s2 = OLD_RE.sub(NEW, s)
    open(p, 'w').write(s2)
    print(f'[apply.sh] [Patch 2] applied: {p}')
elif 'rm -f src/common_interfaces/std_srvs/COLCON_IGNORE' in s:
    print(f'[apply.sh] [Patch 2] already applied: {p}')
else:
    print(f'[apply.sh] [Patch 2] ERROR: pattern not found, manual inspection needed', file=sys.stderr)
    print(f'  File: {p}', file=sys.stderr)
    sys.exit(1)
PY

# -----------------------------------------------------------------------------
# Stale build artifact cleanup: ha a std_srvs COLCON_IGNORE már létezik egy
# korábbi build-ből, töröljük (Patch 2 új buildnél nem tudja utólag elérni).
# -----------------------------------------------------------------------------
STALE_CI="$WS/modules/lib/micro_ros_zephyr_module/modules/libmicroros/micro_ros_src/src/common_interfaces/std_srvs/COLCON_IGNORE"
if [ -f "$STALE_CI" ]; then
    rm -f "$STALE_CI"
    echo "[apply.sh] removed stale COLCON_IGNORE: $STALE_CI"
fi

echo "[apply.sh] All patches applied successfully."
