# SSP: Python to C Conversion & System Performance Analysis
## Phase-by-Phase Implementation with Novelty

---

## Executive Summary

Convert the existing Python-based SSP (System Scheduling Performance) benchmark suite to C with advanced system performance analysis capabilities:

- **Load generation**: CPU, I/O, Memory, Mixed (with precise control)
- **Microarchitecture profiling**: Cache misses, branch misses, TLB events
- **Scheduling analysis**: Context switches, scheduling latency, CPU affinity
- **System events**: Real-time collection via Linux perf subsystem
- **Scaling analysis**: Performance under varying core counts

**Key Novelty**:
1. Real-time perf event collection integrated into load generation
2. Per-CPU context switch tracking and latency analysis
3. Cache hierarchy impact measurement under controlled load
4. CPU scaling (P-state) effect on context switch latency
5. NUMA topology-aware scheduling analysis

---

## Phase 1: Core Infrastructure & Load Generator (Foundation)

### Deliverables
- Common utilities library (`ssp_lib.h/c`)
- C load generator replacing `load_generator.py`
- Build system (Makefile)

### Implementation Details

#### 1.1 Utilities Library (`src/ssp_lib.h`)
```c
// Time measurement with nanosecond precision
typedef struct {
    uint64_t tv_sec;
    uint64_t tv_nsec;
} ssp_time_t;

ssp_time_t ssp_now(void);
double ssp_time_diff_us(ssp_time_t start, ssp_time_t end);

// CPU affinity
int ssp_set_affinity(int cpu_id);
int ssp_get_cpu_count(void);
int ssp_get_node_cpus(int node_id, int *cpus, int max_cpus);

// Memory operations
void *ssp_allocate_aligned(size_t size, size_t alignment);
void ssp_free_aligned(void *ptr);

// Signal handling for clean shutdown
void ssp_init_signal_handlers(void);
extern volatile sig_atomic_t ssp_stop_flag;
```

#### 1.2 CPU Load Generator (`src/load_cpu.c`)
- Configurable duty-cycle using high-precision timers
- Float arithmetic to defeat compiler optimization
- Thread-safe operation with signal handlers
- Real-time scheduling capability

**Novel Feature**: Frequency-aware busy-loop that adapts to CPU frequency scaling

```c
void cpu_load_worker(int intensity, int duration, int cpu_id) {
    // Pin to CPU
    ssp_set_affinity(cpu_id);
    
    // Duty cycle control
    double work_frac = intensity / 100.0;
    double sleep_frac = 1.0 - work_frac;
    
    // High-precision timing loop
    ssp_time_t deadline = ssp_now();
    deadline.tv_sec += duration;
    
    volatile double x = 1.0;
    while (ssp_now() < deadline && !ssp_stop_flag) {
        ssp_time_t phase_start = ssp_now();
        
        // Compute phase - prevent optimization
        while (ssp_time_diff_us(phase_start, ssp_now()) < 
               CHUNK_MS * work_frac * 1000) {
            x = sin(x + 0.001) * cos(x - 0.001) + x * 1.0000001;
        }
        
        // Sleep phase
        if (sleep_frac > 1e-6) {
            usleep((useconds_t)(CHUNK_MS * sleep_frac * 1000));
        }
    }
}
```

#### 1.3 I/O Load Generator (`src/load_io.c`)
- Temporary file create/write/fsync/read cycles
- Block size scales with intensity (1-128 KB)
- VFS/page cache stress testing

```c
void io_load_worker(int intensity, int duration) {
    char tmpdir[] = "/tmp/ssp_io_XXXXXX";
    mkdtemp(tmpdir);
    
    int block_kb = (intensity > 0) ? (128 * intensity / 100) : 1;
    block_kb = (block_kb > 0) ? block_kb : 1;
    
    char *data = malloc(block_kb * 1024);
    for (int i = 0; i < block_kb * 1024; i++) {
        data[i] = rand() & 0xFF;
    }
    
    char fpath[PATH_MAX];
    snprintf(fpath, PATH_MAX, "%s/ioload.bin", tmpdir);
    
    ssp_time_t deadline = ssp_now();
    deadline.tv_sec += duration;
    
    while (ssp_now() < deadline && !ssp_stop_flag) {
        int fd = open(fpath, O_CREAT|O_WRONLY|O_TRUNC, 0600);
        write(fd, data, block_kb * 1024);
        fsync(fd);
        close(fd);
        
        fd = open(fpath, O_RDONLY);
        read(fd, data, block_kb * 1024);
        close(fd);
    }
    
    free(data);
    rmdir(tmpdir);
}
```

