#!/usr/bin/env bash
# =============================================================================
# bench_syscalls.sh – System call overhead benchmarks using lmbench lat_syscall
# =============================================================================

LMBENCH_BIN="/usr/lib/lmbench/bin/x86_64-linux-gnu"
RESULTS_DIR="$(dirname "$0")/../results"
mkdir -p "$RESULTS_DIR"

OUT="$RESULTS_DIR/syscalls.txt"
echo "# System Call Overhead Benchmark" > "$OUT"
echo "# Timestamp: $(date --iso-8601=seconds)" >> "$OUT"
echo "# Tool: lat_syscall" >> "$OUT"
echo "" >> "$OUT"

SYSCALLS=(null read write stat fstat open)

echo "[bench_syscalls] Starting syscall benchmarks..."
for sc in "${SYSCALLS[@]}"; do
    echo -n "  [lat_syscall] $sc ... "
    RAW=$("$LMBENCH_BIN/lat_syscall" "$sc" 2>&1)
    LAT=$(echo "$RAW" | grep -oP '[\d.]+(?= microseconds)' | head -1)
    if [[ -z "$LAT" ]]; then
        LAT=$(echo "$RAW" | grep -oP '[\d.]+' | tail -1)
    fi
    echo "syscall=$sc lat=${LAT}us" >> "$OUT"
    echo "done => ${LAT} us"
done

echo ""
echo "[bench_syscalls] Done. Results in $OUT"
