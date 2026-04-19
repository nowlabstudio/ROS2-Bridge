#!/usr/bin/env python3
"""rc_measure.py — RC bridge topic mérőszkript.

Feliratkozik a 6 RC csatorna topicjára (`/robot/motor_right`, `/robot/motor_left`,
`/robot/rc_ch3`, `/robot/rc_ch4`, `/robot/rc_mode`, `/robot/winch`), és két
fázisban mintavételez:

  1. Idle (alaphelyzet) — 5 s, stickek középen
  2. Sweep — 5 s, a felhasználó a CH1 és CH2 stickeket a két végállásba mozgatja

Kimenet: per-csatorna statisztika (n, Hz, min, max, mean, std, p2p) + CSV.

Futtatás (a ros2 Jazzy docker shellben):

    python3 /tools/rc_measure.py --label before    # EMA nélkül
    # ... shell: bridge config set rc_trim.ema_alpha 0.25; save; reboot
    python3 /tools/rc_measure.py --label after     # EMA 0.25-tel

    python3 /tools/rc_measure.py --compare \\
        logs/rc_*_before_summary.csv \\
        logs/rc_*_after_summary.csv
"""

import argparse
import csv
import math
import os
import sys
import time
from collections import defaultdict
from datetime import datetime

try:
    import rclpy
    from rclpy.node import Node
    from std_msgs.msg import Float32
except ImportError:
    print("[rc_measure] rclpy / std_msgs nincs elérhető. "
          "Futtasd a ros2 Jazzy shellen belül:", file=sys.stderr)
    print("    ./tools/docker-run-ros2.sh", file=sys.stderr)
    sys.exit(2)


DEFAULT_TOPICS = [
    ("ch1", "/robot/motor_right"),
    ("ch2", "/robot/motor_left"),
    ("ch3", "/robot/rc_ch3"),
    ("ch4", "/robot/rc_ch4"),
    ("ch5", "/robot/rc_mode"),
    ("ch6", "/robot/winch"),
]


class RCMeasureNode(Node):
    def __init__(self, topics):
        super().__init__("rc_measure")
        self.samples = defaultdict(list)
        self.stamps = defaultdict(list)
        self._subs = []
        for label, topic in topics:
            self._subs.append(
                self.create_subscription(
                    Float32,
                    topic,
                    lambda msg, lbl=label: self._on_msg(lbl, msg),
                    50,
                )
            )

    def _on_msg(self, label, msg):
        self.stamps[label].append(time.monotonic_ns())
        self.samples[label].append(float(msg.data))


def _stats(values):
    n = len(values)
    if n == 0:
        return None
    mn = min(values)
    mx = max(values)
    mean = sum(values) / n
    var = sum((v - mean) ** 2 for v in values) / n
    std = math.sqrt(var)
    return {"n": n, "min": mn, "max": mx, "mean": mean, "std": std, "p2p": mx - mn}


def _rate_hz(stamps, duration_s):
    if not stamps or duration_s <= 0:
        return 0.0
    return len(stamps) / duration_s


def _record(node, duration_s):
    t0 = time.monotonic()
    deadline = t0 + duration_s
    while rclpy.ok() and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.05)
    return time.monotonic() - t0


def _print_phase(phase_name, topics, node_samples, node_stamps, elapsed):
    header = (
        f'{"ch":<5} {"topic":<22} {"n":>5} {"Hz":>6} '
        f'{"min":>9} {"max":>9} {"mean":>10} {"std":>9} {"p2p":>9}'
    )
    print(f"\n── {phase_name} ── ({elapsed:.2f} s)")
    print(header)
    print("-" * len(header))
    rows = []
    for label, topic in topics:
        s = _stats(node_samples[label])
        hz = _rate_hz(node_stamps[label], elapsed)
        if s is None:
            print(f'{label:<5} {topic:<22} {"—":>5} (no data)')
            rows.append({"ch": label, "topic": topic, "n": 0, "hz": 0.0,
                         "min": "", "max": "", "mean": "", "std": "", "p2p": ""})
            continue
        print(
            f'{label:<5} {topic:<22} {s["n"]:>5d} {hz:>6.2f} '
            f'{s["min"]:>9.4f} {s["max"]:>9.4f} {s["mean"]:>10.5f} '
            f'{s["std"]:>9.5f} {s["p2p"]:>9.4f}'
        )
        rows.append({"ch": label, "topic": topic, "n": s["n"], "hz": hz, **s})
    return rows


