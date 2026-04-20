# Phase 2: System Event Collection - Implementation Guide

## Overview

Phase 2 extends the core load generator with advanced system performance monitoring using Linux perf events. This enables real-time collection of microarchitecture metrics while workloads run.

## Completed Phase 2 Headers

### 1. `perf_events.h/c` - Perf Event Collection
- Wraps Linux `perf_event_open()` syscall
- Session-based event management
- Automatic computation of microarchitecture metrics (IPC, cache miss rates, etc.)
- JSON serialization of results
- Support for hardware and software events

**Key Features**:
- Hardware events: cycles, instructions, cache refs/misses, branch pred
- Software events: context switches, CPU migrations, page faults  
- Multiplexing support (more events than hardware counters)
- Per-CPU and system-wide monitoring

### 2. `microarch.h` - Microarchitecture Metrics
- High-level performance analysis
- Cache efficiency scoring
- Scheduling efficiency analysis
- Bottleneck identification
- Multi-core scaling measurement

## Remaining Phase 2 Work

### TODO #1: Complete `microarch.c`

```c
/**
 * microarch.c - Implementation
 * Computes metrics from perf statistics
 */

microarch_metrics_t compute_microarch_metrics(const perf_stats_t *before,
                                              const perf_stats_t *after) {
    perf_stats_t diff = perf_stats_diff(before, after);
    
    microarch_metrics_t metrics = {
        .ipc = diff.ipc,
        .cycle_per_instruction = (diff.ipc > 0) ? (1.0 / diff.ipc) : 0.0,
        .l1_miss_rate = diff.cache_miss_rate,
        .branch_miss_rate = diff.branch_miss_rate,
        .context_switch_ratio = diff.context_switch_ratio,
        .cpu_migration_ratio = (diff.cycles > 0) ? 
            ((double)diff.cpu_migrations / (double)diff.cycles) : 0.0,
    };
    
    metrics.memory_efficiency = compute_cache_efficiency(&diff);
    metrics.scheduling_efficiency = compute_scheduling_efficiency(&diff);
    metrics.overall_efficiency = 
        (metrics.memory_efficiency + metrics.scheduling_efficiency) / 2.0;
    
    return metrics;
}

double compute_cache_efficiency(const perf_stats_t *stats) {
    // 1.0 = no misses (perfect), 0.0 = all misses (terrible)
    if (stats->cache_references == 0) return 0.5;
    
    double miss_rate = (double)stats->cache_misses / stats->cache_references;
    return (1.0 - miss_rate);  // Invert: high miss rate = low efficiency
}

double compute_scheduling_efficiency(const perf_stats_t *stats) {
    // Fewer context switches = better efficiency
    // Estimate based on context switches per 1M cycles
    if (stats->cycles == 0) return 0.5;
    
    double switches_per_1m_cycles = 
        ((double)stats->context_switches / (double)stats->cycles) * 1000000;
    
    // Use logistic function: 0.5 at ~1000 switches/1M cycles
    // Ranges from ~1.0 (few switches) to ~0.0 (many switches)
    return 1.0 / (1.0 + exp(switches_per_1m_cycles / 500.0 - 2.0));
}
```

### TODO #2: Create `ctx_switch.h/c` - Context Switch Measurement

Ring-buffer based latency measurement:

```c
/**
 * ctx_switch.h - Context switch latency measurement
 */

typedef struct {
    int num_threads;
    int data_size_kb;
    double measurement_duration_s;
    
    // Results
    double latency_us;
    double latency_std_us;
    double p99_latency_us;
    uint64_t total_switches;
} ctx_switch_test_result_t;

/**
 * Measure context switch latency using thread ring
 * Threads wake each other via futex, measuring wake latency
 */
ctx_switch_test_result_t *ctx_switch_measure(int num_threads, 
                                             int data_size_kb,
                                             double duration);
```

### TODO #3: Create `sched_analysis.h/c` - Scheduling Analysis

Parse `/proc/sched_debug` and track scheduling patterns:

```c
typedef struct {
    int cpu_id;
    uint64_t context_switches;
    uint64_t voluntary_switches;
    uint64_t involuntary_switches;
    uint64_t migrations_in;
    uint64_t migrations_out;
} per_cpu_sched_stats_t;

sched_analysis_t *analyze_scheduling_patterns(pid_t target_pid, 
                                               double duration);
```

### TODO #4: Create `benchmark.c` - Integrated Benchmark

Master program that:
1. Starts load generator in background
2. Collects perf events during load
3. Measures context switch latency
4. Tracks scheduling patterns
5. Outputs JSON results

