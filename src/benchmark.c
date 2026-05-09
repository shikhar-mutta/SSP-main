/**
 * SSP Benchmark Suite - Main Orchestrator
 *
 * Features:
 * - Deterministic core placement (child i -> core i)
 * - Warmup/measure/cooldown phase forwarding to workers
 * - Per-PID perf counter attribution
 * - Unit-aware latency and throughput metrics
 * - Slowdown vs workload baseline (cores=1, intensity=25)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>

#include "ssp_lib.h"
#include "sched_tracer.h"
#include "perf_events.h"

#define CSV_FILE "results/workload_benchmark_v2.csv"
#define MAX_CORES 64
#define MAX_TOKENS 128

typedef struct {
    pid_t pid;
    int core_id;
    char metrics_path[128];
    perf_session_t *perf_session;
} child_proc_t;

static void print_usage(const char *prog) {
    printf("SSP Benchmark Suite - Main Orchestrator\n\n");
    printf("Usage: %s --type <TYPE> --intensity <INT> [--duration <DUR> | --measure <DUR>] [options]\n", prog);
    printf("  TYPE:      cpu | io | memory | mixed\n");
    printf("  INTENSITY: 0-100\n");
    printf("  DUR:       seconds per run\n\n");
    printf("Options:\n");
    printf("  --repeat <N>       repetitions (default: 1)\n");
    printf("  --cores <C>        active core count (default: 1)\n");
    printf("  --warmup <S>       warmup seconds (default: 0)\n");
    printf("  --measure <S>      steady-state measure seconds\n");
    printf("  --cooldown <S>     cooldown seconds (default: 0)\n");
    printf("  --no-perf          disable perf collection\n");
}

static void sanitize_csv_field(char *s) {
    if (!s) return;
    for (; *s; ++s) {
        if (*s == ',') *s = ';';
        if (*s == '\n' || *s == '\r') *s = ' ';
    }
}

static int read_first_line(const char *path, char *out, size_t out_sz) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    if (!fgets(out, (int)out_sz, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    char *nl = strpbrk(out, "\r\n");
    if (nl) *nl = '\0';
    sanitize_csv_field(out);
    return 0;
}

static int read_cpu_model(char *out, size_t out_sz) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return -1;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "model name", 10) == 0) {
            char *colon = strchr(line, ':');
            if (!colon) continue;
            colon++;
            while (*colon == ' ' || *colon == '\t') colon++;
            strncpy(out, colon, out_sz - 1);
            out[out_sz - 1] = '\0';
            char *nl = strpbrk(out, "\r\n");
            if (nl) *nl = '\0';
            sanitize_csv_field(out);
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return -1;
}

static int run_cmd_first_line(const char *cmd, char *out, size_t out_sz) {
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return -1;

    if (!fgets(out, (int)out_sz, pipe)) {
        pclose(pipe);
        return -1;
    }
    pclose(pipe);

    char *nl = strpbrk(out, "\r\n");
    if (nl) *nl = '\0';
    sanitize_csv_field(out);
    return 0;
}

static int read_metrics_file(const char *path, ssp_metrics_t *out) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    unsigned long long ops = 0ULL, total_ns = 0ULL;
    unsigned long long io_cycles = 0ULL, io_cycle_total_ns = 0ULL;
    long double sum_sq_ns = 0.0L, io_cycle_sum_sq_ns = 0.0L;
    int rc = fscanf(fp, "%llu,%llu,%Lf,%llu,%llu,%Lf",
                    &ops, &total_ns, &sum_sq_ns,
                    &io_cycles, &io_cycle_total_ns, &io_cycle_sum_sq_ns);
    fclose(fp);

    if (rc != 3 && rc != 6) return -1;

    out->operations = (uint64_t)ops;
    out->total_latency_ns = (uint64_t)total_ns;
    out->sum_latency_sq_ns = sum_sq_ns;
    if (rc == 6) {
        out->io_cycles = (uint64_t)io_cycles;
        out->io_cycle_total_latency_ns = (uint64_t)io_cycle_total_ns;
        out->io_cycle_sum_latency_sq_ns = io_cycle_sum_sq_ns;
    }
    return 0;
}

static void perf_stats_accumulate(perf_stats_t *dst, const perf_stats_t *src) {
    if (!dst || !src) return;

    dst->cycles += src->cycles;
    dst->instructions += src->instructions;
    dst->cache_references += src->cache_references;
    dst->cache_misses += src->cache_misses;
    dst->branch_instructions += src->branch_instructions;
    dst->branch_misses += src->branch_misses;
    dst->context_switches += src->context_switches;
    dst->cpu_migrations += src->cpu_migrations;
    dst->page_faults += src->page_faults;
    dst->minor_faults += src->minor_faults;
    dst->major_faults += src->major_faults;
}

static void perf_stats_recompute_derived(perf_stats_t *stats) {
    if (!stats) return;

    stats->ipc = (stats->cycles > 0)
        ? ((double)stats->instructions / (double)stats->cycles)
        : 0.0;
    stats->cache_miss_rate = (stats->cache_references > 0)
        ? ((double)stats->cache_misses / (double)stats->cache_references)
        : 0.0;
    stats->branch_miss_rate = (stats->branch_instructions > 0)
        ? ((double)stats->branch_misses / (double)stats->branch_instructions)
        : 0.0;
    stats->context_switch_ratio = (stats->cycles > 0)
        ? ((double)stats->context_switches / (double)stats->cycles)
        : 0.0;
}

static int header_index(char tokens[][128], int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(tokens[i], name) == 0) return i;
    }
    return -1;
}

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

static double parse_double_or_zero(const char *s) {
    if (!s || !*s) return 0.0;
    return atof(s);
}

static int parse_int_or_zero(const char *s) {
    if (!s || !*s) return 0;
    return atoi(s);
}

static int lookup_baseline_latency(const char *workload, double *baseline_out) {
    FILE *fp = fopen(CSV_FILE, "r");
    if (!fp) return -1;

    char line[4096];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }

    char hdr[4096];
    strncpy(hdr, line, sizeof(hdr) - 1);
    hdr[sizeof(hdr) - 1] = '\0';

    char header_tokens[MAX_TOKENS][128];
    int hcount = split_csv_line(hdr, header_tokens, MAX_TOKENS);

    int idx_workload = header_index(header_tokens, hcount, "workload_type");
    int idx_cores = header_index(header_tokens, hcount, "active_cores");
    int idx_intensity = header_index(header_tokens, hcount, "intensity_pct");
    int idx_unit_latency = header_index(header_tokens, hcount, "unit_latency_us");
    int idx_latency = header_index(header_tokens, hcount, "latency_us");
    int idx_io_cycle = header_index(header_tokens, hcount, "io_cycle_latency_us");

    if (idx_workload < 0 || idx_cores < 0 || idx_intensity < 0) {
        fclose(fp);
        return -1;
    }

    int samples = 0;
    double sum = 0.0;

    while (fgets(line, sizeof(line), fp)) {
        char row[4096];
        strncpy(row, line, sizeof(row) - 1);
        row[sizeof(row) - 1] = '\0';

        char tokens[MAX_TOKENS][128];
        int tcount = split_csv_line(row, tokens, MAX_TOKENS);
        if (tcount <= idx_intensity || tcount <= idx_workload || tcount <= idx_cores) continue;

        if (strcmp(tokens[idx_workload], workload) != 0) continue;
        if (parse_int_or_zero(tokens[idx_cores]) != 1) continue;
        if (parse_int_or_zero(tokens[idx_intensity]) != 25) continue;

        double candidate = 0.0;
        if (idx_unit_latency >= 0 && idx_unit_latency < tcount) {
            candidate = parse_double_or_zero(tokens[idx_unit_latency]);
        }

        if (candidate <= 0.0) {
            if (strcmp(workload, "io") == 0 && idx_io_cycle >= 0 && idx_io_cycle < tcount) {
                candidate = parse_double_or_zero(tokens[idx_io_cycle]);
            } else if (idx_latency >= 0 && idx_latency < tcount) {
                candidate = parse_double_or_zero(tokens[idx_latency]);
            }
        }

        if (candidate > 0.0) {
            sum += candidate;
            samples++;
        }
    }

    fclose(fp);
    if (samples <= 0) return -1;

    *baseline_out = sum / (double)samples;
    return 0;
}

int main(int argc, char *argv[]) {
    char type[32] = "";
    int intensity = -1;
    int duration = -1;
    int warmup = 0;
    int measure = -1;
    int cooldown = 0;
    int repeat = 1;
    int active_cores = 1;
    int perf_enabled = 1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
            strncpy(type, argv[++i], sizeof(type) - 1);
        } else if (strcmp(argv[i], "--intensity") == 0 && i + 1 < argc) {
            intensity = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            warmup = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--measure") == 0 && i + 1 < argc) {
            measure = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cooldown") == 0 && i + 1 < argc) {
            cooldown = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
            repeat = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cores") == 0 && i + 1 < argc) {
            active_cores = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-perf") == 0) {
            perf_enabled = 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!type[0] || intensity < 0 || intensity > 100) {
        print_usage(argv[0]);
        return 1;
    }

    if (measure <= 0) {
        measure = duration;
    }

    if (measure <= 0 || warmup < 0 || cooldown < 0) {
        fprintf(stderr, "Invalid phase durations.\n");
        return 1;
    }

    int total_cores = get_nprocs();
    if (active_cores < 1) active_cores = 1;
    if (active_cores > total_cores) active_cores = total_cores;
    if (active_cores > MAX_CORES) active_cores = MAX_CORES;

    char hostname[128] = "unknown";
    gethostname(hostname, sizeof(hostname) - 1);
    sanitize_csv_field(hostname);

    char kernel_release[128] = "unknown";
    struct utsname uts;
    if (uname(&uts) == 0) {
        strncpy(kernel_release, uts.release, sizeof(kernel_release) - 1);
        kernel_release[sizeof(kernel_release) - 1] = '\0';
        sanitize_csv_field(kernel_release);
    }

    char cpu_model[256] = "unknown";
    if (read_cpu_model(cpu_model, sizeof(cpu_model)) != 0) {
        strncpy(cpu_model, "unknown", sizeof(cpu_model) - 1);
    }

    char cpu_governor[64] = "unknown";
    if (read_first_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",
                        cpu_governor, sizeof(cpu_governor)) != 0) {
        strncpy(cpu_governor, "unknown", sizeof(cpu_governor) - 1);
    }

    char perf_paranoid[32] = "unknown";
    if (read_first_line("/proc/sys/kernel/perf_event_paranoid",
                        perf_paranoid, sizeof(perf_paranoid)) != 0) {
        strncpy(perf_paranoid, "unknown", sizeof(perf_paranoid) - 1);
    }

    char fs_type[64] = "unknown";
    if (run_cmd_first_line("stat -f -c %T .", fs_type, sizeof(fs_type)) != 0) {
        strncpy(fs_type, "unknown", sizeof(fs_type) - 1);
    }

    if (perf_enabled && !perf_is_available()) {
        fprintf(stderr, "[perf unavailable, continuing without perf counters]\n");
        perf_enabled = 0;
    }

    FILE *csv = fopen(CSV_FILE, "a");
    if (!csv) {
        perror("fopen CSV");
        return 1;
    }

    fseek(csv, 0, SEEK_END);
    if (ftell(csv) == 0) {
        fprintf(csv,
            "timestamp,os,hostname,kernel_release,cpu_model,cpu_governor,fs_type,perf_event_paranoid,"
            "total_cores,active_cores,workload_type,intensity_pct,warmup_s,measure_s,cooldown_s,"
            "num_procs,unit_label,unit_latency_us,unit_std_us,latency_us,std_us,"
            "io_cycle_latency_us,io_cycle_std_us,throughput_value,throughput_unit,"
            "slowdown_vs_baseline,raw_ops,raw_total_latency_ns,baseline_latency_us,"
            "perf_cycles,perf_instructions,perf_ipc,perf_cache_miss_rate,"
            "perf_branch_miss_rate,perf_context_switches,perf_cpu_migrations,perf_page_faults,"
            "perf_scope,method,sched_contention\n"
        );
        fflush(csv);
    }

    const char *loader_path = NULL;
    if (access("./load_generator", X_OK) == 0) {
        loader_path = "./load_generator";
    } else if (access("./src/load_generator", X_OK) == 0) {
        loader_path = "./src/load_generator";
    } else {
        fprintf(stderr, "Could not locate load_generator executable.\n");
        fclose(csv);
        return 1;
    }

    for (int run = 1; run <= repeat; ++run) {
        printf("[Run %d/%d] workload=%s intensity=%d%% cores=%d warmup=%ds measure=%ds cooldown=%ds\n",
               run, repeat, type, intensity, active_cores, warmup, measure, cooldown);
        fflush(stdout);

        sched_trace_t *sched_trace = NULL;
        double sched_contention = 0.0;
        if (getuid() == 0 || geteuid() == 0) {
            char trace_name[64];
            snprintf(trace_name, sizeof(trace_name), "%s_run%d", type, run);
            sched_trace = sched_trace_start(trace_name);
            if (sched_trace) {
                fprintf(stderr, "[Scheduling trace started for run %d]\n", run);
            }
        }

        char int_str[16], warmup_str[16], measure_str[16], cooldown_str[16];
        snprintf(int_str, sizeof(int_str), "%d", intensity);
        snprintf(warmup_str, sizeof(warmup_str), "%d", warmup);
        snprintf(measure_str, sizeof(measure_str), "%d", measure);
        snprintf(cooldown_str, sizeof(cooldown_str), "%d", cooldown);

        child_proc_t children[MAX_CORES];
        memset(children, 0, sizeof(children));
        int spawned = 0;

        for (int c = 0; c < active_cores; c++) {
            int core_id = c % total_cores;
            char core_str[16];
            snprintf(core_str, sizeof(core_str), "%d", core_id);

            snprintf(children[c].metrics_path, sizeof(children[c].metrics_path),
                     "/tmp/ssp_metrics_%d_%d_%d.csv", getpid(), run, c);
            children[c].core_id = core_id;

            pid_t pid = fork();
            if (pid == 0) {
                freopen("/dev/null", "w", stdout);
                freopen("/dev/null", "w", stderr);
                    execl(loader_path, loader_path,
                      "--type", type,
                      "--intensity", int_str,
                      "--warmup", warmup_str,
                      "--measure", measure_str,
                      "--cooldown", cooldown_str,
                      "--cpu", core_str,
                      "--metrics-file", children[c].metrics_path,
                      NULL);
                _exit(1);
            } else if (pid > 0) {
                children[c].pid = pid;
                if (perf_enabled) {
                    perf_session_t *sess = perf_session_create(-1, pid);
                    if (sess) {
                        if (perf_add_standard_events(sess) > 0 &&
                            perf_reset_events(sess) == 0 &&
                            perf_enable_events(sess) == 0) {
                            children[c].perf_session = sess;
                        } else {
                            perf_session_destroy(sess);
                        }
                    }
                }
                spawned++;
            } else {
                perror("fork");
            }
        }

        for (int c = 0; c < spawned; c++) {
            waitpid(children[c].pid, NULL, 0);
        }

        if (sched_trace) {
            int sched_events = sched_trace_stop(sched_trace);
            sched_contention = sched_trace_contention_score(sched_trace);
            fprintf(stderr, "[Scheduling trace: %d events, contention=%.3f]\n",
                    sched_events, sched_contention);
            sched_trace_free(sched_trace);
        }

        ssp_metrics_t total_metrics;
        ssp_metrics_init(&total_metrics);

        perf_stats_t perf_total;
        memset(&perf_total, 0, sizeof(perf_total));

        for (int c = 0; c < spawned; c++) {
            ssp_metrics_t child;
            ssp_metrics_init(&child);
            if (read_metrics_file(children[c].metrics_path, &child) == 0) {
                ssp_metrics_merge(&total_metrics, &child);
            }
            unlink(children[c].metrics_path);

            if (children[c].perf_session) {
                perf_disable_events(children[c].perf_session);
                perf_stats_t st;
                if (perf_collect_stats(children[c].perf_session, &st) == 0) {
                    perf_stats_accumulate(&perf_total, &st);
                }
                perf_session_destroy(children[c].perf_session);
                children[c].perf_session = NULL;
            }
        }

        perf_stats_recompute_derived(&perf_total);

        if (total_metrics.total_latency_ns == 0 || total_metrics.operations == 0) {
            fprintf(stderr, "Warning: no measured operations collected for run %d (type=%s)\n", run, type);
            continue;
        }

        long double mean_ns = (long double)total_metrics.total_latency_ns / (long double)total_metrics.operations;
        long double second_moment = total_metrics.sum_latency_sq_ns / (long double)total_metrics.operations;
        long double variance_ns2 = second_moment - mean_ns * mean_ns;
        if (variance_ns2 < 0.0L) variance_ns2 = 0.0L;

        double op_latency_us = (double)(mean_ns / 1000.0L);
        double op_std_us = (double)(sqrt((double)variance_ns2) / 1000.0L);

        double io_cycle_latency_us = 0.0;
        double io_cycle_std_us = 0.0;
        if (total_metrics.io_cycles > 0) {
            long double io_cycle_mean_ns =
                (long double)total_metrics.io_cycle_total_latency_ns / (long double)total_metrics.io_cycles;
            long double io_cycle_second_moment =
                total_metrics.io_cycle_sum_latency_sq_ns / (long double)total_metrics.io_cycles;
            long double io_cycle_variance_ns2 = io_cycle_second_moment - io_cycle_mean_ns * io_cycle_mean_ns;
            if (io_cycle_variance_ns2 < 0.0L) io_cycle_variance_ns2 = 0.0L;

            io_cycle_latency_us = (double)(io_cycle_mean_ns / 1000.0L);
            io_cycle_std_us = (double)(sqrt((double)io_cycle_variance_ns2) / 1000.0L);
        }

        const char *unit_label = "time/op";
        double unit_latency_us = op_latency_us;
        double unit_std_us = op_std_us;

        if (strcmp(type, "memory") == 0) {
            unit_label = "time/access";
        } else if (strcmp(type, "io") == 0) {
            unit_label = "time/cycle";
            unit_latency_us = io_cycle_latency_us;
            unit_std_us = io_cycle_std_us;
        } else if (strcmp(type, "mixed") == 0) {
            unit_label = "time/mixed-op";
        }

        double runtime_s = (double)total_metrics.total_latency_ns / 1e9;
        if (runtime_s <= 0.0) runtime_s = 1e-9;

        double throughput_value = 0.0;
        const char *throughput_unit = "ops/s";
        if (strcmp(type, "cpu") == 0) {
            throughput_value = (double)total_metrics.operations / runtime_s;
            throughput_unit = "ops/s";
        } else if (strcmp(type, "memory") == 0) {
            throughput_value = (double)total_metrics.operations / runtime_s;
            throughput_unit = "access/s";
        } else if (strcmp(type, "io") == 0) {
            throughput_value = ((double)total_metrics.operations / (1024.0 * 1024.0)) / runtime_s;
            throughput_unit = "MB/s";
        } else {
            throughput_value = (double)total_metrics.operations / runtime_s;
            throughput_unit = "mixed-op/s";
        }

        double baseline_latency_us = 0.0;
        int baseline_found = (lookup_baseline_latency(type, &baseline_latency_us) == 0);
        if (!baseline_found && active_cores == 1 && intensity == 25) {
            baseline_latency_us = unit_latency_us;
            baseline_found = 1;
        }

        double slowdown_vs_baseline = 0.0;
        if (baseline_found && baseline_latency_us > 0.0) {
            slowdown_vs_baseline = unit_latency_us / baseline_latency_us;
        }

        time_t now = time(NULL);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));

        fprintf(csv,
            "%s,Linux,%s,%s,%s,%s,%s,%s,"
            "%d,%d,%s,%d,%d,%d,%d,"
            "%d,%s,%.6f,%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,%s,"
            "%.6f,%llu,%llu,%.6f,"
            "%llu,%llu,%.6f,%.6f,"
            "%.6f,%llu,%llu,%llu,"
            "full_run,measured_unit,%.6f\n",
            ts,
            hostname,
            kernel_release,
            cpu_model,
            cpu_governor,
            fs_type,
            perf_paranoid,
            total_cores,
            active_cores,
            type,
            intensity,
            warmup,
            measure,
            cooldown,
            active_cores,
            unit_label,
            unit_latency_us,
            unit_std_us,
            unit_latency_us,
            unit_std_us,
            io_cycle_latency_us,
            io_cycle_std_us,
            throughput_value,
            throughput_unit,
            slowdown_vs_baseline,
            (unsigned long long)total_metrics.operations,
            (unsigned long long)total_metrics.total_latency_ns,
            baseline_latency_us,
            (unsigned long long)perf_total.cycles,
            (unsigned long long)perf_total.instructions,
            perf_total.ipc,
            perf_total.cache_miss_rate,
            perf_total.branch_miss_rate,
            (unsigned long long)perf_total.context_switches,
            (unsigned long long)perf_total.cpu_migrations,
            (unsigned long long)perf_total.page_faults,
            sched_contention
        );
        fflush(csv);
    }

    fclose(csv);
    printf("Results appended to %s\n", CSV_FILE);
    return 0;
}
