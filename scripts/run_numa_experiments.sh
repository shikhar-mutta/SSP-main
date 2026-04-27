#!/usr/bin/env bash
# =============================================================================
# scripts/run_numa_experiments.sh
# Optional NUMA/local-vs-remote placement experiments for the SSP project.
# =============================================================================

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINDIR="$ROOT_DIR/bin"
RESDIR="$ROOT_DIR/results"
OUT="$RESDIR/numa_compare.csv"

mkdir -p "$RESDIR"

if ! command -v numactl >/dev/null 2>&1; then
    echo "[WARN] numactl not found; skipping NUMA comparison."
    exit 0
fi

if [ ! -x "$BINDIR/ctx_switch_bench" ]; then
    echo "[ERROR] $BINDIR/ctx_switch_bench not found. Run 'make all' first."
    exit 1
fi

NUMAINFO="$(numactl --hardware 2>/dev/null || true)"
NODE_COUNT="$(printf '%s\n' "$NUMAINFO" | grep -c '^node [0-9]')"

if [ "$NODE_COUNT" -lt 2 ]; then
    echo "[WARN] Fewer than 2 NUMA nodes detected; nothing to compare."
    exit 0
fi

echo "scenario,n_procs,mean_us,p50_us,p99_us,stddev_us" > "$OUT"

run_case() {
    local scenario="$1"
    shift
    local raw_csv="$RESDIR/${scenario}_raw.csv"
    "$@" -o "$raw_csv" >/dev/null

    python3 - "$scenario" "$raw_csv" "$OUT" <<'PY'
import csv
import statistics as stats
import sys
from pathlib import Path

scenario = sys.argv[1]
raw_csv = Path(sys.argv[2])
out_csv = Path(sys.argv[3])

with raw_csv.open(newline="") as f:
    rows = list(csv.DictReader(f))

latencies = []
for row in rows:
    try:
        latencies.append(float(row["latency_us"]))
    except (KeyError, TypeError, ValueError):
        continue

if not latencies:
    raise SystemExit(f"no numeric latency samples found in {raw_csv}")

latencies.sort()
n = len(latencies)

def percentile(values, p):
    if len(values) == 1:
        return values[0]
    idx = (len(values) - 1) * p
    lo = int(idx)
    hi = min(lo + 1, len(values) - 1)
    frac = idx - lo
    return values[lo] * (1 - frac) + values[hi] * frac

mean_us = stats.mean(latencies)
p50_us = percentile(latencies, 0.50)
p99_us = percentile(latencies, 0.99)
stddev_us = stats.pstdev(latencies)

with out_csv.open("a", newline="") as f:
    writer = csv.writer(f)
    writer.writerow([scenario, n, f"{mean_us:.4f}", f"{p50_us:.4f}", f"{p99_us:.4f}", f"{stddev_us:.4f}"])
PY
}

echo "[INFO] Running NUMA-local case (node 0 CPU+memory)."
run_case "numa_local" numactl --cpunodebind=0 --membind=0 "$BINDIR/ctx_switch_bench" -n 8 -s 4096 -i 5000 -c 0

echo "[INFO] Running NUMA-remote case (node 0 CPU, node 1 memory)."
run_case "numa_remote" numactl --cpunodebind=0 --membind=1 "$BINDIR/ctx_switch_bench" -n 8 -s 4096 -i 5000 -c 0

echo "✅  NUMA comparison written to $OUT"
