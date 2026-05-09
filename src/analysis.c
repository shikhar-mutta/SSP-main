/**
 * SSP Analysis Tool - Unit-Aware Results Summary
 *
 * Reports three core views:
 * 1) Unit latency per workload type
 * 2) Throughput per workload type
 * 3) Slowdown vs baseline (cores=1, intensity=25)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>

#define MAX_RECORDS 4096
#define MAX_LINE_LENGTH 4096
#define MAX_TOKENS 128

typedef struct {
    char workload_type[32];
    int active_cores;
    int intensity_pct;
    char unit_label[32];
    double unit_latency_us;
    double unit_std_us;
    double throughput_value;
    char throughput_unit[32];
    double slowdown_vs_baseline;
    double io_cycle_latency_us;
} benchmark_record_t;

typedef struct {
    double mean;
    double std_dev;
    double min;
    double max;
    int count;
} stats_t;

static int split_csv_line(char *line, char tokens[][128], int max_tokens) {
    int n = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(line, ",", &saveptr);
    while (tok && n < max_tokens) {
        strncpy(tokens[n], tok, sizeof(tokens[n]) - 1);
        tokens[n][sizeof(tokens[n]) - 1] = '\0';
        n++;
        tok = strtok_r(NULL, ",", &saveptr);
    }
    return n;
}

static int header_index(char tokens[][128], int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(tokens[i], name) == 0) return i;
    }
    return -1;
}

static double parse_double_or_zero(const char *s) {
    if (!s || !*s) return 0.0;
    return atof(s);
}

static int parse_int_or_zero(const char *s) {
    if (!s || !*s) return 0;
    return atoi(s);
}

static int load_csv(const char *filename, benchmark_record_t *records, int max_records) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return -1;
    }

    char line[MAX_LINE_LENGTH];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }

    char header_line[MAX_LINE_LENGTH];
    strncpy(header_line, line, sizeof(header_line) - 1);
    header_line[sizeof(header_line) - 1] = '\0';

    char headers[MAX_TOKENS][128];
    int hcount = split_csv_line(header_line, headers, MAX_TOKENS);

    int idx_workload = header_index(headers, hcount, "workload_type");
    int idx_cores = header_index(headers, hcount, "active_cores");
    int idx_intensity = header_index(headers, hcount, "intensity_pct");
    int idx_unit_label = header_index(headers, hcount, "unit_label");
    int idx_unit_latency = header_index(headers, hcount, "unit_latency_us");
    int idx_unit_std = header_index(headers, hcount, "unit_std_us");
    int idx_throughput = header_index(headers, hcount, "throughput_value");
    int idx_throughput_unit = header_index(headers, hcount, "throughput_unit");
    int idx_slowdown = header_index(headers, hcount, "slowdown_vs_baseline");
    int idx_io_cycle = header_index(headers, hcount, "io_cycle_latency_us");

    int idx_latency = header_index(headers, hcount, "latency_us");
    int idx_std = header_index(headers, hcount, "std_us");

    if (idx_workload < 0 || idx_cores < 0 || idx_intensity < 0) {
        fclose(fp);
        return -1;
    }

    int count = 0;
    while (fgets(line, sizeof(line), fp) && count < max_records) {
        char row[MAX_LINE_LENGTH];
        strncpy(row, line, sizeof(row) - 1);
        row[sizeof(row) - 1] = '\0';

        char tokens[MAX_TOKENS][128];
        int tcount = split_csv_line(row, tokens, MAX_TOKENS);

        if (tcount <= idx_intensity || tcount <= idx_workload || tcount <= idx_cores) {
            continue;
        }

        benchmark_record_t *r = &records[count];
        memset(r, 0, sizeof(*r));

        strncpy(r->workload_type, tokens[idx_workload], sizeof(r->workload_type) - 1);
        r->active_cores = parse_int_or_zero(tokens[idx_cores]);
        r->intensity_pct = parse_int_or_zero(tokens[idx_intensity]);

        if (idx_unit_label >= 0 && idx_unit_label < tcount) {
            strncpy(r->unit_label, tokens[idx_unit_label], sizeof(r->unit_label) - 1);
        }
        if (idx_unit_latency >= 0 && idx_unit_latency < tcount) {
            r->unit_latency_us = parse_double_or_zero(tokens[idx_unit_latency]);
        }
        if (idx_unit_std >= 0 && idx_unit_std < tcount) {
            r->unit_std_us = parse_double_or_zero(tokens[idx_unit_std]);
        }
        if (idx_throughput >= 0 && idx_throughput < tcount) {
            r->throughput_value = parse_double_or_zero(tokens[idx_throughput]);
        }
        if (idx_throughput_unit >= 0 && idx_throughput_unit < tcount) {
            strncpy(r->throughput_unit, tokens[idx_throughput_unit], sizeof(r->throughput_unit) - 1);
        }
        if (idx_slowdown >= 0 && idx_slowdown < tcount) {
            r->slowdown_vs_baseline = parse_double_or_zero(tokens[idx_slowdown]);
        }
        if (idx_io_cycle >= 0 && idx_io_cycle < tcount) {
            r->io_cycle_latency_us = parse_double_or_zero(tokens[idx_io_cycle]);
        }

        /* Backward compatibility with older CSV schemas */
        if (r->unit_latency_us <= 0.0 && idx_latency >= 0 && idx_latency < tcount) {
            r->unit_latency_us = parse_double_or_zero(tokens[idx_latency]);
        }
        if (r->unit_std_us <= 0.0 && idx_std >= 0 && idx_std < tcount) {
            r->unit_std_us = parse_double_or_zero(tokens[idx_std]);
        }
        if (strcmp(r->workload_type, "io") == 0 && r->unit_latency_us <= 0.0 && r->io_cycle_latency_us > 0.0) {
            r->unit_latency_us = r->io_cycle_latency_us;
            strncpy(r->unit_label, "time/cycle", sizeof(r->unit_label) - 1);
        }

        if (r->unit_label[0] == '\0') {
            if (strcmp(r->workload_type, "memory") == 0) {
                strncpy(r->unit_label, "time/access", sizeof(r->unit_label) - 1);
            } else if (strcmp(r->workload_type, "io") == 0) {
                strncpy(r->unit_label, "time/cycle", sizeof(r->unit_label) - 1);
            } else {
                strncpy(r->unit_label, "time/op", sizeof(r->unit_label) - 1);
            }
        }

        if (r->throughput_unit[0] == '\0') {
            if (strcmp(r->workload_type, "memory") == 0) {
                strncpy(r->throughput_unit, "access/s", sizeof(r->throughput_unit) - 1);
            } else if (strcmp(r->workload_type, "io") == 0) {
                strncpy(r->throughput_unit, "MB/s", sizeof(r->throughput_unit) - 1);
            } else {
                strncpy(r->throughput_unit, "ops/s", sizeof(r->throughput_unit) - 1);
            }
        }

        count++;
    }

    fclose(fp);
    return count;
}

