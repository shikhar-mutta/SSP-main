#!/usr/bin/env python3
"""
Background Load Generator
=========================
Generates configurable CPU, I/O, or Memory pressure as a long-lived
background process.  Launched by benchmark.py; can also be used standalone.

Usage
-----
  python3 load_generator.py --type cpu     --intensity 75 --duration 60
  python3 load_generator.py --type io      --intensity 50 --duration 60
  python3 load_generator.py --type memory  --intensity 75 --duration 60
  python3 load_generator.py --type mixed   --intensity 50 --duration 60

Types
-----
  cpu    – Arithmetic busy-loop with duty-cycle control (intensity = % busy)
  io     – Repeated file create/write/read/delete cycle
  memory – Large array allocation + strided sequential access (cache eviction)
  mixed  – CPU + memory pressure combined (two threads)
"""

import argparse
import array
import math
import os
import sys
import tempfile
import threading
import time

# ── Shared signal for clean shutdown ─────────────────────────────────────────
_stop = threading.Event()


# ═══════════════════════════════════════════════════════════════════════════════
# CPU Load
# ═══════════════════════════════════════════════════════════════════════════════
def cpu_load(intensity: int, duration: float):
    """
    Busy-loop with duty-cycle control.

    The thread alternates between a compute phase (arithmetic on floats to
    defeat constant folding) and a sleep phase so that:

        busy_fraction = intensity / 100

    At 100 % intensity the loop runs continuously; at 0 % the thread sleeps.
    """
    if intensity <= 0:
        _stop.wait(timeout=duration)
        return

    work_frac  = intensity / 100.0
    sleep_frac = 1.0 - work_frac
    CHUNK      = 0.01        # 10 ms scheduling granularity

    deadline = time.monotonic() + duration
    x = 1.0

    while time.monotonic() < deadline and not _stop.is_set():
        t0 = time.monotonic()
        # Busy phase — prevent compiler-level optimisation via floating ops
        while time.monotonic() - t0 < CHUNK * work_frac:
            x = math.sin(x + 0.001) * math.cos(x - 0.001) + x * 1.0000001
        # Idle phase — yield CPU
        if sleep_frac > 0:
            _stop.wait(timeout=CHUNK * sleep_frac)

    # Prevent the variable from being optimised away
    if x == 999_999_999.0:
        print("unreachable", file=sys.stderr)


# ═══════════════════════════════════════════════════════════════════════════════
# I/O Load
# ═══════════════════════════════════════════════════════════════════════════════
def io_load(intensity: int, duration: float):
    """
    Repeated small file write + fsync + read loop.

    Block size scales with intensity (1–128 KB):
      block_kb = max(1, round(128 * intensity / 100))

    This stresses the VFS layer, page cache, and dentry/inode caches
    without saturating disk I/O, analogous to the kind of I/O interference
    a real workload exerts on scheduling latency.
    """
    if intensity <= 0:
        _stop.wait(timeout=duration)
        return

    block_kb = max(1, round(128 * intensity / 100))
    data     = os.urandom(block_kb * 1024)
    pause    = max(0.005, 0.10 * (1.0 - intensity / 100.0))
    deadline = time.monotonic() + duration

    with tempfile.TemporaryDirectory(prefix="ssp_io_") as tmpdir:
        fpath = os.path.join(tmpdir, "ioload.bin")
        while time.monotonic() < deadline and not _stop.is_set():
            try:
                with open(fpath, "wb") as f:
                    f.write(data)
                    f.flush()
                    os.fsync(f.fileno())
                with open(fpath, "rb") as f:
                    _ = f.read()
            except OSError:
                pass
            _stop.wait(timeout=pause)


# ═══════════════════════════════════════════════════════════════════════════════
# Memory Load
# ═══════════════════════════════════════════════════════════════════════════════
def memory_load(intensity: int, duration: float):
    """
    Allocates a working-set proportional to intensity (up to ~512 MB) and
    repeatedly traverses it with a stride equal to a cache line (64 B).

    The strided access pattern forces L1/L2/LLC misses and raises DRAM
    bandwidth, which directly competes with the cache footprint of the
    benchmarked processes — a key source of indirect context-switch overhead.

    Array size:  size_mb = max(8, round(512 * intensity / 100))
    """
    if intensity <= 0:
        _stop.wait(timeout=duration)
        return

    size_mb   = max(8, round(512 * intensity / 100))
    n_doubles = (size_mb * 1024 * 1024) // 8
    STRIDE    = 8    # 8 doubles × 8 bytes = 64 B = 1 cache line

    try:
        buf = array.array('d', bytes(n_doubles * 8))
    except MemoryError:
        buf = array.array('d', bytes((n_doubles // 4) * 8))

    n = len(buf)
    deadline = time.monotonic() + duration
    acc  = 0.0
    idx  = 0

    while time.monotonic() < deadline and not _stop.is_set():
        # One "pass" = 100 000 strided reads over the buffer
        for _ in range(100_000):
            acc += buf[idx]
            buf[idx] = acc * 0.000_000_001   # write-back to keep memory dirty
            idx = (idx + STRIDE) % n
        _stop.wait(timeout=0.001)   # brief yield so OS can schedule other tasks

    # Prevent dead-code elimination
    if acc == -999_999_999.0:
        print("unreachable", file=sys.stderr)


# ═══════════════════════════════════════════════════════════════════════════════
# Mixed Load  (CPU + Memory)
# ═══════════════════════════════════════════════════════════════════════════════
def mixed_load(intensity: int, duration: float):
    """Run cpu_load and memory_load concurrently on two threads."""
    t_cpu = threading.Thread(
        target=cpu_load, args=(intensity, duration), daemon=True
    )
    t_mem = threading.Thread(
        target=memory_load, args=(intensity, duration), daemon=True
    )
    t_cpu.start()
    t_mem.start()
    t_cpu.join()
    t_mem.join()


# ── Dispatch table ────────────────────────────────────────────────────────────
LOAD_FUNCTIONS = {
    "cpu":    cpu_load,
    "io":     io_load,
    "memory": memory_load,
    "mixed":  mixed_load,
}


# ═══════════════════════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════════════════════
def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Background load generator for SSP benchmark suite",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--type", choices=list(LOAD_FUNCTIONS),
                   required=True, help="Load type")
    p.add_argument("--intensity", type=int, default=50,
                   help="Load intensity 0-100 %% (default: 50)")
    p.add_argument("--duration", type=float, default=60.0,
                   help="Run for this many seconds (default: 60)")
    return p.parse_args()


def main():
    import signal

    args = parse_args()
    intensity = max(0, min(100, args.intensity))

    # Clean shutdown on SIGTERM / SIGINT
    def _handler(sig, frame):
        _stop.set()

    signal.signal(signal.SIGTERM, _handler)
    signal.signal(signal.SIGINT,  _handler)

    fn = LOAD_FUNCTIONS[args.type]
    fn(intensity, args.duration)


if __name__ == "__main__":
    main()
