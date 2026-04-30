/**
 * I/O Load Generator (NORMALIZED: Latency per Fixed Data Volume)
 * 
 * Measures latency per 10 MB of data (write+fsync+read cycles).
 * Stress-tests VFS layer, page cache, and dentry/inode caches.
 * 
 * FIX for Issue 1: Now measures latency per fixed data volume (10 MB),
 * making it comparable to CPU (fixed op count) and memory (fixed accesses).
 * 
 * Intensity controls block size (1-128 KB), affecting contention and throughput.
 */

#include "ssp_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define TARGET_DATA_SIZE_MB 10  /* Normalized unit: 10 MB per measurement batch */

/**
 * I/O load worker
 * 
 * intensity: 0-100, controls block size (1-128 KB)
 * duration: seconds to run
 */
void io_load_worker(int intensity, int duration, ssp_metrics_t *metrics) {
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
    
    /* Block size scales with intensity: 1-128 KB */
    int block_kb = (128 * intensity / 100);
    if (block_kb < 1) block_kb = 1;
    
    /* Calculate how many cycles to reach TARGET_DATA_SIZE_MB (each cycle does 2x buffer: write+read) */
    size_t target_bytes = (size_t)TARGET_DATA_SIZE_MB * 1024 * 1024;
    size_t buffer_size = block_kb * 1024;
    int cycles_per_batch = (target_bytes + buffer_size - 1) / buffer_size;  /* Ceiling division */
    
    /* Create temporary directory */
    char tmpdir[] = "/tmp/ssp_io_XXXXXX";
    if (mkdtemp(tmpdir) == NULL) {
        ssp_log(SSP_LOG_ERROR, "mkdtemp failed: %s", strerror(errno));
        return;
    }
    
    ssp_log(SSP_LOG_DEBUG, "I/O load: intensity=%d%%, block_size=%dKB, "
            "target_mb=%d, cycles_per_batch=%d, tmpdir=%s", 
            intensity, block_kb, TARGET_DATA_SIZE_MB, cycles_per_batch, tmpdir);
    
    /* Allocate and fill buffer with random data */
    unsigned char *data = malloc(buffer_size);
    if (data == NULL) {
        ssp_log(SSP_LOG_ERROR, "malloc failed");
        rmdir(tmpdir);
        return;
    }
    
    /* Fill with pseudo-random data */
    for (size_t i = 0; i < buffer_size; i++) {
        data[i] = (unsigned char)(rand() & 0xFF);
    }
    
    char fpath[256];
    snprintf(fpath, sizeof(fpath), "%s/ioload.bin", tmpdir);
    
    ssp_time_t deadline = ssp_now();
    deadline.tv_sec += duration;
    
    while (ssp_time_cmp(ssp_now(), deadline) < 0 && !ssp_stop_flag) {
        uint64_t batch_start_ns = ssp_now_ns();
        size_t total_bytes_in_batch = 0;

        /* Execute cycles_per_batch I/O cycles to reach TARGET_DATA_SIZE_MB */
        for (int cycle = 0; cycle < cycles_per_batch; cycle++) {
            uint64_t cycle_start_ns = ssp_now_ns();

            /* Write phase */
            int fd = open(fpath, O_CREAT | O_WRONLY | O_TRUNC, 0600);
            if (fd < 0) {
                ssp_log(SSP_LOG_WARN, "open failed: %s", strerror(errno));
                continue;
            }
            
            ssize_t written = write(fd, data, buffer_size);
            if (written < 0) {
                ssp_log(SSP_LOG_WARN, "write failed: %s", strerror(errno));
            } else {
                total_bytes_in_batch += written;
            }
            
            /* Sync to disk */
            if (fsync(fd) < 0) {
                ssp_log(SSP_LOG_WARN, "fsync failed: %s", strerror(errno));
            }
            close(fd);
            
            /* Read phase */
            fd = open(fpath, O_RDONLY);
            if (fd < 0) {
                ssp_log(SSP_LOG_WARN, "open failed: %s", strerror(errno));
                continue;
            }
            
            ssize_t read_bytes = read(fd, data, buffer_size);
            if (read_bytes < 0) {
                ssp_log(SSP_LOG_WARN, "read failed: %s", strerror(errno));
            } else {
                total_bytes_in_batch += read_bytes;
            }
            close(fd);

            if (metrics) {
                uint64_t cycle_end_ns = ssp_now_ns();
                ssp_metrics_add_io_cycle(metrics, cycle_end_ns - cycle_start_ns);
            }
        }

        if (metrics) {
            uint64_t batch_end_ns = ssp_now_ns();
            /* Record: total_bytes_in_batch transferred in (end-start) ns */
            ssp_metrics_add_batch(metrics, total_bytes_in_batch, batch_end_ns - batch_start_ns);
        }
    }
    
    /* Cleanup */
    unlink(fpath);
    rmdir(tmpdir);
    free(data);
    
    ssp_log(SSP_LOG_INFO, "I/O load generation complete");
}


