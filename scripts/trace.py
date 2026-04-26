#!/usr/bin/env python3
import re
import sys
import statistics

IRQ = 186
COMM = "edge_gatewayd"
PID = 134131

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} trace.txt")
    sys.exit(1)

trace_file = sys.argv[1]

ts_re = r"\s+(\d+\.\d+):"

irq_entry_re = re.compile(ts_re + rf".*irq_handler_entry: irq={IRQ}\b")
irq_exit_re = re.compile(ts_re + rf".*irq_handler_exit: irq={IRQ}\b")
waking_re = re.compile(ts_re + rf".*sched_waking: comm={COMM} pid={PID}\b")
wakeup_re = re.compile(ts_re + rf".*sched_wakeup: comm={COMM} pid={PID}\b")
switch_re = re.compile(ts_re + rf".*sched_switch: .*==> next_comm={COMM} next_pid={PID}\b")
marker_re = re.compile(ts_re + r".*tracing_mark_write: USER_EPOLL_RETURN")

samples = []
current = None
dropped = 0

def to_us(start, end):
    if start is None or end is None:
        return None
    return (end - start) * 1_000_000.0

with open(trace_file, "r", errors="ignore") as f:
    for line in f:
        m = irq_entry_re.search(line)
        if m:
            if current is not None:
                dropped += 1
            current = {
                "irq_entry": float(m.group(1)),
                "irq_exit": None,
                "sched_waking": None,
                "sched_wakeup": None,
                "sched_switch": None,
                "user_return": None,
            }
            continue

        if current is None:
            continue

        m = waking_re.search(line)
        if m and current["sched_waking"] is None:
            current["sched_waking"] = float(m.group(1))
            continue

        m = wakeup_re.search(line)
        if m and current["sched_wakeup"] is None:
            current["sched_wakeup"] = float(m.group(1))
            continue

        m = irq_exit_re.search(line)
        if m and current["irq_exit"] is None:
            current["irq_exit"] = float(m.group(1))
            continue

        m = switch_re.search(line)
        if m and current["sched_switch"] is None:
            current["sched_switch"] = float(m.group(1))
            continue

        m = marker_re.search(line)
        if m:
            current["user_return"] = float(m.group(1))
            samples.append(current)
            current = None
            continue

if current is not None:
    dropped += 1

def collect(field):
    vals = []
    for s in samples:
        v = to_us(s["irq_entry"], s[field])
        if v is not None:
            vals.append(v)
    return vals

def collect_pair(a, b):
    vals = []
    for s in samples:
        v = to_us(s[a], s[b])
        if v is not None:
            vals.append(v)
    return vals

def percentile(values, p):
    if not values:
        return None
    values = sorted(values)
    idx = round((p / 100.0) * (len(values) - 1))
    return values[int(idx)]

def report(name, values):
    if not values:
        print(f"{name}: no samples")
        return

    values = sorted(values)
    print(f"\n{name}")
    print(f"  count: {len(values)}")
    print(f"  min:   {values[0]:.3f} us")
    print(f"  avg:   {statistics.mean(values):.3f} us")
    print(f"  p50:   {percentile(values, 50):.3f} us")
    print(f"  p90:   {percentile(values, 90):.3f} us")
    print(f"  p95:   {percentile(values, 95):.3f} us")
    print(f"  p99:   {percentile(values, 99):.3f} us")
    print(f"  max:   {values[-1]:.3f} us")

print(f"complete samples: {len(samples)}")
print(f"dropped incomplete samples: {dropped}")

report("IRQ -> sched_waking", collect("sched_waking"))
report("IRQ -> sched_wakeup", collect("sched_wakeup"))
report("IRQ -> irq_handler_exit", collect("irq_exit"))
report("IRQ -> sched_switch_to_task", collect("sched_switch"))
report("sched_switch_to_task -> USER_EPOLL_RETURN", collect_pair("sched_switch", "user_return"))
report("IRQ -> USER_EPOLL_RETURN", collect("user_return"))

print("\nFirst 10 complete samples:")
for i, s in enumerate(samples[:10]):
    e2e = to_us(s["irq_entry"], s["user_return"])
    sw = to_us(s["irq_entry"], s["sched_switch"])
    print(
        f"  #{i}: "
        f"irq={s['irq_entry']:.6f}, "
        f"switch_delta={sw:.3f} us, "
        f"e2e={e2e:.3f} us"
    )