#### 1.4 Memory Load Generator (`src/load_memory.c`)
- Strided access pattern (64-byte = 1 cache line)
- Working set scales with intensity (8-512 MB)
- Cache hierarchy stress testing

**Novel Feature**: Configurable stride pattern to target specific cache levels

```c
void memory_load_worker(int intensity, int duration, int cache_level) {
    int size_mb = (intensity > 0) ? (512 * intensity / 100) : 8;
    size_mb = (size_mb > 0) ? size_mb : 8;
    
    size_t n_doubles = (size_mb * 1024 * 1024) / sizeof(double);
    double *buf = ssp_allocate_aligned(n_doubles * sizeof(double), 64);
    
    // Select stride based on cache level
    int stride;
    switch (cache_level) {
        case 1: stride = 8;      // 64B = L1 line (16 doubles)
                break;
        case 2: stride = 128;    // 1KB typical L2 pattern
                break;
        case 3: stride = 512;    // LLC pattern
                break;
        default: stride = 8;
    }
    
    ssp_time_t deadline = ssp_now();
    deadline.tv_sec += duration;
    
    volatile double acc = 0.0;
    size_t idx = 0;
    
    while (ssp_now() < deadline && !ssp_stop_flag) {
        for (int i = 0; i < 100000; i++) {
            acc += buf[idx];
            buf[idx] = acc * 1e-9;  // keep dirty
            idx = (idx + stride) % n_doubles;
        }
        usleep(1000);  // brief yield
    }
    
    ssp_free_aligned(buf);
}
```

### Deliverables
- `src/ssp_lib.h` - Common utilities
- `src/ssp_lib.c` - Implementation
- `src/load_cpu.c` - CPU load generator
- `src/load_io.c` - I/O load generator
- `src/load_memory.c` - Memory load generator
- `src/load_mixed.c` - Multi-threaded mixed load
- `Makefile` - Build system

---

## Phase 2: System Event Collection (Perf Integration)

### Deliverables
- Perf event collection framework
- Microarchitecture profiling module
- Real-time event aggregation

### Implementation Details

#### 2.1 Perf Event Interface (`src/perf_events.h`)
```c
typedef struct {
    int fd;
    uint64_t event_id;
    char event_name[64];
    uint64_t value;
    uint64_t enabled;
    uint64_t running;
} perf_event_t;

typedef struct {
    perf_event_t *events;
    int num_events;
    int cpu_id;
    pid_t target_pid;
} perf_session_t;

// Initialize perf session for CPU and events
perf_session_t *perf_session_create(int cpu_id, pid_t target_pid);

// Add standard event set
int perf_add_standard_events(perf_session_t *sess);

// Read all events
int perf_read_all_events(perf_session_t *sess);

// Standard events to track
typedef struct {
    uint64_t cycles;
    uint64_t instructions;
    uint64_t cache_references;
    uint64_t cache_misses;
    uint64_t branch_instructions;
    uint64_t branch_misses;
    uint64_t context_switches;
    uint64_t cpu_migrations;
    uint64_t page_faults;
} perf_stats_t;
```

#### 2.2 Perf Event Collection (`src/perf_events.c`)
- Use `perf_event_open()` syscall
- Non-privileged mode where possible
- Multiplexing for more events than hardware counters

```c
int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu,
                    int group_fd, unsigned long flags);

perf_session_t *perf_session_create(int cpu_id, pid_t target_pid) {
    perf_session_t *sess = malloc(sizeof(perf_session_t));
    sess->cpu_id = cpu_id;
    sess->target_pid = target_pid;
    sess->num_events = 0;
    sess->events = malloc(32 * sizeof(perf_event_t));
    
    return sess;
}

int perf_add_event(perf_session_t *sess, uint32_t type, uint64_t config,
                   const char *name) {
    if (sess->num_events >= 32) return -1;
    
    struct perf_event_attr attr = {
        .type = type,
        .config = config,
        .size = sizeof(struct perf_event_attr),
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
                       PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    
    int fd = perf_event_open(&attr, sess->target_pid, sess->cpu_id, -1, 0);
    if (fd < 0) return -1;
    
    perf_event_t *ev = &sess->events[sess->num_events++];
    ev->fd = fd;
    ev->event_id = config;
    strncpy(ev->event_name, name, 63);
    
    return 0;
}

int perf_read_all_events(perf_session_t *sess) {
    for (int i = 0; i < sess->num_events; i++) {
        uint64_t buf[3];
        if (read(sess->events[i].fd, buf, sizeof(buf)) == sizeof(buf)) {
            sess->events[i].value = buf[0];
            sess->events[i].enabled = buf[1];
            sess->events[i].running = buf[2];
        }
    }
    return 0;
}
```

