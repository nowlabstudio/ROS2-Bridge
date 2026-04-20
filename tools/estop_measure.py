#!/usr/bin/env python3
"""estop_measure.py — E-stop bridge publish rate + latency mérőszkript.

Feliratkozik az `/robot/estop` topicra (`std_msgs/Bool`) és N másodpercen
keresztül rögzíti a callback érkezési időt (monotonic_ns) + a payload-ot.
A firmware két utat használ a publish-hoz (lásd `app/src/bridge/channel_manager.c`):

  1. Periodic fallback — `estop_channel.period_ms` lejárta után kötelezően
     újrapublikál (jelen kódban 500 ms = 2 Hz).
  2. IRQ-alapú azonnali publish — edge-both GPIO IRQ setelte flaget a fő
     loop lereagálja (`channel_manager_handle_irq_pending`) → extra publish
     a state-váltás pillanatában.

Metrikák:

  - Total message count, effektív Hz (n / elapsed)
  - Inter-message gap stat (min / max / mean / median / p95 / p99 / std) ms
  - Edge-events: a state-váltások (True↔False) száma + az utolsó gap
    értéke előttük/mögöttük — így látható, hogy érkezik-e IRQ-publish
    a fix rate fölé, vagy a firmware csak a periodic publish-t adja.

Futtatás (ros2 Jazzy docker shellben):

    ./tools/docker-run-ros2.sh
    python3 /tools/estop_measure.py --label baseline --duration 15

Opciók:

    --topic   /robot/estop (alapértelmezett; config-remap esetén felülírható)
    --duration  mintavételi idő s-ban (default: 15)
    --label   cimke a CSV-ben (before / after_period50 / …)
    --out-dir logs/  (.gitignore-olt)
"""

import argparse
import csv
import math
import os
import statistics
import sys
import time
from datetime import datetime

try:
    import rclpy
    from rclpy.node import Node
    from std_msgs.msg import Bool
except ImportError:
    print("[estop_measure] rclpy / std_msgs nincs elérhető. "
          "Futtasd a ros2 Jazzy shellen belül:", file=sys.stderr)
    print("    ./tools/docker-run-ros2.sh", file=sys.stderr)
    sys.exit(2)


class EstopMeasureNode(Node):
    def __init__(self, topic: str):
        super().__init__("estop_measure")
        self.topic = topic
        self.stamps: list[int] = []   # monotonic_ns
        self.values: list[bool] = []
        self._sub = self.create_subscription(
            Bool, topic, self._on_msg, 50
        )

    def _on_msg(self, msg):
        self.stamps.append(time.monotonic_ns())
        self.values.append(bool(msg.data))


def _gaps_ms(stamps_ns: list[int]) -> list[float]:
    if len(stamps_ns) < 2:
        return []
    return [(b - a) / 1e6 for a, b in zip(stamps_ns, stamps_ns[1:])]


def _pct(sorted_values: list[float], pct: float) -> float:
    if not sorted_values:
        return float("nan")
    k = (len(sorted_values) - 1) * (pct / 100.0)
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return sorted_values[int(k)]
    d0 = sorted_values[f] * (c - k)
    d1 = sorted_values[c] * (k - f)
    return d0 + d1


def _stats(gaps_ms: list[float]) -> dict:
    if not gaps_ms:
        return {"n": 0}
    s = sorted(gaps_ms)
    return {
        "n":      len(s),
        "min":    s[0],
        "max":    s[-1],
        "mean":   statistics.mean(s),
        "median": statistics.median(s),
        "p95":    _pct(s, 95.0),
        "p99":    _pct(s, 99.0),
        "std":    statistics.pstdev(s),
    }


def _count_edges(values: list[bool]) -> int:
    if len(values) < 2:
        return 0
    return sum(1 for a, b in zip(values, values[1:]) if a != b)


