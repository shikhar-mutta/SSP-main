#!/usr/bin/env bash
# =============================================================================
# scripts/run_ebpf_trace.sh
# Optional eBPF/bpftrace scheduling trace for the SSP project.
# =============================================================================

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINDIR="$ROOT_DIR/bin"
RESDIR="$ROOT_DIR/results"
OUT="$RESDIR/ebpf_sched_switch.txt"

mkdir -p "$RESDIR"

if ! command -v bpftrace >/dev/null 2>&1; then
    echo "[WARN] bpftrace not found; skipping eBPF trace."
    exit 0
fi

if [ ! -x "$BINDIR/process_vs_thread" ]; then
    echo "[ERROR] $BINDIR/process_vs_thread not found. Run 'make all' first."
    exit 1
fi

TRACE_SCRIPT='tracepoint:sched:sched_switch { @[comm] = count(); }'

echo "[INFO] Capturing sched_switch counts while process_vs_thread runs."
if command -v timeout >/dev/null 2>&1; then
    timeout 15s sudo bpftrace -e "$TRACE_SCRIPT" > "$OUT" 2>&1 &
    BPF_PID=$!
    sleep 2
    "$BINDIR/process_vs_thread" >/dev/null 2>&1 || true
    wait "$BPF_PID" || true
else
    sudo bpftrace -e "$TRACE_SCRIPT" > "$OUT" 2>&1 &
    BPF_PID=$!
    sleep 2
    "$BINDIR/process_vs_thread" >/dev/null 2>&1 || true
    kill "$BPF_PID" >/dev/null 2>&1 || true
fi

echo "✅  eBPF trace written to $OUT"
