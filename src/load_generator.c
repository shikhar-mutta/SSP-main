/**
 * SSP Load Generator - Main Entry Point
 * 
 * Unified interface for generating CPU, I/O, Memory, or Mixed workloads.
 * This replaces the Python load_generator.py
 */

#include "ssp_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

/* Forward declarations */
void cpu_load_worker(int intensity, int duration, int cpu_id);
void io_load_worker(int intensity, int duration);
void memory_load_worker(int intensity, int duration, int cache_level);
void mixed_load_worker(int intensity, int duration, int num_cpus);

/**
 * Display usage information
 */
static void print_usage(const char *prog_name) {
    printf("SSP Load Generator - Generate configurable system load\n\n");
    printf("Usage: %s --type <TYPE> --intensity <INTENSITY> --duration <DURATION> [options]\n\n", prog_name);
    
    printf("Load Types:\n");
    printf("  cpu       – CPU-bound arithmetic loop with duty-cycle control\n");
    printf("  io        – File create/write/fsync/read cycles\n");
    printf("  memory    – Strided memory access (cache hierarchy stress)\n");
    printf("  mixed     – Combined CPU + memory load (two threads)\n\n");
    
    printf("Required Arguments:\n");
    printf("  --type <TYPE>              Load type (cpu|io|memory|mixed)\n");
    printf("  --intensity <INTENSITY>    Load intensity 0-100 (percent)\n");
    printf("  --duration <DURATION>      Run for this many seconds\n\n");
    
    printf("Optional Arguments:\n");
    printf("  --cpu <N>                  Pin CPU load to CPU N (default: -1, no pinning)\n");
    printf("  --cache-level <N>          Memory stride target: 1=L1, 2=L2, 3=L3 (default: 1)\n");
    printf("  --verbose                  Enable verbose logging\n");
    printf("  --help                     Show this help message\n\n");
    
    printf("Examples:\n");
    printf("  %s --type cpu --intensity 75 --duration 60\n", prog_name);
    printf("  %s --type memory --intensity 50 --duration 60 --cache-level 2\n", prog_name);
    printf("  %s --type mixed --intensity 80 --duration 120 --verbose\n", prog_name);
}

/**
 * Parse command-line arguments
 */
typedef struct {
    char load_type[32];
    int intensity;
    int duration;
    int cpu_id;
    int cache_level;
    int verbose;
    int valid;
} args_t;

static args_t parse_args(int argc, char *argv[]) {
    args_t args = {
        .load_type = {0},
        .intensity = -1,
        .duration = -1,
        .cpu_id = -1,
        .cache_level = 1,
        .verbose = 0,
        .valid = 1
    };
    
    static struct option long_options[] = {
        {"type",        required_argument, 0, 't'},
        {"intensity",   required_argument, 0, 'i'},
        {"duration",    required_argument, 0, 'd'},
        {"cpu",         required_argument, 0, 'c'},
        {"cache-level", required_argument, 0, 'l'},
        {"verbose",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt_index = 0;
    int opt;
    
    while ((opt = getopt_long(argc, argv, "t:i:d:c:l:vh", 
                              long_options, &opt_index)) != -1) {
        switch (opt) {
            case 't':
                strncpy(args.load_type, optarg, sizeof(args.load_type) - 1);
                break;
            case 'i':
                args.intensity = atoi(optarg);
                break;
            case 'd':
                args.duration = atoi(optarg);
                break;
            case 'c':
                args.cpu_id = atoi(optarg);
                break;
            case 'l':
                args.cache_level = atoi(optarg);
                break;
            case 'v':
                args.verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case '?':
            default:
                args.valid = 0;
                break;
        }
    }
    
    return args;
}

/**
 * Validate arguments
 */
static int validate_args(const args_t *args) {
    if (!args->valid) {
        return 0;
    }
    
    if (args->load_type[0] == '\0') {
        fprintf(stderr, "Error: --type is required\n");
        return 0;
    }
    
    if (strcmp(args->load_type, "cpu") != 0 &&
        strcmp(args->load_type, "io") != 0 &&
        strcmp(args->load_type, "memory") != 0 &&
        strcmp(args->load_type, "mixed") != 0) {
        fprintf(stderr, "Error: invalid load type '%s'\n", args->load_type);
        return 0;
    }
    
    if (args->intensity < 0 || args->intensity > 100) {
        fprintf(stderr, "Error: intensity must be 0-100 (got %d)\n", args->intensity);
        return 0;
    }
    
    if (args->duration <= 0) {
        fprintf(stderr, "Error: duration must be positive (got %d)\n", args->duration);
        return 0;
    }
    
    if (args->cache_level < 0 || args->cache_level > 3) {
        fprintf(stderr, "Error: cache-level must be 0-3 (got %d)\n", args->cache_level);
        return 0;
    }
    
    return 1;
}

/**
 * Main entry point
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }
    
    /* Parse arguments */
    args_t args = parse_args(argc, argv);
    
    if (!validate_args(&args)) {
        fprintf(stderr, "\nRun with --help for usage information.\n");
        return -1;
    }
    
    /* Setup logging */
    if (args.verbose) {
        ssp_set_log_level(SSP_LOG_DEBUG);
    } else {
        ssp_set_log_level(SSP_LOG_INFO);
    }
    
    /* Initialize signal handlers for clean shutdown */
    ssp_init_signal_handlers();
    
    ssp_log(SSP_LOG_INFO, "SSP Load Generator started");
    ssp_log(SSP_LOG_INFO, "Type=%s Intensity=%d%% Duration=%ds", 
            args.load_type, args.intensity, args.duration);
    
    /* Dispatch to appropriate load generator */
    if (strcmp(args.load_type, "cpu") == 0) {
        cpu_load_worker(args.intensity, args.duration, args.cpu_id);
    } else if (strcmp(args.load_type, "io") == 0) {
        io_load_worker(args.intensity, args.duration);
    } else if (strcmp(args.load_type, "memory") == 0) {
        memory_load_worker(args.intensity, args.duration, args.cache_level);
    } else if (strcmp(args.load_type, "mixed") == 0) {
        int num_cpus = ssp_get_cpu_count();
        mixed_load_worker(args.intensity, args.duration, num_cpus);
    }
    
    ssp_log(SSP_LOG_INFO, "Load generation complete");
    
    return 0;
}
