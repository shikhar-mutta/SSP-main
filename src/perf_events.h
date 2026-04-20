/**
 * Linux Perf Event Collection Interface
 * 
 * Provides abstraction over Linux perf_event_open() syscall for monitoring
 * system events like cache misses, context switches, page faults, etc.
 */

#ifndef PERF_EVENTS_H
#define PERF_EVENTS_H

#include <stdint.h>
#include <unistd.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Types */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Single perf event tracking
 */
typedef struct {
    int fd;
    uint64_t event_id;
    char event_name[64];
    uint64_t value;
    uint64_t enabled;
    uint64_t running;
} perf_event_t;

/**
 * Perf event collection session
 */
typedef struct {
    perf_event_t *events;
    int num_events;
    int max_events;
    int cpu_id;
    pid_t target_pid;
    int initialized;
} perf_session_t;

/**
 * Aggregated performance statistics
 */
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
    uint64_t minor_faults;
    uint64_t major_faults;
    
    /* Computed metrics */
    double ipc;                    /* Instructions per cycle */
    double cache_miss_rate;
    double branch_miss_rate;
    double context_switch_ratio;
} perf_stats_t;

/* ─────────────────────────────────────────────────────────────────────────── */
/* Session Management */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Create new perf event collection session
 * 
 * cpu_id: CPU to monitor (-1 for any CPU)
 * target_pid: Process to monitor (0 for current process)
 * 
 * Returns: allocated session or NULL on error
 */
perf_session_t *perf_session_create(int cpu_id, pid_t target_pid);

/**
 * Destroy perf session, closing all file descriptors
 */
void perf_session_destroy(perf_session_t *sess);

/* ─────────────────────────────────────────────────────────────────────────── */
/* Event Management */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Add single perf event to session
 * 
 * Returns: 0 on success, -1 on error
 */
int perf_add_event(perf_session_t *sess, uint32_t type, uint64_t config,
                   const char *name);

/**
 * Add a standard set of events for microarchitecture analysis
 * Includes: cycles, instructions, cache refs/misses, branch pred, etc.
 * 
 * Returns: number of events added, -1 on error
 */
int perf_add_standard_events(perf_session_t *sess);

/**
 * Add only lightweight events (minimal overhead)
 * Includes: cycles, instructions, context switches
 */
int perf_add_lightweight_events(perf_session_t *sess);

/* ─────────────────────────────────────────────────────────────────────────── */
/* Event Collection */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Enable all events in session
 */
int perf_enable_events(perf_session_t *sess);

/**
 * Disable all events in session
 */
int perf_disable_events(perf_session_t *sess);

/**
 * Read current values of all events
 * 
 * Returns: 0 on success, -1 on error
 */
int perf_read_events(perf_session_t *sess);

/**
 * Reset all event counters to zero
 */
int perf_reset_events(perf_session_t *sess);

/* ─────────────────────────────────────────────────────────────────────────── */
/* Data Collection */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Collect perf statistics from session
 * Populates perf_stats_t with current values and computed metrics
 */
int perf_collect_stats(perf_session_t *sess, perf_stats_t *stats);

/**
 * Difference between two perf stats (after - before)
 */
perf_stats_t perf_stats_diff(const perf_stats_t *before, 
                             const perf_stats_t *after);

/**
 * Print human-readable perf statistics
 */
void perf_stats_print(const perf_stats_t *stats);

/**
 * Convert perf stats to JSON string (caller must free)
 */
char *perf_stats_to_json(const perf_stats_t *stats);

/* ─────────────────────────────────────────────────────────────────────────── */
/* Utilities */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Get number of hardware counters available
 */
int perf_get_num_counters(void);

/**
 * Check if perf events are available (might need root)
 */
int perf_is_available(void);

/**
 * Increase perf event limit for non-root user
 * (Usually requires admin setup)
 */
int perf_set_max_events_limit(int limit);

#endif // PERF_EVENTS_H
