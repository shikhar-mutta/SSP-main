/*
 * process_vs_thread.c
 * ====================
 * NOVELTY 4 — "Process vs Thread Context Switch Cost Comparison"
 *
 * PURPOSE:
 *   Directly compare the context-switch overhead of:
 *   (A) PROCESS switches  — fork()-based, separate address spaces,
 *                           full TLB flush on each switch (CR3 reload)
 *   (B) THREAD  switches  — pthread_create()-based, shared address space,
 *                           NO CR3 reload, TLB entries remain valid
 *
 * THEORY:
 *   The core difference between process and thread context switches is:
 *   - Process: OS must save/restore CR3 (page table base pointer),
 *              invalidate TLB, reload segment descriptors.
 *   - Thread:  CR3 stays the same (shared VM), TLB is NOT flushed.
 *              Only register file + kernel stack need saving.
 *   Expected: thread switches ~20–40% faster than process switches.
 *
 * METHOD:
 *   - Process version:  two child processes, pipe ping-pong (same as before)
 *   - Thread  version:  two pthreads, pass token via condition variable +
 *                       mutex (forces voluntary context switch / futex)
 *
 * OUTPUT:
 *   CSV: switch_type, mean_us, p50_us, p99_us, stddev_us
 *
 * BUILD:
 *   gcc -O2 -o process_vs_thread process_vs_thread.c -lm -lpthread
 *
 * Author: SSP Final Project, 2026
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>

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
    double ns = (t1.tv_sec-t0.tv_sec)*1e9+(t1.tv_nsec-t0.tv_nsec);
    return (double)(r1-r0)/ns;
}

static double ghz_global; /* set once in main */

typedef struct { double mean, p50, p99, stddev; } Stat;

/* ═══════════════════════════════════════════════════════════════════
 * PART A: Process context switch measurement (pipe-based)
 * ═══════════════════════════════════════════════════════════════════ */
static Stat measure_process_switch(void)
{
    int p2c[2], c2p[2];
    pipe(p2c); pipe(c2p);

    pid_t pid = fork();
    if (pid == 0) {
        close(p2c[1]); close(c2p[0]);
        char t;
        for (int i = 0; i < ITERATIONS + WARMUP; i++) {
            read(p2c[0], &t, 1);
            write(c2p[1], &t, 1);
        }
        exit(0);
    }

    close(p2c[0]); close(c2p[1]);

    double *lat = malloc(ITERATIONS * sizeof(double));
    char t = 'x'; double sum = 0;

    for (int w = 0; w < WARMUP; w++) { write(p2c[1],&t,1); read(c2p[0],&t,1); }

    for (int i = 0; i < ITERATIONS; i++) {
        uint64_t a = rdtsc();
        write(p2c[1], &t, 1);
        read(c2p[0], &t, 1);
        uint64_t b = rdtsc();
        double us = ((double)(b-a)/ghz_global)/2000.0; /* /2 switches */
        lat[i] = us; sum += us;
    }

    double mean = sum/ITERATIONS;
    for (int i=1;i<ITERATIONS;i++){double k=lat[i];int j=i-1;while(j>=0&&lat[j]>k){lat[j+1]=lat[j];j--;}lat[j+1]=k;}
    double var=0; for(int i=0;i<ITERATIONS;i++){double d=lat[i]-mean;var+=d*d;}

    Stat s = { mean, lat[(int)(0.50*ITERATIONS)],
               lat[(int)(0.99*ITERATIONS)], sqrt(var/ITERATIONS) };
    free(lat);
    waitpid(pid, NULL, 0);
    return s;
}

/* ═══════════════════════════════════════════════════════════════════
 * PART B: Thread context switch measurement (futex/condvar-based)
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * Shared state for thread ping-pong.
 * Thread 0 and Thread 1 take turns incrementing 'turn'.
 * Each waits on the condition variable until it is their turn.
 */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    int             turn;       /* 0 = thread-0's turn, 1 = thread-1's turn */
    int             total;      /* total rounds to run                        */
    double         *latencies;
    double          ghz;
    int             done;
} ThreadState;

