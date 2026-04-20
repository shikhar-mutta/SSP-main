# SSP Benchmark Suite - Complete Implementation
# Makefile for building C programs with microarchitecture analysis

CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -D_GNU_SOURCE -std=c99
LDFLAGS = -pthread -lm
SRCDIR = .
OBJDIR = obj

# Object files (common library)
COMMON_OBJS = $(OBJDIR)/ssp_lib.o

# All targets (Phase 1, 2, 3)
TARGETS = load_generator benchmark analysis

# Load generator dependencies (modular architecture)
LOAD_GEN_OBJS = $(OBJDIR)/load_cpu.o \
                $(OBJDIR)/load_io.o \
                $(OBJDIR)/load_memory.o \
                $(OBJDIR)/load_mixed.o \
                $(OBJDIR)/load_generator.o

# Load worker modules (for use in other programs)
LOAD_WORKER_OBJS = $(OBJDIR)/load_cpu.o \
                   $(OBJDIR)/load_io.o \
                   $(OBJDIR)/load_memory.o \
                   $(OBJDIR)/load_mixed.o

# Benchmark orchestrator dependencies (note: NOT load_generator.o to avoid main conflicts)
BENCH_OBJS = $(OBJDIR)/benchmark.o \
             $(OBJDIR)/perf_events.o \
             $(OBJDIR)/microarch.o \
             $(LOAD_WORKER_OBJS)

# Analysis tool dependencies
ANALYSIS_OBJS = $(OBJDIR)/analysis.o \
                $(OBJDIR)/microarch.o

all: $(TARGETS)
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════════════╗"
	@echo "║  ✓ SSP Benchmark Suite - All programs built successfully!          ║"
	@echo "╚════════════════════════════════════════════════════════════════════╝"
	@echo ""

# Create object directory
$(OBJDIR):
	@mkdir -p $(OBJDIR)

# Compile common library
$(OBJDIR)/ssp_lib.o: ssp_lib.c ssp_lib.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Load Generator Modules
$(OBJDIR)/load_cpu.o: load_cpu.c ssp_lib.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/load_io.o: load_io.c ssp_lib.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/load_memory.o: load_memory.c ssp_lib.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/load_mixed.o: load_mixed.c ssp_lib.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/load_generator.o: load_generator.c ssp_lib.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Perf Events Collection
$(OBJDIR)/perf_events.o: perf_events.c perf_events.h ssp_lib.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Microarchitecture Analysis
$(OBJDIR)/microarch.o: microarch.c microarch.h perf_events.h ssp_lib.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Main Programs
$(OBJDIR)/benchmark.o: benchmark.c ssp_lib.h perf_events.h microarch.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/analysis.o: analysis.c ssp_lib.h microarch.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link load generator executable
load_generator: $(LOAD_GEN_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "  ✓ Built load_generator"

# Link benchmark executable (uses worker modules but NOT load_generator.o)
benchmark: $(BENCH_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "  ✓ Built benchmark"

# Link analysis executable
analysis: $(ANALYSIS_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "  ✓ Built analysis"

# Clean targets
clean:
	rm -rf $(OBJDIR) $(TARGETS)
	@echo "✓ Cleaned all build artifacts"

# Display help
help:
	@echo ""
	@echo "SSP Benchmark Suite - Makefile"
	@echo "=============================="
	@echo ""
	@echo "Targets:"
	@echo "  all           - Build all programs (default)"
	@echo "  load_generator - Build only load generator"
	@echo "  benchmark      - Build only benchmark orchestrator"
	@echo "  analysis       - Build only analysis tool"
	@echo "  clean          - Remove built files"
	@echo "  help           - Show this help"
	@echo ""

.PHONY: all clean help load_generator benchmark analysis
