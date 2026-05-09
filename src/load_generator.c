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
void cpu_load_worker(int intensity, int duration, int cpu_id, ssp_metrics_t *metrics);
void io_load_worker(int intensity, int duration, ssp_metrics_t *metrics);
void memory_load_worker(int intensity, int duration, int cache_level, ssp_metrics_t *metrics);
void mixed_load_worker(int intensity, int duration, int num_cpus, ssp_metrics_t *metrics);

/**
 * Display usage information
 */
static void print_usage(const char *prog_name) {
    printf("SSP Load Generator - Generate configurable system load\n\n");
    printf("Usage: %s --type <TYPE> --intensity <INTENSITY> --duration <DURATION> [options]\n", prog_name);
    printf("   or: %s --type <TYPE> --intensity <INTENSITY> --measure <SECONDS> [options]\n\n", prog_name);
    
    printf("Load Types:\n");
    printf("  cpu       – CPU-bound arithmetic loop with duty-cycle control\n");
    printf("  io        – File create/write/fsync/read cycles\n");
    printf("  memory    – Strided memory access (cache hierarchy stress)\n");
    printf("  mixed     – Combined CPU + memory load (two threads)\n\n");
    
    printf("Required Arguments:\n");
    printf("  --type <TYPE>              Load type (cpu|io|memory|mixed)\n");
    printf("  --intensity <INTENSITY>    Load intensity 0-100 (percent)\n");
    printf("  --duration <DURATION>      Steady-state measurement seconds\n\n");
    
    printf("Optional Arguments:\n");
    printf("  --warmup <SECONDS>         Warmup phase duration (default: 0)\n");
    printf("  --measure <SECONDS>        Steady-state measurement duration\n");
    printf("  --cooldown <SECONDS>       Cooldown phase duration (default: 0)\n");
    printf("  --cpu <N>                  Pin process to CPU N (default: -1, no pinning)\n");
    printf("  --cache-level <N>          Memory stride target: 1=L1, 2=L2, 3=L3 (default: 1)\n");
    printf("  --verbose                  Enable verbose logging\n");
    printf("  --help                     Show this help message\n\n");
    
    printf("Examples:\n");
    printf("  %s --type cpu --intensity 75 --duration 60\n", prog_name);
    printf("  %s --type cpu --intensity 75 --warmup 5 --measure 30 --cooldown 5 --cpu 0\n", prog_name);
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
    int warmup;
    int measure;
    int cooldown;
    int cpu_id;
    int cache_level;
    int verbose;
    char metrics_file[256];
    int valid;
} args_t;

static args_t parse_args(int argc, char *argv[]) {
    args_t args = {
        .load_type = {0},
        .intensity = -1,
        .duration = -1,
        .warmup = 0,
        .measure = -1,
        .cooldown = 0,
        .cpu_id = -1,
        .cache_level = 1,
        .verbose = 0,
        .metrics_file = {0},
        .valid = 1
    };
    
    static struct option long_options[] = {
        {"type",        required_argument, 0, 't'},
        {"intensity",   required_argument, 0, 'i'},
        {"duration",    required_argument, 0, 'd'},
        {"warmup",      required_argument, 0, 'w'},
        {"measure",     required_argument, 0, 'M'},
        {"cooldown",    required_argument, 0, 'C'},
        {"cpu",         required_argument, 0, 'c'},
        {"cache-level", required_argument, 0, 'l'},
        {"metrics-file", required_argument, 0, 'm'},
        {"verbose",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt_index = 0;
    int opt;
    
    while ((opt = getopt_long(argc, argv, "t:i:d:w:M:C:c:l:m:vh", 
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
            case 'w':
                args.warmup = atoi(optarg);
                break;
            case 'M':
                args.measure = atoi(optarg);
                break;
            case 'C':
                args.cooldown = atoi(optarg);
                break;
            case 'c':
                args.cpu_id = atoi(optarg);
                break;
            case 'l':
                args.cache_level = atoi(optarg);
                break;
            case 'm':
                strncpy(args.metrics_file, optarg, sizeof(args.metrics_file) - 1);
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
    
    if (args->duration <= 0 && args->measure <= 0) {
        fprintf(stderr, "Error: --duration or --measure must be positive\n");
        return 0;
    }

    if (args->warmup < 0 || args->cooldown < 0) {
        fprintf(stderr, "Error: warmup/cooldown must be >= 0\n");
        return 0;
    }

    if (args->measure == 0) {
        fprintf(stderr, "Error: measure must be > 0 if specified\n");
        return 0;
    }
    
    if (args->cache_level < 0 || args->cache_level > 3) {
        fprintf(stderr, "Error: cache-level must be 0-3 (got %d)\n", args->cache_level);
        return 0;
    }
    
    return 1;
}

static int write_metrics_file(const char *path, const ssp_metrics_t *m) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }

    fprintf(fp, "%llu,%llu,%.12Lf,%llu,%llu,%.12Lf\n",
            (unsigned long long)m->operations,
            (unsigned long long)m->total_latency_ns,
            m->sum_latency_sq_ns,
            (unsigned long long)m->io_cycles,
            (unsigned long long)m->io_cycle_total_latency_ns,
            m->io_cycle_sum_latency_sq_ns);
    fclose(fp);
    return 0;
}

static void run_one_phase(const args_t *args, int phase_seconds, ssp_metrics_t *metrics) {
    if (phase_seconds <= 0) {
        return;
    }

    if (strcmp(args->load_type, "cpu") == 0) {
        cpu_load_worker(args->intensity, phase_seconds, args->cpu_id, metrics);
    } else if (strcmp(args->load_type, "io") == 0) {
        io_load_worker(args->intensity, phase_seconds, metrics);
    } else if (strcmp(args->load_type, "memory") == 0) {
        memory_load_worker(args->intensity, phase_seconds, args->cache_level, metrics);
    } else if (strcmp(args->load_type, "mixed") == 0) {
        int num_cpus = ssp_get_cpu_count();
        mixed_load_worker(args->intensity, phase_seconds, num_cpus, metrics);
    }
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
    int measure_sec = (args.measure > 0) ? args.measure : args.duration;
    ssp_log(SSP_LOG_INFO,
            "Type=%s Intensity=%d%% warmup=%ds measure=%ds cooldown=%ds",
            args.load_type, args.intensity, args.warmup, measure_sec, args.cooldown);

    /* Deterministic placement when benchmark passes --cpu. */
    if (args.cpu_id >= 0) {
        ssp_set_affinity(args.cpu_id);
    }

    ssp_metrics_t metrics;
    ssp_metrics_init(&metrics);

    /* Warmup and cooldown intentionally run without recording metrics. */
    run_one_phase(&args, args.warmup, NULL);
    run_one_phase(&args, measure_sec, &metrics);
    run_one_phase(&args, args.cooldown, NULL);

    if (args.metrics_file[0]) {
        if (write_metrics_file(args.metrics_file, &metrics) != 0) {
            ssp_log(SSP_LOG_ERROR, "Failed to write metrics file: %s", args.metrics_file);
            return -1;
        }
    }
    
    ssp_log(SSP_LOG_INFO, "Load generation complete");
    
    return 0;
}