/* Thread 1 worker: waits for turn=0, signals turn=1 */
static void *thread1_worker(void *arg)
{
    ThreadState *st = (ThreadState*)arg;
    for (int i = 0; i < st->total; i++) {
        pthread_mutex_lock(&st->mu);
        while (st->turn != 1)
            pthread_cond_wait(&st->cv, &st->mu);
        st->turn = 0;
        pthread_cond_signal(&st->cv);
        pthread_mutex_unlock(&st->mu);
    }
    return NULL;
}

static Stat measure_thread_switch(void)
{
    ThreadState st;
    pthread_mutex_init(&st.mu, NULL);
    pthread_cond_init(&st.cv, NULL);
    st.turn     = 0;
    st.total    = ITERATIONS + WARMUP;
    st.ghz      = ghz_global;
    st.done     = 0;
    st.latencies= malloc(ITERATIONS * sizeof(double));

    pthread_t tid;
    pthread_create(&tid, NULL, thread1_worker, &st);

    double *lat = st.latencies;
    double sum  = 0;

    for (int i = 0; i < ITERATIONS + WARMUP; i++) {
        uint64_t a = rdtsc();

        /* Thread 0's turn: signal thread 1 */
        pthread_mutex_lock(&st.mu);
        st.turn = 1;
        pthread_cond_signal(&st.cv);
        /* Wait for thread 1 to return the token */
        while (st.turn != 0)
            pthread_cond_wait(&st.cv, &st.mu);
        pthread_mutex_unlock(&st.mu);

        uint64_t b = rdtsc();

        if (i >= WARMUP) {
            double us = ((double)(b-a)/ghz_global)/2000.0; /* /2 switches */
            lat[i-WARMUP] = us;
            sum += us;
        }
    }

    pthread_join(tid, NULL);

    double mean = sum / ITERATIONS;
    for(int i=1;i<ITERATIONS;i++){double k=lat[i];int j=i-1;while(j>=0&&lat[j]>k){lat[j+1]=lat[j];j--;}lat[j+1]=k;}
    double var=0; for(int i=0;i<ITERATIONS;i++){double d=lat[i]-mean;var+=d*d;}

    Stat s = { mean, lat[(int)(0.50*ITERATIONS)],
               lat[(int)(0.99*ITERATIONS)], sqrt(var/ITERATIONS) };
    free(st.latencies);
    pthread_mutex_destroy(&st.mu);
    pthread_cond_destroy(&st.cv);
    return s;
}

/* ── Main ───────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *outfile = "results/proc_vs_thread.csv";
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-o") && i+1 < argc) outfile = argv[++i];

    printf("Detecting TSC frequency...\n");
    ghz_global = tsc_ghz();
    printf("TSC frequency: %.3f GHz\n\n", ghz_global);

    FILE *fp = fopen(outfile, "w");
    if (!fp) { perror("fopen"); return 1; }
    fprintf(fp, "switch_type,mean_us,p50_us,p99_us,stddev_us\n");

    printf("%-16s  %-10s  %-10s  %-10s  %-10s\n",
           "Switch Type", "Mean(µs)", "P50(µs)", "P99(µs)", "Stddev");
    printf("%-16s  %-10s  %-10s  %-10s  %-10s\n",
           "----------","--------","-------","-------","------");

    printf("Running process switch benchmark...\n");
    Stat ps = measure_process_switch();
    printf("%-16s  %-10.3f  %-10.3f  %-10.3f  %-10.3f\n",
           "Process", ps.mean, ps.p50, ps.p99, ps.stddev);
    fprintf(fp, "process,%.4f,%.4f,%.4f,%.4f\n",
            ps.mean, ps.p50, ps.p99, ps.stddev);

    printf("Running thread  switch benchmark...\n");
    Stat ts = measure_thread_switch();
    printf("%-16s  %-10.3f  %-10.3f  %-10.3f  %-10.3f\n",
           "Thread", ts.mean, ts.p50, ts.p99, ts.stddev);
    fprintf(fp, "thread,%.4f,%.4f,%.4f,%.4f\n",
            ts.mean, ts.p50, ts.p99, ts.stddev);

    double speedup = ps.mean / ts.mean;
    printf("\nThread switch is %.2fx faster than process switch\n", speedup);
    printf("Overhead difference: %.3f µs (TLB flush + CR3 reload cost)\n",
           ps.mean - ts.mean);

    fclose(fp);
    printf("\nResults written to: %s\n", outfile);
    return 0;
}
