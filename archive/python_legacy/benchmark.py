#!/usr/bin/env python3
"""
SSP Cross-Platform Context Switch & Workload Scaling Benchmark
==============================================================

Measures context-switch latency under varied workload conditions and
different active-core counts.  Uses lmbench lat_ctx on Linux when
available; falls back to a Python thread-ring measurement on Windows
(or when lmbench is absent).

Usage
-----
  python3 benchmark.py [options]

Key options
-----------
  --cores        1,2,4         Comma-separated list of core counts to test
  --workloads    cpu,io,memory Workload types (baseline | cpu | io | memory | mixed)
  --intensities  25,50,75,100  Workload intensities in percent
  --proc-counts  2,4,8,16,32  Process/thread counts fed to lat_ctx
  --data-sizes   0,16,64      Context-switch data sizes in KB (lat_ctx -s)
  --iterations   3            Repetitions per configuration (median taken)
  --output       <path>       CSV file for results
  --quick                     Abbreviated run for testing
  --verbose                   Show raw tool output
"""

import argparse
import csv
import math
import multiprocessing
import os
import platform
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ── Project layout ────────────────────────────────────────────────────────────
SCRIPT_DIR  = Path(__file__).parent.resolve()
PROJECT_DIR = SCRIPT_DIR.parent
RESULTS_DIR = PROJECT_DIR / "results"
PLOTS_DIR   = PROJECT_DIR / "plots"
LOAD_GEN    = SCRIPT_DIR / "load_generator.py"
LMBENCH_BIN = "/usr/lib/lmbench/bin/x86_64-linux-gnu"

# ── Benchmark defaults ────────────────────────────────────────────────────────
DEFAULT_PROC_COUNTS  = [2, 4, 8, 16, 32, 64]
DEFAULT_DATA_SIZES   = [0, 16, 64]          # KB
DEFAULT_INTENSITIES  = [25, 50, 75, 100]    # %  (0 handled as baseline)
DEFAULT_WORKLOADS    = ["baseline", "cpu", "io", "memory", "mixed"]
WARMUP_SEC           = 1.5   # let background load settle before measuring
COOLDOWN_SEC         = 0.8   # pause between back-to-back lat_ctx calls

CSV_COLUMNS = [
    "timestamp", "os", "hostname", "total_cores", "active_cores",
    "workload_type", "intensity_pct", "data_size_kb", "num_procs",
    "latency_us", "std_us", "method", "notes",
]


# ═══════════════════════════════════════════════════════════════════════════════
# 1.  System Detection
# ═══════════════════════════════════════════════════════════════════════════════
class SystemInfo:
    """Detects OS, CPU count, and available benchmarking tools."""

    def __init__(self):
        self.os_name     = platform.system()   # 'Linux' | 'Windows' | 'Darwin'
        self.hostname    = platform.node()
        self.total_cores = multiprocessing.cpu_count()
        self.has_lmbench = self._probe_lmbench()
        self.has_taskset = self._probe_taskset()
        self.has_psutil  = self._probe_psutil()
        self.python_ring_baseline = None  # calibrated later

    # ── Probes ─────────────────────────────────────────────────────────────────
    def _probe_lmbench(self) -> bool:
        if self.os_name != "Linux":
            return False
        lat_ctx = Path(LMBENCH_BIN) / "lat_ctx"
        return lat_ctx.exists() and os.access(lat_ctx, os.X_OK)

    def _probe_taskset(self) -> bool:
        if self.os_name != "Linux":
            return False
        return subprocess.run(
            ["which", "taskset"], capture_output=True
        ).returncode == 0

    def _probe_psutil(self) -> bool:
        try:
            import psutil  # noqa: F401
            return True
        except ImportError:
            return False

    # ── Core config helpers ────────────────────────────────────────────────────
    def resolve_core_list(self, requested: List[int]) -> List[int]:
        """Return a valid, sorted, de-duplicated list of core counts."""
        n = self.total_cores
        if not requested:
            candidates = sorted({1, 2, min(4, n), n})
        else:
            candidates = requested
        return sorted({c for c in candidates if 1 <= c <= n})

    # ── Summary ───────────────────────────────────────────────────────────────
    def summary(self) -> str:
        lm = "lmbench" if self.has_lmbench else "python-ring"
        return (
            f"OS={self.os_name}  Host={self.hostname}  "
            f"Cores={self.total_cores}  Method={lm}"
        )


