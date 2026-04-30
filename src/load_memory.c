/**
 * Memory Load Generator (NORMALIZED: Latency per Fixed Memory Operation)
 * 
 * Measures latency per 10 million cache-line accesses (normalized unit).
 * Working set scales with intensity (8-512 MB), affecting cache hit rate.
 * Stride = 64 bytes (cache line size) to expose cache hierarchy.
 * 
 * FIX for Issue 1: Now measures latency per fixed byte count (~10M accesses per batch),
 * making it comparable to CPU (fixed operation count) and I/O (fixed data size).
 */

#include "ssp_lib.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CACHE_LINE_SIZE 64
#define STRIDE_DOUBLES (CACHE_LINE_SIZE / sizeof(double))  /* 8 doubles */
#define ACCESSES_PER_BATCH 10000000  /* 10M cache-line accesses = ~640MB touched */

/**
 * Memory load worker
 * 
 * intensity: 0-100, controls working set size (8-512 MB)
 * duration: seconds to run
 * cache_level: 0=auto, 1=L1, 2=L2, 3=L3 (affects stride pattern)
 */
void memory_load_worker(int intensity, int duration, int cache_level, ssp_metrics_t *metrics) {
    /* Clamp intensity */
    if (intensity < 0) intensity = 0;
    if (intensity > 100) intensity = 100;
    
    /* If no load, just sleep */
    if (intensity == 0) {
        ssp_time_t deadline = ssp_now();
        deadline.tv_sec += duration;
        
        while (ssp_time_cmp(ssp_now(), deadline) < 0 && !ssp_stop_flag) {
            usleep(100000);
        }
        return;
    }
    
    /* Working set size: 8-512 MB, scaling with intensity */
    int size_mb = (512 * intensity / 100);
    if (size_mb < 8) size_mb = 8;
    
    size_t n_doubles = ((size_t)size_mb * 1024 * 1024) / sizeof(double);
    
    /* Select stride based on cache level
       L1: 64B = 8 doubles (default)
       L2: ~1KB = 128 doubles
       L3: ~8KB = 1024 doubles
    */
    int stride = STRIDE_DOUBLES;  /* 8 */
    switch (cache_level) {
        case 2:
            stride = 128;
            break;
        case 3:
            stride = 1024;
            break;
        case 1:
        default:
            stride = STRIDE_DOUBLES;
    }
    
    ssp_log(SSP_LOG_DEBUG, "Memory load: intensity=%d%%, size=%dMB, "
            "stride=%d doubles, cache_level=%d, accesses_per_batch=%llu",
            intensity, size_mb, stride, cache_level, (unsigned long long)ACCESSES_PER_BATCH);
    
    /* Allocate buffer aligned to cache line */
    double *buf = (double *)ssp_allocate_aligned(
        n_doubles * sizeof(double), CACHE_LINE_SIZE);
    
    if (buf == NULL) {
        ssp_log(SSP_LOG_ERROR, "Failed to allocate %zu bytes", 
                n_doubles * sizeof(double));
        return;
    }
    
    /* Initialize with pattern */
    for (size_t i = 0; i < n_doubles; i++) {
        buf[i] = (double)i;
    }
    
    ssp_time_t deadline = ssp_now();
    deadline.tv_sec += duration;
    
    volatile double acc = 0.0;
    size_t idx = 0;
    
    while (ssp_time_cmp(ssp_now(), deadline) < 0 && !ssp_stop_flag) {
        uint64_t batch_start_ns = ssp_now_ns();

        /* Execute exactly ACCESSES_PER_BATCH strided reads + writes */
        for (uint64_t access = 0; access < ACCESSES_PER_BATCH; access++) {
            /* Read - accumulate to defeat dead code elimination */
            acc += buf[idx];
            
            /* Write - keep memory dirty, small modification prevents optimization */
            buf[idx] = acc * 0.000000001;
            
            /* Advance index with stride, wrap around */
            idx = (idx + stride) % n_doubles;
        }

        if (metrics) {
            uint64_t batch_end_ns = ssp_now_ns();
            /* Record: ACCESSES_PER_BATCH cache-line accesses in (end-start) ns */
            ssp_metrics_add_batch(metrics, ACCESSES_PER_BATCH, batch_end_ns - batch_start_ns);
        }
        
        /* Brief yield to let OS schedule other tasks */
        usleep(1000);
    }
    
    ssp_free_aligned(buf);
    
    /* Prevent the variable from being optimized away */
    if (acc == -999999999.0) {
        ssp_log(SSP_LOG_DEBUG, "unreachable");
    }
    
    ssp_log(SSP_LOG_INFO, "Memory load generation complete");
}


