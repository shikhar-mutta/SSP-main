#!/usr/bin/env bash
# =============================================================================
# bench_signals.sh – Signal handling latency benchmarks using lmbench lat_sig
# =============================================================================

LMBENCH_BIN="/usr/lib/lmbench/bin/x86_64-linux-gnu"
RESULTS_DIR="$(dirname "$0")/../results"
mkdir -p "$RESULTS_DIR"

OUT="$RESULTS_DIR/signals.txt"
echo "# Signal Handling Benchmark" > "$OUT"
echo "# Timestamp: $(date --iso-8601=seconds)" >> "$OUT"
echo "# Tool: lat_sig" >> "$OUT"
echo "" >> "$OUT"

SIG_TESTS=(install catch prot)

echo "[bench_signals] Starting signal benchmarks..."
for st in "${SIG_TESTS[@]}"; do
    echo -n "  [lat_sig] $st ... "
    RAW=$("$LMBENCH_BIN/lat_sig" "$st" 2>&1)
    LAT=$(echo "$RAW" | grep -oP '[\d.]+(?= microseconds)' | head -1)
    if [[ -z "$LAT" ]]; then
        LAT=$(echo "$RAW" | grep -oP '[\d.]+' | tail -1)
    fi
    echo "sig=$st lat=${LAT}us" >> "$OUT"
    echo "done => ${LAT} us"
done

echo ""
echo "[bench_signals] Done. Results in $OUT"