```c
/**
 * benchmark.c - Integrated benchmark
 */

typedef struct {
    int num_cores;
    int *active_cores;
    char load_type[32];
    int intensity;
    int duration;
    
    // Measurements
    int measure_perf_events;
    int measure_ctx_switch;
    int measure_scheduling;
} benchmark_config_t;

int run_benchmark(benchmark_config_t *config, 
                 benchmark_result_t *result);
```

## Dependencies to Install

```bash
# Debian/Ubuntu
sudo apt-get install libjson-c-dev

# Fedora/CentOS
sudo dnf install json-c-devel

# macOS (if porting to macOS, use alternative perf library)
# Not available - use alternative performance monitoring
```

## Compilation Commands

```bash
# Once microarch.c is implemented:
gcc -Wall -O2 -pthread -D_GNU_SOURCE -c microarch.c -o obj/microarch.o

# Link with perf_events
gcc -Wall -O2 -pthread -D_GNU_SOURCE -o benchmark obj/perf_events.o obj/microarch.o ... -lm -ljson-c
```

## Testing Strategy for Phase 2

### Test 1: Perf Event Collection
```bash
# Create simple test that collects events
./test_perf_events --duration 5 --type cycles,instructions
```

### Test 2: Microarchitecture Metrics
```bash
# Measure memory-intensive workload
./load_generator --type memory --intensity 100 --duration 10 &
PID=$!
./test_microarch --target $PID --duration 10
```

### Test 3: Context Switch Measurement
```bash
# Baseline without load
./test_ctx_switch --num-threads 4 --duration 5

# With CPU load
./load_generator --type cpu --intensity 80 --duration 10 &
./test_ctx_switch --num-threads 4 --duration 5
```

## Integration with Phase 1

Phase 2 builds on Phase 1:
- Uses `ssp_lib.h` for time measurement and CPU affinity
- Uses load generator modules (load_cpu, load_memory, etc.)
- Extends with system event collection capability

## Architecture Diagram

```
┌─────────────────────────────────────────────────┐
│      Integrated Benchmark (benchmark.c)         │
└─────────────────────────────────────────────────┘
          ↓
  ┌───────┴──────────┬──────────────┐
  ↓                  ↓              ↓
Load Gen        Perf Events    Ctx Switch
(Phase 1)    (perf_events.c)  (ctx_switch.c)
  ↓                  ↓              ↓
  └───────┬──────────┴──────────────┘
          ↓
  Microarch Metrics (microarch.c)
          ↓
  Scheduling Analysis (sched_analysis.c)
          ↓
  ┌──────────────────┐
  │  JSON Output     │
  │  (results.json)  │
  └──────────────────┘
```

## JSON Output Format

```json
{
  "timestamp": "2024-04-20T12:34:56Z",
  "hostname": "server01",
  "load_type": "memory",
  "intensity_pct": 75,
  "duration_sec": 60,
  "num_cores": 8,
  
  "perf_events": {
    "cycles": 120000000000,
    "instructions": 45000000000,
    "ipc": 0.375,
    "cache_misses": 5000000,
    "cache_miss_rate": 0.15,
    "context_switches": 1200,
    "context_switch_ratio": 0.00001
  },
  
  "microarch_metrics": {
    "cache_efficiency": 0.85,
    "scheduling_efficiency": 0.92,
    "overall_efficiency": 0.885
  },
  
  "context_switch_latency": {
    "mean_us": 2.5,
    "std_us": 0.8,
    "p99_us": 4.2
  },
  
  "scheduling_patterns": {
    "total_migrations": 45,
    "voluntary_switches": 1000,
    "involuntary_switches": 200
  }
}
```

## Next Steps (Phase 3)

After Phase 2 completes:
1. Focus on context switch measurement accuracy
2. Implement ring-buffer thread synchronization
3. Add per-CPU scheduling statistics
4. Validate measurements against lmbench

## Files to Create/Modify

**New Files**:
- `src/microarch.c` - Metrics computation
- `src/ctx_switch.h/c` - Context switch measurement
- `src/sched_analysis.h/c` - Scheduling analysis
- `src/benchmark.c` - Integrated benchmark
- `src/json_output.c` - JSON serialization

**Modified Files**:
- `src/Makefile` - Add new targets
- `src/ssp_lib.c` - May need utility functions

**Test Files** (optional but recommended):
- `src/test_perf_events.c` - Perf event tests
- `src/test_microarch.c` - Metrics validation
- `src/test_ctx_switch.c` - Context switch tests

## Novelty Aspects Implemented in Phase 2

✓ **Real-time perf event collection** - Simultaneous load generation + monitoring
✓ **Microarchitecture profiling under load** - Understand impact of different workloads
✓ **Automatic metrics computation** - IPC, cache efficiency, scheduling efficiency
✓ **JSON output for analysis** - Easy integration with Python visualization tools
