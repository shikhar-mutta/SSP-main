/**
 * CPU Load Generator
 * 
 * Generates configurable CPU load via busy-loop with duty-cycle control.
 * Uses floating-point arithmetic to defeat compiler optimization.
 */

#include "ssp_lib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHUNK_MS 10.0   /* 10ms scheduling granularity */

/**
 * CPU load worker
 * 
 * intensity: 0-100, percentage of time busy
 * duration: seconds to run
 * cpu_id: CPU to pin to (-1 for no affinity)
 */
void cpu_load_worker(int intensity, int duration, int cpu_id) {
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
    
    double work_frac = intensity / 100.0;
    double sleep_frac = 1.0 - work_frac;
    
    ssp_time_t deadline = ssp_now();
    deadline.tv_sec += duration;
    
    /* Volatile to prevent optimization */
    volatile double x = 1.0;
    
    ssp_log(SSP_LOG_DEBUG, "CPU load: intensity=%d%%, duration=%ds, affinity=%d",
            intensity, duration, cpu_id);
    
    while (ssp_time_cmp(ssp_now(), deadline) < 0 && !ssp_stop_flag) {
        ssp_time_t phase_start = ssp_now();
        
        /* ── Busy phase ─────────────────────────────────────────────────────── */
        /* Prevent compiler-level optimization via floating ops */
        double chunk_us = CHUNK_MS * work_frac * 1000.0;
        
        while (ssp_time_diff_us(phase_start, ssp_now()) < chunk_us) {
            /* CPU-bound arithmetic - high latency of sin/cos prevents ILP */
            x = sin(x + 0.001) * cos(x - 0.001) + x * 1.0000001;
        }
        
        /* ── Idle phase ────────────────────────────────────────────────────── */
        if (sleep_frac > 1e-6) {
            useconds_t sleep_us = (useconds_t)(CHUNK_MS * sleep_frac * 1000.0);
            usleep(sleep_us);
        }
    }
    
    /* Prevent the variable from being optimized away */
    if (x == 999999999.0) {
        ssp_log(SSP_LOG_DEBUG, "unreachable");
    }
}