# ═══════════════════════════════════════════════════════════════════════════════
# 2.  Core Affinity Manager
# ═══════════════════════════════════════════════════════════════════════════════
class CoreManager:
    """Sets CPU affinity for the current process and builds taskset prefixes."""

    def __init__(self, sysinfo: SystemInfo):
        self.sysinfo = sysinfo

    def set_affinity(self, core_count: int) -> bool:
        """Pin THIS process to the first `core_count` logical CPUs."""
        cores = list(range(core_count))
        if self.sysinfo.has_psutil:
            try:
                import psutil
                psutil.Process().cpu_affinity(cores)
                return True
            except Exception:
                pass
        # Linux fallback – re-exec under taskset is too disruptive;
        # we rely on taskset in subprocess for lat_ctx instead.
        return False

    def taskset_prefix(self, core_count: int) -> List[str]:
        """Return a taskset argv prefix (Linux).  Empty list on other OSes."""
        if not self.sysinfo.has_taskset:
            return []
        cpu_list = ",".join(str(i) for i in range(core_count))
        return ["taskset", "-c", cpu_list]


# ═══════════════════════════════════════════════════════════════════════════════
# 3.  Context-Switch Measurement
# ═══════════════════════════════════════════════════════════════════════════════
class ContextSwitchBenchmark:
    """
    Measures context-switch latency.

    Strategy
    --------
    - Linux + lmbench present  → use lat_ctx (gold standard)
    - Otherwise               → Python thread-ring (portable fallback)
    """

    def __init__(self, sysinfo: SystemInfo, cmgr: CoreManager,
                 verbose: bool = False):
        self.sysinfo = sysinfo
        self.cmgr    = cmgr
        self.verbose = verbose

    # ── lmbench backend ────────────────────────────────────────────────────────
    def _lmbench(self, core_count: int,
                 data_size_kb: int, num_procs: int) -> Optional[float]:
        import re
        lat_ctx = str(Path(LMBENCH_BIN) / "lat_ctx")
        prefix  = self.cmgr.taskset_prefix(core_count)
        cmd     = prefix + [lat_ctx, "-s", str(data_size_kb), str(num_procs)]

        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
            out = r.stdout + r.stderr
            if self.verbose:
                print(f"          [lat_ctx raw] {out.strip()!r}")

            # lat_ctx output format: "<num_procs> <latency_us>\n..."
            # Match: one-or-more digits, whitespace, float
            matches = re.findall(r'^\s*\d+\s+([\d.]+)', out, re.MULTILINE)
            if matches:
                return float(matches[-1])

            # Fallback: take the last float on any line
            floats = re.findall(r'(?<!\w)([\d]+\.[\d]+)', out)
            return float(floats[-1]) if floats else None

        except (subprocess.TimeoutExpired, subprocess.SubprocessError,
                ValueError, IndexError) as exc:
            if self.verbose:
                print(f"          [lat_ctx error] {exc}")
            return None

    # ── Python thread-ring backend ─────────────────────────────────────────────
    @staticmethod
    def _python_ring(n_threads: int, iterations: int = 3000) -> float:
        """
        Thread-ring token-passing benchmark.

        N threads are arranged in a ring.  Thread i waits on Event[i],
        clears it, then sets Event[(i+1) % N].  We time how long it takes
        for the token to complete `iterations` full laps.

          latency = total_time / (N * iterations)   [microseconds]

        This correlates well with kernel thread context-switch cost.
        """
        events = [threading.Event() for _ in range(n_threads)]
        error  = threading.Event()

        def worker(idx: int):
            my_ev   = events[idx]
            next_ev = events[(idx + 1) % n_threads]
            try:
                for _ in range(iterations):
                    my_ev.wait(timeout=30)
                    my_ev.clear()
                    next_ev.set()
            except Exception:
                error.set()

        threads = [
            threading.Thread(target=worker, args=(i,), daemon=True)
            for i in range(n_threads)
        ]
        for t in threads:
            t.start()

        t0 = time.perf_counter()
        events[0].set()
        for t in threads:
            t.join(timeout=60)
        elapsed = time.perf_counter() - t0

        if error.is_set():
            return -1.0

        total_switches = n_threads * iterations
        return (elapsed / total_switches) * 1_000_000   # → microseconds

    # ── Statistics helper ──────────────────────────────────────────────────────
    @staticmethod
    def _stats(samples: List[float]) -> Tuple[float, float]:
        """Return (mean, std) of `samples`, or (-1, 0) on empty input."""
        if not samples:
            return -1.0, 0.0
        n    = len(samples)
        mean = sum(samples) / n
        std  = math.sqrt(sum((x - mean) ** 2 for x in samples) / max(n - 1, 1))
        return round(mean, 4), round(std, 4)

    # ── Public measurement API ─────────────────────────────────────────────────
    def measure(self, core_count: int, data_size_kb: int, num_procs: int,
                repeat: int = 3) -> Tuple[float, float, str]:
        """
        Run `repeat` measurements, drop the worst outlier, return
        (mean_us, std_us, method).
        """
        samples: List[float] = []
        method = "lmbench" if self.sysinfo.has_lmbench else "python-ring"

        for _ in range(repeat):
            if self.sysinfo.has_lmbench:
                val = self._lmbench(core_count, data_size_kb, num_procs)
            else:
                val = self._python_ring(num_procs)

            if val is not None and val > 0:
                samples.append(val)
            time.sleep(COOLDOWN_SEC)

        # Drop highest outlier if we have ≥ 3 samples
        if len(samples) >= 3:
            samples.remove(max(samples))

        mean, std = self._stats(samples)
        return mean, std, method


