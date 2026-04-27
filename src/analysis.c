/**
 * SSP Analysis Tool - Results Processing and Graph Generation
 * 
 * This tool processes benchmark CSV results and generates:
 * - Statistical analysis (mean, std-dev, min, max)
 * - Tail latency analysis (P50, P95, P99 percentiles)
 * - Scaling efficiency graphs (ASCII-based)
 * - Performance classification summaries
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>

#define MAX_RECORDS 1000
#define MAX_LINE_LENGTH 512

/* Data structures */
typedef struct {
    char timestamp[64];
    char os[32];
    char hostname[32];
    int total_cores;
    int active_cores;
    char workload_type[32];
    int intensity_pct;
    int data_size_kb;
    int num_procs;
    double latency_us;
    double std_us;
    char method[32];
    char notes[128];
} benchmark_record_t;

typedef struct {
    double mean;
    double std_dev;
    double min;
    double max;
    double p50;
    double p95;
    double p99;
    int count;
} stats_t;

/* Function prototypes */
int load_csv(const char *filename, benchmark_record_t *records, int max_records);
stats_t compute_stats(benchmark_record_t *records, int count, const char *workload, int cores);
void print_ascii_bar(double value, double max_val, int width);
void print_scaling_graph(benchmark_record_t *records, int count);
void print_latency_distribution(benchmark_record_t *records, int count);
void print_classification_summary(benchmark_record_t *records, int count);
void classify_performance(double ipc, double miss_rate, char *classification);

/**
 * Main entry point
 */
int main(int argc, char *argv[]) {
    const char *filename = "results/workload_benchmark.csv";
    int show_scaling = 0;
    int show_latency = 0;
    int show_classification = 0;
    int show_all = 1;
    
    /* Parse command line options */
    int opt;
    while ((opt = getopt(argc, argv, "f:slcah")) != -1) {
        switch (opt) {
            case 'f':
                filename = optarg;
                break;
            case 's':
                show_scaling = 1;
                show_all = 0;
                break;
            case 'l':
                show_latency = 1;
                show_all = 0;
                break;
            case 'c':
                show_classification = 1;
                show_all = 0;
                break;
            case 'a':
                show_all = 1;
                break;
            case 'h':
            default:
                printf("SSP Analysis Tool - Graph Generation\n\n");
                printf("Usage: %s [options]\n\n", argv[0]);
                printf("Options:\n");
                printf("  -f <file>    CSV file to analyze (default: results/workload_benchmark.csv)\n");
                printf("  -s           Show scaling efficiency graph\n");
                printf("  -l           Show tail latency distribution\n");
                printf("  -c           Show performance classification\n");
                printf("  -a           Show all analyses (default)\n");
                printf("  -h           Show this help\n");
                return 0;
        }
    }
    
    /* Load CSV data */
    benchmark_record_t records[MAX_RECORDS];
    int count = load_csv(filename, records, MAX_RECORDS);
    
    if (count <= 0) {
        fprintf(stderr, "Error: No data loaded from %s\n", filename);
        return 1;
    }
    
    printf("=== SSP Benchmark Analysis ===\n");
    printf("Loaded %d records from %s\n\n", count, filename);
    
    /* Show scaling graph */
    if (show_all || show_scaling) {
        printf("=== Scaling Efficiency Graph ===\n");
        print_scaling_graph(records, count);
        printf("\n");
    }
    
    /* Show latency distribution */
    if (show_all || show_latency) {
        printf("=== Tail Latency Analysis (P50, P95, P99) ===\n");
        print_latency_distribution(records, count);
        printf("\n");
    }
    
    /* Show classification */
    if (show_all || show_classification) {
        printf("=== Performance Classification ===\n");
        print_classification_summary(records, count);
        printf("\n");
    }
    
    return 0;
}

/**
 * Load CSV data into records
 */
int load_csv(const char *filename, benchmark_record_t *records, int max_records) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return -1;
    }
    
    char line[MAX_LINE_LENGTH];
    int count = 0;
    
    /* Skip header line */
    if (fgets(line, sizeof(line), fp)) {
        /* Header found */
    }
    
    /* Read data lines */
    while (fgets(line, sizeof(line), fp) && count < max_records) {
        benchmark_record_t *r = &records[count];
        int fields = sscanf(line, "%63[^,],%31[^,],%31[^,],%d,%d,%31[^,],%d,%d,%d,%lf,%lf,%31[^,],%127[^,]",
            r->timestamp, r->os, r->hostname,
            &r->total_cores, &r->active_cores,
            r->workload_type, &r->intensity_pct,
            &r->data_size_kb, &r->num_procs,
            &r->latency_us, &r->std_us,
            r->method, r->notes);
        
        if (fields >= 10) {
            count++;
        }
    }
    
    fclose(fp);
    return count;
}

/**
 * Compute statistics including tail latency percentiles
 */
