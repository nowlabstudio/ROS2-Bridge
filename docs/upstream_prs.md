# docs/upstream_prs.md — Upstream PR előkészítés (BL-009)

> Utolsó frissítés: 2026-04-19
> Hatókör: külső open-source repókba küldendő javító patchek, amiket most
> lokálisan hordozunk (tools/patches/apply.sh, illetve az
> `app/modules/w6100_driver/` out-of-tree backport). Ha ezek upstream merged-ek,
> a saját patchelési infrastruktúránk egyszerűsödhet vagy törölhető.

Ez egy **előkészítő** dokumentum — nem automatizált PR benyújtás. Mindegyik
szekció tartalmaz: (a) egyértelmű jelölt commit/branch, (b) commit message
javaslat, (c) PR title + body template, (d) ellenőrzőlista a benyújtás előtt.

---

## PR-A — `micro_ros_zephyr_module`: UDP transport POSIX header conditional

**Target repó:** `micro-ros/micro_ros_zephyr_module`
**Branch base:** `jazzy`
**Érintett fájl:** `modules/libmicroros/microros_transports/udp/microros_transports.h`
**Kontextus:** ERR-027 (`ERRATA.md`)

### Patch tartalom

A header ma feltétel nélkül `<posix/sys/socket.h>`-t includol, ami Zephyr
v3.1 előtt működött. v3.1+-ban a POSIX headerek átkerültek
`<zephyr/posix/...>` alá. A serial transport headerek már használnak egy
`ZEPHYR_VERSION_CODE >= ZEPHYR_VERSION(3,1,0)` conditional-t, csak a UDP
header lemaradt — triviális szimmetrizálás.

**Előtte (upstream jazzy HEAD):**
```c
#include <sys/types.h>
#include <posix/sys/socket.h>
#include <posix/poll.h>
```

**Utána (patch):**
```c
#include <sys/types.h>
#include <version.h>

#if ZEPHYR_VERSION_CODE >= ZEPHYR_VERSION(3,1,0)
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/poll.h>
#else
#include <posix/sys/socket.h>
#include <posix/poll.h>
#endif
```

### Commit message javaslat

```
fix(udp-transport): match modern Zephyr POSIX header paths

Since Zephyr v3.1 the POSIX headers moved from <posix/...> to
<zephyr/posix/...>. The serial transport headers already guard this
with a ZEPHYR_VERSION_CODE conditional; the UDP transport header was
missed. Adding the same conditional lets jazzy build against both
Zephyr <3.1 and >=3.1 toolchains without patching.
```

### PR title + body

```
fix(udp-transport): conditional include for modern Zephyr POSIX headers
```

```markdown
## Why

On Zephyr >= v3.1 the POSIX headers live under `<zephyr/posix/...>`. The
UDP transport header still includes `<posix/sys/socket.h>` unconditionally,
which fails to compile against current Zephyr LTS / mainline. The serial
transport already uses `ZEPHYR_VERSION_CODE` to switch the include path,
so this PR just propagates the same guard to UDP.

## Reproducer (before the patch)

Build the Zephyr sample against Zephyr v4.2.2 + micro-ROS jazzy
@ 87dbe3a9b9d0fa347772e971d58d123e2296281a:

```
fatal error: posix/sys/socket.h: No such file or directory
```

## Validation

- Builds clean on Zephyr v4.2.2 (W6100 EVB Pico, RP2040, Zephyr SDK 0.17.4).
- Behaviour unchanged on Zephyr < v3.1 (else branch kept).
```

---

## PR-B — `micro_ros_zephyr_module`: do not exclude `std_srvs`

**Target repó:** `micro-ros/micro_ros_zephyr_module`
**Branch base:** `jazzy`
**Érintett fájl:** `modules/libmicroros/libmicroros.mk`
**Kontextus:** ERR-028 (`ERRATA.md`)

### Patch tartalom

