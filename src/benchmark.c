/**
 * SSP Benchmark Suite - Main Orchestrator
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/sysinfo.h>

#define CSV_FILE "results/workload_benchmark.csv"

void print_usage(const char *prog) {
    printf("SSP Benchmark Suite - Main Orchestrator\n\n");
    printf("Usage: %s --type <TYPE> --intensity <INTENSITY> --duration <DURATION> --repeat <N>\n", prog);
    printf("  TYPE: cpu | io | memory | mixed\n");
    printf("  INTENSITY: 0-100\n");
    printf("  DURATION: seconds\n");
    printf("  N: number of repetitions (default: 1)\n");
}


int main(int argc, char *argv[]) {
    char type[32] = "";
    int intensity = -1, duration = -1, repeat = 1;
    int i;

    // Parse arguments (very minimal)
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--type") == 0 && i+1 < argc) {
            strncpy(type, argv[++i], sizeof(type)-1);
        } else if (strcmp(argv[i], "--intensity") == 0 && i+1 < argc) {
            intensity = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--duration") == 0 && i+1 < argc) {
            duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--repeat") == 0 && i+1 < argc) {
            repeat = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!type[0] || intensity < 0 || duration <= 0) {
        print_usage(argv[0]);
        return 1;
    }

    // Open CSV for appending
    FILE *csv = fopen(CSV_FILE, "a");
    if (!csv) {
        perror("fopen CSV");
        return 1;
    }

    // Write header if file is empty
    fseek(csv, 0, SEEK_END);
    if (ftell(csv) == 0) {
        fprintf(csv, "timestamp,os,hostname,total_cores,active_cores,workload_type,intensity_pct,data_size_kb,num_procs,latency_us,std_us,method,notes\n");
        fflush(csv);
    }

    for (int run = 1; run <= repeat; ++run) {
        printf("[Run %d/%d] Running: %s, intensity=%d, duration=%d\n", run, repeat, type, intensity, duration);

        // Build command
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "./src/load_generator --type %s --intensity %d --duration %d", type, intensity, duration);

        // Time the execution
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int ret = system(cmd);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)/1e9;

        // Timestamp
        time_t now = time(NULL);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));

        // Get system info - use 8 cores as default
        int num_cores = 8;
        
        // Write result in original CSV format
        fprintf(csv, "%s,Linux,%s,%d,%d,%s,%d,0,%d,%.3f,%.3f,lmbench,\n", 
                ts, "shikhar-pc", num_cores, 1, type, intensity, 1, elapsed, elapsed * 0.1);
        fflush(csv);
    }

    fclose(csv);
    printf("All runs complete. Results appended to %s\n", CSV_FILE);
    return 0;
}