def cmd_stream(args):
    rclpy.init()
    node = EstopMeasureNode(args.topic)

    print(f"[estop_measure] topic={args.topic}  duration={args.duration:.1f}s  label={args.label}")
    print("")
    print("A felvétel alatt:")
    print("  - először hagyd nyugalomban (idle publish rate mérés)")
    print("  - majd ~5 s után nyomd / engedd fel párszor az E-stop gombot")
    print("    (IRQ-alapú azonnali publish láthatóságához)")
    print("")
    for s in range(3, 0, -1):
        print(f"  ...indul {s}", flush=True)
        time.sleep(1.0)
    print(f"  ── REC {args.duration:.0f}s ──", flush=True)

    t0 = time.monotonic()
    deadline = t0 + args.duration
    while rclpy.ok() and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.05)
    elapsed = time.monotonic() - t0

    stamps = list(node.stamps)
    values = list(node.values)
    gaps = _gaps_ms(stamps)
    st = _stats(gaps)
    edges = _count_edges(values)
    hz = (len(stamps) / elapsed) if elapsed > 0 else 0.0

    print(f"\n── STREAM eredmény ── ({elapsed:.2f} s, {len(stamps)} msg)")
    print(f"  effektív rate     : {hz:7.2f} Hz")
    print(f"  edge-events       : {edges}")
    if st["n"] > 0:
        print(f"  gap min / max     : {st['min']:7.2f} / {st['max']:7.2f} ms")
        print(f"  gap mean / median : {st['mean']:7.2f} / {st['median']:7.2f} ms")
        print(f"  gap p95 / p99     : {st['p95']:7.2f} / {st['p99']:7.2f} ms")
        print(f"  gap std           : {st['std']:7.2f} ms")
    else:
        print("  (nincs elég minta a gap statisztikához)")

    os.makedirs(args.out_dir, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    stem = f"estop_{ts}_{args.label}"
    summary_path = os.path.join(args.out_dir, f"{stem}_summary.csv")
    raw_path = os.path.join(args.out_dir, f"{stem}_raw.csv")

    with open(summary_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["label", "topic", "duration_s", "n",
                    "hz", "edges",
                    "gap_min_ms", "gap_max_ms",
                    "gap_mean_ms", "gap_median_ms",
                    "gap_p95_ms", "gap_p99_ms", "gap_std_ms"])
        row = [args.label, args.topic, f"{elapsed:.3f}", len(stamps),
               f"{hz:.3f}", edges]
        if st["n"] > 0:
            row += [f"{st['min']:.3f}", f"{st['max']:.3f}",
                    f"{st['mean']:.3f}", f"{st['median']:.3f}",
                    f"{st['p95']:.3f}", f"{st['p99']:.3f}",
                    f"{st['std']:.3f}"]
        else:
            row += [""] * 7
        w.writerow(row)

    with open(raw_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["idx", "monotonic_ns", "value",
                    "gap_ms_to_prev"])
        prev = None
        for i, (ts_ns, v) in enumerate(zip(stamps, values)):
            gap = "" if prev is None else f"{(ts_ns - prev) / 1e6:.3f}"
            w.writerow([i, ts_ns, int(bool(v)), gap])
            prev = ts_ns

    print(f"\n[estop_measure] Summary CSV: {summary_path}")
    print(f"[estop_measure] Raw CSV:     {raw_path}")

    node.destroy_node()
    rclpy.shutdown()


def cmd_compare(args):
    def _load(path):
        with open(path, newline="") as f:
            return list(csv.DictReader(f))

    before = _load(args.before)
    after = _load(args.after)
    if not before or not after:
        print("[estop_measure] üres summary CSV", file=sys.stderr)
        sys.exit(1)

    b = before[0]
    a = after[0]

    print(f"\n BEFORE: {args.before}  (label={b['label']})")
    print(f" AFTER : {args.after}  (label={a['label']})\n")

    metrics = [
        ("hz",             "Hz"),
        ("edges",          "edges"),
        ("gap_min_ms",     "min(ms)"),
        ("gap_max_ms",     "max(ms)"),
        ("gap_mean_ms",    "mean(ms)"),
        ("gap_median_ms",  "median(ms)"),
        ("gap_p95_ms",     "p95(ms)"),
        ("gap_p99_ms",     "p99(ms)"),
        ("gap_std_ms",     "std(ms)"),
    ]

    header = f'{"metric":<11} {"before":>12} {"after":>12} {"delta":>12} {"ratio":>8}'
    print(header)
    print("-" * len(header))
    for key, label in metrics:
        try:
            bv = float(b[key])
            av = float(a[key])
        except (KeyError, ValueError):
            continue
        delta = av - bv
        ratio = (av / bv) if abs(bv) > 1e-9 else float("inf")
        print(f"{label:<11} {bv:>12.3f} {av:>12.3f} {delta:>+12.3f} {ratio:>7.2f}x")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = ap.add_subparsers(dest="cmd")

    s = sub.add_parser("stream", help="Folyamatos felvétel + statisztika + CSV.")
    s.add_argument("--topic", default="/robot/estop")
    s.add_argument("--duration", type=float, default=15.0)
    s.add_argument("--label", required=True,
                   help="Rövid cimke (pl. baseline / after_period50).")
    s.add_argument("--out-dir", default="logs")
    s.set_defaults(func=cmd_stream)

    c = sub.add_parser("compare", help="Két summary CSV összehasonlítása.")
    c.add_argument("before")
    c.add_argument("after")
    c.set_defaults(func=cmd_compare)

    # Kényelem: ha az első argumentum nem subcommand, stream-et feltételez.
    argv = sys.argv[1:]
    if argv and argv[0] not in ("stream", "compare", "-h", "--help"):
        argv = ["stream"] + argv

    args = ap.parse_args(argv)
    if not hasattr(args, "func"):
        ap.print_help()
        sys.exit(1)
    args.func(args)


if __name__ == "__main__":
    main()