static stats_t compute_stats(const double *vals, int n) {
    stats_t st = {0};
    if (n <= 0) return st;

    st.min = vals[0];
    st.max = vals[0];

    double sum = 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < n; i++) {
        double v = vals[i];
        sum += v;
        sum_sq += v * v;
        if (v < st.min) st.min = v;
        if (v > st.max) st.max = v;
    }

    st.mean = sum / (double)n;
    double var = (sum_sq / (double)n) - (st.mean * st.mean);
    if (var < 0.0) var = 0.0;
    st.std_dev = sqrt(var);
    st.count = n;
    return st;
}

static void print_unit_latency_view(benchmark_record_t *records, int count) {
    const char *workloads[] = {"cpu", "memory", "io", "mixed"};
    int nw = (int)(sizeof(workloads) / sizeof(workloads[0]));

    printf("=== View 1: Unit Latency Per Workload ===\n");
    printf("%-10s %-14s %-14s %-14s %-14s\n", "Workload", "Unit", "Mean(us)", "Std(us)", "Samples");
    printf("%-10s %-14s %-14s %-14s %-14s\n", "--------", "----", "--------", "-------", "-------");

    for (int w = 0; w < nw; w++) {
        double vals[MAX_RECORDS];
        int n = 0;
        char unit_label[32] = "";

        for (int i = 0; i < count; i++) {
            if (strcmp(records[i].workload_type, workloads[w]) != 0) continue;
            if (records[i].unit_latency_us <= 0.0) continue;
            vals[n++] = records[i].unit_latency_us;
            if (unit_label[0] == '\0') {
                strncpy(unit_label, records[i].unit_label, sizeof(unit_label) - 1);
            }
        }

        if (n > 0) {
            stats_t st = compute_stats(vals, n);
            printf("%-10s %-14s %-14.6f %-14.6f %-14d\n",
                   workloads[w], unit_label[0] ? unit_label : "n/a", st.mean, st.std_dev, st.count);
        }
    }
    printf("\n");
}