#### 2.3 Microarchitecture Analysis (`src/microarch.h`)
```c
typedef struct {
    // IPC = instructions per cycle
    double ipc;
    
    // Cache hierarchy
    double cache_miss_rate;
    double l1_miss_rate;
    
    // Branch prediction
    double branch_miss_rate;
    
    // Context switch overhead
    double context_switch_ratio;
    
    // Scheduling efficiency
    double cpu_migration_rate;
    
    // Memory efficiency
    double page_fault_ratio;
} microarch_metrics_t;

// Calculate microarchitecture metrics from perf stats
microarch_metrics_t compute_microarch_metrics(perf_stats_t *before,
                                              perf_stats_t *after);
```

### Deliverables
- `src/perf_events.h/c` - Perf event collection
- `src/microarch.h/c` - Microarchitecture metrics computation
- `src/perf_sample.c` - Standalone perf sampling utility

---

## Phase 3: Context Switch & Scheduling Analysis

### Deliverables
- Context switch latency measurement
- Per-CPU scheduling analysis
- Scheduling latency distribution

### Implementation Details

#### 3.1 Context Switch Measurement (`src/ctx_switch.h`)
```c
typedef struct {
    uint64_t count;
    uint64_t total_latency_ns;
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    uint64_t *samples;  // histogram
} ctx_switch_stats_t;

// Measure context switch latency using ring buffer approach
typedef struct {
    int num_threads;
    int data_size_kb;
    double measurement_duration_s;
    
    // Results
    double latency_us;
    double std_dev_us;
    ctx_switch_stats_t per_thread_stats;
} ctx_switch_test_t;

ctx_switch_test_t *ctx_switch_measure(int num_threads, int data_size_kb,
                                       double duration);
void ctx_switch_print_stats(ctx_switch_test_t *test);
```

#### 3.2 Ring Buffer Context Switch Bench (`src/ctx_switch_bench.c`)
**Novel Feature**: Real-time latency measurement with per-CPU tracking

```c
// Thread ring: T0 → T1 → T2 → ... → T0
// Each thread measures latency when woken up via futex

typedef struct {
    int thread_id;
    int futex_fd;
    volatile uint64_t wake_count;
    uint64_t last_wake_ns;
    uint64_t latencies[10000];
    int latency_idx;
} thread_ring_context_t;

void *thread_ring_worker(void *arg) {
    thread_ring_context_t *ctx = (thread_ring_context_t *)arg;
    ssp_set_affinity(ctx->thread_id);
    
    uint64_t *local_buf = malloc(16384 * 1024);  // working set
    int idx = 0;
    
    for (int iteration = 0; iteration < num_iterations; iteration++) {
        // Measure wake latency
        uint64_t t_before = ssp_now_ns();
        
        futex_wait(ctx->futex_fd, ctx->wake_count);
        
        uint64_t t_after = ssp_now_ns();
        
        // Record latency
        uint64_t latency = t_after - t_before;
        ctx->latencies[ctx->latency_idx++] = latency;
        
        // Do work with local buffer (keep cache hot)
        for (int i = 0; i < 10000; i++) {
            local_buf[idx] = local_buf[idx] * 1.0000001;
            idx = (idx + 8) % 16384;
        }
        
        // Wake next thread
        futex_wake(&next_thread->futex_fd, 1);
    }
}
```

#### 3.3 CPU Affinity & Migration Tracking (`src/sched_analysis.h`)
```c
typedef struct {
    int cpu_id;
    uint64_t migrations_in;
    uint64_t migrations_out;
    uint64_t context_switches;
    uint64_t voluntary_switches;
    uint64_t involuntary_switches;
} per_cpu_sched_stats_t;

typedef struct {
    int num_cpus;
    per_cpu_sched_stats_t *per_cpu;
    
    // Overall stats
    uint64_t total_migrations;
    uint64_t total_context_switches;
} sched_analysis_t;

// Analyze scheduling patterns from /proc/sched_debug
sched_analysis_t *analyze_scheduling_patterns(pid_t target_pid, 
                                               double duration);
```

