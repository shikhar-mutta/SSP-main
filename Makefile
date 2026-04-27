# =============================================================================
# Makefile — SSP Final Project: Context Switch & Scheduling Analysis
# =============================================================================
#
# Targets:
#   make all        — build all scheduling benchmarks
#   make run        — run ALL experiments end-to-end (takes ~10 min)
#   make phase1     — install lmbench + run baseline (lat_ctx, lat_proc, lat_syscall)
#   make phase2     — core migration penalty (cross-core scheduling cost)
#   make phase3     — scheduler policy comparison (FIFO, RR, normal)
#   make phase4     — process vs thread context switch comparison
#   make plots      — generate all matplotlib plots
#   make numa       — run optional NUMA/local-vs-remote memory placement tests
#   make ebpf       — run optional sched_switch tracing while a benchmark runs
#   make compare    — compare local and cloud CSV results if provided
#   make clean      — remove binaries and results
#   make report     — compile the LaTeX PDF
#
# =============================================================================

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -g
LDFLAGS = -lm
SRCDIR  = src
BINDIR  = bin
RESDIR  = results
PLOTDIR = report/figures

# All benchmark binaries (scheduling focus only)
TARGETS = $(BINDIR)/ctx_switch_bench      \
          $(BINDIR)/core_migration_penalty \
          $(BINDIR)/sched_policy_bench    \
          $(BINDIR)/process_vs_thread

.PHONY: all dirs run phase1 phase2 phase3 phase4 plots numa ebpf compare report clean

# ── Default: build everything ─────────────────────────────────────
all: dirs $(TARGETS)
	@echo ""
	@echo "✅  All benchmarks compiled successfully."
	@echo "    Run 'make run' to execute all experiments."

# ── Create required directories ───────────────────────────────────
dirs:
	@mkdir -p $(BINDIR) $(RESDIR) $(PLOTDIR)

# ── Individual build rules ────────────────────────────────────────
$(BINDIR)/ctx_switch_bench: $(SRCDIR)/ctx_switch_bench.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "  Built: $@"

$(BINDIR)/core_migration_penalty: $(SRCDIR)/core_migration_penalty.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "  Built: $@"

$(BINDIR)/sched_policy_bench: $(SRCDIR)/sched_policy_bench.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "  Built: $@"

$(BINDIR)/process_vs_thread: $(SRCDIR)/process_vs_thread.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lpthread
	@echo "  Built: $@"

# ── Run all phases in sequence ────────────────────────────────────
run: all
	@bash scripts/run_all.sh

# ── Individual phases ─────────────────────────────────────────────
phase1:
	@bash scripts/phase1_lmbench_baseline.sh

phase2: $(BINDIR)/core_migration_penalty
	@echo "=== Phase 2: Core Migration Penalty & Scaling ==="
	$(BINDIR)/core_migration_penalty -o $(RESDIR)/core_migration.csv

phase3: $(BINDIR)/sched_policy_bench
	@echo "=== Phase 3: Scheduler Policy Comparison ==="
	@echo "(Re-run with sudo for RT policies)"
	$(BINDIR)/sched_policy_bench -o $(RESDIR)/sched_policy.csv

phase4: $(BINDIR)/process_vs_thread
	@echo "=== Phase 4: Process vs Thread Context Switch ==="
	$(BINDIR)/process_vs_thread -o $(RESDIR)/proc_vs_thread.csv

# ── Generate plots ────────────────────────────────────────────────
plots:
	@echo "Generating plots..."
	@mkdir -p $(PLOTDIR)
	python3 analysis/plot_all.py
	@echo "✅  Plots saved to $(PLOTDIR)/"

# ── Optional experiments ──────────────────────────────────────────
numa:
	@bash scripts/run_numa_experiments.sh

ebpf:
	@bash scripts/run_ebpf_trace.sh

compare:
	@bash scripts/compare_local_cloud.sh

# ── Compile LaTeX report ──────────────────────────────────────────
report:
	@echo "Compiling LaTeX report..."
	cd report && pdflatex -interaction=nonstopmode main.tex
	cd report && pdflatex -interaction=nonstopmode main.tex
	cd report && pdflatex -interaction=nonstopmode main.tex
	@echo "✅  Report: report/main.pdf"

# ── Clean ─────────────────────────────────────────────────────────
clean:
	rm -rf $(BINDIR) $(RESDIR)/*.csv
	cd report && rm -f *.aux *.bbl *.blg *.log *.out *.toc || true
	@echo "Cleaned build artifacts."
