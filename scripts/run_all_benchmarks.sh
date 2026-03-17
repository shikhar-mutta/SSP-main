#!/usr/bin/env bash
# =============================================================================
# run_all_benchmarks.sh
# Master script: runs all lmbench benchmarks and collects system info
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/../results"
mkdir -p "$RESULTS_DIR"

LMBENCH_BIN="/usr/lib/lmbench/bin/x86_64-linux-gnu"

echo "========================================================"
echo "  SSP lmbench Benchmark Suite"
echo "  $(date)"
echo "========================================================"

# ── System Information ──────────────────────────────────────
SYS_OUT="$RESULTS_DIR/sysinfo.txt"
echo "# System Information" > "$SYS_OUT"
echo "# Collected: $(date --iso-8601=seconds)" >> "$SYS_OUT"
echo "" >> "$SYS_OUT"

echo "=== uname ===" >> "$SYS_OUT"
uname -a >> "$SYS_OUT"

echo "" >> "$SYS_OUT"
echo "=== lscpu ===" >> "$SYS_OUT"
lscpu >> "$SYS_OUT"

echo "" >> "$SYS_OUT"
echo "=== /proc/meminfo ===" >> "$SYS_OUT"
cat /proc/meminfo >> "$SYS_OUT"

echo "" >> "$SYS_OUT"
echo "=== /proc/cpuinfo (summary) ===" >> "$SYS_OUT"
grep -E "model name|cpu MHz|cache size|cpu cores|siblings|stepping|vendor" \
     /proc/cpuinfo | sort -u >> "$SYS_OUT"

echo "" >> "$SYS_OUT"
echo "=== Kernel scheduler ===" >> "$SYS_OUT"
cat /proc/sys/kernel/sched_latency_ns      2>/dev/null && echo "" || true
grep "" /proc/sys/kernel/sched_*           2>/dev/null >> "$SYS_OUT" || true
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null \
    && echo "" >> "$SYS_OUT" || true
echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo 'unknown')" >> "$SYS_OUT"

echo "[run_all] System info collected → $SYS_OUT"

# ── Run Individual Benchmarks ────────────────────────────────
chmod +x "$SCRIPT_DIR"/*.sh

echo ""
echo "────────────────────────────────────────────────────────"
echo " (1/5) Context Switch & Scheduling"
echo "────────────────────────────────────────────────────────"
bash "$SCRIPT_DIR/bench_context_switch.sh"

echo ""
echo "────────────────────────────────────────────────────────"
echo " (2/5) System Call Overheads"
echo "────────────────────────────────────────────────────────"
bash "$SCRIPT_DIR/bench_syscalls.sh"

echo ""
echo "────────────────────────────────────────────────────────"
echo " (3/5) Signal Handling"
echo "────────────────────────────────────────────────────────"
bash "$SCRIPT_DIR/bench_signals.sh"

echo ""
echo "────────────────────────────────────────────────────────"
echo " (4/5) Process Creation"
echo "────────────────────────────────────────────────────────"
bash "$SCRIPT_DIR/bench_process.sh"

echo ""
echo "────────────────────────────────────────────────────────"
echo " (5/5) Filesystem Operations"
echo "────────────────────────────────────────────────────────"
bash "$SCRIPT_DIR/bench_filesystem.sh"

# ── Memory Bandwidth (for microarchitecture reference) ───────
echo ""
echo "────────────────────────────────────────────────────────"
echo " (+) Memory Latency (lat_mem_rd, 512MB stride=128)"
echo "────────────────────────────────────────────────────────"
MEM_OUT="$RESULTS_DIR/mem_latency.txt"
echo "# Memory Latency (lat_mem_rd)" > "$MEM_OUT"
echo "# Timestamp: $(date --iso-8601=seconds)" >> "$MEM_OUT"
echo "" >> "$MEM_OUT"
"$LMBENCH_BIN/lat_mem_rd" -t -P 1 512 128 2>&1 | tee -a "$MEM_OUT"
echo "[run_all] Memory latency → $MEM_OUT"

echo ""
echo "========================================================"
echo " All benchmarks complete!"
echo " Results directory: $RESULTS_DIR"
echo "========================================================"
