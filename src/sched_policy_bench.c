/*
 * sched_policy_bench.c
 * ====================
 * NOVELTY 3 — "Scheduler Policy Latency Comparison"
 *
 * PURPOSE:
 *   Compare context-switch latency under different Linux scheduling policies:
 *
 *   SCHED_OTHER (CFS) — default, fairness-based, dynamic priority
 *   SCHED_FIFO        — real-time, FIFO within priority, no preemption
 *   SCHED_RR          — real-time, round-robin with time quanta
 *   SCHED_BATCH       — CFS variant optimised for throughput (longer quanta)
 *   SCHED_IDLE        — lowest priority, runs only when nothing else does
 *
 * THEORY:
 *   Real-time policies (FIFO/RR) bypass the CFS runqueue entirely, so the
 *   scheduler decision is O(1) vs O(log N) for CFS.  This should reduce
 *   scheduling jitter and mean latency — especially under load.
 *
 * METHOD:
 *   For each policy:
 *     1. Set policy via sched_setscheduler()
 *     2. Run 10,000 ping-pong rounds between two processes
 *     3. Report mean / p50 / p99 latency
 *
 * NOTE:
 *   SCHED_FIFO and SCHED_RR require CAP_SYS_NICE (run as root or with sudo).
 *   If permission is denied, the benchmark prints a warning and skips.
 *
 * OUTPUT:
 *   CSV: policy, mean_us, p50_us, p99_us, stddev_us
 *
 * BUILD:
 *   gcc -O2 -o sched_policy_bench sched_policy_bench.c -lm
 *
 * USAGE:
 *   sudo ./sched_policy_bench [-o <outfile.csv>]
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

#define ITERATIONS   10000
#define WARMUP       200

/* ── RDTSC ──────────────────────────────────────────────────────── */
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("cpuid\n\trdtsc" : "=a"(lo),"=d"(hi) :: "%rbx","%rcx");
    return ((uint64_t)hi << 32) | lo;
}

static double tsc_ghz(void)
{
    struct timespec t0, t1; uint64_t r0, r1;
    clock_gettime(CLOCK_MONOTONIC, &t0); r0 = rdtsc();
    usleep(200000);
    r1 = rdtsc(); clock_gettime(CLOCK_MONOTONIC, &t1);
    double ns = (t1.tv_sec-t0.tv_sec)*1e9 + (t1.tv_nsec-t0.tv_nsec);
    return (double)(r1-r0)/ns;
}

/* ── Set scheduling policy for calling process + its children ────── */
/*
 * Returns 0 on success, -1 on failure (e.g. insufficient privilege).
 * priority: for FIFO/RR use 1..99; for others use 0.
 */
static int set_policy(int policy, int priority)
{
    struct sched_param sp;
    sp.sched_priority = priority;
    if (sched_setscheduler(0, policy, &sp) < 0) {
        fprintf(stderr, "[warn] sched_setscheduler(%d, prio=%d): %s\n",
                policy, priority, strerror(errno));
        return -1;
    }
    return 0;
}

/* ── Restore to CFS (SCHED_OTHER, prio=0) ───────────────────────── */
static void restore_cfs(void)
{
    struct sched_param sp = {0};
    sched_setscheduler(0, SCHED_OTHER, &sp);
}

/* ── Child worker (same for all policies) ───────────────────────── */
/* Child must set the same policy as parent after fork */
static void child_loop(int rfd, int wfd, int policy, int prio, int iters)
{
    /* Child also switches to the requested policy */
    set_policy(policy, prio);

    char t;
    for (int i = 0; i < iters + WARMUP; i++) {
        if (read(rfd, &t, 1) != 1) exit(1);
        if (write(wfd, &t, 1) != 1) exit(1);
    }
    exit(0);
}

/* ── Run benchmark under one scheduling policy ──────────────────── */
typedef struct { double mean, p50, p99, stddev; } Stat;