### Deliverables
- `src/ctx_switch.h/c` - Context switch measurement
- `src/ctx_switch_bench.c` - Ring buffer benchmark
- `src/sched_analysis.h/c` - Scheduling pattern analysis

---

## Phase 4: Integrated Benchmark & Data Collection

### Deliverables
- Master benchmark program combining all components
- Real-time data collection and aggregation
- JSON output format

### Implementation Details

#### 4.1 Master Benchmark (`src/benchmark.c`)

**Novel Feature**: Simultaneous load generation + system event collection

```c
typedef struct {
    int num_cores;
    int *active_cores;
    char load_type[32];  // cpu, io, memory, mixed
    int intensity;
    int duration;
    
    // Events to collect
    int measure_perf;
    int measure_scheduling;
    int measure_cache;
    
    // Output
    char output_file[PATH_MAX];
    int verbose;
} benchmark_config_t;

typedef struct {
    // Configuration
    benchmark_config_t config;
    
    // Results
    double context_switch_latency_us;
    double context_switch_std_us;
    microarch_metrics_t microarch;
    per_cpu_sched_stats_t *per_cpu_stats;
    perf_stats_t perf_before, perf_after;
    
    // Timestamps
    time_t start_time;
    time_t end_time;
} benchmark_result_t;

// Main benchmark loop
int run_benchmark(benchmark_config_t *config, benchmark_result_t *result);
```

#### 4.2 JSON Output (`src/json_output.c`)

```c
void write_results_json(const char *filename, benchmark_result_t *result) {
    FILE *fp = fopen(filename, "a");
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"timestamp\": \"%s\",\n", get_timestamp_iso());
    fprintf(fp, "  \"hostname\": \"%s\",\n", hostname);
    fprintf(fp, "  \"load_type\": \"%s\",\n", result->config.load_type);
    fprintf(fp, "  \"intensity_pct\": %d,\n", result->config.intensity);
    fprintf(fp, "  \"num_cores\": %d,\n", result->config.num_cores);
    fprintf(fp, "  \"ctx_switch_latency_us\": %.2f,\n",
            result->context_switch_latency_us);
    fprintf(fp, "  \"microarch\": {\n");
    fprintf(fp, "    \"ipc\": %.2f,\n", result->microarch.ipc);
    fprintf(fp, "    \"cache_miss_rate\": %.2f,\n",
            result->microarch.cache_miss_rate);
    fprintf(fp, "    \"branch_miss_rate\": %.2f\n",
            result->microarch.branch_miss_rate);
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
}
```

### Deliverables
- `src/benchmark.c` - Master benchmark
- `src/json_output.c` - JSON result serialization
- `src/config_parser.c` - Command-line argument parsing

---

## Phase 5: Analysis Tools & Visualization

### Deliverables
- Result aggregation tool (C)
- Python visualization wrapper (enhanced from original)
- Statistical analysis

### Implementation Details

#### 5.1 Result Aggregator (`src/aggregate_results.c`)

**Novel Feature**: Multi-dimensional analysis combining perf data with scheduling

```c
typedef struct {
    // Aggregate across measurements
    double mean_ctx_switch_latency;
    double std_dev_ctx_switch;
    double p99_ctx_switch_latency;
    
    double mean_cache_miss_rate;
    double mean_ipc;
    
    // Per-core breakdowns
    per_cpu_sched_stats_t *per_cpu_summary;
    
    // Scaling efficiency
    double scaling_efficiency;  // latency(2 cores) / latency(1 core)
} aggregated_results_t;

aggregated_results_t *aggregate_json_results(const char *results_dir);
```

#### 5.2 Enhanced Python Visualization (`scripts/plot_results_enhanced.py`)

Build on existing plotter with C-generated data:
- Perf event trends
- Microarchitecture metrics heatmaps
- Scheduling efficiency curves
- Cache hierarchy impact analysis

### Deliverables
- `src/aggregate_results.c` - JSON aggregation
- `scripts/plot_results_enhanced.py` - Enhanced visualization
- `src/statistical_analysis.c` - Statistical computations

---

## Phase 6: Advanced Features (Optional Novelty)

