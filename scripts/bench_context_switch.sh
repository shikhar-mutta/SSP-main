#!/usr/bin/env bash
# =============================================================================
# bench_context_switch.sh
# Benchmarks context-switch latency and scheduling overhead using lmbench lat_ctx
# Tests varying process counts and data sizes for scaling analysis
# =============================================================================

LMBENCH_BIN="/usr/lib/lmbench/bin/x86_64-linux-gnu"
RESULTS_DIR="$(dirname "$0")/../results"
mkdir -p "$RESULTS_DIR"

OUT="$RESULTS_DIR/context_switch.txt"
echo "# Context Switch & Scheduling Benchmark" > "$OUT"
echo "# Timestamp: $(date --iso-8601=seconds)" >> "$OUT"
echo "# Tool: lat_ctx" >> "$OUT"
echo "# Format: processes datasize_KB  latency_us" >> "$OUT"
echo "" >> "$OUT"

DATA_SIZES=(0 16 64)
PROC_COUNTS=(2 4 8 16 32 64 96)

echo "[bench_context_switch] Starting context-switch benchmark..."

for ds in "${DATA_SIZES[@]}"; do
    echo "=== Data Size: ${ds} KB ===" >> "$OUT"
    echo "[bench_context_switch] Data size = ${ds} KB"
    for np in "${PROC_COUNTS[@]}"; do
        echo -n "  [lat_ctx] procs=$np, data=${ds}KB ... "
        # lat_ctx runs 5 iterations and reports median
        RAW=$("$LMBENCH_BIN/lat_ctx" -s "$ds" "$np" 2>&1)
        # Extract numeric latency (lat_ctx prints: "Context switching - times in microseconds - smaller is better")
        # Line looks like: "2 8.18" or similar
        LAT=$(echo "$RAW" | grep -oP '\d+ \K[\d.]+' | tail -1)
        if [[ -z "$LAT" ]]; then
            LAT=$(echo "$RAW" | grep -oP '[\d.]+' | tail -1)
        fi
        echo "procs=$np ds=${ds}KB lat=${LAT}us" >> "$OUT"
        echo "done => ${LAT} us"
    done
    echo "" >> "$OUT"
done

echo ""
echo "[bench_context_switch] Done. Results in $OUT"
