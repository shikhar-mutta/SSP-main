#!/usr/bin/env python3
"""
run_lat_ctx_sweep.py
Runs lat_ctx_fixed for all WS/NP combinations and saves real lmbench_lat_ctx.csv
"""
import subprocess, os, csv, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
BIN  = os.path.join(ROOT, "bin", "lat_ctx_fixed")
CSV  = os.path.join(ROOT, "results", "lmbench_lat_ctx.csv")

WS_LIST = [0, 4, 8, 16, 32, 48, 64, 128, 256, 512, 1024, 2048, 4096, 8192]
NP_LIST = [2, 4, 8, 16, 32]

if not os.path.exists(BIN):
    print("lat_ctx_fixed binary not found. Rebuild with: gcc -O2 -o bin/lat_ctx_fixed src/lat_ctx_fixed.c -lm")
    sys.exit(1)

rows = []
print(f"{'WS(KB)':>8}  {'NProcs':>6}  {'Latency(µs)':>12}")
print("-" * 32)

for np in NP_LIST:
    for ws in WS_LIST:
        try:
            r = subprocess.run(
                [BIN, "-s", str(ws), str(np)],
                capture_output=True, text=True, timeout=20, cwd=ROOT)
            if r.returncode == 0 and r.stdout.strip():
                parts = r.stdout.strip().split()
                lat = parts[-1]
                rows.append((ws, np, lat))
                print(f"{ws:>8}  {np:>6}  {lat:>12}")
            else:
                print(f"{ws:>8}  {np:>6}  FAILED")
        except subprocess.TimeoutExpired:
            print(f"{ws:>8}  {np:>6}  TIMEOUT")
        except Exception as e:
            print(f"{ws:>8}  {np:>6}  ERROR: {e}")

with open(CSV, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["ws", "n_procs", "latency"])
    w.writerows(rows)

print(f"\nSaved {len(rows)} rows -> {CSV}")
