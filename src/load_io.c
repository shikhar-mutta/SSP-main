/**
 * I/O Load Generator
 * 
 * Generates I/O load via repeated file create/write/fsync/read cycles.
 * Stresses the VFS layer, page cache, and dentry/inode caches.
 */

#include "ssp_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

/**
 * I/O load worker
 * 
 * intensity: 0-100, controls block size (1-128 KB)
 * duration: seconds to run
 */
void io_load_worker(int intensity, int duration) {
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
    
    /* Pause between operations scales inversely with intensity */
    double pause_s = (intensity < 100) ? 
        (0.10 * (1.0 - intensity / 100.0)) : 0.005;
    
    /* Create temporary directory */
    char tmpdir[] = "/tmp/ssp_io_XXXXXX";
    if (mkdtemp(tmpdir) == NULL) {
        ssp_log(SSP_LOG_ERROR, "mkdtemp failed: %s", strerror(errno));
        return;
    }
    
    ssp_log(SSP_LOG_DEBUG, "I/O load: intensity=%d%%, block_size=%dKB, "
            "tmpdir=%s", intensity, block_kb, tmpdir);
    
    /* Allocate and fill buffer with random data */
    size_t buffer_size = block_kb * 1024;
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
        /* Write phase */
        int fd = open(fpath, O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (fd < 0) {
            ssp_log(SSP_LOG_WARN, "open failed: %s", strerror(errno));
            continue;
        }
        
        ssize_t written = write(fd, data, buffer_size);
        if (written < 0) {
            ssp_log(SSP_LOG_WARN, "write failed: %s", strerror(errno));
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
        }
        close(fd);
        
        /* Pause */
        if (pause_s > 0.0) {
            usleep((useconds_t)(pause_s * 1000000.0));
        }
    }
    
    /* Cleanup */
    unlink(fpath);
    rmdir(tmpdir);
    free(data);
    
    ssp_log(SSP_LOG_INFO, "I/O load generation complete");
}


