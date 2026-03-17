# SSP-main: System Performance Analysis using lmbench

## 📋 Project Overview

This project uses the **lmbench microbenchmark suite** to analyze system performance across multiple dimensions on an Intel Core i5-1035G1 (Ice Lake) processor. The analysis covers:

1. **Context Switches & Scheduling** — Measuring context switch latency across varying numbers of processes on multicore processors
2. **Filesystem Creates & Deletes** — Benchmarking file creation and deletion operations with different file sizes
3. **Signal Handling** — Measuring signal installation and delivery latency
4. **System Call Overheads** — Profiling null syscall, read, write, stat, fstat, and open/close operations
5. **Process Creation** — Benchmarking fork, exec, and shell process creation latencies

Additionally, **scaling analysis** (varying workload parameters) and **microarchitecture analysis** (relating results to CPU pipeline, cache hierarchy, etc.) are performed.

## 🖥️ System Under Test

| Parameter | Value |
|-----------|-------|
| CPU | Intel Core i5-1035G1 @ 1.00GHz (Turbo: 3.60GHz) |
| Microarchitecture | Ice Lake (10nm) |
| Cores / Threads | 4 / 8 |
| L1d / L1i Cache | 48 KB / 32 KB per core |
| L2 Cache | 512 KB per core |
| L3 Cache | 6 MB shared |
| RAM | 24 GB DDR4 |
| OS | Ubuntu (Kernel 6.17.0-14-generic) |

## 📁 Project Structure

```
SSP-main/
├── README.md                    # This file
├── scripts/
│   ├── run_all_benchmarks.sh    # Master script to run all benchmarks
│   ├── bench_context_switch.sh  # Context switch & scheduling benchmarks
│   ├── bench_filesystem.sh      # Filesystem create/delete benchmarks
│   ├── bench_signals.sh         # Signal handling benchmarks
│   ├── bench_syscalls.sh        # System call overhead benchmarks
│   └── bench_process.sh         # Process creation benchmarks
├── results/                     # Raw benchmark output (auto-generated)
├── analysis/
│   ├── analyze_results.py       # Main analysis & visualization script
│   └── generate_report.py       # Generate summary report with tables
├── plots/                       # Generated plots (auto-generated)
└── report/                      # Final report output
```

## 🚀 Quick Start

### Prerequisites
```bash
sudo apt-get install -y lmbench
pip3 install matplotlib pandas numpy
```

### Run All Benchmarks
```bash
cd scripts/
chmod +x *.sh
./run_all_benchmarks.sh
```

### Analyze Results & Generate Plots
```bash
cd analysis/
python3 analyze_results.py
python3 generate_report.py
```

## 📊 Benchmarks Detail

### 1. Context Switches (`lat_ctx`)
- Measures context switch latency with 2, 4, 8, 16, 32, 64, 96 processes
- Tests with different data sizes (0KB, 16KB, 64KB)
- Shows scheduling overhead scaling on multicore

### 2. Filesystem Operations (`lat_fs`)
- Measures file create+delete latency
- Tests with sizes: 0KB, 1KB, 4KB, 10KB
- Shows filesystem metadata performance

### 3. Signal Handling (`lat_sig`)
- `lat_sig install` — Signal handler installation latency
- `lat_sig catch` — Signal catch/delivery latency
- `lat_sig prot` — Protection fault handling latency

### 4. System Call Overheads (`lat_syscall`)
- `null` — Minimal syscall (getppid)
- `read` — Read from /dev/zero
- `write` — Write to /dev/null
- `stat` — stat() on a file
- `fstat` — fstat() on an open fd
- `open` — open()+close() combined

### 5. Process Creation (`lat_proc`)
- `fork+exit` — Fork and immediate exit
- `fork+execve` — Fork + exec /bin/true
- `fork+/bin/sh` — Fork + shell invocation

## 📝 License
Academic project for SSP (System Software Performance) course.
