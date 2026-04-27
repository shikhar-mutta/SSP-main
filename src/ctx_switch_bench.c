/*
 * ctx_switch_bench.c
 * ==================
 * Custom Context Switch Latency Benchmark
 *
 * PURPOSE:
 *   Measures the round-trip cost of a voluntary context switch between
 *   two processes using pipe-based synchronization (same technique as lmbench
 *   lat_ctx, but fully instrumented so we can read RDTSC directly).
 *
 * METHOD:
 *   - Parent and child processes ping-pong a single byte through a pipe.
 *   - Each round-trip forces two context switches (parent→child, child→parent).
 *   - We use RDTSC (CPU timestamp counter) for nanosecond-resolution timing.
 *   - Working-set size (-s KB) lets us study cache-pollution effect.
 *
 * NOVELTY:
 *   - Outputs per-iteration latency histogram (not just average).
 *   - Reports min/max/p50/p95/p99 percentiles.
 *   - Allows pinning to specific CPU core via -c flag (CPU affinity).
 *
 * USAGE:
 *   ./ctx_switch_bench [-s <working_set_KB>] [-n <num_processes>]
 *                      [-i <iterations>] [-c <cpu_mask>] [-o <outfile>]
 *
 * BUILD:
 *   gcc -O2 -o ctx_switch_bench ctx_switch_bench.c -lm
 *
 * Author: SSP Final Project, 2026
 */

#define _GNU_SOURCE          /* for sched_setaffinity, CPU_SET etc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdint.h>

/* ── Configuration defaults ─────────────────────────────────────── */
#define DEFAULT_ITERS       10000   /* number of ping-pong round-trips  */
#define DEFAULT_PROCS       2       /* number of processes in the chain */
#define DEFAULT_WS_KB       0       /* working-set size in KB           */
#define DEFAULT_CPU_MASK    -1      /* -1 = no pinning                  */
#define MAX_PROCS           64      /* hard cap on process count        */
#define HIST_BUCKETS        200     /* histogram resolution             */
#define HIST_MAX_US         500     /* histogram covers 0–500 µs        */

/* ── RDTSC helper ───────────────────────────────────────────────── */
/*
 * rdtsc() returns the 64-bit CPU timestamp counter.
 * We use CPUID before each read to serialize the instruction stream
 * (prevents out-of-order execution from corrupting measurements).
 */
static inline uint64_t rdtsc_serialize(void)
{
    uint32_t lo, hi;
    /* CPUID serializes, then RDTSC reads the counter */
    __asm__ volatile (
        "cpuid\n\t"
        "rdtsc\n\t"
        : "=a"(lo), "=d"(hi)
        :: "%rbx", "%rcx"
    );
    return ((uint64_t)hi << 32) | lo;
}

/* ── CPU frequency detection ────────────────────────────────────── */
/*
 * We measure the TSC frequency by sleeping 100 ms and comparing
 * TSC ticks.  This gives us a ticks-per-nanosecond ratio for
 * converting raw TSC to real time.
 */
static double measure_tsc_freq_ghz(void)
{
    struct timespec t0, t1;
    uint64_t tsc0, tsc1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    tsc0 = rdtsc_serialize();

    /* sleep 200 ms to get a stable reading */
    usleep(200000);

    tsc1 = rdtsc_serialize();
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed_ns = (t1.tv_sec - t0.tv_sec) * 1e9
                      + (t1.tv_nsec - t0.tv_nsec);
    double ticks      = (double)(tsc1 - tsc0);

    return ticks / elapsed_ns;   /* GHz */
}

/* ── CPU affinity helper ────────────────────────────────────────── */
/*
 * Pin the calling process to a specific logical CPU.
 * If cpu == -1 we do nothing (let the OS schedule freely).
 */
static void pin_to_cpu(int cpu)
{
    if (cpu < 0) return;

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    if (sched_setaffinity(0, sizeof(set), &set) < 0) {
        perror("sched_setaffinity");
        /* non-fatal: we continue without pinning */
    }
}

