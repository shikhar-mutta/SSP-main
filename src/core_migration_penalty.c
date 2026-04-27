/*
 * core_migration_penalty.c
 * ========================
 * NOVELTY 2 — "Core Migration Penalty Analysis"
 *
 * PURPOSE:
 *   Measure and compare context-switch latency under four CPU affinity scenarios:
 *
 *   (A) SAME_CORE_HT   — both processes on sibling HyperThreads of the SAME physical core
 *                         (cores 0 and 1 on Intel i5-1035G1 share one physical core)
 *   (B) DIFF_CORE_SAME — processes on two DIFFERENT physical cores, same NUMA node
 *   (C) FREE           — no affinity pinning (OS scheduler decides)
 *   (D) SCALING        — vary number of processes 2→16 with no pinning
 *
 * INSIGHT:
 *   Same-physical-core HT switches are cheaper (shared L1/L2).
 *   Cross-core switches force full TLB shootdown + cache-line transfer.
 *   This is the "Core Migration Penalty" — measured in microseconds.
 *
 * METHOD:
 *   Token-passing pipe ring (same as ctx_switch_bench.c).
 *   sched_setaffinity() pins each process to specified cores.
 *
 * OUTPUT:
 *   CSV:  scenario, n_procs, mean_us, p50_us, p99_us
 *   Console: human-readable table
 *
 * BUILD:
 *   gcc -O2 -o core_migration_penalty core_migration_penalty.c -lm
 *
 * Author: SSP Final Project, 2026
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

#define ITERATIONS    8000
#define MAX_PROCS     16
#define WARMUP        200

/* ── RDTSC ──────────────────────────────────────────────────────── */
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("cpuid\n\trdtsc" : "=a"(lo),"=d"(hi) :: "%rbx","%rcx");
    return ((uint64_t)hi << 32) | lo;
}

/* ── TSC frequency ──────────────────────────────────────────────── */
static double tsc_ghz(void)
{
    struct timespec t0, t1; uint64_t r0, r1;
    clock_gettime(CLOCK_MONOTONIC, &t0); r0 = rdtsc();
    usleep(200000);
    r1 = rdtsc(); clock_gettime(CLOCK_MONOTONIC, &t1);
    double ns = (t1.tv_sec-t0.tv_sec)*1e9 + (t1.tv_nsec-t0.tv_nsec);
    return (double)(r1-r0)/ns;
}

/* ── Pin process to a specific CPU ──────────────────────────────── */
static void pin_cpu(int cpu)
{
    if (cpu < 0) return;
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
    if (sched_setaffinity(0, sizeof(s), &s) < 0) {
        /* non-fatal: print warning and continue */
        fprintf(stderr, "[warn] Cannot pin to cpu %d: %s\n",
                cpu, strerror(errno));
    }
}

/* ── Result structure ───────────────────────────────────────────── */
typedef struct {
    double mean_us, p50_us, p99_us, stddev_us;
} Result;

/* ── Core benchmark: ring of n_procs, each pinned to cpus[] ─────── */
/*
 * cpus[i] = CPU to pin process i to (-1 = free).
 * Returns per-context-switch latency statistics.
 */
static Result run_ring(int n_procs, int cpus[], double ghz)
{
    /* Build n_procs pipes forming a ring */
    int (*pipes)[2] = malloc(n_procs * sizeof(*pipes));
    for (int i = 0; i < n_procs; i++) pipe(pipes[i]);

    pid_t pids[MAX_PROCS];

    for (int i = 0; i < n_procs - 1; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            /* Pin this child to its assigned CPU */
            pin_cpu(cpus ? cpus[i] : -1);

            /* Close unneeded pipe ends */
            for (int j = 0; j < n_procs; j++) {
                if (j != i)               close(pipes[j][0]);
                if (j != (i+1)%n_procs)   close(pipes[j][1]);
            }

            char t;
            for (int k = 0; k < ITERATIONS + WARMUP; k++) {
                read(pipes[i][0], &t, 1);
                write(pipes[(i+1)%n_procs][1], &t, 1);
            }
            exit(0);
        }
    }

    /* Parent = last node in ring */
    pin_cpu(cpus ? cpus[n_procs-1] : -1);
    for (int i = 0; i < n_procs; i++) {
        if (i != 0)           close(pipes[i][1]);
        if (i != n_procs-1)   close(pipes[i][0]);
    }

    double *lat = malloc(ITERATIONS * sizeof(double));
    char t = 'x';
    double sum = 0;

    /* Warm-up */
    for (int w = 0; w < WARMUP; w++) {
        write(pipes[0][1], &t, 1);
        read(pipes[n_procs-1][0], &t, 1);
    }

    /* Measured rounds */
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t a = rdtsc();
        write(pipes[0][1], &t, 1);
        read(pipes[n_procs-1][0], &t, 1);
        uint64_t b = rdtsc();

        double us = ((double)(b-a)/ghz) / ((double)n_procs * 1000.0);
        lat[i] = us;
        sum   += us;
    }

    /* Sort for percentiles */
    for (int i = 1; i < ITERATIONS; i++) {
        double key = lat[i]; int j = i-1;
        while (j >= 0 && lat[j] > key) { lat[j+1] = lat[j]; j--; }
        lat[j+1] = key;
    }

    double mean = sum / ITERATIONS;
    double var  = 0;
    for (int i = 0; i < ITERATIONS; i++) { double d=lat[i]-mean; var+=d*d; }

    Result r;
    r.mean_us  = mean;
    r.p50_us   = lat[(int)(0.50*ITERATIONS)];
    r.p99_us   = lat[(int)(0.99*ITERATIONS)];
    r.stddev_us= sqrt(var/ITERATIONS);

    free(lat);
    free(pipes);
    for (int i = 0; i < n_procs-1; i++) waitpid(pids[i], NULL, 0);
    return r;
}

