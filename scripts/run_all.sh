#!/usr/bin/env bash
# =============================================================================
# scripts/run_all.sh
# Master runner — executes all phases in sequence and logs everything.
# =============================================================================
#
# USAGE:  bash scripts/run_all.sh
#         (or via: make run)
#
# OUTPUT: results/*.csv     — raw data
#         results/run_log.txt — timestamped full log
#
# Author: SSP Final Project, 2026

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BINDIR="$ROOT_DIR/bin"
RESDIR="$ROOT_DIR/results"
LOG="$RESDIR/run_log.txt"

mkdir -p "$RESDIR"

log() { echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG"; }

log "============================================="
log "  SSP Project — Context Switch & Scheduling"
log "  Experiment Suite (Focused)"
log "  Host   : $(hostname)"
log "  Kernel : $(uname -r)"
log "  CPUs   : $(nproc)"
log "  Date   : $(date)"
log "============================================="

# Phase 1 — lmbench lat_ctx baseline
log ""
log "=== PHASE 1: lmbench lat_ctx Baseline ==="
bash "$SCRIPT_DIR/phase1_lmbench_baseline.sh" 2>&1 | tee -a "$LOG"

# Phase 2 — Core migration + scaling
log ""
log "=== PHASE 2: Core Migration Penalty & Scaling ==="
"$BINDIR/core_migration_penalty" -o "$RESDIR/core_migration.csv" \
    2>&1 | tee -a "$LOG"

# Phase 3 — Scheduler policy (no sudo — RT policies skipped gracefully)
log ""
log "=== PHASE 3: Scheduler Policy Comparison ==="
log "  (RT policies require sudo; skipped if not root)"
"$BINDIR/sched_policy_bench" -o "$RESDIR/sched_policy.csv" \
    2>&1 | tee -a "$LOG"

# Phase 4 — Process vs Thread
log ""
log "=== PHASE 4: Process vs Thread Context Switch ==="
"$BINDIR/process_vs_thread" -o "$RESDIR/proc_vs_thread.csv" \
    2>&1 | tee -a "$LOG"

log ""
log "=== ALL PHASES COMPLETE ==="
log "Results in: $RESDIR/"
log "Now run:  python3 analysis/plot_all.py"
