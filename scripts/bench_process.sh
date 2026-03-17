#!/usr/bin/env bash
# =============================================================================
# bench_process.sh – Process creation benchmarks using lmbench lat_proc
# =============================================================================

LMBENCH_BIN="/usr/lib/lmbench/bin/x86_64-linux-gnu"
RESULTS_DIR="$(dirname "$0")/../results"
mkdir -p "$RESULTS_DIR"

OUT="$RESULTS_DIR/process.txt"
echo "# Process Creation Benchmark" > "$OUT"
echo "# Timestamp: $(date --iso-8601=seconds)" >> "$OUT"
echo "# Tool: lat_proc" >> "$OUT"
echo "" >> "$OUT"

PROC_TESTS=(fork exec shell)

echo "[bench_process] Starting process creation benchmarks..."
for pt in "${PROC_TESTS[@]}"; do
    echo -n "  [lat_proc] $pt ... "
    RAW=$("$LMBENCH_BIN/lat_proc" "$pt" 2>&1)
    LAT=$(echo "$RAW" | grep -oP '[\d.]+(?= microseconds)' | head -1)
    if [[ -z "$LAT" ]]; then
        LAT=$(echo "$RAW" | grep -oP '[\d.]+' | tail -1)
    fi
    echo "proc=$pt lat=${LAT}us" >> "$OUT"
    echo "done => ${LAT} us"
done

echo ""
echo "[bench_process] Done. Results in $OUT"
