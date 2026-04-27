#!/usr/bin/env bash
# =============================================================================
# scripts/phase1_lmbench_baseline.sh
# Phase 1 — Install lmbench and run baseline lat_ctx measurements
# =============================================================================
#
# This script:
#   1. Clones and builds lmbench (Intel fork)
#   2. Runs lat_ctx with the exact configurations for our analysis:
#      - Working-set sizes: 0, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 4096 KB
#      - Process counts:    2, 4, 8, 16, 32
#   3. Saves output to results/lmbench_lat_ctx.csv
#
# Why these sizes?
#   They bracket our CPU's cache hierarchy:
#   L1=48KB/core, L2=512KB/core, L3=6MB shared
#
# Author: SSP Final Project, 2026

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESDIR="$ROOT_DIR/results"
LMBENCH_DIR="$ROOT_DIR/lmbench"
LAT_CTX=""

mkdir -p "$RESDIR"

echo "=== Phase 1: lmbench Baseline Setup ==="

# ── Step 1: Clone lmbench if not present ──────────────────────────
if [ ! -d "$LMBENCH_DIR" ]; then
    echo "Cloning lmbench..."
    git clone https://github.com/intel/lmbench.git "$LMBENCH_DIR"
else
    echo "lmbench directory already exists — skipping clone."
fi

# ── Step 2: Build lmbench ─────────────────────────────────────────
echo "Building lmbench..."
cd "$LMBENCH_DIR"

# lmbench's build system is old — this avoids interactive prompts
make OS=x86_64-linux-gnu CFLAGS="-O2" 2>&1 | tail -20

# Find the lat_ctx binary (location varies)
LAT_CTX=$(find "$LMBENCH_DIR/bin" -name "lat_ctx" 2>/dev/null | head -1)

if [ -z "$LAT_CTX" ]; then
    echo "[ERROR] lat_ctx not found after build."
    echo "  Try:  cd lmbench && make"
    exit 1
fi

echo "Found lat_ctx at: $LAT_CTX"

# ── Step 3: Run lat_ctx sweep ─────────────────────────────────────
CSV="$RESDIR/lmbench_lat_ctx.csv"
echo "tool,ws_kb,n_procs,latency_us" > "$CSV"

# Working-set sizes in KB (0 = no working set)
WS_SIZES=(0 4 8 16 32 48 64 128 256 512 1024 2048 4096 8192)

# Process counts
PROC_COUNTS=(2 4 8 16 32)

echo ""
echo "Running lat_ctx sweep (this may take 10–15 minutes)..."
echo "%-8s  %-8s  %-14s" "WS(KB)" "Procs" "Latency(µs)"
echo "-----------------------------------"

for WS in "${WS_SIZES[@]}"; do
    for NP in "${PROC_COUNTS[@]}"; do
        # lat_ctx usage: lat_ctx [-s size_kb] num_procs
        # Output format:  "size N M ... latency\n"  or just "latency" on some builds
        RAW=$("$LAT_CTX" -s "$WS" "$NP" 2>/dev/null || echo "ERROR")

        if [ "$RAW" = "ERROR" ]; then
            echo "  [WARN] lat_ctx failed for WS=$WS NP=$NP"
            continue
        fi

        # Extract the last number (latency in µs) from the output
        LATENCY=$(echo "$RAW" | awk '{print $NF}' | grep -E '^[0-9]+(\.[0-9]+)?$' | tail -1)

        if [ -z "$LATENCY" ]; then
            LATENCY="N/A"
        fi

        echo "  WS=${WS}KB  NP=${NP}  -> ${LATENCY} µs"
        echo "lmbench_lat_ctx,${WS},${NP},${LATENCY}" >> "$CSV"
    done
done

echo ""
echo "✅  lmbench lat_ctx results: $CSV"
echo ""

# ── Step 4: Additional lmbench benchmarks ─────────────────────────
echo "Running lat_proc (process creation latency)..."
LAT_PROC=$(find "$LMBENCH_DIR/bin" -name "lat_proc" 2>/dev/null | head -1)
if [ -n "$LAT_PROC" ]; then
    "$LAT_PROC" fork 2>/dev/null | tee "$RESDIR/lmbench_lat_proc.txt" || true
    "$LAT_PROC" exec 2>/dev/null | tee -a "$RESDIR/lmbench_lat_proc.txt" || true
fi

echo "Running lat_syscall (syscall overhead)..."
LAT_SYS=$(find "$LMBENCH_DIR/bin" -name "lat_syscall" 2>/dev/null | head -1)
if [ -n "$LAT_SYS" ]; then
    for CALL in null read write stat fstat open; do
        "$LAT_SYS" "$CALL" 2>/dev/null | tee -a "$RESDIR/lmbench_lat_syscall.txt" || true
    done
fi

echo ""
echo "✅  Phase 1 complete."
echo "    CSV:  $RESDIR/lmbench_lat_ctx.csv"
echo "    Proc: $RESDIR/lmbench_lat_proc.txt"
echo "    Sys:  $RESDIR/lmbench_lat_syscall.txt"
