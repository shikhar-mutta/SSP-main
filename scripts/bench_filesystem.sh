#!/usr/bin/env bash
# =============================================================================
# bench_filesystem.sh – Filesystem create/delete benchmarks using lmbench lat_fs
# =============================================================================

LMBENCH_BIN="/usr/lib/lmbench/bin/x86_64-linux-gnu"
RESULTS_DIR="$(dirname "$0")/../results"
mkdir -p "$RESULTS_DIR"

OUT="$RESULTS_DIR/filesystem.txt"
echo "# Filesystem Create/Delete Benchmark" > "$OUT"
echo "# Timestamp: $(date --iso-8601=seconds)" >> "$OUT"
echo "# Tool: lat_fs" >> "$OUT"
echo "" >> "$OUT"

FILE_SIZES=(0 1 4 10)

echo "[bench_filesystem] Starting filesystem benchmarks..."
for fs_size in "${FILE_SIZES[@]}"; do
    echo -n "  [lat_fs] size=${fs_size}KB ... "
    RAW=$("$LMBENCH_BIN/lat_fs" "$fs_size" 2>&1)
    # lat_fs prints: "File system latency: Xus create, Yus delete"
    CREATE=$(echo "$RAW" | grep -oP '[\d.]+(?= us create)' | head -1)
    DELETE=$(echo "$RAW" | grep -oP '[\d.]+(?= us delete)' | head -1)
    if [[ -z "$CREATE" ]]; then
        CREATE=$(echo "$RAW" | grep -oP '[\d.]+' | sed -n '1p')
        DELETE=$(echo "$RAW" | grep -oP '[\d.]+' | sed -n '2p')
    fi
    echo "size=${fs_size}KB create=${CREATE}us delete=${DELETE}us" >> "$OUT"
    echo "done => create=${CREATE}us delete=${DELETE}us"
done

echo ""
echo "[bench_filesystem] Done. Results in $OUT"