/* ── Working-set touch ──────────────────────────────────────────── */
/*
 * Allocate and repeatedly touch 'kb' kilobytes of memory.
 * This pollutes the CPU cache with our data before each context switch,
 * simulating realistic workloads where cache state is non-empty.
 * Calling this INSIDE the timed loop models the true cache-pollution penalty.
 */
static void touch_working_set(volatile char *buf, size_t bytes)
{
    /* stride = 64 bytes (one cache line) to touch every line */
    for (size_t i = 0; i < bytes; i += 64)
        buf[i] += 1;
}

/* ── Histogram helpers ──────────────────────────────────────────── */
typedef struct {
    uint64_t  counts[HIST_BUCKETS];
    double    bucket_width_us;   /* µs per bucket */
    uint64_t  total;
    double    sum_us;
    double    min_us;
    double    max_us;
} Histogram;

static void hist_init(Histogram *h)
{
    memset(h, 0, sizeof(*h));
    h->bucket_width_us = (double)HIST_MAX_US / HIST_BUCKETS;
    h->min_us = 1e18;
    h->max_us = 0.0;
}

static void hist_add(Histogram *h, double us)
{
    h->total++;
    h->sum_us += us;
    if (us < h->min_us) h->min_us = us;
    if (us > h->max_us) h->max_us = us;

    int bucket = (int)(us / h->bucket_width_us);
    if (bucket >= HIST_BUCKETS) bucket = HIST_BUCKETS - 1;
    if (bucket < 0)             bucket = 0;
    h->counts[bucket]++;
}

/* Return the value (µs) at percentile p (0–100) */
static double hist_percentile(const Histogram *h, double p)
{
    uint64_t target = (uint64_t)(p / 100.0 * h->total);
    uint64_t cumul  = 0;
    for (int i = 0; i < HIST_BUCKETS; i++) {
        cumul += h->counts[i];
        if (cumul >= target)
            return (i + 0.5) * h->bucket_width_us;
    }
    return h->max_us;
}

/* ── Child worker process ───────────────────────────────────────── */
/*
 * Each child in the ring:
 *  1. Reads one byte from pipe_in  (blocks = yields CPU)
 *  2. Writes one byte to pipe_out  (wakes next process)
 * Repeating this creates a round-robin token-passing ring.
 */
static void child_worker(int pipe_in, int pipe_out,
                          volatile char *ws_buf, size_t ws_bytes,
                          int iterations, int cpu)
{
    pin_to_cpu(cpu);
    char token = 'x';

    for (int i = 0; i < iterations; i++) {
        /* Block until we receive the token (context switch happens here) */
        if (read(pipe_in, &token, 1) != 1) {
            perror("child read");
            exit(EXIT_FAILURE);
        }

        /* Touch working set to model cache state at switch point */
        if (ws_bytes > 0)
            touch_working_set(ws_buf, ws_bytes);

        /* Pass token to next process in ring */
        if (write(pipe_out, &token, 1) != 1) {
            perror("child write");
            exit(EXIT_FAILURE);
        }
    }
    exit(EXIT_SUCCESS);
}

/* ── Comparison for qsort ───────────────────────────────────────── */
static int cmp_double(const void *a, const void *b)
{
    double x = *(const double*)a;
    double y = *(const double*)b;
    return (x > y) - (x < y);
}

