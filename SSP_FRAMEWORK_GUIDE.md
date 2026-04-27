# SSP (System Scaling Performance) Benchmark Framework - Complete Guide

This document provides a step-by-step explanation of the SSP Benchmark Framework, from architecture to results interpretation.

---

## Table of Contents

1. [Framework Overview](#1-framework-overview)
2. [Directory Structure](#2-directory-structure)
3. [Core Components](#3-core-components)
4. [Build System](#4-build-system)
5. [Running Benchmarks](#5-running-benchmarks)
6. [Understanding Results](#6-understanding-results)
7. [Code Flow Analysis](#7-code-flow-analysis)
8. [Troubleshooting](#8-troubleshooting)

---

## 1. Framework Overview

The SSP Benchmark Suite is a C-based framework for analyzing multicore processor performance. It provides:

- **Configurable Workload Generation**: CPU, Memory, I/O, and Mixed workloads
- **Direct Linux Perf Integration**: Hardware counter collection via `perf_event_open()`
- **Tail Latency Analysis**: P50, P95, P99 percentile computations
- **Automated Classification**: CPU behavior categorization (MIXED, MEMORY_BOUND, etc.)
- **Scaling Efficiency Analysis**: Performance evaluation across core counts

### Key Features

| Feature | Description |
|---------|-------------|
| Duty-cycle control | 0-100% intensity for CPU workloads |
| Cache hierarchy targeting | L1, L2, L3 strided memory access |
| Signal handling | Clean shutdown on SIGINT/SIGTERM |
| CPU affinity | Pin workloads to specific cores |
| Nanosecond timing | Using `CLOCK_MONOTONIC` |

---

## 2. Directory Structure

```
SSP-main/
├── src/                    # Source code
│   ├── benchmark.c         # Main orchestrator
│   ├── load_generator.c    # CLI parser and dispatcher
│   ├── load_cpu.c          # CPU workload
│   ├── load_memory.c       # Memory workload
│   ├── load_io.c           # I/O workload
│   ├── load_mixed.c        # Mixed CPU+Memory workload
│   ├── ssp_lib.c            # Common utilities
│   ├── ssp_lib.h            # Header file
│   ├── perf_events.c       # Linux perf integration
│   ├── microarch.c          # Microarchitecture analysis
│   ├── analysis.c           # Results analysis tool
│   └── Makefile            # Build configuration
├── results/
│   └── workload_benchmark.csv   # Benchmark results
├── report/
│   ├── SSP_Paper_Final.tex       # IEEE paper
│   └── SSP_Paper_Final.pdf       # Generated PDF
├── run_exhaustive_benchmark.sh  # Automation script
└── README.md
```

---

## 3. Core Components

### 3.1 ssp_lib.c / ssp_lib.h - Common Utilities

**Purpose**: Provides shared functionality for all components.

**Key Functions**:

| Function | Purpose |
|----------|---------|
| `ssp_now()` | Get current time with nanosecond precision |
| `ssp_now_ns()` | Get current time in nanoseconds |
| `ssp_set_affinity(cpu_id)` | Pin thread to specific CPU |
| `ssp_get_cpu_count()` | Get number of logical CPUs |
| `ssp_allocate_aligned()` | Allocate cache-line aligned memory |
| `ssp_init_signal_handlers()` | Setup SIGINT/SIGTERM handlers |
| `ssp_log()` | Logging with levels (DEBUG, INFO, WARN, ERROR) |

**Time Measurement Implementation**:
```c
ssp_time_t ssp_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);  // Use monotonic clock
    // ... convert to ssp_time_t
}
```

### 3.2 load_cpu.c - CPU Workload Generator

**Purpose**: Generates CPU-bound load with configurable intensity.

**Algorithm**:
1. Calculate work/sleep fractions based on intensity
2. Use 10ms scheduling chunks
3. In each chunk: busy-loop for work fraction, sleep for idle fraction
4. Use floating-point math (`sin`, `cos`) to prevent compiler optimization

**Intensity Control**:
- 0% = No work, just sleep
- 50% = 50% busy, 50% idle
- 100% = Continuous work

**Code Flow**:
```c
void cpu_load_worker(int intensity, int duration, int cpu_id) {
    // 1. Pin to CPU if specified
    if (cpu_id >= 0) ssp_set_affinity(cpu_id);
    
    // 2. Calculate duty cycle
    double work_frac = intensity / 100.0;
    double sleep_frac = 1.0 - work_frac;
    
    // 3. Run for specified duration
    while (not expired && !ssp_stop_flag) {
        // Busy phase: compute-intensive loop
        // Idle phase: sleep
    }
}
```

### 3.3 load_memory.c - Memory Workload Generator

**Purpose**: Generates memory load with strided access patterns to stress cache hierarchy.

**Key Parameters**:
- **Intensity**: Controls working set size (8-512 MB)
- **Cache Level**: Determines stride pattern
  - L1: 64-byte stride (8 doubles)
  - L2: 1KB stride (128 doubles)
  - L3: 8KB stride (1024 doubles)

**Algorithm**:
1. Allocate working set based on intensity
2. Strided sequential access through memory
3. Cache line size (64B) forces cache misses

### 3.4 load_io.c - I/O Workload Generator

**Purpose**: Generates file I/O load with create/write/fsync/read cycles.

**Operations**:
- Create temporary file
- Write data in chunks
- Fsync to force disk write
- Read back and verify

### 3.5 load_mixed.c - Mixed Workload Generator

**Purpose**: Combines CPU and memory workloads in two threads.

**Configuration**:
- Thread 1: CPU load
- Thread 2: Memory load
- Both run concurrently with same intensity

### 3.6 benchmark.c - Main Orchestrator

**Purpose**: Coordinates benchmark runs and result collection.

**Command-Line Arguments**:
```bash
./src/benchmark --type <TYPE> --intensity <INT> --duration <SEC> --repeat <N>
```

| Argument | Description | Values |
|----------|-------------|--------|
| `--type` | Workload type | cpu, memory, io, mixed |
| `--intensity` | Load intensity | 0-100 |
| `--duration` | Run duration | seconds |
| `--repeat` | Number of iterations | default: 1 |

**Output Format** (CSV):
```csv
timestamp,os,hostname,total_cores,active_cores,workload_type,intensity_pct,data_size_kb,num_procs,latency_us,std_us,method,notes
```

### 3.7 analysis.c - Results Analysis Tool

**Purpose**: Analyzes benchmark CSV and generates reports.

**Output Sections**:
1. **Scaling Efficiency Graph**: ASCII visualization of latency vs core count
2. **Tail Latency Analysis**: P50, P95, P99 percentiles by workload
3. **Performance Classification**: IPC estimates and bottleneck identification

**Usage**:
```bash
./src/analysis -a           # Show all analyses
./src/analysis -s           # Show scaling graph
./src/analysis -l           # Show latency distribution
./src/analysis -c           # Show classification
```

---

## 4. Build System

### Makefile Targets

| Target | Description |
|--------|-------------|
| `make` or `make all` | Build all executables |
| `make benchmark` | Build benchmark only |
| `make load_generator` | Build load_generator only |
| `make analysis` | Build analysis only |
| `make clean` | Remove object files |

### Compilation Flags

```makefile
gcc -Wall -Wextra -O2 -pthread -D_GNU_SOURCE -std=c99
```

- `-Wall -Wextra`: Enable all warnings
- `-O2`: Optimization level 2
- `-pthread`: POSIX threads support
- `-D_GNU_SOURCE`: GNU extensions
- `-std=c99`: C99 standard

### Generated Executables

```
src/
├── benchmark        # Main orchestrator
├── load_generator   # Workload generator
└── analysis         # Results analyzer
```

---

## 5. Running Benchmarks

### Method 1: Manual Single Run

```bash
cd /home/shrinithi/Project/SSP-main

# Build
make -C src all

# Run single benchmark
./src/benchmark --type cpu --intensity 75 --duration 10 --repeat 3

# Analyze results
./src/analysis -a
```

### Method 2: Automated Exhaustive Run

```bash
./run_exhaustive_benchmark.sh
```

This script:
1. Builds all executables
2. Clears previous results
3. Runs all workload types at all intensity levels
4. Generates analysis report

**Configuration** (in script):
```bash
ITERATIONS=3
DURATION=10
INTENSITIES=(25 50 75)
WORKLOADS=(cpu memory io mixed)
```

### Method 3: Direct load_generator

```bash
./src/load_generator --type cpu --intensity 75 --duration 10
./src/load_generator --type memory --intensity 50 --duration 60 --cache-level 2
./src/load_generator --type mixed --intensity 80 --duration 120 --verbose
```

---

## 6. Understanding Results

### CSV File Format

```csv
timestamp,os,hostname,total_cores,active_cores,workload_type,intensity_pct,data_size_kb,num_procs,latency_us,std_us,method,notes
2026-04-27T13:06:18,Linux,shikhar-pc,8,1,cpu,25,0,2,8.92,0.099,lmbench,
```

| Column | Description |
|--------|-------------|
| timestamp | ISO 8601 timestamp |
| os | Operating system |
| hostname | Machine name |
| total_cores | Total available cores |
| active_cores | Cores used in test |
| workload_type | cpu/memory/io/mixed |
| intensity_pct | Load intensity (0-100) |
| latency_us | Measured latency in μs |
| std_us | Standard deviation |

### Analysis Output Example

```
=== SSP Benchmark Analysis ===
Loaded 36 records from results/workload_benchmark.csv

=== Scaling Efficiency Graph ===
Core Count vs. Latency (ASCII Graph)
Cores      Mean(μs)  Graph                Classification
------     --------   -----                -------------
1          10.17      ██████████░░░░░░░░░░ MIXED

=== Tail Latency Analysis (P50, P95, P99) ===
Workload        Mean       P50        P95        P99       
-----------     ----       ---        ---        ---       
cpu             10.01      10.01      10.01      10.01     
memory          10.31      10.31      10.16      10.16     

=== Performance Classification ===
Workload        Cores      Intensity  IPC(est)        Classification
-----------     -----      ---------  --------        -------------
cpu             1          25         0.99            MIXED
cpu             1          75         0.98            MEMORY_BOUND
```

### Performance Classifications

| Classification | Description |
|----------------|-------------|
| COMPUTE_OPTIMAL | CPU-bound with good IPC |
| MEMORY_BOUND | Memory access is bottleneck |
| MIXED | Both CPU and memory constraints |
| IO_BOUND | I/O operations are bottleneck |
| CONTEXT_BOUND | Excessive context switching |

---

## 7. Code Flow Analysis

### End-to-End Flow

```
1. User runs benchmark
   └─> benchmark.c:main()
       ├─> Parse arguments (type, intensity, duration, repeat)
       ├─> Open CSV file for appending
       └─> Loop for each iteration:
           ├─> Build command: "./load_generator --type X --intensity Y --duration Z"
           ├─> Record start time (clock_gettime)
           ├─> Execute load_generator (system())
           ├─> Record end time
           ├─> Calculate elapsed time
           └─> Write to CSV

2. load_generator runs
   └─> load_generator.c:main()
       ├─> Parse arguments
       ├─> Initialize signal handlers
       └─> Dispatch to workload:
           ├─> cpu_load_worker()     → load_cpu.c
           ├─> memory_load_worker() → load_memory.c
           ├─> io_load_worker()     → load_io.c
           └─> mixed_load_worker() → load_mixed.c

3. Workload executes
   └─> e.g., cpu_load_worker()
       ├─> Set CPU affinity (optional)
       ├─> Calculate duty cycle
       └─> Run loop for duration:
           ├─> Busy phase: compute-intensive arithmetic
           └─> Idle phase: sleep

4. Analysis runs
   └─> analysis.c:main()
       ├─> Load CSV data (load_csv())
       ├─> Compute statistics (compute_stats())
       └─> Generate reports:
           ├─> print_scaling_graph()
           ├─> print_latency_distribution()
           └─> print_classification_summary()
```

### Key Data Structures

```c
// From ssp_lib.h
typedef struct {
    uint64_t tv_sec;
    uint64_t tv_nsec;
} ssp_time_t;

// From analysis.c
typedef struct {
    char timestamp[64];
    char workload_type[32];
    int intensity_pct;
    double latency_us;
    // ... more fields
} benchmark_record_t;

typedef struct {
    double mean;
    double std_dev;
    double min;
    double max;
    double p50;
    double p95;
    double p99;
    int count;
} stats_t;
```

---

## 8. Troubleshooting

### Common Issues

| Issue | Solution |
|-------|----------|
| `fopen CSV: No such file or directory` | Run from project root, ensure `results/` exists |
| `./load_generator: not found` | Use correct path: `./src/load_generator` |
| Analysis shows "No data loaded" | Check CSV format matches expected header |
| Compilation warnings | Warnings are normal; check for errors |

### Debug Mode

```bash
# Run with verbose output
./src/load_generator --type cpu --intensity 75 --duration 10 --verbose
```

### Clean Rebuild

```bash
cd /home/shrinithi/Project/SSP-main
make -C src clean
make -C src all
```

---

## Summary

The SSP Benchmark Framework provides a comprehensive tool for analyzing multicore processor performance. The flow is:

1. **Build** → `make -C src all`
2. **Run** → `./src/benchmark --type cpu --intensity 75 --duration 10`
3. **Analyze** → `./src/analysis -a`
4. **Document** → Generate PDF paper

The framework is modular, extensible, and produces consistent, reproducible results suitable for academic research and performance analysis.

---

*Generated on: April 27, 2026*
*Framework Version: 1.0*