### Features
1. **NUMA-Aware Analysis**: Track memory access patterns across NUMA nodes
2. **CPU Frequency Scaling Analysis**: Measure impact of DVFS on context switches
3. **TLB Miss Tracking**: Advanced memory hierarchy analysis
4. **IPC Variation Under Load**: Real-time IPC monitoring
5. **Lock Contention Analysis**: Using futex-based measurements

### Implementation
```c
// src/numa_analysis.c
typedef struct {
    int node_id;
    uint64_t local_accesses;
    uint64_t remote_accesses;
    double remote_access_ratio;
} numa_node_stats_t;

// src/dvfs_analysis.c
typedef struct {
    int cpu_id;
    uint64_t current_freq_mhz;
    uint64_t min_freq_mhz;
    uint64_t max_freq_mhz;
    double latency_vs_freq_slope;
} dvfs_stats_t;
```

---

## Build & Project Structure

### Directory Layout
```
SSP-main/
├── src/
│   ├── ssp_lib.h/c              # Common utilities
│   ├── load_cpu.c                # CPU load
│   ├── load_io.c                 # I/O load
│   ├── load_memory.c             # Memory load
│   ├── load_mixed.c              # Mixed load
│   ├── perf_events.h/c           # Perf integration
│   ├── microarch.h/c             # Microarch metrics
│   ├── ctx_switch.h/c            # Context switch measurement
│   ├── ctx_switch_bench.c        # Ring buffer bench
│   ├── sched_analysis.h/c        # Scheduling analysis
│   ├── benchmark.c               # Master benchmark
│   ├── json_output.c             # JSON serialization
│   ├── aggregate_results.c       # Result aggregation
│   └── Makefile
├── scripts/
│   ├── plot_results_enhanced.py  # Enhanced visualization
│   └── run_benchmark.sh          # Benchmark orchestration
├── results/
│   ├── raw/                      # Raw JSON results
│   ├── aggregated/               # Aggregated results
│   └── analysis/                 # Statistical analysis
└── IMPLEMENTATION_PLAN.md        # This file
```

### Makefile
```makefile
CC = gcc
CFLAGS = -Wall -O2 -pthread -D_GNU_SOURCE
LIBS = -lm -lpthread

TARGETS = load_generator benchmark aggregate_results
OBJS = ssp_lib.o perf_events.o microarch.o ctx_switch.o sched_analysis.o

all: $(TARGETS)

load_generator: $(OBJS) load_cpu.o load_io.o load_memory.o load_mixed.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

benchmark: $(OBJS) benchmark.o json_output.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

aggregate_results: $(OBJS) aggregate_results.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f *.o $(TARGETS)
```

---

## Key Novelty Contributions

1. **Integrated System Events**: Simultaneously generate load and measure system events
2. **Microarchitecture Profiling Under Load**: Understand how different loads affect cache/branch prediction
3. **Per-CPU Scheduling Analysis**: Fine-grained tracking of context switches and migrations
4. **Frequency-Aware Load Generation**: CPU load adapts to frequency scaling
5. **Comprehensive Performance Model**: Combines IPC, cache misses, branch misses, and context switches into unified analysis
6. **Real-time Latency Distribution**: Capture full latency distribution, not just averages

---

## Implementation Timeline

| Phase | Duration | Dependencies |
|-------|----------|--------------|
| Phase 1 (Load Generator) | 2-3 days | None |
| Phase 2 (Perf Events) | 3-4 days | Phase 1 |
| Phase 3 (Context Switch) | 2-3 days | Phase 1, 2 |
| Phase 4 (Integrated Benchmark) | 3-4 days | Phase 1-3 |
| Phase 5 (Analysis & Visualization) | 2-3 days | Phase 4 |
| Phase 6 (Advanced Features) | 2-3 days | Phase 4 |

**Total: ~15-20 days for full implementation**

---

## Testing Strategy

1. **Unit Tests**: Each module independently
2. **Integration Tests**: Load generator + perf events
3. **Validation Against Original**: Compare C results with Python baselines
4. **Scaling Tests**: Verify correctness at 2, 4, 8, 16, 32+ cores
5. **Long-Duration Stability**: Multi-hour runs to catch resource leaks

---

## Success Criteria

✓ C programs produce similar results to Python baseline
✓ System event collection functional on Linux
✓ Accurate microarchitecture metrics (validated via perf record)
✓ Context switch latency measurements within 1% of lmbench
✓ Scaling efficiency clearly visible in results
✓ Per-CPU analysis reveals scheduling patterns
✓ JSON output compatible with enhanced visualization
