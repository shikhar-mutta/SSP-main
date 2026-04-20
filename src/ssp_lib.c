/**
 * SSP Common Utilities Library - Implementation
 */

#include "ssp_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <stdarg.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Global State */
/* ─────────────────────────────────────────────────────────────────────────── */

volatile sig_atomic_t ssp_stop_flag = 0;

static ssp_log_level_t _current_log_level = SSP_LOG_INFO;

/* ─────────────────────────────────────────────────────────────────────────── */
/* Signal Handlers */
/* ─────────────────────────────────────────────────────────────────────────── */

static void _signal_handler(int sig) {
    ssp_stop_flag = 1;
}

void ssp_init_signal_handlers(void) {
    signal(SIGINT, _signal_handler);
    signal(SIGTERM, _signal_handler);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Time Measurement */
/* ─────────────────────────────────────────────────────────────────────────── */

ssp_time_t ssp_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    ssp_time_t result = {
        .tv_sec = (uint64_t)ts.tv_sec,
        .tv_nsec = (uint64_t)ts.tv_nsec
    };
    
    return result;
}

uint64_t ssp_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int ssp_time_cmp(ssp_time_t t1, ssp_time_t t2) {
    if (t1.tv_sec != t2.tv_sec) {
        return (t1.tv_sec < t2.tv_sec) ? -1 : 1;
    }
    if (t1.tv_nsec != t2.tv_nsec) {
        return (t1.tv_nsec < t2.tv_nsec) ? -1 : 1;
    }
    return 0;
}

double ssp_time_diff_us(ssp_time_t start, ssp_time_t end) {
    uint64_t start_ns = start.tv_sec * 1000000000ULL + start.tv_nsec;
    uint64_t end_ns = end.tv_sec * 1000000000ULL + end.tv_nsec;
    return (double)(end_ns - start_ns) / 1000.0;
}

double ssp_time_diff_ms(ssp_time_t start, ssp_time_t end) {
    uint64_t start_ns = start.tv_sec * 1000000000ULL + start.tv_nsec;
    uint64_t end_ns = end.tv_sec * 1000000000ULL + end.tv_nsec;
    return (double)(end_ns - start_ns) / 1000000.0;
}

uint64_t ssp_time_diff_ns(ssp_time_t start, ssp_time_t end) {
    uint64_t start_ns = start.tv_sec * 1000000000ULL + start.tv_nsec;
    uint64_t end_ns = end.tv_sec * 1000000000ULL + end.tv_nsec;
    return end_ns - start_ns;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* CPU Affinity */
/* ─────────────────────────────────────────────────────────────────────────── */

int ssp_set_affinity(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        ssp_log(SSP_LOG_WARN, "Failed to set affinity to CPU %d", cpu_id);
        return -1;
    }
    
    return 0;
}

int ssp_get_cpu_count(void) {
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
}

int ssp_get_node_cpus(int node_id, int *cpus, int max_cpus) {
    // This requires NUMA library in production; simplified version here
    (void)node_id;
    (void)cpus;
    (void)max_cpus;
    return -1;  // NUMA not implemented in this version
}

int ssp_get_current_cpu(void) {
    return (int)sched_getcpu();
}

int ssp_get_current_node(void) {
    // Simplified: on single-node systems, always return 0
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Memory Operations */
/* ─────────────────────────────────────────────────────────────────────────── */

void *ssp_allocate_aligned(size_t size, size_t alignment) {
    void *ptr;
    int ret = posix_memalign(&ptr, alignment, size);
    
    if (ret != 0) {
        ssp_log(SSP_LOG_ERROR, "posix_memalign failed: %s", strerror(ret));
        return NULL;
    }
    
    return ptr;
}

void ssp_free_aligned(void *ptr) {
    free(ptr);
}

size_t ssp_get_page_size(void) {
    return (size_t)sysconf(_SC_PAGESIZE);
}

int ssp_madvise(void *addr, size_t size, int advice) {
    return madvise(addr, size, advice);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Logging */
/* ─────────────────────────────────────────────────────────────────────────── */

void ssp_set_log_level(ssp_log_level_t level) {
    _current_log_level = level;
}

void ssp_log(ssp_log_level_t level, const char *fmt, ...) {
    if (level < _current_log_level) {
        return;
    }
    
    const char *level_str;
    switch (level) {
        case SSP_LOG_DEBUG: level_str = "[DEBUG]"; break;
        case SSP_LOG_INFO:  level_str = "[INFO] "; break;
        case SSP_LOG_WARN:  level_str = "[WARN] "; break;
        case SSP_LOG_ERROR: level_str = "[ERROR]"; break;
        default:            level_str = "[?]   "; break;
    }
    
    fprintf(stderr, "%s ", level_str);
    
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

const char *ssp_strerror(int errnum) {
    return strerror(errnum);
}
