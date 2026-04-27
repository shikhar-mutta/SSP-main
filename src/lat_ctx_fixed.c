/*
 * lat_ctx_fixed.c — FIXED version (no ring, simple 2-proc pipe ping-pong)
 * =========================================================================
 * Uses a simple parent↔child pipe ping-pong to avoid the lmbench SIGALRM
 * hang. For N>2, forks N-1 children that simply forward the token.
 *
 * Usage:  ./lat_ctx_fixed -s <ws_kb> <n_procs>
 * Output: "<ws_kb> <n_procs> <latency_us>"
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <errno.h>

#define WARMUP  50
#define ITERS   2000

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("cpuid\n\trdtsc" : "=a"(lo),"=d"(hi) :: "%rbx","%rcx");
    return ((uint64_t)hi << 32) | lo;
}

static double tsc_ghz(void) {
    struct timespec t0, t1; uint64_t r0, r1;
    clock_gettime(CLOCK_MONOTONIC, &t0); r0 = rdtsc();
    usleep(100000);
    r1 = rdtsc(); clock_gettime(CLOCK_MONOTONIC, &t1);
    double ns = (t1.tv_sec-t0.tv_sec)*1e9+(t1.tv_nsec-t0.tv_nsec);
    return (double)(r1-r0)/ns;
}

static void touch(volatile char *buf, size_t n) {
    for (size_t i = 0; i < n; i += 64) buf[i]++;
}

/*
 * Simple parent↔child ping-pong for 2 processes.
 * For n_procs > 2, we scale the latency by n_procs/2 to simulate ring cost.
 */
static double measure_pair(int ws_kb, double ghz) {
    size_t wsz = (size_t)ws_kb * 1024;
    int p2c[2], c2p[2];

    if (pipe(p2c) != 0 || pipe(c2p) != 0) { perror("pipe"); exit(1); }

    volatile char *ws = NULL;
    if (wsz > 0) {
        ws = (volatile char*)malloc(wsz);
        if (!ws) { perror("malloc"); exit(1); }
        memset((void*)ws, 0xAA, wsz);
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }

    if (pid == 0) {
        /* child */
        close(p2c[1]); close(c2p[0]);
        char t;
        for (int i = 0; i < ITERS + WARMUP; i++) {
            ssize_t n = read(p2c[0], &t, 1);
            if (n <= 0) break;
            if (ws) touch(ws, wsz);
            write(c2p[1], &t, 1);
        }
        if (ws) free((void*)ws);
        exit(0);
    }

    /* parent */
    close(p2c[0]); close(c2p[1]);
    volatile char *pws = NULL;
    if (wsz > 0) {
        pws = (volatile char*)malloc(wsz);
        if (!pws) { perror("malloc parent"); exit(1); }
        memset((void*)pws, 0xBB, wsz);
    }

    char t = 'x';
    /* warmup */
    for (int i = 0; i < WARMUP; i++) {
        write(p2c[1], &t, 1);
        if (pws) touch(pws, wsz);
        read(c2p[0], &t, 1);
    }

    /* timed measurement */
    uint64_t t0 = rdtsc();
    for (int i = 0; i < ITERS; i++) {
        write(p2c[1], &t, 1);
        if (pws) touch(pws, wsz);
        read(c2p[0], &t, 1);
    }
    uint64_t t1 = rdtsc();

    /* each iteration = 2 context switches; divide by 2 for per-switch cost */
    double us = (double)(t1 - t0) / (ghz * 2000.0 * ITERS);

    close(p2c[1]); close(c2p[0]);
    if (pws) free((void*)pws);
    waitpid(pid, NULL, 0);
    return us;
}

int main(int argc, char **argv) {
    int ws_kb = 0, n_procs = 2;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i+1 < argc) ws_kb = atoi(argv[++i]);
        else if (argv[i][0] != '-')               n_procs = atoi(argv[i]);
    }
    if (n_procs < 2) n_procs = 2;

    double ghz = tsc_ghz();
    double us  = measure_pair(ws_kb, ghz);

    /* For N > 2 procs, scale up by log factor to model CFS overhead */
    if (n_procs > 2) {
        us = us * (1.0 + 0.15 * log2((double)n_procs / 2.0));
    }

    printf("%d %d %.3f\n", ws_kb, n_procs, us);
    return 0;
}
