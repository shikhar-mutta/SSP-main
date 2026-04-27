# 🔬 Microarchitectural and Scaling Analysis of Context Switching and Scheduling in Multicore Systems

> **Final Semester Project — System Software Performance (SSP)**
> Intel Core i5-1035G1 · Linux · lmbench + Custom C Benchmarks

---

## 📋 Table of Contents

- [Project Overview](#-project-overview)
- [System Requirements](#-system-requirements)
- [Project Structure](#-project-structure)
- [Novel Contributions](#-novel-contributions)
- [Quick Start](#-quick-start)
- [How to Install & Run lmbench](#-how-to-install--run-lmbench)
- [Custom Benchmark Programs](#-custom-benchmark-programs)
- [Running Individual Phases](#-running-individual-phases)
- [Generating Plots](#-generating-plots)
- [Compiling the LaTeX Report](#-compiling-the-latex-report)
- [Key Results Summary](#-key-results-summary)
- [Research Papers Referenced](#-research-papers-referenced)
- [Viva Q&A Cheatsheet](#-viva-qa-cheatsheet)

---

## 🎯 Project Overview

This project performs a **systematic microarchitectural and scaling analysis** of **context-switch latency** on a multicore processor.

### What We Measure

| Experiment | Tool | Goal |
|-----------|------|------|
| Context-switch latency baseline | `lmbench lat_ctx` | Establish reference measurements |
| Working-set vs latency (cache effect) | Custom C | Model cache-hierarchy impact |
| HT-sibling vs cross-core switch cost | Custom C | Quantify core migration penalty |
| Process count scaling (2 → 16) | Custom C | Show scheduler overhead growth |
| Scheduler policy comparison | Custom C | CFS vs FIFO vs RR vs BATCH vs IDLE |
| Process vs thread switch cost | Custom C | Isolate TLB-flush overhead |
| Raw syscall overhead | Custom C | Establish scheduling baseline |

### Test Platform

```
CPU    : Intel Core i5-1035G1 (Ice Lake)
Cores  : 4 physical / 8 logical (HyperThreaded)
L1d    : 48 KB per core
L2     : 512 KB per core
L3     : 6 MB shared
RAM    : 24 GB DDR4
OS     : Ubuntu 22.04 / Linux kernel 6.x
```

---

## 🖥️ System Requirements

```bash
# Build tools
sudo apt update
sudo apt install -y build-essential git python3-pip

# Python plotting dependencies
pip3 install matplotlib numpy pandas scipy

# LaTeX (for report compilation)
sudo apt install -y texlive-full

# Optional: perf for hardware counter support
sudo apt install -y linux-tools-common linux-tools-$(uname -r)
```

---

## 📁 Project Structure

```
SSP-main/
│
├── 📄 Makefile                        ← Build + run orchestrator
│
├── 📂 src/                            ← All C benchmark programs
│   ├── ctx_switch_bench.c             ← General context-switch bench (RDTSC + histogram)
│   ├── cache_pollution_model.c        ← Cache pollution sweep (NOVELTY 1)
│   ├── core_migration_penalty.c       ← HT-sibling vs cross-core (NOVELTY 2)
│   ├── sched_policy_bench.c           ← Scheduler policy comparison (NOVELTY 3)
│   ├── process_vs_thread.c            ← Process vs thread TLB cost (NOVELTY 4)
│   └── syscall_overhead.c             ← Raw syscall baseline
│
├── 📂 bin/                            ← Compiled binaries (auto-generated)
│
├── 📂 scripts/
│   ├── run_all.sh                     ← Master experiment runner
│   └── phase1_lmbench_baseline.sh     ← lmbench setup + lat_ctx sweep
│
├── 📂 analysis/
│   └── plot_all.py                    ← Generates all 9 publication figures
│
├── 📂 results/                        ← CSV outputs from all experiments
│
├── 📂 report/
│   ├── main.tex                       ← Full IEEE two-column LaTeX paper
│   └── figures/                       ← PDF figures (auto-generated)
│
└── 📂 lmbench/                        ← Cloned by phase1 script (auto)
```

---

## 🚀 Novel Contributions

This project goes beyond a plain lmbench run — here are the **4 novel contributions**:

### 💡 Novelty 1 — Cache Pollution Model

> **"Context switch cost follows a staircase pattern matching the cache hierarchy."**

- Sweeps working-set size from 0 KB → 16 GB across **25 data points**
- Identifies distinct latency "steps" at L1 (48 KB), L2 (512 KB), and L3 (6 MB) boundaries
- Fits a **piecewise log-linear model**: `L(w) = a + b·log₂(w/boundary)`
- Shows p99 variance explodes in the DRAM region (>12 µs IQR)

```
Latency staircase observed:
  L1-resident (≤48 KB)   : ~2.8 µs  (low variance)
  L2-resident (≤512 KB)  : ~5.6 µs  (+100%)
  L3-resident (≤6 MB)    : ~9.2 µs  (+65%)
  DRAM (>6 MB)           : ~22.4 µs (high jitter)
```

---

### 💡 Novelty 2 — HyperThread-Aware Core Migration Penalty

> **"Cross-physical-core switches are 2× costlier than HT-sibling switches."**

- Compares **3 CPU affinity scenarios** using `sched_setaffinity()`:
  - Same physical core (HT siblings: CPU 0 & 1)
  - Different physical cores (CPU 0 & 2)
  - Free scheduling (no pinning)
- Cross-core penalty comes from: L1/L2 cache refill + TLB shootdown + cache-line coherence transfer

```
Same Core (HT)   : 2.4 µs  ← 1.0× baseline
Cross Core       : 4.9 µs  ← 2.0× penalty
Free (OS picks)  : 3.6 µs  ← 1.5× (random migration)
```

---

### 💡 Novelty 3 — Scheduler Fairness vs Latency Trade-off

> **"Real-time policies reduce context-switch latency by 38% vs CFS."**

- Tests all 5 Linux scheduling classes under identical conditions:
  - `SCHED_OTHER` (CFS default)
  - `SCHED_BATCH` (throughput-optimised)
  - `SCHED_IDLE`  (lowest priority)
  - `SCHED_FIFO`  (real-time, O(1))
  - `SCHED_RR`    (real-time, round-robin)

```
SCHED_FIFO (RT)  : 2.1 µs  ← fastest, O(1) runqueue
SCHED_OTHER(CFS) : 3.4 µs  ← default
SCHED_IDLE       : 8.7 µs  ← highest latency, huge jitter
```

---

### 💡 Novelty 4 — Quantified TLB-Flush Cost

> **"Thread switches are ~1.3 µs cheaper than process switches — the TLB flush cost."**

- Process switches reload CR3 → full TLB invalidation
- Thread switches within same process → CR3 unchanged, TLB intact
- Measured via:
  - **Pipe ping-pong** (process version)
  - **Futex/condvar ping-pong** (thread version)

---

## ⚡ Quick Start

```bash
# Clone (if not already done)
git clone <your-repo-url>
cd SSP-main

# Build all C benchmarks
make all

# Run everything end-to-end
make run          # ~30 min total

# Generate plots
make plots

# Compile PDF report
make report
```

---

## 🔧 How to Install & Run lmbench

### Step 1: Clone and Build

```bash
# The script does this automatically, but you can also do it manually:
git clone https://github.com/intel/lmbench.git
cd lmbench
make OS=x86_64-linux-gnu CFLAGS="-O2"
```

> ⚠️ lmbench's build system is old. If `make results` asks interactive questions, use `Ctrl+C` and run the binaries directly instead.

### Step 2: Find lat_ctx binary

```bash
find lmbench/bin -name "lat_ctx"
# Usually: lmbench/bin/x86_64-linux-gnu/lat_ctx
```

### Step 3: Run lat_ctx manually

```bash
LAT_CTX=./lmbench/bin/x86_64-linux-gnu/lat_ctx

# Basic: 2 processes, no working set
$LAT_CTX -s 0 2

# With cache pollution (64 KB working set, 4 processes)
$LAT_CTX -s 64 4

# Full sweep (run by our script automatically)
for WS in 0 4 16 48 64 256 512 1024 4096 8192; do
  for NP in 2 4 8 16 32; do
    echo -n "WS=${WS}KB NP=${NP}: "
    $LAT_CTX -s $WS $NP
  done
done
```

### Step 4: Other useful lmbench tools

```bash
# Process creation latency
./lmbench/bin/x86_64-linux-gnu/lat_proc fork
./lmbench/bin/x86_64-linux-gnu/lat_proc exec

# System call latency
./lmbench/bin/x86_64-linux-gnu/lat_syscall null
./lmbench/bin/x86_64-linux-gnu/lat_syscall read
./lmbench/bin/x86_64-linux-gnu/lat_syscall write

# Memory latency (for NUMA analysis)
./lmbench/bin/x86_64-linux-gnu/lat_mem_rd 128m 128
```

---

## 🧪 Optional Experiments Added to This Project

The project now includes extra wrappers around the existing benchmarks so you can
extend the report with optional NUMA and tracing studies without changing the core
benchmarks.

```bash
# NUMA local vs remote memory placement (requires numactl)
make numa

# sched_switch tracing while a benchmark runs (requires bpftrace + sudo)
make ebpf

# Compare a local and cloud CSV export if you have both files
make compare
```

Expected input files for the comparison helper:

- `results/local_results.csv`
- `results/cloud_results.csv`

The comparison helper writes `results/local_vs_cloud_summary.csv`.

### ⚙️ Fix CPU Frequency Scaling (IMPORTANT before experiments)

```bash
# Set performance governor to prevent frequency throttling
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Verify
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
```

---

## 🧪 Custom Benchmark Programs

### `ctx_switch_bench` — General Context-Switch Benchmark

```bash
# Basic run: 2 processes, 0 KB working set, 10000 iterations
./bin/ctx_switch_bench -n 2 -s 0 -i 10000

# Pinned to CPU 0, with 64 KB working set
./bin/ctx_switch_bench -n 2 -s 64 -c 0 -o results/custom_ctx.csv

# Options:
#   -n <procs>   number of processes (2–64)
#   -s <kb>      working-set size in KB
#   -i <iters>   number of measured iterations
#   -c <cpu>     pin to this CPU core
#   -o <file>    save raw CSV output
```

### `cache_pollution_model` — Cache Staircase

```bash
./bin/cache_pollution_model -o results/cache_pollution.csv
# Sweeps 25 working-set sizes automatically
# Takes ~10 minutes
```

### `core_migration_penalty` — Affinity Analysis

```bash
./bin/core_migration_penalty -o results/core_migration.csv
# Runs all scenarios: HT-siblings, cross-core, free, + scaling 2→16
```

### `sched_policy_bench` — Scheduler Comparison

```bash
# Without real-time (no sudo needed)
./bin/sched_policy_bench -o results/sched_policy.csv

# With real-time policies (requires sudo)
sudo ./bin/sched_policy_bench -o results/sched_policy.csv
```

### `process_vs_thread` — TLB Flush Cost

```bash
./bin/process_vs_thread -o results/proc_vs_thread.csv
```

### `syscall_overhead` — Syscall Baseline

```bash
./bin/syscall_overhead -o results/syscall_overhead.csv
```

---

## 🏃 Running Individual Phases

```bash
make phase1   # lmbench baseline (requires internet for first clone)
make phase2   # cache pollution sweep
make phase3   # core migration + scaling
make phase4   # scheduler policies (sudo for RT)
make phase5   # process vs thread
make phase6   # syscall overhead
```

---

## 📊 Generating Plots

```bash
# Install Python dependencies
pip3 install matplotlib numpy pandas scipy

# Generate all 9 figures
make plots
# OR directly:
python3 analysis/plot_all.py
```

Figures saved to `report/figures/`:

| Figure | Description |
|--------|-------------|
| `fig1_lmbench_scaling.pdf` | lmbench: processes vs latency |
| `fig2_lmbench_ws.pdf` | lmbench: working-set vs latency |
| `fig3_cache_pollution.pdf` | Cache pollution model with region shading |
| `fig4_cache_regions.pdf` | Box-plot per cache region |
| `fig5_core_migration.pdf` | Core migration penalty bar chart |
| `fig6_scaling.pdf` | Process-count scaling curve |
| `fig7_sched_policy.pdf` | Scheduler policy comparison |
| `fig8_proc_vs_thread.pdf` | Process vs thread switch cost |
| `fig9_syscall_overhead.pdf` | Syscall overhead baseline |

---

## 📝 Compiling the LaTeX Report

```bash
# Install LaTeX (once)
sudo apt install -y texlive-full

# Compile (3 passes needed for cross-references)
make report
# OR manually:
cd report
pdflatex main.tex
bibtex main
pdflatex main.tex
pdflatex main.tex

# Output: report/main.pdf
```

---

## 📈 Key Results Summary

| Metric | Value | Condition |
|--------|-------|-----------|
| Min context-switch latency | ~2.1 µs | SCHED_FIFO, 2 threads, WS=0 |
| Typical (CFS) latency | ~3.4 µs | 2 processes, WS=0 |
| Max latency (DRAM) | ~35 µs | large working set |
| HT-sibling switch | ~2.4 µs | pinned to same physical core |
| Cross-core switch | ~4.9 µs | 2× penalty |
| TLB-flush cost | ~1.3 µs | process − thread latency |
| Syscall ring cost | ~250 ns | getpid() baseline |
| Scheduler overhead | ~3.15 µs | ctx_switch − syscall cost |

---

## 📄 Research Papers Referenced

| # | Paper | Link |
|---|-------|------|
| 1 | **lmbench: Portable Tools for Performance Analysis** — McVoy & Staelin, USENIX 1996 | [PDF](https://www.usenix.org/legacy/publications/library/proceedings/usenix99/full_papers/mcvoy/mcvoy.pdf) |
| 2 | **An Analysis of Linux Scalability to Many Cores** — Boyd-Wickizer et al., OSDI 2010 | [PDF](https://www.usenix.org/legacy/event/osdi10/tech/full_papers/Boyd-Wickizer.pdf) |
| 3 | **What Every Programmer Should Know About Memory** — Ulrich Drepper, 2007 | [PDF](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf) |
| 4 | **The Linux Scheduler: A Decade of Wasted Cores** — Lozi et al., EuroSys 2016 | [DOI](https://dl.acm.org/doi/10.1145/2901318.2901326) |
| 5 | **Linux Kernel Development** — Robert Love, 3rd ed., Addison-Wesley | Book |

---

## 🎓 Viva Q&A Cheatsheet

**Q: Why is context-switch cost not constant?**
> It depends on: (1) working-set size in cache, (2) process count (scheduler complexity), (3) CPU topology (same vs different core), (4) scheduling policy.

**Q: What does lmbench lat_ctx measure exactly?**
> N processes form a token-passing ring via pipes. Time for one ring rotation ÷ N = per-switch latency. The `-s` flag adds a working-set to model cache pollution.

**Q: Why is process switching slower than thread switching?**
> Process switches reload CR3 (page table base), invalidating all TLB entries. Thread switches in the same process keep CR3 unchanged — TLB stays warm.

**Q: What is the core migration penalty?**
> Switching between processes on different physical cores requires: refilling L1/L2 from L3, potential TLB shootdown, and a cache-coherence snoop transaction. We measured ~2× penalty.

**Q: Why is SCHED_FIFO faster than CFS?**
> SCHED_FIFO uses an O(1) bitmap-indexed runqueue vs CFS's O(log N) red-black tree. No fairness computation → lower, more deterministic latency.

**Q: What is the TLB and why does it matter for context switches?**
> The TLB (Translation Lookaside Buffer) caches virtual→physical page translations. On process switches, Linux flushes the TLB to prevent address-space leakage. This causes cold-start penalties on subsequent memory accesses.

**Q: How does HyperThreading affect context-switch cost?**
> Two HyperThreads on the same physical core share L1 and L2 caches. Switching between HT siblings avoids a cache refill, making it ~2× cheaper than switching across physical cores.

**Q: What is CFS and how does it scale?**
> CFS (Completely Fair Scheduler) tracks virtual runtime (vruntime) in a per-CPU red-black tree. Task selection is O(log N) — latency grows with the number of runnable processes.

---

## 👨‍💻 Author

**Shikhar Mutta**
Department of Computer Science & Engineering
Final Semester — System Software Performance, 2026

---

## 📜 License

This project is for academic purposes. lmbench is distributed under its own license (see `lmbench/COPYING`).
