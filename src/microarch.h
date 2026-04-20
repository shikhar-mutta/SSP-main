/**
 * Microarchitecture Analysis
 * 
 * Computes high-level performance metrics from low-level perf statistics.
 * Analyzes cache hierarchy, branch prediction, and scheduling efficiency.
 */

#ifndef MICROARCH_H
#define MICROARCH_H

#include "perf_events.h"

/* ─────────────────────────────────────────────────────────────────────────── */
/* Microarchitecture Metrics */
/* ─────────────────────────────────────────────────────────────────────────── */

typedef struct {
    /* Instructions and cycles */
    double ipc;                    /* Instructions per cycle */
    double cycle_per_instruction;
    
    /* Cache hierarchy (estimated based on available events) */
    double l1_miss_rate;          /* Estimate: cache_misses / cache_refs */
    double l2_miss_rate;
    double llc_miss_rate;
    
    /* Branch prediction */
    double branch_miss_rate;       /* Mispredicts / total branches */
    
    /* Context switching and scheduling */
    double context_switch_ratio;   /* Context switches per cycle */
    double cpu_migration_ratio;    /* Migrations per cycle */
    
    /* Memory hierarchy efficiency */
    double memory_efficiency;      /* Estimate: useful bytes / total accesses */
    
    /* Overall efficiency score (0.0 - 1.0) */
    double overall_efficiency;
    
    /* Scaling efficiency (multicore performance) */
    double scaling_efficiency;
    
} microarch_metrics_t;

/* ─────────────────────────────────────────────────────────────────────────── */
/* Computation Functions */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Compute microarchitecture metrics from perf statistics
 */
microarch_metrics_t compute_microarch_metrics(const perf_stats_t *before,
                                              const perf_stats_t *after);

/**
 * Compute metrics delta between two measurements
 */
microarch_metrics_t compute_metrics_delta(const microarch_metrics_t *m1,
                                          const microarch_metrics_t *m2);

/**
 * Classify CPU behavior based on metrics
 * Returns: human-readable classification string
 */
const char *classify_cpu_behavior(const microarch_metrics_t *metrics);

/**
 * Compute cache efficiency score (0.0 - 1.0)
 */
double compute_cache_efficiency(const perf_stats_t *stats);

/**
 * Compute scheduling efficiency score (0.0 - 1.0)
 * 1.0 = perfect (no context switches), 0.0 = severe thrashing
 */
double compute_scheduling_efficiency(const perf_stats_t *stats);

/**
 * Estimate multicore scaling efficiency
 * Useful for comparing single-core vs multi-core performance
 */
double compute_scaling_efficiency(const perf_stats_t *single_core,
                                  const perf_stats_t *multi_core,
                                  int num_cores);

/* ─────────────────────────────────────────────────────────────────────────── */
/* Analysis Output */
/* ─────────────────────────────────────────────────────────────────────────── */

/**
 * Print detailed microarchitecture analysis
 */
void microarch_print_analysis(const microarch_metrics_t *metrics,
                             const perf_stats_t *stats);

/**
 * Convert metrics to JSON string (caller must free)
 */
char *microarch_to_json(const microarch_metrics_t *metrics);

/**
 * Generate performance bottleneck report
 * Identifies primary limitations: memory, branch prediction, scheduling, etc.
 */
void microarch_identify_bottlenecks(const microarch_metrics_t *metrics,
                                   const perf_stats_t *stats);

#endif // MICROARCH_H
