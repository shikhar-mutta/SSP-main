#!/usr/bin/env bash
# =============================================================================
# scripts/compare_local_cloud.sh
# Compare a local CSV against a cloud CSV export, if both are present.
# =============================================================================

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESDIR="$ROOT_DIR/results"
LOCAL_CSV="$RESDIR/local_results.csv"
CLOUD_CSV="$RESDIR/cloud_results.csv"
OUT="$RESDIR/local_vs_cloud_summary.csv"

if [ ! -f "$LOCAL_CSV" ] || [ ! -f "$CLOUD_CSV" ]; then
    cat <<EOF
[WARN] Missing comparison inputs.
Place CSV exports at:
  $LOCAL_CSV
  $CLOUD_CSV
Then rerun this target.
EOF
    exit 0
fi

python3 - "$RESDIR" <<'PY'
from pathlib import Path
import csv
import statistics as stats
import sys

root = Path(sys.argv[1])
local_csv = root / "local_results.csv"
cloud_csv = root / "cloud_results.csv"
out_csv = root / "local_vs_cloud_summary.csv"

def load(path):
    with path.open(newline="") as f:
        rows = list(csv.DictReader(f))
    data = {}
    for row in rows:
        key = row.get("scenario") or row.get("switch_type") or row.get("policy") or "unknown"
        val = row.get("mean_us")
        if val is None:
            continue
        try:
            data.setdefault(key, []).append(float(val))
        except ValueError:
            continue
    return data

local = load(local_csv)
cloud = load(cloud_csv)

with out_csv.open("w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["key", "local_mean_us", "cloud_mean_us", "delta_us", "delta_pct"])
    for key in sorted(set(local) | set(cloud)):
        l = stats.mean(local.get(key, [float('nan')]))
        c = stats.mean(cloud.get(key, [float('nan')]))
        if l != l or c != c:
            continue
        delta = c - l
        pct = (delta / l) * 100 if l else float('nan')
        w.writerow([key, f"{l:.4f}", f"{c:.4f}", f"{delta:.4f}", f"{pct:.2f}"])

print(f"Wrote {out_csv}")
PY

echo "✅  Comparison summary written to $OUT"
