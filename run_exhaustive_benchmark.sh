#!/bin/bash
# Exhaustive SSP Benchmark Runner
# Runs all workload types × intensities × core counts with multiple iterations.

set -e

echo "=========================================="
echo "SSP Exhaustive Benchmark Suite"
echo "=========================================="

# ── Configuration ──────────────────────────────────────────────────────────
ITERATIONS=3
DURATION=10
INTENSITIES=(25 50 75)
WORKLOADS=(cpu memory io mixed)

# Determine available cores; build a list of core counts up to the hardware max
MAX_CORES=$(nproc)
ALL_CORES=(1 2 4 8)
CORES=()
for c in "${ALL_CORES[@]}"; do
    if [ "$c" -le "$MAX_CORES" ]; then
        CORES+=("$c")
    fi
done
# Always have at least 1
[ ${#CORES[@]} -eq 0 ] && CORES=(1)

echo "Hardware cores available : $MAX_CORES"
echo "Core counts to benchmark : ${CORES[*]}"
echo "Workloads                : ${WORKLOADS[*]}"
echo "Intensities              : ${INTENSITIES[*]}"
echo "Iterations per cell      : $ITERATIONS"
echo "Duration per run (s)     : $DURATION"

# Change to project root
cd "$(dirname "$0")"

# ── 1. Build ────────────────────────────────────────────────────────────────
echo ""
echo "[1/5] Building executables..."
make -C src clean > /dev/null 2>&1 || true
make -C src all

# ── 2. Clear previous results (keep header) ─────────────────────────────────
echo ""
echo "[2/5] Clearing previous benchmark results..."
if [ -f results/workload_benchmark.csv ]; then
    head -1 results/workload_benchmark.csv > results/workload_benchmark.csv.tmp
    mv results/workload_benchmark.csv.tmp results/workload_benchmark.csv
fi

# ── 3. Run benchmarks ────────────────────────────────────────────────────────
echo ""
echo "[3/5] Running exhaustive benchmarks..."
echo ""

total_cells=0
for cores in "${CORES[@]}"; do
    for workload in "${WORKLOADS[@]}"; do
        for intensity in "${INTENSITIES[@]}"; do
            echo ">>> cores=$cores  workload=$workload  intensity=$intensity%  ($ITERATIONS iterations)"
            ./src/benchmark \
                --type      "$workload"  \
                --intensity "$intensity" \
                --duration  "$DURATION"  \
                --repeat    "$ITERATIONS" \
                --cores     "$cores"
            total_cells=$((total_cells + 1))
            echo ""
        done
    done
done

# ── 4. Analysis ──────────────────────────────────────────────────────────────
echo "[4/5] Running statistical analysis..."
./src/analysis -a
echo ""

# ── 5. Generate graphs ───────────────────────────────────────────────────────
echo "[5/5] Generating visualisation graphs..."
python3 scripts/generate_graphs.py

echo ""
echo "=========================================="
echo "Exhaustive benchmark complete!"
echo "Total measurement cells : $total_cells"
echo "Total runs              : $((total_cells * ITERATIONS))"
echo "Results CSV             : results/workload_benchmark.csv"
echo "Graphs                  : results/*.png"
echo "=========================================="