stats_t compute_stats(benchmark_record_t *records, int count, const char *workload, int cores) {
    stats_t stats = {0};
    double *latencies = malloc(count * sizeof(double));
    int n = 0;
    
    /* Collect matching records */
    for (int i = 0; i < count; i++) {
        if ((workload == NULL || strcmp(records[i].workload_type, workload) == 0) &&
            (cores <= 0 || records[i].active_cores == cores)) {
            latencies[n++] = records[i].latency_us;
        }
    }
    
    if (n == 0) {
        free(latencies);
        return stats;
    }
    
    /* Sort for percentile calculation */
    qsort(latencies, n, sizeof(double), (int(*)(const void*, const void*))strcmp);
    
    /* Compute mean and std dev */
    double sum = 0, sum_sq = 0;
    stats.min = latencies[0];
    stats.max = latencies[n-1];
    
    for (int i = 0; i < n; i++) {
        sum += latencies[i];
        sum_sq += latencies[i] * latencies[i];
    }
    
    stats.mean = sum / n;
    double variance = (sum_sq / n) - (stats.mean * stats.mean);
    stats.std_dev = sqrt(variance > 0 ? variance : 0);
    
    /* Compute percentiles (tail latency) */
    if (n > 0) {
        stats.p50 = latencies[(int)(n * 0.50)];
        stats.p95 = latencies[(int)(n * 0.95)];
        stats.p99 = latencies[(int)(n * 0.99)];
    }
    
    stats.count = n;
    free(latencies);
    
    return stats;
}

/**
 * Print ASCII bar chart
 */
void print_ascii_bar(double value, double max_val, int width) {
    int bars = (int)((value / max_val) * width);
    if (bars > width) bars = width;
    if (bars < 0) bars = 0;
    
    for (int i = 0; i < bars; i++) {
        printf("█");
    }
    for (int i = bars; i < width; i++) {
        printf("░");
    }
}

/**
 * Print scaling efficiency graph
 */
void print_scaling_graph(benchmark_record_t *records, int count) {
    printf("Core Count vs. Latency (ASCII Graph)\n");
    printf("%-10s %-10s %-20s %s\n", "Cores", "Mean(μs)", "Graph", "Classification");
    printf("%-10s %-10s %-20s %s\n", "------", "--------", "-----", "-------------");
    
    /* Group by core count */
    for (int cores = 1; cores <= 8; cores *= 2) {
        stats_t stats = compute_stats(records, count, NULL, cores);
        
        if (stats.count > 0) {
            char classification[32];
            double estimated_ipc = 1.0 / (stats.mean / 10.0);  /* Rough estimate */
            double estimated_miss_rate = 0.1;  /* Placeholder */
            classify_performance(estimated_ipc, estimated_miss_rate, classification);
            
            printf("%-10d %-10.2f ", cores, stats.mean);
            print_ascii_bar(stats.mean, 20.0, 20);
            printf(" %s\n", classification);
        }
    }
}

/**
 * Print tail latency distribution
 */
void print_latency_distribution(benchmark_record_t *records, int count) {
    printf("%-15s %-10s %-10s %-10s %-10s\n", "Workload", "Mean", "P50", "P95", "P99");
    printf("%-15s %-10s %-10s %-10s %-10s\n", "-----------", "----", "---", "---", "---");
    
    const char *workloads[] = {"baseline", "cpu", "memory", "io", "mixed"};
    int num_workloads = sizeof(workloads) / sizeof(workloads[0]);
    
    for (int w = 0; w < num_workloads; w++) {
        stats_t stats = compute_stats(records, count, workloads[w], -1);
        
        if (stats.count > 0) {
            printf("%-15s %-10.2f %-10.2f %-10.2f %-10.2f\n",
                workloads[w], stats.mean, stats.p50, stats.p95, stats.p99);
        }
    }
}

/**
 * Print performance classification summary
 */
void print_classification_summary(benchmark_record_t *records, int count) {
    printf("%-15s %-10s %-10s %-15s %s\n", "Workload", "Cores", "Intensity", "IPC(est)", "Classification");
    printf("%-15s %-10s %-10s %-15s %s\n", "-----------", "-----", "---------", "--------", "-------------");
    
    /* Sample key configurations */
    for (int cores = 1; cores <= 4; cores *= 2) {
        for (int intensity = 25; intensity <= 75; intensity += 50) {
            int found = 0;
            double total_ipc = 0;
            int n = 0;
            char workload[32] = "";
            
            for (int i = 0; i < count; i++) {
                if (records[i].active_cores == cores && 
                    records[i].intensity_pct == intensity) {
                    
                    if (!found) {
                        strcpy(workload, records[i].workload_type);
                        found = 1;
                    }
                    
                    /* Estimate IPC from latency (inverse relationship) */
                    double estimated_ipc = 10.0 / records[i].latency_us;
                    total_ipc += estimated_ipc;
                    n++;
                }
            }
            
            if (found && n > 0) {
                double avg_ipc = total_ipc / n;
                double miss_rate = 0.1 + (intensity / 100.0) * 0.15;
                char classification[32];
                classify_performance(avg_ipc, miss_rate, classification);
                
                printf("%-15s %-10d %-10d %-15.2f %s\n",
                    workload, cores, intensity, avg_ipc, classification);
            }
        }
    }
}

/**
 * Classify performance based on metrics
 */
void classify_performance(double ipc, double miss_rate, char *classification) {
    if (ipc < 0.5) {
        strcpy(classification, "ILP_LIMITED");
    } else if (miss_rate > 0.20) {
        strcpy(classification, "MEMORY_BOUND");
    } else if (ipc > 1.5 && miss_rate < 0.10) {
        strcpy(classification, "COMPUTE_OPTIMAL");
    } else {
        strcpy(classification, "MIXED");
    }
}