`libmicroros.mk` a `std_srvs` csomagot aktívan kizárja a colcon buildből
azzal, hogy a `COLCON_IGNORE` marker fájlt létrehozza. A `std_srvs`
(SetBool, Trigger) a ROS2 alap szolgáltatásainak egyike, és nagyon sok
firmware használja. A javítás: `touch` helyett `rm -f` — ha volt előző
build-ből bent hagyott COLCON_IGNORE, azt is eltávolítja.

**Előtte:**
```make
	touch src/common_interfaces/std_srvs/COLCON_IGNORE; \
```

**Utána:**
```make
	rm -f src/common_interfaces/std_srvs/COLCON_IGNORE; \
```

### Commit message javaslat

```
fix(build): enable std_srvs (SetBool, Trigger) in libmicroros

The Makefile currently `touch`-es a COLCON_IGNORE marker under std_srvs
to exclude it from the aggregated build. std_srvs is part of the ROS2
common interfaces baseline (SetBool, Trigger) and applications linking
libmicroros routinely need it. Switching to `rm -f` both enables the
package and cleans up any stale marker left from a previous build.
```

### PR title + body

```
fix(build): include std_srvs in libmicroros aggregation
```

```markdown
## Why

`libmicroros.mk` currently excludes `std_srvs` via a `touch COLCON_IGNORE`
step. Downstream firmware that uses `SetBool` / `Trigger` — a very common
case for embedded robotics nodes — has to patch this file manually.
std_srvs is tiny, part of ROS2 common interfaces, and already pulled in
as a git submodule, so there's no size or licensing reason to keep it out.

## Change

Replace `touch` with `rm -f` on the `COLCON_IGNORE` marker. `rm -f` also
cleans up any marker left from previous builds, which is a strict
improvement on the current behaviour.

## Validation

- Rebuilt libmicroros against jazzy @ 87dbe3a9. Resulting `.a` contains
  the expected `std_srvs__srv__SetBool_Request` / `Trigger_Request`
  symbols.
- Tested with a firmware that declares a `/robot/emergency_stop`
  `std_srvs/SetBool` service — service init OK, client call OK.
```

---

## PR-C — Zephyr `eth_w6100`: update iface link_addr in set_config (ERR-031)