/* ── Main ───────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *outfile = "results/core_migration.csv";
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-o") && i+1 < argc) outfile = argv[++i];

    printf("Detecting TSC frequency...\n");
    double ghz = tsc_ghz();
    printf("TSC frequency: %.3f GHz\n\n", ghz);

    FILE *fp = fopen(outfile, "w");
    if (!fp) { perror("fopen"); return 1; }
    fprintf(fp, "scenario,n_procs,mean_us,p50_us,p99_us,stddev_us\n");

    printf("%-28s  %-6s  %-10s  %-10s  %-10s  %-10s\n",
           "Scenario", "Procs", "Mean(µs)", "P50(µs)", "P99(µs)", "Stddev");
    printf("%-28s  %-6s  %-10s  %-10s  %-10s  %-10s\n",
           "--------------------------","-----","--------","-------","-------","------");

    /* ──────────────────────────────────────────────────────────────
     * Scenario A: Same Physical Core — HyperThread siblings
     * On Intel i5-1035G1: CPU0+CPU1 are HT siblings of physical core 0
     *                      CPU2+CPU3 are HT siblings of physical core 1
     * ──────────────────────────────────────────────────────────── */
    {
        int cpus[] = {0, 1};   /* Two HT siblings of the SAME physical core */
        Result r = run_ring(2, cpus, ghz);
        printf("%-28s  %-6d  %-10.3f  %-10.3f  %-10.3f  %-10.3f\n",
               "Same Core (HT siblings)", 2, r.mean_us, r.p50_us, r.p99_us, r.stddev_us);
        fprintf(fp, "same_core_ht,2,%.4f,%.4f,%.4f,%.4f\n",
                r.mean_us, r.p50_us, r.p99_us, r.stddev_us);
    }

    /* ──────────────────────────────────────────────────────────────
     * Scenario B: Different Physical Cores (CPU0 vs CPU2)
     * CPU0 = physical core 0, CPU2 = physical core 1 (different L1/L2)
     * ──────────────────────────────────────────────────────────── */
    {
        int cpus[] = {0, 2};   /* Different physical cores */
        Result r = run_ring(2, cpus, ghz);
        printf("%-28s  %-6d  %-10.3f  %-10.3f  %-10.3f  %-10.3f\n",
               "Diff Core (same socket)", 2, r.mean_us, r.p50_us, r.p99_us, r.stddev_us);
        fprintf(fp, "diff_core,2,%.4f,%.4f,%.4f,%.4f\n",
                r.mean_us, r.p50_us, r.p99_us, r.stddev_us);
    }

    /* ──────────────────────────────────────────────────────────────
     * Scenario C: No affinity (OS decides — scheduler may migrate)
     * ──────────────────────────────────────────────────────────── */
    {
        Result r = run_ring(2, NULL, ghz);
        printf("%-28s  %-6d  %-10.3f  %-10.3f  %-10.3f  %-10.3f\n",
               "Free (no affinity)", 2, r.mean_us, r.p50_us, r.p99_us, r.stddev_us);
        fprintf(fp, "free_sched,2,%.4f,%.4f,%.4f,%.4f\n",
                r.mean_us, r.p50_us, r.p99_us, r.stddev_us);
    }

    /* ──────────────────────────────────────────────────────────────
     * Scenario D: Process count scaling (2 → 16), no pinning
     * Shows scheduler overhead as contention grows
     * ──────────────────────────────────────────────────────────── */
    int scale_counts[] = {2, 4, 6, 8, 10, 12, 16};
    int n_scale = (int)(sizeof(scale_counts)/sizeof(scale_counts[0]));

    for (int k = 0; k < n_scale; k++) {
        int n = scale_counts[k];
        Result r = run_ring(n, NULL, ghz);
        char label[40];
        snprintf(label, sizeof(label), "Scaling (free, n=%d)", n);
        printf("%-28s  %-6d  %-10.3f  %-10.3f  %-10.3f  %-10.3f\n",
               label, n, r.mean_us, r.p50_us, r.p99_us, r.stddev_us);
        fprintf(fp, "scaling_free,%d,%.4f,%.4f,%.4f,%.4f\n",
                n, r.mean_us, r.p50_us, r.p99_us, r.stddev_us);
    }

    fclose(fp);
    printf("\nResults written to: %s\n", outfile);
    return 0;
}
