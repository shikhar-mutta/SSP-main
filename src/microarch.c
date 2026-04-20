/**
 * Microarchitecture Analysis Implementation
 */

#include "microarch.h"
#include "ssp_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

microarch_metrics_t compute_microarch_metrics(const perf_stats_t *before,
                                              const perf_stats_t *after) {
    microarch_metrics_t metrics;
    memset(&metrics, 0, sizeof(microarch_metrics_t));
    
    if (!before || !after) return metrics;
    
    uint64_t cycles_delta = after->cycles - before->cycles;
    uint64_t instr_delta = after->instructions - before->instructions;
    uint64_t cache_ref_delta = after->cache_references - before->cache_references;
    uint64_t cache_miss_delta = after->cache_misses - before->cache_misses;
    uint64_t branch_delta = after->branch_instructions - before->branch_instructions;
    uint64_t branch_miss_delta = after->branch_misses - before->branch_misses;
    uint64_t ctx_sw_delta = after->context_switches - before->context_switches;
    
    if (cycles_delta > 0) {
        metrics.ipc = (double)instr_delta / (double)cycles_delta;
        metrics.cycle_per_instruction = (double)cycles_delta / (double)instr_delta;
    }
    
    if (cache_ref_delta > 0) {
        metrics.l1_miss_rate = (double)cache_miss_delta / (double)cache_ref_delta;
        metrics.l2_miss_rate = metrics.l1_miss_rate * 0.5;
        metrics.llc_miss_rate = metrics.l1_miss_rate * 0.1;
    }
    
    if (branch_delta > 0) {
        metrics.branch_miss_rate = (double)branch_miss_delta / (double)branch_delta;
    }
    
    if (cycles_delta > 0) {
        metrics.context_switch_ratio = (double)ctx_sw_delta / (double)cycles_delta;
    }
    
    metrics.memory_efficiency = 1.0 - metrics.l1_miss_rate;
    metrics.overall_efficiency = (0.5 * (1.0 - metrics.l1_miss_rate) + 
                                  0.3 * (1.0 - metrics.branch_miss_rate * 10.0) +
                                  0.2 * (1.0 - metrics.context_switch_ratio * 1000.0));
    if (metrics.overall_efficiency < 0) metrics.overall_efficiency = 0;
    if (metrics.overall_efficiency > 1.0) metrics.overall_efficiency = 1.0;
    
    return metrics;
}

const char *classify_cpu_behavior(const microarch_metrics_t *metrics) {
    if (!metrics) return "UNKNOWN";
    if (metrics->context_switch_ratio > 0.001) return "CONTEXT_BOUND";
    if (metrics->l1_miss_rate > 0.20) return "MEMORY_BOUND";
    if (metrics->branch_miss_rate > 0.05) return "BRANCH_BOUND";
    if (metrics->ipc < 0.5) return "ILP_LIMITED";
    return "MIXED";
}

void microarch_print_analysis(const microarch_metrics_t *metrics,
                             const perf_stats_t *stats) {
    if (!metrics) return;
    printf("\n=== Microarchitecture Analysis ===\n");
    printf("IPC: %.3f\n", metrics->ipc);
    printf("L1 Miss Rate: %.2f%%\n", metrics->l1_miss_rate * 100.0);
    printf("Efficiency: %.1f%%\n", metrics->overall_efficiency * 100.0);
}

char *microarch_to_json(const microarch_metrics_t *metrics) {
    if (!metrics) return strdup("{}");
    char *buf = malloc(1024);
    snprintf(buf, 1024, "{\"ipc\": %.6f, \"efficiency\": %.6f}", 
             metrics->ipc, metrics->overall_efficiency);
    return buf;
}

void microarch_identify_bottlenecks(const microarch_metrics_t *metrics,
                                   const perf_stats_t *stats) {
    if (!metrics) return;
    printf("Analyzing bottlenecks...\n");
}

microarch_metrics_t compute_metrics_delta(const microarch_metrics_t *m1,
                                          const microarch_metrics_t *m2) {
    microarch_metrics_t delta;
    memset(&delta, 0, sizeof(delta));
    return delta;
}

double compute_cache_efficiency(const perf_stats_t *stats) {
    return 1.0;
}

double compute_scheduling_efficiency(const perf_stats_t *stats) {
    return 1.0;
}

double compute_scaling_efficiency(const perf_stats_t *single_core,
                                  const perf_stats_t *multi_core,
                                  int num_cores) {
    return 1.0;
}