static int run_policy(int policy, int prio, double ghz, Stat *out)
{
    /* Set policy on parent BEFORE fork so child inherits it */
    if (set_policy(policy, prio) < 0) return -1;

    int p2c[2], c2p[2];
    pipe(p2c); pipe(c2p);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); restore_cfs(); return -1; }

    if (pid == 0) {
        close(p2c[1]); close(c2p[0]);
        child_loop(p2c[0], c2p[1], policy, prio, ITERATIONS);
    }

    close(p2c[0]); close(c2p[1]);

    double *lat = malloc(ITERATIONS * sizeof(double));
    char t = 'x';
    double sum = 0;

    /* Warm-up rounds — not measured */
    for (int w = 0; w < WARMUP; w++) {
        write(p2c[1], &t, 1);
        read(c2p[0], &t, 1);
    }

    /* Measured rounds */
    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t a = rdtsc();
        write(p2c[1], &t, 1);
        read(c2p[0], &t, 1);
        uint64_t b = rdtsc();
        /* round-trip / 2 = per-switch cost */
        double us = ((double)(b-a)/ghz) / 2000.0;
        lat[i] = us;
        sum   += us;
    }

    double mean = sum / ITERATIONS;
    /* sort */
    for (int i = 1; i < ITERATIONS; i++) {
        double key = lat[i]; int j = i-1;
        while (j >= 0 && lat[j] > key) { lat[j+1] = lat[j]; j--; }
        lat[j+1] = key;
    }
    double var = 0;
    for (int i = 0; i < ITERATIONS; i++) { double d=lat[i]-mean; var+=d*d; }

    out->mean   = mean;
    out->p50    = lat[(int)(0.50*ITERATIONS)];
    out->p99    = lat[(int)(0.99*ITERATIONS)];
    out->stddev = sqrt(var/ITERATIONS);

    free(lat);
    waitpid(pid, NULL, 0);

    /* Restore parent to CFS after each test */
    restore_cfs();
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *outfile = "results/sched_policy.csv";
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-o") && i+1 < argc) outfile = argv[++i];

    printf("Detecting TSC frequency...\n");
    double ghz = tsc_ghz();
    printf("TSC frequency: %.3f GHz\n\n", ghz);

    FILE *fp = fopen(outfile, "w");
    if (!fp) { perror("fopen"); return 1; }
    fprintf(fp, "policy,mean_us,p50_us,p99_us,stddev_us\n");

    printf("%-18s  %-10s  %-10s  %-10s  %-10s\n",
           "Policy", "Mean(µs)", "P50(µs)", "P99(µs)", "Stddev");
    printf("%-18s  %-10s  %-10s  %-10s  %-10s\n",
           "------","--------","-------","-------","------");

    /* Policy table: {name, SCHED_* constant, priority} */
    struct { const char *name; int policy; int prio; } tests[] = {
        {"SCHED_OTHER (CFS)", SCHED_OTHER, 0  },
        {"SCHED_BATCH",       SCHED_BATCH, 0  },
        {"SCHED_IDLE",        SCHED_IDLE,  0  },
        {"SCHED_FIFO  (RT)",  SCHED_FIFO,  10 },  /* prio 10 = moderate RT */
        {"SCHED_RR    (RT)",  SCHED_RR,    10 },
    };
    int n = (int)(sizeof(tests)/sizeof(tests[0]));

    for (int k = 0; k < n; k++) {
        Stat s;
        int rc = run_policy(tests[k].policy, tests[k].prio, ghz, &s);
        if (rc < 0) {
            printf("%-18s  [SKIPPED — need root / CAP_SYS_NICE]\n",
                   tests[k].name);
            fprintf(fp, "%s,N/A,N/A,N/A,N/A\n", tests[k].name);
            continue;
        }
        printf("%-18s  %-10.3f  %-10.3f  %-10.3f  %-10.3f\n",
               tests[k].name, s.mean, s.p50, s.p99, s.stddev);
        fprintf(fp, "%s,%.4f,%.4f,%.4f,%.4f\n",
                tests[k].name, s.mean, s.p50, s.p99, s.stddev);
    }

    fclose(fp);
    printf("\nResults written to: %s\n", outfile);
    printf("\nNote: For SCHED_FIFO/SCHED_RR re-run with:  sudo ./sched_policy_bench\n");
    return 0;
}
