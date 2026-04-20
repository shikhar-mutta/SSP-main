/**
 * Mixed Load Generator
 * 
 * Combines CPU load and memory load running concurrently on separate threads.
 * This simulates realistic workloads that stress both computation and memory subsystem.
 */

#include "ssp_lib.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Forward declarations */
void cpu_load_worker(int intensity, int duration, int cpu_id);
void memory_load_worker(int intensity, int duration, int cache_level);

/**
 * Thread context for mixed load
 */
typedef struct {
    int intensity;
    int duration;
    int cpu_id;
    int thread_type;  /* 0=CPU, 1=Memory */
    int cache_level;
} mixed_thread_context_t;

/**
 * Worker thread function
 */
static void *mixed_thread_worker(void *arg) {
    mixed_thread_context_t *ctx = (mixed_thread_context_t *)arg;
    
    if (ctx->thread_type == 0) {
        /* CPU load thread */
        cpu_load_worker(ctx->intensity, ctx->duration, ctx->cpu_id);
    } else {
        /* Memory load thread */
        memory_load_worker(ctx->intensity, ctx->duration, ctx->cache_level);
    }
    
    free(ctx);
    return NULL;
}

/**
 * Mixed load worker - spawns CPU + Memory load threads
 * 
 * intensity: 0-100, applied to both CPU and memory loads
 * duration: seconds to run
 * num_cpus: number of CPUs (CPU thread pins to first CPU)
 */
void mixed_load_worker(int intensity, int duration, int num_cpus) {
    /* Clamp intensity */
    if (intensity < 0) intensity = 0;
    if (intensity > 100) intensity = 100;
    
    if (duration <= 0) {
        ssp_log(SSP_LOG_ERROR, "Duration must be positive");
        return;
    }
    
    ssp_log(SSP_LOG_DEBUG, "Mixed load: intensity=%d%%, duration=%ds, cpus=%d",
            intensity, duration, num_cpus);
    
    pthread_t cpu_thread, mem_thread;
    
    /* Create CPU load thread (pin to CPU 0) */
    mixed_thread_context_t *cpu_ctx = malloc(sizeof(mixed_thread_context_t));
    if (cpu_ctx == NULL) {
        ssp_log(SSP_LOG_ERROR, "malloc failed");
        return;
    }
    cpu_ctx->intensity = intensity;
    cpu_ctx->duration = duration;
    cpu_ctx->cpu_id = 0;
    cpu_ctx->thread_type = 0;
    cpu_ctx->cache_level = 0;
    
    if (pthread_create(&cpu_thread, NULL, mixed_thread_worker, cpu_ctx) != 0) {
        ssp_log(SSP_LOG_ERROR, "Failed to create CPU load thread");
        free(cpu_ctx);
        return;
    }
    
    /* Create memory load thread (pin to CPU 1 if available) */
    mixed_thread_context_t *mem_ctx = malloc(sizeof(mixed_thread_context_t));
    if (mem_ctx == NULL) {
        ssp_log(SSP_LOG_ERROR, "malloc failed");
        pthread_cancel(cpu_thread);
        pthread_join(cpu_thread, NULL);
        return;
    }
    mem_ctx->intensity = intensity;
    mem_ctx->duration = duration;
    mem_ctx->cpu_id = (num_cpus > 1) ? 1 : 0;
    mem_ctx->thread_type = 1;
    mem_ctx->cache_level = 0;
    
    if (pthread_create(&mem_thread, NULL, mixed_thread_worker, mem_ctx) != 0) {
        ssp_log(SSP_LOG_ERROR, "Failed to create memory load thread");
        free(mem_ctx);
        pthread_cancel(cpu_thread);
        pthread_join(cpu_thread, NULL);
        return;
    }
    
    ssp_log(SSP_LOG_INFO, "Mixed load threads started");
    
    /* Wait for both threads to complete */
    pthread_join(cpu_thread, NULL);
    pthread_join(mem_thread, NULL);
    
    ssp_log(SSP_LOG_INFO, "Mixed load generation complete");
}


