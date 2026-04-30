/**
 * CPU Load Generator (NORMALIZED: Latency per Fixed Operation Count)
 * 
 * Measures latency per 1M fixed sin/cos operations (normalized unit).
 * Intensity controls duty cycle (% of time generating load vs idle).
 * Uses floating-point arithmetic to defeat compiler optimization.
 * 
 * FIX for Issue 2: Instead of measuring chunk_duration (which scales by construction),
 * we now measure latency per N fixed operations, making it comparable to other workloads.
 */

#include "ssp_lib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#define OPS_PER_MEASUREMENT 1000000  /* Fixed 1M operations per measurement batch */

/**
 * CPU load worker (single thread)
 * 
 * intensity: 0-100, duty cycle percentage
 * duration: seconds to run
 * cpu_id: CPU to pin to (-1 for no affinity)
 * metrics: aggregated metrics structure
 */
void cpu_load_worker(int intensity, int duration, int cpu_id, ssp_metrics_t *metrics) {
    /* Pin to CPU if specified */
    if (cpu_id >= 0) {
        ssp_set_affinity(cpu_id);
    }
    
    /* Clamp intensity */
    if (intensity < 0) intensity = 0;
    if (intensity > 100) intensity = 100;
    
    /* If no load, just sleep */
    if (intensity == 0) {
        ssp_time_t deadline = ssp_now();
        deadline.tv_sec += duration;
        
        while (ssp_time_cmp(ssp_now(), deadline) < 0 && !ssp_stop_flag) {
            usleep(100000);  /* 100ms */
        }
        return;
    }
    
    ssp_time_t deadline = ssp_now();
    deadline.tv_sec += duration;
    
    /* Volatile to prevent optimization */
    volatile double x = 1.0;
    
    ssp_log(SSP_LOG_DEBUG, "CPU load: intensity=%d%% (duty cycle), duration=%ds, "
            "ops_per_batch=%d, affinity=%d",
            intensity, duration, OPS_PER_MEASUREMENT, cpu_id);
    
    while (ssp_time_cmp(ssp_now(), deadline) < 0 && !ssp_stop_flag) {
        uint64_t batch_start_ns = ssp_now_ns();
        
        /* Execute exactly OPS_PER_MEASUREMENT fixed operations */
        for (uint64_t op = 0; op < OPS_PER_MEASUREMENT; op++) {
            /* CPU-bound arithmetic - high latency of sin/cos prevents ILP */
            x = sin(x + 0.001) * cos(x - 0.001) + x * 1.0000001;
        }
        
        uint64_t batch_end_ns = ssp_now_ns();
        
        /* Record: OPS_PER_MEASUREMENT operations completed in (end - start) ns */
        if (metrics) {
            ssp_metrics_add_batch(metrics, OPS_PER_MEASUREMENT, batch_end_ns - batch_start_ns);
        }
        
        /* Intensity affects duty cycle: idle time inversely proportional */
        if (intensity < 100) {
            /* Sleep time based on work time and desired intensity */
            uint64_t work_ns = batch_end_ns - batch_start_ns;
            double intensity_frac = intensity / 100.0;
            double sleep_ns = (work_ns / intensity_frac) * (1.0 - intensity_frac);
            
            if (sleep_ns > 1000.0) {  /* Only sleep if >= 1 µs */
                useconds_t sleep_us = (useconds_t)(sleep_ns / 1000.0);
                usleep(sleep_us);
            }
        }
    }
    
    /* Prevent the variable from being optimized away */
    if (x == 999999999.0) {
        ssp_log(SSP_LOG_DEBUG, "unreachable");
    }
}


