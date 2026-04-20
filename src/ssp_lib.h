/**
 * SSP Common Utilities Library
 * Provides time measurement, CPU affinity, memory operations, and signal handling
 */

#ifndef SSP_LIB_H
#define SSP_LIB_H

#include <stdint.h>
#include <stddef.h>
#include <signal.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Time Measurement with Nanosecond Precision */
/* ─────────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint64_t tv_sec;
    uint64_t tv_nsec;
} ssp_time_t;

/**
 * Get current time with nanosecond precision using CLOCK_MONOTONIC
 */
ssp_time_t ssp_now(void);

/**
 * Get current time in nanoseconds since epoch
 */
uint64_t ssp_now_ns(void);

/**
 * Compare two ssp_time_t values
 * Returns: < 0 if t1 < t2, 0 if equal, > 0 if t1 > t2
 */
int ssp_time_cmp(ssp_time_t t1, ssp_time_t t2);

/**
 * Calculate difference in microseconds
 */
double ssp_time_diff_us(ssp_time_t start, ssp_time_t end);

/**
 * Calculate difference in milliseconds
 */
double ssp_time_diff_ms(ssp_time_t start, ssp_time_t end);

/**
 * Calculate difference in nanoseconds
 */
uint64_t ssp_time_diff_ns(ssp_time_t start, ssp_time_t end);

/* ─────────────────────────────────────────────────────────────────────────── */
/* CPU Affinity */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Set CPU affinity for current thread to given CPU ID
 * Returns: 0 on success, -1 on error
 */
int ssp_set_affinity(int cpu_id);

/**
 * Get total number of logical CPUs
 */
int ssp_get_cpu_count(void);

/**
 * Get list of CPUs in a NUMA node (if available)
 * Returns: number of CPUs in the node, -1 if NUMA not supported
 */
int ssp_get_node_cpus(int node_id, int *cpus, int max_cpus);

/**
 * Get current CPU ID of this thread
 */
int ssp_get_current_cpu(void);

/**
 * Get NUMA node ID for current thread
 */
int ssp_get_current_node(void);

/* ─────────────────────────────────────────────────────────────────────────── */
/* Memory Operations */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Allocate memory aligned to given boundary
 * Useful for cache-line alignment (64B) or page alignment
 */
void *ssp_allocate_aligned(size_t size, size_t alignment);

/**
 * Free memory allocated by ssp_allocate_aligned
 */
void ssp_free_aligned(void *ptr);

/**
 * Get page size
 */
size_t ssp_get_page_size(void);

/**
 * Advise kernel on memory access pattern
 * Returns: 0 on success, -1 on error
 */
int ssp_madvise(void *addr, size_t size, int advice);

/* ─────────────────────────────────────────────────────────────────────────── */
/* Signal Handling */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Global stop flag for clean shutdown (set by signal handlers)
 */
extern volatile sig_atomic_t ssp_stop_flag;

/**
 * Initialize signal handlers for SIGINT, SIGTERM
 * These will set ssp_stop_flag
 */
void ssp_init_signal_handlers(void);

/* ─────────────────────────────────────────────────────────────────────────── */
/* Logging and Error Handling */
/* ─────────────────────────────────────────────────────────────────────────── */

typedef enum {
    SSP_LOG_DEBUG,
    SSP_LOG_INFO,
    SSP_LOG_WARN,
    SSP_LOG_ERROR
} ssp_log_level_t;

/**
 * Set verbosity level
 */
void ssp_set_log_level(ssp_log_level_t level);

/**
 * Log message (varargs like printf)
 */
void ssp_log(ssp_log_level_t level, const char *fmt, ...);

/**
 * Get human-readable error message
 */
const char *ssp_strerror(int errnum);

#endif // SSP_LIB_H