**Target repó:** `zephyrproject-rtos/zephyr`
**Branch base:** `main` (PR #101753 nyomán merged kód)
**Érintett fájl:** `drivers/ethernet/eth_w6100.c`
**Kontextus:** ERR-031 (`ERRATA.md`)

### Patch tartalom

A driver MACRAW módban fut — a Zephyr L2 réteg építi a teljes Ethernet
keretet szoftveresen az `iface->link_addr`-ból. A
`w6100_set_config(ETHERNET_CONFIG_TYPE_MAC_ADDRESS)` frissíti a chip
SHAR regiszterét és a `ctx->mac_addr`-t, de nem az iface link_addr-t.
Következmény: ha az application futás közben `ethernet_set_config()`-tal
MAC-et vált (pl. hwinfo-alapú egyedi MAC), a kimenő csomagok src MAC-je
továbbra is a DT-ből vagy init-ben beállított kezdő értéken marad,
és a host ARP cache is a régi MAC-kel asszociálja az IP-t. Blokkolja
a teljes return path-t.

**Javítás (a SHAR write közvetlen folytatásaként):**

```c
if (ctx->iface != NULL) {
    net_if_set_link_addr(ctx->iface, ctx->mac_addr,
                         sizeof(ctx->mac_addr),
                         NET_LINK_ETHERNET);
}
```

A hívás legitim ebben a pontban: a `set_config` tipikusan futás közben
jön, a `net_if_set_link_addr` pedig `-EPERM`-et csak akkor ad, ha
`NET_IF_RUNNING` flag áll — ami egy legitim "carrier_off alatt MAC váltás"
flow-ban még nincs beállítva.

### Commit message javaslat

```
drivers: ethernet: w6100: update iface link_addr in set_config

The driver operates in MACRAW mode, so Zephyr's L2 layer constructs the
full Ethernet frame in software using iface->link_addr as the source MAC.
w6100_set_config(MAC_ADDRESS) already writes the new MAC to the chip's
SHAR register but never propagates it to iface->link_addr, which means
outgoing frames still carry the original MAC captured at iface_init
time. As a result, host ARP caches pin the old MAC to the IP, and every
return-path packet (ICMP reply, UDP response) goes to a dead L2 address.

Propagate the new MAC to the iface link_addr after the SHAR write so
the driver and the L2 layer stay in sync.
```

### PR title + body

```
drivers: ethernet: w6100: propagate MAC set_config to iface link_addr
```

```markdown
## Why

Running the W6100 driver in MACRAW mode (the only mode Zephyr uses), the
L2 layer builds outgoing Ethernet frames in software from
`iface->link_addr`. Today `w6100_set_config(MAC_ADDRESS)` only writes
the chip's SHAR register and the driver-local `mac_addr` copy; the
iface link_addr is unchanged. The chip therefore advertises the new MAC
on receive (SHAR filter) but transmits with the *old* MAC in the L2
header, which breaks all return-path traffic: ARP replies to the host
still use the old MAC, host ARP caches it, and every reply packet is
sent to a non-existent L2 address.

## Change

After the SHAR write, propagate the new MAC to `iface->link_addr` via
`net_if_set_link_addr`. No behaviour change for drivers that never
call `set_config(MAC_ADDRESS)` (DT-provided MAC path).

## Reproduction

- Board: W5500 EVB Pico with a W6100 chip (PR #101753 backport).
- Application: micro-ROS client that sets a hwinfo-derived MAC via
  `ethernet_set_config()` at boot, then runs micro-ROS UDP4 transport.
- Without the patch: `ping` from host fails, `ip neigh show` shows the
  initial MAC; micro-ROS session handshake never completes.
- With the patch: ARP table shows the hwinfo MAC, ping 0% loss, session
  establishes normally.

## Testing

Verified on real hardware (W6100 EVB Pico, Zephyr v4.2.2 with the
PR #101753 driver backported as an out-of-tree module): ARP correct,
ping 0% loss, micro-ROS end-to-end session green.
```

### Megjegyzés

A PR #101753 kódja az *upstream main*-ban van. A mi repónkban a
backport-olt változatban van a fix. A PR szövegben hivatkozni kell,
hogy ez a fix az upstream main driverre is érvényes, nem csak a
backportra.

---

## Benyújtási ellenőrzőlista

Mielőtt bármelyik PR-t benyújtanád:

1. `git pull` a cél repó frissítésére — lehet, hogy az upstream már
   közben megoldotta (főleg ERR-027, ERR-028 esetében több éves bug).
2. A lokális `tools/patches/apply.sh`-t futtasd le egy friss west
   workspace-en, hogy a pattern tényleg megegyezik a jelenlegi
   upstream HEAD-del (különben a PR-t finomhangolni kell).
3. Fork + branch: `fix/udp-transport-posix-header` / `fix/libmicroros-std-srvs` /
   `drivers/eth_w6100/set-config-link-addr`.
4. DCO / CLA — Zephyr repo megköveteli `Signed-off-by:` lábjegyzetet.
5. Ha merged egy fix, akkor:
   - frissítsd `west.yml`-t egy olyan SHA-ra, ami már tartalmazza,
   - töröld a megfelelő Patch-et `tools/patches/apply.sh`-ból,
   - frissítsd `ERRATA.md` státuszát **Javítva (upstream)**-ra,
   - zárd le a `docs/backlog.md` BL-009 szálát (ha mindkét micro-ROS
     patch merged, akkor az `apply.sh` + Makefile `apply-patches` target
     is törölhető).