def _reset_capture(node):
    node.samples = defaultdict(list)
    node.stamps = defaultdict(list)


def cmd_measure(args):
    topics = list(DEFAULT_TOPICS)
    rclpy.init()
    node = RCMeasureNode(topics)

    print(f"[rc_measure] label={args.label}  duration={args.duration:.1f}s  topics:")
    for label, topic in topics:
        print(f"   {label:5s} → {topic}")

    def _wait_or_prompt(prompt, auto_delay):
        if args.auto:
            print(prompt)
            for s in range(auto_delay, 0, -1):
                print(f"  ...indul {s}", flush=True)
                time.sleep(1.0)
        else:
            input(prompt)

    print("\n[Phase 1 — IDLE] stickek középen, ne nyúlj hozzá.")
    _wait_or_prompt("  ENTER a 5 másodperces idle mérés indításához... ", 3)
    _reset_capture(node)
    idle_elapsed = _record(node, args.duration)
    idle_samples = dict(node.samples)
    idle_stamps = dict(node.stamps)
    idle_rows = _print_phase("IDLE", topics, idle_samples, idle_stamps, idle_elapsed)

    print("\n[Phase 2 — SWEEP] mozgasd a CH1 és CH2 stickeket a két végállásba,")
    print("                  oda-vissza, amíg a visszaszámláló le nem jár.")
    _wait_or_prompt("  ENTER a 5 másodperces sweep mérés indításához... ", 5)
    _reset_capture(node)
    sweep_elapsed = _record(node, args.duration)
    sweep_samples = dict(node.samples)
    sweep_stamps = dict(node.stamps)
    sweep_rows = _print_phase("SWEEP", topics, sweep_samples, sweep_stamps, sweep_elapsed)

    os.makedirs(args.out_dir, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    stem = f"rc_{ts}_{args.label}"
    summary_path = os.path.join(args.out_dir, f"{stem}_summary.csv")
    raw_path = os.path.join(args.out_dir, f"{stem}_raw.csv")

    fieldnames = ["phase", "ch", "topic", "n", "hz", "min", "max", "mean", "std", "p2p"]
    with open(summary_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in idle_rows:
            w.writerow({"phase": "idle", **r})
        for r in sweep_rows:
            w.writerow({"phase": "sweep", **r})

    with open(raw_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["phase", "ch", "topic", "monotonic_ns", "value"])
        for label, topic in topics:
            for ts_ns, v in zip(idle_stamps.get(label, []), idle_samples.get(label, [])):
                w.writerow(["idle", label, topic, ts_ns, v])
            for ts_ns, v in zip(sweep_stamps.get(label, []), sweep_samples.get(label, [])):
                w.writerow(["sweep", label, topic, ts_ns, v])

    print(f"\n[rc_measure] Summary CSV: {summary_path}")
    print(f"[rc_measure] Raw CSV:     {raw_path}")

    node.destroy_node()
    rclpy.shutdown()


def cmd_stream(args):
    topics = list(DEFAULT_TOPICS)
    rclpy.init()
    node = RCMeasureNode(topics)

    print(f"[rc_measure stream] label={args.label}  duration={args.duration:.1f}s")
    print("Mozgasd a stickeket szabadon a felvétel teljes ideje alatt.\n")
    for s in range(3, 0, -1):
        print(f"  ...indul {s}", flush=True)
        time.sleep(1.0)
    print(f"  ── REC {args.duration:.0f}s ──", flush=True)

    _reset_capture(node)
    elapsed = _record(node, args.duration)
    samples = dict(node.samples)
    stamps = dict(node.stamps)
    rows = _print_phase("STREAM", topics, samples, stamps, elapsed)

    os.makedirs(args.out_dir, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    stem = f"rc_{ts}_{args.label}"
    summary_path = os.path.join(args.out_dir, f"{stem}_summary.csv")
    raw_path = os.path.join(args.out_dir, f"{stem}_raw.csv")

    fieldnames = ["phase", "ch", "topic", "n", "hz", "min", "max", "mean", "std", "p2p"]
    with open(summary_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow({"phase": "stream", **r})

    with open(raw_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["phase", "ch", "topic", "monotonic_ns", "value"])
        for label, topic in topics:
            for ts_ns, v in zip(stamps.get(label, []), samples.get(label, [])):
                w.writerow(["stream", label, topic, ts_ns, v])

    print(f"\n[rc_measure] Summary CSV: {summary_path}")
    print(f"[rc_measure] Raw CSV:     {raw_path}")
    node.destroy_node()
    rclpy.shutdown()


def _load_summary(path):
    rows = []
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            rows.append(row)
    return rows


def cmd_compare(args):
    before_rows = _load_summary(args.before)
    after_rows = _load_summary(args.after)

    def key(r):
        return (r["phase"], r["ch"])

    idx_before = {key(r): r for r in before_rows}
    idx_after = {key(r): r for r in after_rows}
    all_keys = sorted(set(idx_before.keys()) | set(idx_after.keys()))

    def fnum(s):
        try:
            return float(s)
        except (TypeError, ValueError):
            return None

    print(f"\n BEFORE: {args.before}")
    print(f" AFTER : {args.after}\n")

    header = (
        f'{"phase":<6} {"ch":<5} {"metric":<6} '
        f'{"before":>11} {"after":>11} {"delta":>11} {"ratio":>8}'
    )
    print(header)
    print("-" * len(header))

    for k in all_keys:
        b = idx_before.get(k, {})
        a = idx_after.get(k, {})
        phase, ch = k
        for metric in ("std", "p2p"):
            bv = fnum(b.get(metric))
            av = fnum(a.get(metric))
            if bv is None or av is None:
                continue
            delta = av - bv
            ratio = (av / bv) if bv > 1e-9 else float("inf")
            print(
                f"{phase:<6} {ch:<5} {metric:<6} "
                f"{bv:>11.5f} {av:>11.5f} {delta:>+11.5f} {ratio:>7.2f}x"
            )


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")

    m = sub.add_parser("measure", help="Két fázisú (idle + sweep) mérés + CSV.")
    m.add_argument("--label", required=True, help="Rövid cimke (pl. before / after_ema025).")
    m.add_argument("--duration", type=float, default=5.0)
    m.add_argument("--out-dir", default=".")
    m.add_argument("--auto", action="store_true",
                   help="Nem interaktív: visszaszámláló ENTER helyett.")
    m.set_defaults(func=cmd_measure)

    s = sub.add_parser("stream", help="Egyablakos folyamatos felvétel (nincs idle/sweep fázis).")
    s.add_argument("--label", required=True)
    s.add_argument("--duration", type=float, default=12.0)
    s.add_argument("--out-dir", default=".")
    s.set_defaults(func=cmd_stream)

    c = sub.add_parser("compare", help="Két summary CSV összehasonlítása.")
    c.add_argument("before")
    c.add_argument("after")
    c.set_defaults(func=cmd_compare)

    # Default: ha csak --label van, measure subcommand.
    argv = sys.argv[1:]
    if argv and argv[0] not in ("measure", "stream", "compare", "-h", "--help"):
        argv = ["measure"] + argv

    args = ap.parse_args(argv)
    if not hasattr(args, "func"):
        ap.print_help()
        sys.exit(1)
    args.func(args)


if __name__ == "__main__":
    main()
