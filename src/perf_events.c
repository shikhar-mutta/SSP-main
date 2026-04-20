/**
 * Linux Perf Event Collection - Implementation
 */

#include "perf_events.h"
#include "ssp_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>

#include <sys/ioctl.h>
/* ─────────────────────────────────────────────────────────────────────────── */
#include <sys/ioctl.h>
/* ─────────────────────────────────────────────────────────────────────────── */

#define MAX_EVENTS 32

/* ─────────────────────────────────────────────────────────────────────────── */
/* perf_event_open() syscall wrapper */
/* ─────────────────────────────────────────────────────────────────────────── */

static long perf_event_open(struct perf_event_attr *attr, pid_t pid, 
                            int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Session Management */
/* ─────────────────────────────────────────────────────────────────────────── */

perf_session_t *perf_session_create(int cpu_id, pid_t target_pid) {
    perf_session_t *sess = malloc(sizeof(perf_session_t));
    if (sess == NULL) {
        ssp_log(SSP_LOG_ERROR, "malloc failed for perf_session_t");
        return NULL;
    }
    
    sess->events = malloc(MAX_EVENTS * sizeof(perf_event_t));
    if (sess->events == NULL) {
        ssp_log(SSP_LOG_ERROR, "malloc failed for perf events array");
        free(sess);
        return NULL;
    }
    
    sess->num_events = 0;
    sess->max_events = MAX_EVENTS;
    sess->cpu_id = cpu_id;
    sess->target_pid = target_pid;
    sess->initialized = 1;
    
    ssp_log(SSP_LOG_DEBUG, "Created perf session: cpu=%d, pid=%d", 
            cpu_id, target_pid);
    
    return sess;
}

void perf_session_destroy(perf_session_t *sess) {
    if (sess == NULL) return;
    
    /* Close all file descriptors */
    for (int i = 0; i < sess->num_events; i++) {
        if (sess->events[i].fd >= 0) {
            close(sess->events[i].fd);
        }
    }
    
    free(sess->events);
    free(sess);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Event Management */
/* ─────────────────────────────────────────────────────────────────────────── */

int perf_add_event(perf_session_t *sess, uint32_t type, uint64_t config,
                   const char *name) {
    if (sess == NULL || !sess->initialized) {
        ssp_log(SSP_LOG_ERROR, "Invalid perf session");
        return -1;
    }
    
    if (sess->num_events >= sess->max_events) {
        ssp_log(SSP_LOG_WARN, "Max events (%d) reached", sess->max_events);
        return -1;
    }
    
    struct perf_event_attr attr = {
        .type = type,
        .config = config,
        .size = sizeof(struct perf_event_attr),
        .disabled = 1,  /* Start disabled, enable explicitly */
        .exclude_kernel = 0,  /* Include kernel events */
        .exclude_hv = 1,      /* Exclude hypervisor */
        .read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
                       PERF_FORMAT_TOTAL_TIME_RUNNING,
    };
    
    int fd = (int)perf_event_open(&attr, sess->target_pid, sess->cpu_id, -1, 0);
    
    if (fd < 0) {
        ssp_log(SSP_LOG_DEBUG, "Failed to open event '%s': %s",
                name, strerror(errno));
        return -1;
    }
    
    perf_event_t *ev = &sess->events[sess->num_events++];
    ev->fd = fd;
    ev->event_id = config;
    strncpy(ev->event_name, name, 63);
    ev->event_name[63] = '\0';
    ev->value = 0;
    ev->enabled = 0;
    ev->running = 0;
    
    ssp_log(SSP_LOG_DEBUG, "Added event '%s' (type=%u, config=%llu)", 
            name, type, config);
    
    return 0;
}

int perf_add_standard_events(perf_session_t *sess) {
    if (sess == NULL) return -1;
    
    int count = 0;
    
    /* Hardware events */
    if (perf_add_event(sess, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, 
                      "cycles") == 0) count++;
    if (perf_add_event(sess, PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,
                      "instructions") == 0) count++;
    if (perf_add_event(sess, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES,
                      "cache_references") == 0) count++;
    if (perf_add_event(sess, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES,
                      "cache_misses") == 0) count++;
    if (perf_add_event(sess, PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
                      "branch_instructions") == 0) count++;
    if (perf_add_event(sess, PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES,
                      "branch_misses") == 0) count++;
    
    /* Software events */
    if (perf_add_event(sess, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES,
                      "context_switches") == 0) count++;
    if (perf_add_event(sess, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS,
                      "cpu_migrations") == 0) count++;
    if (perf_add_event(sess, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS,
                      "page_faults") == 0) count++;
    if (perf_add_event(sess, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ,
                      "major_faults") == 0) count++;
    
    ssp_log(SSP_LOG_INFO, "Added %d standard perf events", count);
    
    return count;
}

int perf_add_lightweight_events(perf_session_t *sess) {
    if (sess == NULL) return -1;
    
    int count = 0;
    
    if (perf_add_event(sess, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,
                      "cycles") == 0) count++;
    if (perf_add_event(sess, PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,
                      "instructions") == 0) count++;
    if (perf_add_event(sess, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES,
                      "context_switches") == 0) count++;
    
    return count;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Event Collection */
/* ─────────────────────────────────────────────────────────────────────────── */

int perf_enable_events(perf_session_t *sess) {
    if (sess == NULL) return -1;
    
    for (int i = 0; i < sess->num_events; i++) {
        if (ioctl(sess->events[i].fd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
            ssp_log(SSP_LOG_WARN, "Failed to enable event '%s'",
                   sess->events[i].event_name);
            return -1;
        }
    }
    
    return 0;
}

int perf_disable_events(perf_session_t *sess) {
    if (sess == NULL) return -1;
    
    for (int i = 0; i < sess->num_events; i++) {
        if (ioctl(sess->events[i].fd, PERF_EVENT_IOC_DISABLE, 0) < 0) {
            ssp_log(SSP_LOG_WARN, "Failed to disable event '%s'",
                   sess->events[i].event_name);
            return -1;
        }
    }
    
    return 0;
}

int perf_read_events(perf_session_t *sess) {
    if (sess == NULL) return -1;
    
    for (int i = 0; i < sess->num_events; i++) {
        uint64_t buf[3];
        ssize_t ret = read(sess->events[i].fd, buf, sizeof(buf));
        
        if (ret != sizeof(buf)) {
            ssp_log(SSP_LOG_WARN, "Failed to read event '%s': %s",
                   sess->events[i].event_name, strerror(errno));
            return -1;
        }
        
        sess->events[i].value = buf[0];
        sess->events[i].enabled = buf[1];
        sess->events[i].running = buf[2];
    }
    
    return 0;
}

int perf_reset_events(perf_session_t *sess) {
    if (sess == NULL) return -1;
    
    for (int i = 0; i < sess->num_events; i++) {
        if (ioctl(sess->events[i].fd, PERF_EVENT_IOC_RESET, 0) < 0) {
            ssp_log(SSP_LOG_WARN, "Failed to reset event '%s'",
                   sess->events[i].event_name);
            return -1;
        }
    }
    
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Statistics Collection */
/* ─────────────────────────────────────────────────────────────────────────── */

int perf_collect_stats(perf_session_t *sess, perf_stats_t *stats) {
    if (sess == NULL || stats == NULL) return -1;
    
    memset(stats, 0, sizeof(perf_stats_t));
    
    if (perf_read_events(sess) < 0) {
        return -1;
    }
    
    /* Extract event values */
    for (int i = 0; i < sess->num_events; i++) {
        const perf_event_t *ev = &sess->events[i];
        
        if (strcmp(ev->event_name, "cycles") == 0) {
            stats->cycles = ev->value;
        } else if (strcmp(ev->event_name, "instructions") == 0) {
            stats->instructions = ev->value;
        } else if (strcmp(ev->event_name, "cache_references") == 0) {
            stats->cache_references = ev->value;
        } else if (strcmp(ev->event_name, "cache_misses") == 0) {
            stats->cache_misses = ev->value;
        } else if (strcmp(ev->event_name, "branch_instructions") == 0) {
            stats->branch_instructions = ev->value;
        } else if (strcmp(ev->event_name, "branch_misses") == 0) {
            stats->branch_misses = ev->value;
        } else if (strcmp(ev->event_name, "context_switches") == 0) {
            stats->context_switches = ev->value;
        } else if (strcmp(ev->event_name, "cpu_migrations") == 0) {
            stats->cpu_migrations = ev->value;
        } else if (strcmp(ev->event_name, "page_faults") == 0) {
            stats->page_faults = ev->value;
        } else if (strcmp(ev->event_name, "major_faults") == 0) {
            stats->major_faults = ev->value;
        } else if (strcmp(ev->event_name, "minor_faults") == 0) {
            stats->minor_faults = ev->value;
        }
    }
    
    /* Compute metrics */
    stats->ipc = (stats->cycles > 0) ? 
        ((double)stats->instructions / (double)stats->cycles) : 0.0;
    
    stats->cache_miss_rate = (stats->cache_references > 0) ?
        ((double)stats->cache_misses / (double)stats->cache_references) : 0.0;
    
    stats->branch_miss_rate = (stats->branch_instructions > 0) ?
        ((double)stats->branch_misses / (double)stats->branch_instructions) : 0.0;
    
    stats->context_switch_ratio = (stats->cycles > 0) ?
        ((double)stats->context_switches / (double)stats->cycles) : 0.0;
    
    return 0;
}

perf_stats_t perf_stats_diff(const perf_stats_t *before, 
                             const perf_stats_t *after) {
    perf_stats_t diff = {
        .cycles = after->cycles - before->cycles,
        .instructions = after->instructions - before->instructions,
        .cache_references = after->cache_references - before->cache_references,
        .cache_misses = after->cache_misses - before->cache_misses,
        .branch_instructions = after->branch_instructions - before->branch_instructions,
        .branch_misses = after->branch_misses - before->branch_misses,
        .context_switches = after->context_switches - before->context_switches,
        .cpu_migrations = after->cpu_migrations - before->cpu_migrations,
        .page_faults = after->page_faults - before->page_faults,
        .minor_faults = after->minor_faults - before->minor_faults,
        .major_faults = after->major_faults - before->major_faults,
    };
    
    /* Recompute metrics for differences */
    diff.ipc = (diff.cycles > 0) ?
        ((double)diff.instructions / (double)diff.cycles) : 0.0;
    
    diff.cache_miss_rate = (diff.cache_references > 0) ?
        ((double)diff.cache_misses / (double)diff.cache_references) : 0.0;
    
    diff.branch_miss_rate = (diff.branch_instructions > 0) ?
        ((double)diff.branch_misses / (double)diff.branch_instructions) : 0.0;
    
    diff.context_switch_ratio = (diff.cycles > 0) ?
        ((double)diff.context_switches / (double)diff.cycles) : 0.0;
    
    return diff;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/* Output & Utilities */
/* ─────────────────────────────────────────────────────────────────────────── */

void perf_stats_print(const perf_stats_t *stats) {
    printf("Perf Statistics:\n");
    printf("  Cycles:                %lu\n", stats->cycles);
    printf("  Instructions:          %lu\n", stats->instructions);
    printf("  IPC:                   %.3f\n", stats->ipc);
    printf("  Cache References:      %lu\n", stats->cache_references);
    printf("  Cache Misses:          %lu\n", stats->cache_misses);
    printf("  Cache Miss Rate:       %.3f\n", stats->cache_miss_rate);
    printf("  Branch Instructions:   %lu\n", stats->branch_instructions);
    printf("  Branch Misses:         %lu\n", stats->branch_misses);
    printf("  Branch Miss Rate:      %.3f\n", stats->branch_miss_rate);
    printf("  Context Switches:      %lu\n", stats->context_switches);
    printf("  CPU Migrations:        %lu\n", stats->cpu_migrations);
    printf("  Page Faults:           %lu\n", stats->page_faults);
    printf("  Major Faults:          %lu\n", stats->major_faults);
}

char *perf_stats_to_json(const perf_stats_t *stats) {
    char *result = malloc(2048);
    if (!result) return NULL;
    
    snprintf(result, 2048,
        "{"
        "\"cycles\": %lu, "
        "\"instructions\": %lu, "
        "\"ipc\": %.6f, "
        "\"cache_references\": %lu, "
        "\"cache_misses\": %lu, "
        "\"cache_miss_rate\": %.6f, "
        "\"branch_instructions\": %lu, "
        "\"branch_misses\": %lu, "
        "\"branch_miss_rate\": %.6f, "
        "\"context_switches\": %lu, "
        "\"cpu_migrations\": %lu, "
        "\"page_faults\": %lu, "
        "\"major_faults\": %lu"
        "}",
        stats->cycles, stats->instructions, stats->ipc,
        stats->cache_references, stats->cache_misses, stats->cache_miss_rate,
        stats->branch_instructions, stats->branch_misses, stats->branch_miss_rate,
        stats->context_switches, stats->cpu_migrations,
        stats->page_faults, stats->major_faults);
    
    return result;
}

int perf_get_num_counters(void) {
    /* This would require reading /proc/sys/kernel/perf_event_paranoid
       For now, return a conservative estimate */
    return 4;  /* Most modern Intel/AMD CPUs have 4 hardware counters */
}

int perf_is_available(void) {
    /* Try to create a dummy session */
    perf_session_t *sess = perf_session_create(-1, 0);
    if (sess == NULL) return 0;
    
    int ret = perf_add_event(sess, PERF_TYPE_HARDWARE,
                            PERF_COUNT_HW_CPU_CYCLES, "test");
    
    perf_session_destroy(sess);
    
    return (ret == 0) ? 1 : 0;
}

int perf_set_max_events_limit(int limit) {
    /* This would require writing to /proc/sys/kernel/perf_event_paranoid
       Typically requires root, so just return success for now */
    (void)limit;
    return 0;
}