/* ── Main measurement loop (parent process) ─────────────────────── */
static void run_benchmark(int n_procs, int iterations,
                           size_t ws_bytes, int cpu,
                           const char *outfile)
{
    /*
     * Build a ring of pipes:
     *   parent → child[0] → child[1] → … → child[n-2] → parent
     *
     * pipe[i][READ]  is read  by process i
     * pipe[i][WRITE] is written by process i-1
     */
    int (*pipes)[2] = malloc(n_procs * sizeof(*pipes));
    if (!pipes) { perror("malloc pipes"); exit(1); }

    for (int i = 0; i < n_procs; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); exit(1); }
        /* Set O_NONBLOCK on write end so we don't deadlock */
    }

    /* Allocate the shared working-set buffer (each process has its own copy
     * after fork, so we allocate before fork and each child sees the data) */
    volatile char *ws_buf = NULL;
    if (ws_bytes > 0) {
        ws_buf = (volatile char *)malloc(ws_bytes);
        if (!ws_buf) { perror("malloc ws"); exit(1); }
        memset((void*)ws_buf, 0xAB, ws_bytes);  /* pre-fault all pages */
    }

    /* ── Fork child workers ──────────────────────────────────────── */
    pid_t pids[MAX_PROCS];
    for (int i = 0; i < n_procs - 1; i++) {
        /*
         * Child i:
         *   reads from  pipes[i]
         *   writes to   pipes[(i+1) % n_procs]
         */
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); exit(1); }

        if (pids[i] == 0) {
            /* ── Child process ── */
            /* Close all pipe ends we don't need */
            for (int j = 0; j < n_procs; j++) {
                if (j != i)               close(pipes[j][0]);
                if (j != (i+1) % n_procs) close(pipes[j][1]);
            }
            child_worker(pipes[i][0],
                         pipes[(i+1) % n_procs][1],
                         ws_buf, ws_bytes,
                         iterations, cpu);
            /* child_worker calls exit() internally */
        }
    }

    /* ── Parent acts as the last node in the ring ─────────────────
     * Parent reads from  pipes[n_procs-1]
     * Parent writes to   pipes[0]
     */
    /* Close pipe ends we don't use */
    for (int i = 0; i < n_procs; i++) {
        if (i != 0)           close(pipes[i][1]); /* parent only writes to 0 */
        if (i != n_procs - 1) close(pipes[i][0]); /* parent only reads from n-1 */
    }

    /* Detect TSC frequency once before the loop */
    double tsc_ghz   = measure_tsc_freq_ghz();
    double ns_per_tick = 1.0 / tsc_ghz;

    /* Storage for per-iteration latency samples */
    double *latencies = malloc(iterations * sizeof(double));
    if (!latencies) { perror("malloc latencies"); exit(1); }

    Histogram hist;
    hist_init(&hist);

    /* ── Warm-up: 100 rounds not measured ── */
    char token = 'x';
    for (int w = 0; w < 100; w++) {
        write(pipes[0][1], &token, 1);
        read(pipes[n_procs-1][0], &token, 1);
    }

    /* ── Measured iterations ──────────────────────────────────────── */
    for (int i = 0; i < iterations; i++) {
        uint64_t t0, t1;

        t0 = rdtsc_serialize();

        /* Start the token around the ring */
        if (write(pipes[0][1], &token, 1) != 1) { perror("write"); exit(1); }
        /* Wait for it to come back (n_procs context switches happen) */
        if (read(pipes[n_procs-1][0], &token, 1) != 1) { perror("read"); exit(1); }

        t1 = rdtsc_serialize();

        /*
         * t1 - t0 = total ticks for ONE complete ring rotation.
         * One ring rotation = n_procs context switches.
         * Latency per switch = total_ns / n_procs.
         */
        double total_ns  = (double)(t1 - t0) * ns_per_tick;
        double us_per_cs = (total_ns / n_procs) / 1000.0;

        latencies[i] = us_per_cs;
        hist_add(&hist, us_per_cs);
    }

    /* ── Compute statistics ─────────────────────────────────────── */
    qsort(latencies, iterations, sizeof(double), cmp_double);

    double mean = hist.sum_us / hist.total;

    /* Variance and stddev */
    double var = 0.0;
    for (int i = 0; i < iterations; i++) {
        double d = latencies[i] - mean;
        var += d * d;
    }
    var /= iterations;
    double stddev = sqrt(var);

    double p50 = latencies[(int)(0.50 * iterations)];
    double p95 = latencies[(int)(0.95 * iterations)];
    double p99 = latencies[(int)(0.99 * iterations)];

    /* ── Print results ────────────────────────────────────────────── */
    printf("=== Context Switch Latency Results ===\n");
    printf("Processes      : %d\n",         n_procs);
    printf("Working set    : %zu KB\n",     ws_bytes / 1024);
    printf("Iterations     : %d\n",         iterations);
    printf("TSC freq       : %.3f GHz\n",   tsc_ghz);
    printf("CPU pinned to  : %s\n",         cpu < 0 ? "none (free)" : "");
    printf("--------------------------------------\n");
    printf("Min latency    : %.3f µs\n",    hist.min_us);
    printf("Mean latency   : %.3f µs\n",    mean);
    printf("Stddev         : %.3f µs\n",    stddev);
    printf("Median (p50)   : %.3f µs\n",    p50);
    printf("p95            : %.3f µs\n",    p95);
    printf("p99            : %.3f µs\n",    p99);
    printf("Max latency    : %.3f µs\n",    hist.max_us);
    printf("--------------------------------------\n");

    /* ── Optional: save raw CSV for plotting ─────────────────────── */
    if (outfile) {
        FILE *fp = fopen(outfile, "w");
        if (fp) {
            fprintf(fp, "iteration,latency_us\n");
            /* Note: we print in original order for time-series plots */
            /* latencies[] was sorted so we just print sorted samples */
            for (int i = 0; i < iterations; i++)
                fprintf(fp, "%d,%.4f\n", i, latencies[i]);
            fclose(fp);
            printf("Raw data saved to: %s\n", outfile);
        }
    }

    /* ── Save histogram CSV ───────────────────────────────────────── */
    {
        char histfile[256];
        snprintf(histfile, sizeof(histfile),
                 outfile ? "%s.hist" : "ctx_hist.csv", outfile ? outfile : "");
        FILE *fp = fopen(histfile, "w");
        if (fp) {
            fprintf(fp, "latency_us,count\n");
            for (int i = 0; i < HIST_BUCKETS; i++) {
                double center = (i + 0.5) * hist.bucket_width_us;
                fprintf(fp, "%.3f,%lu\n", center,
                        (unsigned long)hist.counts[i]);
            }
            fclose(fp);
        }
    }

    /* ── Summary line (machine-readable for scripts) ─────────────── */
    printf("SUMMARY: procs=%d ws_kb=%zu mean_us=%.3f p50_us=%.3f p99_us=%.3f\n",
           n_procs, ws_bytes/1024, mean, p50, p99);

    /* ── Reap children ────────────────────────────────────────────── */
    for (int i = 0; i < n_procs - 1; i++) {
        waitpid(pids[i], NULL, 0);
    }

    free(latencies);
    free(pipes);
    if (ws_buf) free((void*)ws_buf);
}

