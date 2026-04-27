#!/bin/bash
# Exhaustive SSP Benchmark Runner
# Runs all workload types with multiple iterations for stability testing

set -e

echo "=========================================="
echo "SSP Exhaustive Benchmark Suite"
echo "=========================================="

# Configuration
ITERATIONS=3
DURATION=10
INTENSITIES=(25 50 75)
WORKLOADS=(cpu memory io mixed)

# Change to project root
cd "$(dirname "$0")"

# Build all executables
echo ""
echo "[1/4] Building executables..."
make -C src clean > /dev/null 2>&1 || true
make -C src all

# Clear previous benchmark results (keep header)
echo ""
echo "[2/4] Clearing previous benchmark results..."
if [ -f results/workload_benchmark.csv ]; then
    # Keep only header
    head -1 results/workload_benchmark.csv > results/workload_benchmark.csv.tmp
    mv results/workload_benchmark.csv.tmp results/workload_benchmark.csv
fi

# Run benchmarks
echo ""
echo "[3/4] Running exhaustive benchmarks..."
echo ""

total_runs=0
for workload in "${WORKLOADS[@]}"; do
    for intensity in "${INTENSITIES[@]}"; do
        echo ">>> Running: $workload at $intensity% intensity ($ITERATIONS iterations)"
        ./src/benchmark --type "$workload" --intensity "$intensity" --duration "$DURATION" --repeat "$ITERATIONS"
        total_runs=$((total_runs + 1))
        echo ""
    done
done

echo "[4/4] Running analysis..."
echo ""
./src/analysis -a

echo ""
echo "=========================================="
echo "Exhaustive benchmark complete!"
echo "Total runs: $((total_runs * ITERATIONS))"
echo "Results saved to: results/workload_benchmark.csv"
echo "=========================================="