static void print_throughput_view(benchmark_record_t *records, int count) {
    const char *workloads[] = {"cpu", "memory", "io", "mixed"};
    int nw = (int)(sizeof(workloads) / sizeof(workloads[0]));

    printf("=== View 2: Throughput Per Workload ===\n");
    printf("%-10s %-14s %-14s %-14s %-14s\n", "Workload", "Unit", "Mean", "Std", "Samples");
    printf("%-10s %-14s %-14s %-14s %-14s\n", "--------", "----", "----", "---", "-------");

    for (int w = 0; w < nw; w++) {
        double vals[MAX_RECORDS];
        int n = 0;
        char unit[32] = "";

        for (int i = 0; i < count; i++) {
            if (strcmp(records[i].workload_type, workloads[w]) != 0) continue;
            if (records[i].throughput_value <= 0.0) continue;
            vals[n++] = records[i].throughput_value;
            if (unit[0] == '\0') {
                strncpy(unit, records[i].throughput_unit, sizeof(unit) - 1);
            }
        }

        if (n > 0) {
            stats_t st = compute_stats(vals, n);
            printf("%-10s %-14s %-14.3f %-14.3f %-14d\n",
                   workloads[w], unit[0] ? unit : "n/a", st.mean, st.std_dev, st.count);
        }
    }
    printf("\n");
}

static void print_slowdown_view(benchmark_record_t *records, int count) {
    const char *workloads[] = {"cpu", "memory", "io", "mixed"};
    int nw = (int)(sizeof(workloads) / sizeof(workloads[0]));

    printf("=== View 3: Slowdown vs Baseline (cores=1, intensity=25) ===\n");
    printf("%-10s %-14s %-14s %-14s %-14s\n", "Workload", "Mean S", "Std S", "Min S", "Max S");
    printf("%-10s %-14s %-14s %-14s %-14s\n", "--------", "------", "-----", "-----", "-----");

    for (int w = 0; w < nw; w++) {
        double vals[MAX_RECORDS];
        int n = 0;

        for (int i = 0; i < count; i++) {
            if (strcmp(records[i].workload_type, workloads[w]) != 0) continue;
            if (records[i].slowdown_vs_baseline <= 0.0) continue;
            vals[n++] = records[i].slowdown_vs_baseline;
        }

        if (n > 0) {
            stats_t st = compute_stats(vals, n);
            printf("%-10s %-14.6f %-14.6f %-14.6f %-14.6f\n",
                   workloads[w], st.mean, st.std_dev, st.min, st.max);
        }
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    const char *filename = "results/workload_benchmark_v2.csv";

    int opt;
    while ((opt = getopt(argc, argv, "f:h")) != -1) {
        switch (opt) {
            case 'f':
                filename = optarg;
                break;
            case 'h':
            default:
                printf("SSP Analysis Tool - Unit-aware summary\n\n");
                printf("Usage: %s [options]\n", argv[0]);
                printf("  -f <file>    CSV file (default: results/workload_benchmark_v2.csv)\n");
                printf("  -h           Show help\n");
                return 0;
        }
    }

    benchmark_record_t records[MAX_RECORDS];
    int count = load_csv(filename, records, MAX_RECORDS);
    if (count <= 0) {
        fprintf(stderr, "Error: No data loaded from %s\n", filename);
        return 1;
    }

    printf("=== SSP Benchmark Analysis ===\n");
    printf("Loaded %d records from %s\n\n", count, filename);

    print_unit_latency_view(records, count);
    print_throughput_view(records, count);
    print_slowdown_view(records, count);

    return 0;
}