/* ── Entry point ────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    int    n_procs    = DEFAULT_PROCS;
    int    iterations = DEFAULT_ITERS;
    int    ws_kb      = DEFAULT_WS_KB;
    int    cpu        = DEFAULT_CPU_MASK;
    char  *outfile    = NULL;

    /* Parse command-line arguments */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i+1 < argc)
            n_procs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i+1 < argc)
            ws_kb = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-i") && i+1 < argc)
            iterations = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-c") && i+1 < argc)
            cpu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i+1 < argc)
            outfile = argv[++i];
        else if (!strcmp(argv[i], "-h")) {
            printf("Usage: %s [-n procs] [-s ws_kb] [-i iters] "
                   "[-c cpu] [-o outfile]\n", argv[0]);
            return 0;
        }
    }

    /* Validation */
    if (n_procs < 2 || n_procs > MAX_PROCS) {
        fprintf(stderr, "Error: -n must be 2..%d\n", MAX_PROCS);
        return 1;
    }

    size_t ws_bytes = (size_t)ws_kb * 1024;

    printf("Starting benchmark: %d processes, %d KB working set, "
           "%d iterations\n", n_procs, ws_kb, iterations);

    run_benchmark(n_procs, iterations, ws_bytes, cpu, outfile);

    return 0;
}
