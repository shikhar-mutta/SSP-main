/**
 * SSP Benchmark Suite - Main Orchestrator
 *
 * Supports variable core counts (--cores N): spawns N parallel load_generator
 * processes to exercise multi-core scheduling.  Latency is computed from real
 * measured operation timings emitted by each child process.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>
#include "ssp_lib.h"

#define CSV_FILE "results/workload_benchmark.csv"
#define MAX_CORES 64
/**
 * SSP Benchmark Suite - Main Orchestrator (with Scheduling Analysis)
 *
 * Supports variable core counts (--cores N): spawns N parallel load_generator
 * processes to exercise multi-core scheduling.  Latency is computed from real
 * measured operation timings emitted by each child process.
 * 
 * FIX for Issue 3: Integrates sched_switch tracing to capture context-switch
 * events and correlate with latency for real scheduling insights.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>
#include <signal.h>
#include "ssp_lib.h"
#include "sched_tracer.h"

#define CSV_FILE "results/workload_benchmark.csv"
#define MAX_CORES 64

void print_usage(const char *prog) {
    printf("SSP Benchmark Suite - Main Orchestrator\n\n");
    printf("Usage: %s --type <TYPE> --intensity <INT> --duration <DUR> [--repeat <N>] [--cores <C>]\n", prog);
    printf("  TYPE:      cpu | io | memory | mixed\n");
    printf("  INTENSITY: 0-100\n");
    printf("  DUR:       seconds per run\n");
    printf("  N:         repetitions (default: 1)\n");
    printf("  C:         active core count (default: 1)\n");
}

static int read_metrics_file(const char *path, ssp_metrics_t *out)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    unsigned long long ops = 0ULL, total_ns = 0ULL;
    unsigned long long io_cycles = 0ULL, io_cycle_total_ns = 0ULL;
    long double sum_sq_ns = 0.0L, io_cycle_sum_sq_ns = 0.0L;
    int rc = fscanf(fp, "%llu,%llu,%Lf,%llu,%llu,%Lf",
                    &ops, &total_ns, &sum_sq_ns,
                    &io_cycles, &io_cycle_total_ns, &io_cycle_sum_sq_ns);
    fclose(fp);

    if (rc != 3 && rc != 6) return -1;

    out->operations = (uint64_t)ops;
    out->total_latency_ns = (uint64_t)total_ns;
    out->sum_latency_sq_ns = sum_sq_ns;
    if (rc == 6) {
        out->io_cycles = (uint64_t)io_cycles;
        out->io_cycle_total_latency_ns = (uint64_t)io_cycle_total_ns;
        out->io_cycle_sum_latency_sq_ns = io_cycle_sum_sq_ns;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    char type[32] = "";
    int intensity = -1, duration = -1, repeat = 1, active_cores = 1;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--type") == 0 && i+1 < argc) {
            strncpy(type, argv[++i], sizeof(type)-1);
        } else if (strcmp(argv[i], "--intensity") == 0 && i+1 < argc) {
            intensity = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--duration") == 0 && i+1 < argc) {
            duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--repeat") == 0 && i+1 < argc) {
            repeat = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cores") == 0 && i+1 < argc) {
            active_cores = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!type[0] || intensity < 0 || duration <= 0) {
        print_usage(argv[0]);
        return 1;
    }

    /* Clamp cores to hardware */
    int total_cores = get_nprocs();
    if (active_cores < 1) active_cores = 1;
    if (active_cores > total_cores) active_cores = total_cores;
    if (active_cores > MAX_CORES) active_cores = MAX_CORES;

    /* Get real hostname */
    char hostname[128] = "unknown";
    gethostname(hostname, sizeof(hostname) - 1);

    /* Open CSV */
    FILE *csv = fopen(CSV_FILE, "a");
    if (!csv) { perror("fopen CSV"); return 1; }

    fseek(csv, 0, SEEK_END);
    if (ftell(csv) == 0) {
        fprintf(csv, "timestamp,os,hostname,total_cores,active_cores,workload_type,"
                 "intensity_pct,data_size_kb,num_procs,latency_us,std_us,io_cycle_latency_us,io_cycle_std_us,method,sched_contention\n");
        fflush(csv);
    }

    for (int run = 1; run <= repeat; ++run) {
        printf("[Run %d/%d] workload=%s  intensity=%d%%  cores=%d  duration=%ds\n",
               run, repeat, type, intensity, active_cores, duration);
          fflush(stdout);   /* flush before fork so the child doesn't inherit a non-empty buffer */

          /* ── Spawn active_cores parallel load_generator processes ────────── */
                /* ── Start scheduling trace (if available) ─────────────────────── */
                sched_trace_t *sched_trace = NULL;
                double sched_contention = 0.0;
                if (getuid() == 0 || geteuid() == 0) {
                    /* Root available: start bpftrace sched_switch tracing */
                    char trace_name[32];
                    snprintf(trace_name, sizeof(trace_name), "%s_run%d", type, run);
                    sched_trace = sched_trace_start(trace_name);
                    if (sched_trace) {
                        fprintf(stderr, "[Scheduling trace started for run %d]\n", run);
                    }
                }

        char int_str[16], dur_str[16];
        snprintf(int_str, sizeof(int_str), "%d", intensity);
        snprintf(dur_str, sizeof(dur_str), "%d", duration);

        pid_t pids[MAX_CORES];
        char metrics_paths[MAX_CORES][128];
        int spawned = 0;
        for (int c = 0; c < active_cores; c++) {
            snprintf(metrics_paths[c], sizeof(metrics_paths[c]),
                     "/tmp/ssp_metrics_%d_%d_%d.csv", getpid(), run, c);

            pid_t pid = fork();
            if (pid == 0) {
                /* Child: redirect stdout/stderr to /dev/null to keep console clean */
                freopen("/dev/null", "w", stdout);
                freopen("/dev/null", "w", stderr);
                execl("./src/load_generator", "./src/load_generator",
                      "--type", type, "--intensity", int_str,
                      "--duration", dur_str,
                      "--metrics-file", metrics_paths[c], NULL);
                _exit(1);
            } else if (pid > 0) {
                pids[spawned++] = pid;
            }
        }
        /* Wait for all children */
        for (int c = 0; c < spawned; c++) {
            waitpid(pids[c], NULL, 0);
        }

        /* ── Stop scheduling trace ────────────────────────────────────── */
        if (sched_trace) {
            int sched_events = sched_trace_stop(sched_trace);
            sched_contention = sched_trace_contention_score(sched_trace);
            fprintf(stderr, "[Scheduling trace: %d events, contention=%.3f]\n", 
                    sched_events, sched_contention);
            sched_trace_free(sched_trace);
        }

        /* ── Aggregate measured metrics from children ─────────────────────── */
        ssp_metrics_t total_metrics;
        ssp_metrics_init(&total_metrics);

        for (int c = 0; c < spawned; c++) {
            ssp_metrics_t child;
            ssp_metrics_init(&child);
            if (read_metrics_file(metrics_paths[c], &child) == 0) {
                ssp_metrics_merge(&total_metrics, &child);
            }
            unlink(metrics_paths[c]);
        }

        if (total_metrics.operations == 0) {
            fprintf(stderr, "Warning: no measured operations collected for run %d (type=%s)\n", run, type);
            continue;
        }

        long double mean_ns = (long double)total_metrics.total_latency_ns / (long double)total_metrics.operations;
        long double second_moment = total_metrics.sum_latency_sq_ns / (long double)total_metrics.operations;
        long double variance_ns2 = second_moment - mean_ns * mean_ns;
        if (variance_ns2 < 0.0L) variance_ns2 = 0.0L;

        double latency_us = (double)(mean_ns / 1000.0L);
        double std_us = (double)(sqrt((double)variance_ns2) / 1000.0);
        double io_cycle_latency_us = 0.0;
        double io_cycle_std_us = 0.0;

        if (total_metrics.io_cycles > 0) {
            long double io_cycle_mean_ns =
                (long double)total_metrics.io_cycle_total_latency_ns / (long double)total_metrics.io_cycles;
            long double io_cycle_second_moment =
                total_metrics.io_cycle_sum_latency_sq_ns / (long double)total_metrics.io_cycles;
            long double io_cycle_variance_ns2 = io_cycle_second_moment - io_cycle_mean_ns * io_cycle_mean_ns;
            if (io_cycle_variance_ns2 < 0.0L) io_cycle_variance_ns2 = 0.0L;

            io_cycle_latency_us = (double)(io_cycle_mean_ns / 1000.0L);
            io_cycle_std_us = (double)(sqrt((double)io_cycle_variance_ns2) / 1000.0L);
        }

        /* ── Timestamp & write CSV row ─────────────────────────────────────── */
        time_t now = time(NULL);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));

        /* data_size_kb: meaningful only for memory/io workloads */
        int data_size_kb = 0;
        if (strcmp(type, "memory") == 0) data_size_kb = (512 * intensity / 100) * 1024;
        else if (strcmp(type, "io") == 0)  data_size_kb = 128 * intensity / 100;

        fprintf(csv, "%s,Linux,%s,%d,%d,%s,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,measured_ops,%.6f\n",
            ts, hostname, total_cores, active_cores,
            type, intensity, data_size_kb, active_cores,
            latency_us, std_us, io_cycle_latency_us, io_cycle_std_us, sched_contention);
        fflush(csv);
    }

    fclose(csv);
    printf("Results appended to %s\n", CSV_FILE);
    return 0;
}