# ═══════════════════════════════════════════════════════════════════════════════
# 4.  Background Load Generator Manager
# ═══════════════════════════════════════════════════════════════════════════════
class LoadGeneratorManager:
    """
    Spawns background load_generator.py subprocesses.
    Acts as a context manager for automatic cleanup.
    """

    def __init__(self, sysinfo: SystemInfo, verbose: bool = False):
        self.sysinfo = sysinfo
        self.verbose = verbose
        self._procs: List[subprocess.Popen] = []

    def start(self, workload_type: str, intensity_pct: int) -> "LoadGeneratorManager":
        """Launch one load-generator worker per physical core (half-core count)."""
        if workload_type == "baseline" or intensity_pct == 0:
            return self   # no load for baseline

        n_workers = max(1, self.sysinfo.total_cores // 2)
        cmd = [
            sys.executable, str(LOAD_GEN),
            "--type",      workload_type,
            "--intensity", str(intensity_pct),
            "--duration",  "7200",   # long-lived; killed explicitly
        ]
        kwargs: Dict = dict(stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if hasattr(os, "setpgrp"):
            kwargs["preexec_fn"] = os.setpgrp

        for _ in range(n_workers):
            p = subprocess.Popen(cmd, **kwargs)
            self._procs.append(p)

        if self.verbose:
            pids = [p.pid for p in self._procs]
            print(f"        [load] {n_workers}x {workload_type}@{intensity_pct}%  PIDs={pids}")

        time.sleep(WARMUP_SEC)   # let load settle
        return self

    def stop(self):
        """Terminate all background load processes and wait for exit."""
        for p in self._procs:
            try:
                p.terminate()
                p.wait(timeout=5)
            except Exception:
                try:
                    p.kill()
                    p.wait(timeout=2)
                except Exception:
                    pass
        self._procs.clear()
        time.sleep(COOLDOWN_SEC)

    # Context-manager protocol
    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.stop()


# ═══════════════════════════════════════════════════════════════════════════════
# 5.  CSV Result Writer
# ═══════════════════════════════════════════════════════════════════════════════
class ResultWriter:
    """Appends rows to a CSV file, writing the header on first creation."""

    def __init__(self, path: Path):
        self.path = path
        path.parent.mkdir(parents=True, exist_ok=True)
        need_header = not path.exists()
        self._file = open(path, "a", newline="", encoding="utf-8")
        self._csv  = csv.DictWriter(self._file, fieldnames=CSV_COLUMNS,
                                    extrasaction="ignore")
        if need_header:
            self._csv.writeheader()

    def write(self, row: Dict):
        row.setdefault("timestamp", datetime.now().isoformat(timespec="seconds"))
        self._csv.writerow(row)
        self._file.flush()

    def close(self):
        self._file.close()


# ═══════════════════════════════════════════════════════════════════════════════
# 6.  Main Orchestrator
# ═══════════════════════════════════════════════════════════════════════════════
class BenchmarkOrchestrator:
    """
    Outer loop: core_count × workload × intensity × data_size × proc_count.

    For each combination it:
      1. Sets CPU affinity
      2. Starts the background load
      3. Calls ContextSwitchBenchmark.measure() `repeat` times
      4. Writes a row to the CSV
    """

    def __init__(self, args: argparse.Namespace, sysinfo: SystemInfo):
        self.args        = args
        self.sysinfo     = sysinfo
        self.cmgr        = CoreManager(sysinfo)
        self.bench       = ContextSwitchBenchmark(sysinfo, self.cmgr, args.verbose)
        self.writer      = ResultWriter(Path(args.output))
        self.core_list   = sysinfo.resolve_core_list(args.cores)
        self.workloads   = args.workloads
        self.intensities = args.intensities
        self.proc_counts = args.proc_counts
        self.data_sizes  = args.data_sizes
        self.repeat      = args.iterations

    # ── Totals ────────────────────────────────────────────────────────────────
    def _estimate_configs(self) -> int:
        n = 0
        for w in self.workloads:
            if w == "baseline":
                n += len(self.core_list) * len(self.data_sizes) * len(self.proc_counts)
            else:
                n += (len(self.core_list) * len(self.intensities)
                      * len(self.data_sizes) * len(self.proc_counts))
        return n

    # ── Main run ──────────────────────────────────────────────────────────────
    def run(self):
        total_configs = self._estimate_configs()
        done = 0

        print(f"\n{'═' * 62}")
        print(f"  SSP Context Switch Benchmark — {self.sysinfo.summary()}")
        print(f"{'═' * 62}")
        print(f"  Core configs   : {self.core_list}")
        print(f"  Workloads      : {self.workloads}")
        print(f"  Intensities    : {self.intensities} %")
        print(f"  Process counts : {self.proc_counts}")
        print(f"  Data sizes     : {self.data_sizes} KB")
        print(f"  Repeat/config  : {self.repeat}")
        print(f"  Total configs  : {total_configs}")
        print(f"  Output CSV     : {self.args.output}")
        print(f"{'═' * 62}\n")

        for n_cores in self.core_list:
            self.cmgr.set_affinity(n_cores)
            print(f"▶  Active cores = {n_cores}/{self.sysinfo.total_cores}")

            for workload in self.workloads:

                # Determine intensity list for this workload
                if workload == "baseline":
                    iter_intensities = [0]
                else:
                    iter_intensities = self.intensities

                for intensity in iter_intensities:
                    label = f"{workload}@{intensity}%"
                    print(f"   ├─ {label}")

                    with LoadGeneratorManager(self.sysinfo, self.args.verbose) as lg:
                        lg.start(workload, intensity)

                        for ds in self.data_sizes:
                            for np_ in self.proc_counts:
                                mean_us, std_us, method = self.bench.measure(
                                    n_cores, ds, np_, self.repeat
                                )
                                row = {
                                    "os":            self.sysinfo.os_name,
                                    "hostname":      self.sysinfo.hostname,
                                    "total_cores":   self.sysinfo.total_cores,
                                    "active_cores":  n_cores,
                                    "workload_type": workload,
                                    "intensity_pct": intensity,
                                    "data_size_kb":  ds,
                                    "num_procs":     np_,
                                    "latency_us":    mean_us,
                                    "std_us":        std_us,
                                    "method":        method,
                                    "notes":         "",
                                }
                                self.writer.write(row)

                                ok = "✓" if mean_us > 0 else "✗"
                                print(
                                    f"   │   [{ok}] ds={ds:2d}KB  procs={np_:3d}"
                                    f"  → {mean_us:8.3f} μs  (±{std_us:.3f})"
                                )
                                done += 1

            print(f"   └─ Cores={n_cores} done.  ({done}/{total_configs})\n")

        self.writer.close()
        print(f"\n{'═' * 62}")
        print(f"  ✔  All {done} measurements saved.")
        print(f"     Results : {self.args.output}")
        print(f"     Plots   : python3 plot_results.py --input {self.args.output}")
        print(f"{'═' * 62}\n")


# ═══════════════════════════════════════════════════════════════════════════════
# 7.  CLI
# ═══════════════════════════════════════════════════════════════════════════════
def _int_list(s: str) -> List[int]:
    return [int(x.strip()) for x in s.split(",")]

def _str_list(s: str) -> List[str]:
    return [x.strip() for x in s.split(",")]

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Cross-platform context-switch benchmark with workload & core scaling",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--cores", type=_int_list, default=[],
                   metavar="N,N,...",
                   help="Core counts (default: auto = 1,2,4,max)")
    p.add_argument("--workloads", type=_str_list,
                   default=DEFAULT_WORKLOADS,
                   metavar="TYPE,...",
                   help="Workload types: baseline,cpu,io,memory,mixed")
    p.add_argument("--intensities", type=_int_list,
                   default=DEFAULT_INTENSITIES,
                   metavar="PCT,...",
                   help="Workload intensities in %% (default: 25,50,75,100)")
    p.add_argument("--proc-counts", type=_int_list,
                   default=DEFAULT_PROC_COUNTS,
                   metavar="N,...",
                   help="Process counts (default: 2,4,8,16,32,64)")
    p.add_argument("--data-sizes", type=_int_list,
                   default=DEFAULT_DATA_SIZES,
                   metavar="KB,...",
                   help="Context data sizes in KB (default: 0,16,64)")
    p.add_argument("--iterations", type=int, default=3,
                   help="Repetitions per configuration (default: 3)")
    p.add_argument("--output",
                   default=str(RESULTS_DIR / "workload_benchmark.csv"),
                   help="Output CSV path")
    p.add_argument("--verbose", action="store_true",
                   help="Print raw tool output")
    p.add_argument("--quick", action="store_true",
                   help="Abbreviated run (fewer configs) for quick testing")
    return p.parse_args()


def main():
    args = parse_args()

    # Quick mode reduces configuration space for fast testing
    if args.quick:
        args.proc_counts  = [2, 8, 32]
        args.data_sizes   = [0]
        args.intensities  = [25, 75]
        args.workloads    = ["baseline", "cpu", "memory"]
        args.iterations   = 2
        print("  [quick mode] Running abbreviated benchmark set.")

    sysinfo = SystemInfo()
    print(f"  System: {sysinfo.summary()}")

    if not sysinfo.has_lmbench:
        print("\n  ⚠  lmbench (lat_ctx) not found — using Python thread-ring fallback.")
        print("     To use lmbench: sudo apt-get install -y lmbench\n")

    if not LOAD_GEN.exists():
        print(f"  ✗  load_generator.py not found at {LOAD_GEN}")
        sys.exit(1)

    orchestrator = BenchmarkOrchestrator(args, sysinfo)
    try:
        orchestrator.run()
    except KeyboardInterrupt:
        print("\n\n  ⚠  Interrupted.  Partial results saved.")
        orchestrator.writer.close()
        sys.exit(0)


if __name__ == "__main__":
    main()
