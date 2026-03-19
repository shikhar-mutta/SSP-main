#!/usr/bin/env python3
"""
SSP Benchmark Results Analyser & Plotter
=========================================
Reads the CSV produced by benchmark.py and generates publication-quality
plots for the report.

Generated figures
-----------------
  1. latency_vs_procs.png      – Context-switch latency vs process count,
                                  one curve per workload type (data_size=0 KB,
                                  cores=max available)
  2. latency_vs_intensity.png  – Latency vs workload intensity for each
                                  workload type (procs=8, data_size=0 KB)
  3. core_scaling_heatmap.png  – Heatmap: rows=active cores, cols=proc count,
                                  colour=latency (baseline workload, ds=0 KB)
  4. workload_comparison.png   – Grouped bar chart: workload types × core
                                  counts at fixed procs=8, ds=0 KB
  5. scaling_summary.png       – 2×2 composite panel summarising all
                                  dimensions

Usage
-----
  python3 plot_results.py --input ../results/workload_benchmark.csv
  python3 plot_results.py --demo          # generate synthetic data & plot

Dependencies
------------
  pip install matplotlib pandas numpy scipy
"""

import argparse
import math
import os
import random
import sys
from pathlib import Path
from typing import List, Tuple, Optional

# ── Import guard ──────────────────────────────────────────────────────────────
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.ticker as mticker
    import numpy as np
    import pandas as pd
except ImportError as e:
    print(f"✗  Missing dependency: {e}")
    print("   Run: pip install matplotlib pandas numpy")
    sys.exit(1)

# ── Project paths ─────────────────────────────────────────────────────────────
SCRIPT_DIR  = Path(__file__).parent.resolve()
PROJECT_DIR = SCRIPT_DIR.parent
PLOTS_DIR   = PROJECT_DIR / "plots"

# ── Plot style ────────────────────────────────────────────────────────────────
PALETTE = {
    "baseline": "#4e8098",
    "cpu":      "#e07a5f",
    "io":       "#f2cc8f",
    "memory":   "#81b29a",
    "mixed":    "#9d6fb5",
}
MARKERS  = {"baseline": "o", "cpu": "s", "io": "^", "memory": "D", "mixed": "P"}
LINESTYL = {"baseline": "-", "cpu": "--", "io": "-.", "memory": ":", "mixed": "-"}

plt.rcParams.update({
    "figure.dpi":        150,
    "font.family":       "DejaVu Sans",
    "font.size":         10,
    "axes.spines.top":   False,
    "axes.spines.right": False,
    "axes.grid":         True,
    "grid.alpha":        0.35,
    "axes.titlesize":    11,
    "axes.labelsize":    10,
    "legend.fontsize":   9,
    "legend.framealpha": 0.8,
})


# ═══════════════════════════════════════════════════════════════════════════════
# Synthetic Data  (--demo mode)
# ═══════════════════════════════════════════════════════════════════════════════
def _gauss(mu: float, sigma: float) -> float:
    return max(0.1, random.gauss(mu, sigma))

def generate_demo_data() -> pd.DataFrame:
    """
    Generates realistic-looking synthetic benchmark data based on published
    lmbench numbers for a modern x86-64 quad-core processor.

    Baseline ctx latency: ~3–5 μs, rising with proc count and data size.
    CPU load adds ~1–4 μs; memory load adds ~2–8 μs; IO adds ~0.5–2 μs.
    More active cores → lower latency (better parallelism, less queuing).
    """
    random.seed(42)
    rows = []
    proc_counts  = [2, 4, 8, 16, 32, 64]
    intensities  = [25, 50, 75, 100]
    data_sizes   = [0, 16, 64]
    core_configs = [1, 2, 4, 8]
    workloads    = ["baseline", "cpu", "io", "memory", "mixed"]

    BASE_LAT  = {0: 3.1, 16: 4.5, 64: 7.8}    # μs @ 2 procs, baseline
    PROC_COEF = 0.07                            # μs per extra proc
    CORE_RED  = {1: 1.45, 2: 1.20, 4: 1.0, 8: 0.82}  # reduction factor

    WL_OFFSET = {  # extra latency added by each workload type
        "baseline": lambda i: 0,
        "cpu":      lambda i: 0.8 + 3.2 * (i / 100) ** 1.2,
        "io":       lambda i: 0.3 + 1.4 * (i / 100),
        "memory":   lambda i: 1.5 + 6.0 * (i / 100) ** 1.5,
        "mixed":    lambda i: 2.0 + 7.5 * (i / 100) ** 1.4,
    }

    for n_cores in core_configs:
        for wl in workloads:
            iter_int = [0] if wl == "baseline" else intensities
            for intensity in iter_int:
                for ds in data_sizes:
                    base = BASE_LAT[ds]
                    for np_ in proc_counts:
                        mu = (base
                              + PROC_COEF * (np_ - 2)
                              + WL_OFFSET[wl](intensity)) * CORE_RED[n_cores]
                        lat = _gauss(mu, mu * 0.06)
                        std = _gauss(mu * 0.04, mu * 0.01)
                        rows.append({
                            "os":            "Linux",
                            "hostname":      "demo-host",
                            "total_cores":   8,
                            "active_cores":  n_cores,
                            "workload_type": wl,
                            "intensity_pct": intensity,
                            "data_size_kb":  ds,
                            "num_procs":     np_,
                            "latency_us":    round(lat, 4),
                            "std_us":        round(std, 4),
                            "method":        "lmbench",
                        })
    return pd.DataFrame(rows)


# ═══════════════════════════════════════════════════════════════════════════════
# Helpers
# ═══════════════════════════════════════════════════════════════════════════════
def _save(fig: plt.Figure, name: str, outdir: Path):
    outdir.mkdir(parents=True, exist_ok=True)
    p = outdir / name
    fig.savefig(p, bbox_inches="tight")
    plt.close(fig)
    print(f"  ✔  {p}")


def _col_present(df: pd.DataFrame, col: str, val) -> bool:
    return col in df.columns and val in df[col].values


def _best_core_count(df: pd.DataFrame) -> int:
    """Return the largest active_cores value present in the data."""
    return int(df["active_cores"].max())


# ═══════════════════════════════════════════════════════════════════════════════
# Plot 1 – Latency vs Process Count
# ═══════════════════════════════════════════════════════════════════════════════
def plot_latency_vs_procs(df: pd.DataFrame, outdir: Path, ds_kb: int = 0):
    """
    One subplot per data_size; every workload type is a separate line.
    Fixed: active_cores = max available.
    """
    n_cores  = _best_core_count(df)
    ds_vals  = sorted(df["data_size_kb"].unique())
    n_panels = len(ds_vals)

    fig, axes = plt.subplots(1, n_panels, figsize=(5 * n_panels, 4.5), sharey=False)
    if n_panels == 1:
        axes = [axes]

    fig.suptitle(
        f"Context-Switch Latency vs Process Count\n"
        f"(active cores = {n_cores})",
        fontsize=12, fontweight="bold", y=1.02,
    )

    workloads = sorted(df["workload_type"].unique())

    for ax, ds in zip(axes, ds_vals):
        for wl in workloads:
            sub = df[
                (df["active_cores"] == n_cores)
                & (df["data_size_kb"] == ds)
                & (df["workload_type"] == wl)
            ]
            if sub.empty:
                continue
            # Aggregate over intensity (take max to show worst case)
            grp = sub.groupby("num_procs")["latency_us"].mean().reset_index()
            grp_std = sub.groupby("num_procs")["std_us"].mean().reset_index()

            x = grp["num_procs"].values
            y = grp["latency_us"].values
            e = grp_std["std_us"].values

            ax.errorbar(
                x, y, yerr=e,
                label=wl.capitalize(),
                color=PALETTE.get(wl, "grey"),
                marker=MARKERS.get(wl, "o"),
                linestyle=LINESTYL.get(wl, "-"),
                linewidth=1.8,
                markersize=5,
                capsize=3,
                elinewidth=1,
            )

        ax.set_title(f"Data size = {ds} KB")
        ax.set_xlabel("Number of Processes")
        ax.set_ylabel("Latency (μs)")
        ax.legend()
        ax.set_xscale("log", base=2)
        ax.xaxis.set_major_formatter(mticker.ScalarFormatter())

    fig.tight_layout()
    _save(fig, "latency_vs_procs.png", outdir)


# ═══════════════════════════════════════════════════════════════════════════════
# Plot 2 – Latency vs Workload Intensity
# ═══════════════════════════════════════════════════════════════════════════════
def plot_latency_vs_intensity(df: pd.DataFrame, outdir: Path,
                               fixed_procs: int = 8, fixed_ds: int = 0):
    """
    Latency vs intensity for each non-baseline workload type.
    Adds a dashed horizontal line for the baseline (no load) reference.
    """
    n_cores = _best_core_count(df)

    baseline_row = df[
        (df["active_cores"] == n_cores)
        & (df["workload_type"] == "baseline")
        & (df["data_size_kb"] == fixed_ds)
        & (df["num_procs"] == fixed_procs)
    ]
    baseline_us = float(baseline_row["latency_us"].mean()) if not baseline_row.empty else None

    non_base = [w for w in sorted(df["workload_type"].unique()) if w != "baseline"]

    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.set_title(
        f"Context-Switch Latency vs Workload Intensity\n"
        f"(processes={fixed_procs}, data={fixed_ds} KB, cores={n_cores})",
        fontsize=11, fontweight="bold",
    )

    for wl in non_base:
        sub = df[
            (df["active_cores"] == n_cores)
            & (df["workload_type"] == wl)
            & (df["data_size_kb"] == fixed_ds)
            & (df["num_procs"] == fixed_procs)
        ]
        if sub.empty:
            continue
        grp = sub.groupby("intensity_pct").agg(
            lat=("latency_us", "mean"), std=("std_us", "mean")
        ).reset_index()

        ax.errorbar(
            grp["intensity_pct"], grp["lat"], yerr=grp["std"],
            label=wl.capitalize(),
            color=PALETTE.get(wl, "grey"),
            marker=MARKERS.get(wl, "o"),
            linestyle=LINESTYL.get(wl, "-"),
            linewidth=1.8, markersize=6, capsize=3, elinewidth=1,
        )

    if baseline_us is not None:
        ax.axhline(baseline_us, linestyle="--", color=PALETTE["baseline"],
                   label=f"Baseline ({baseline_us:.2f} μs)", linewidth=1.5)

    ax.set_xlabel("Workload Intensity (%)")
    ax.set_ylabel("Latency (μs)")
    ax.legend()
    ax.set_xlim(0, 105)
    fig.tight_layout()
    _save(fig, "latency_vs_intensity.png", outdir)


# ═══════════════════════════════════════════════════════════════════════════════
# Plot 3 – Core Scaling Heatmap
# ═══════════════════════════════════════════════════════════════════════════════
def plot_core_scaling_heatmap(df: pd.DataFrame, outdir: Path,
                               workload: str = "baseline",
                               intensity: int = 0, ds: int = 0):
    """
    2-D heatmap: rows = active core count, columns = num_procs.
    Colour encodes mean context-switch latency.
    """
    sub = df[
        (df["workload_type"] == workload)
        & (df["data_size_kb"] == ds)
    ]
    if workload != "baseline":
        sub = sub[sub["intensity_pct"] == intensity]

    if sub.empty:
        print(f"  [heatmap] No data for workload={workload}. Skipping.")
        return

    pivot = sub.pivot_table(
        values="latency_us", index="active_cores", columns="num_procs", aggfunc="mean"
    )

    fig, ax = plt.subplots(figsize=(9, 4))
    im = ax.imshow(pivot.values, aspect="auto", cmap="YlOrRd", origin="lower")

    ax.set_xticks(range(len(pivot.columns)))
    ax.set_xticklabels(pivot.columns)
    ax.set_yticks(range(len(pivot.index)))
    ax.set_yticklabels(pivot.index)
    ax.set_xlabel("Number of Processes")
    ax.set_ylabel("Active Cores")
    ax.set_title(
        f"Context-Switch Latency Heatmap (μs)\n"
        f"Workload = {workload.capitalize()}, Data size = {ds} KB",
        fontsize=11, fontweight="bold",
    )
    ax.grid(False)

    # Annotate cells
    for r in range(pivot.shape[0]):
        for c in range(pivot.shape[1]):
            v = pivot.values[r, c]
            if not math.isnan(v):
                ax.text(c, r, f"{v:.1f}", ha="center", va="center",
                        fontsize=8,
                        color="white" if v > pivot.values.max() * 0.65 else "black")

    cbar = fig.colorbar(im, ax=ax, shrink=0.85)
    cbar.set_label("Latency (μs)")
    fig.tight_layout()
    _save(fig, "core_scaling_heatmap.png", outdir)


# ═══════════════════════════════════════════════════════════════════════════════
# Plot 4 – Workload Comparison Bar Chart
# ═══════════════════════════════════════════════════════════════════════════════
def plot_workload_comparison(df: pd.DataFrame, outdir: Path,
                              fixed_procs: int = 8, fixed_ds: int = 0):
    """
    Grouped bar chart: x-axis = active core count, groups = workload types.
    Each bar shows mean latency; error bar = std.
    For non-baseline workloads the maximum intensity level is used to show
    the worst-case impact.
    """
    core_configs = sorted(df["active_cores"].unique())
    workloads    = sorted(df["workload_type"].unique())

    fig, ax = plt.subplots(figsize=(max(7, len(core_configs) * 2.5), 5))
    ax.set_title(
        f"Workload Impact on Context-Switch Latency\n"
        f"(processes={fixed_procs}, data={fixed_ds} KB)",
        fontsize=11, fontweight="bold",
    )

    n_wl   = len(workloads)
    width  = 0.75 / n_wl
    x_pos  = np.arange(len(core_configs))

    for i, wl in enumerate(workloads):
        intens = 0 if wl == "baseline" else int(df[df["workload_type"] == wl]["intensity_pct"].max())
        means, stds = [], []
        for nc in core_configs:
            sub = df[
                (df["active_cores"] == nc)
                & (df["workload_type"] == wl)
                & (df["data_size_kb"] == fixed_ds)
                & (df["num_procs"] == fixed_procs)
                & (df["intensity_pct"] == intens)
            ]
            means.append(float(sub["latency_us"].mean()) if not sub.empty else 0)
            stds.append(float(sub["std_us"].mean())  if not sub.empty else 0)

        bars = ax.bar(
            x_pos + i * width - (n_wl - 1) * width / 2,
            means, width,
            yerr=stds,
            label=wl.capitalize() + (f" ({intens}%)" if wl != "baseline" else ""),
            color=PALETTE.get(wl, "grey"),
            capsize=3, ecolor="black", error_kw={"elinewidth": 1},
            alpha=0.85, edgecolor="white",
        )

    ax.set_xticks(x_pos)
    ax.set_xticklabels([f"{c} Core{'s' if c > 1 else ''}" for c in core_configs])
    ax.set_ylabel("Latency (μs)")
    ax.legend(loc="upper right", ncol=2)
    fig.tight_layout()
    _save(fig, "workload_comparison.png", outdir)


# ═══════════════════════════════════════════════════════════════════════════════
# Plot 5 – Scaling Summary (2×2 composite)
# ═══════════════════════════════════════════════════════════════════════════════
def plot_scaling_summary(df: pd.DataFrame, outdir: Path):
    """
    2×2 panel combining:
      [0,0] Latency vs procs (baseline, varying data sizes)
      [0,1] Latency vs procs (fixed ds=0, varying cores)
      [1,0] Latency vs intensity (cpu vs memory, fixed procs/ds)
      [1,1] Bar: cores × workloads at procs=8, peak intensity
    """
    n_cores = _best_core_count(df)
    fig, axes = plt.subplots(2, 2, figsize=(13, 9))
    fig.suptitle("Context-Switch Benchmark: Scaling Analysis Summary",
                 fontsize=13, fontweight="bold")

    # ── [0,0] Baseline: proc count × data size ───────────────────────────────
    ax = axes[0, 0]
    ax.set_title("Baseline: Latency vs Processes (vary data size)")
    for ds, color in zip(sorted(df["data_size_kb"].unique()), ["#4e8098", "#e07a5f", "#81b29a"]):
        sub = df[
            (df["active_cores"] == n_cores)
            & (df["workload_type"] == "baseline")
            & (df["data_size_kb"] == ds)
        ].groupby("num_procs")["latency_us"].mean()
        ax.plot(sub.index, sub.values, marker="o", linewidth=1.8,
                label=f"{ds} KB", color=color)
    ax.set_xlabel("Number of Processes")
    ax.set_ylabel("Latency (μs)")
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_formatter(mticker.ScalarFormatter())
    ax.legend(title="Data size")

    # ── [0,1] Core count scaling (baseline, ds=0) ────────────────────────────
    ax = axes[0, 1]
    ax.set_title("Core Scaling: Latency vs Processes (vary cores, ds=0 KB)")
    colors = plt.cm.plasma(np.linspace(0.2, 0.85, len(df["active_cores"].unique())))
    for nc, col in zip(sorted(df["active_cores"].unique()), colors):
        sub = df[
            (df["active_cores"] == nc)
            & (df["workload_type"] == "baseline")
            & (df["data_size_kb"] == 0)
        ].groupby("num_procs")["latency_us"].mean()
        ax.plot(sub.index, sub.values, marker="D", linewidth=1.8,
                label=f"{nc} core{'s' if nc > 1 else ''}", color=col)
    ax.set_xlabel("Number of Processes")
    ax.set_ylabel("Latency (μs)")
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_formatter(mticker.ScalarFormatter())
    ax.legend(title="Active cores")

    # ── [1,0] Workload intensity (cpu vs memory, ds=0) ──────────────────────
    ax = axes[1, 0]
    ax.set_title("Workload Intensity: CPU vs Memory (procs=8, ds=0 KB)")
    baseline_val = df[
        (df["active_cores"] == n_cores) & (df["workload_type"] == "baseline")
        & (df["data_size_kb"] == 0) & (df["num_procs"] == 8)
    ]["latency_us"].mean()
    if not math.isnan(baseline_val):
        ax.axhline(baseline_val, linestyle="--", color=PALETTE["baseline"],
                   label=f"Baseline ({baseline_val:.2f} μs)", linewidth=1.4)
    for wl in ["cpu", "memory", "mixed"]:
        sub = df[
            (df["active_cores"] == n_cores)
            & (df["workload_type"] == wl)
            & (df["data_size_kb"] == 0)
            & (df["num_procs"] == 8)
        ].groupby("intensity_pct")["latency_us"].mean()
        if not sub.empty:
            ax.plot(sub.index, sub.values, marker="s", linewidth=1.8,
                    label=wl.capitalize(), color=PALETTE.get(wl, "grey"))
    ax.set_xlabel("Workload Intensity (%)")
    ax.set_ylabel("Latency (μs)")
    ax.legend()

    # ── [1,1] Workload comparison bar (procs=8, ds=0) ───────────────────────
    ax = axes[1, 1]
    ax.set_title("Workload Comparison at Peak Intensity (procs=8, ds=0 KB)")
    workloads    = sorted(df["workload_type"].unique())
    core_configs = sorted(df["active_cores"].unique())
    n_wl  = len(workloads)
    width = 0.70 / n_wl
    x     = np.arange(len(core_configs))

    for i, wl in enumerate(workloads):
        inten = 0 if wl == "baseline" else int(
            df[df["workload_type"] == wl]["intensity_pct"].max()
        )
        ys = []
        for nc in core_configs:
            sub = df[
                (df["active_cores"] == nc) & (df["workload_type"] == wl)
                & (df["data_size_kb"] == 0) & (df["num_procs"] == 8)
                & (df["intensity_pct"] == inten)
            ]
            ys.append(float(sub["latency_us"].mean()) if not sub.empty else 0)
        ax.bar(x + i * width - (n_wl - 1) * width / 2, ys, width,
               label=wl.capitalize(), color=PALETTE.get(wl, "grey"),
               alpha=0.85, edgecolor="white")

    ax.set_xticks(x)
    ax.set_xticklabels([f"{c}C" for c in core_configs])
    ax.set_xlabel("Active Cores")
    ax.set_ylabel("Latency (μs)")
    ax.legend(ncol=2, fontsize=8)

    fig.tight_layout(rect=[0, 0, 1, 0.96])
    _save(fig, "scaling_summary.png", outdir)


# ═══════════════════════════════════════════════════════════════════════════════
# Entry Point
# ═══════════════════════════════════════════════════════════════════════════════
def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Analyse and plot SSP benchmark CSV results",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument(
        "--input", "-i",
        default=str(PROJECT_DIR / "results" / "workload_benchmark.csv"),
        help="Path to benchmark CSV (default: ../results/workload_benchmark.csv)",
    )
    p.add_argument(
        "--output-dir", "-o",
        default=str(PLOTS_DIR),
        help="Directory to write plots (default: ../plots/)",
    )
    p.add_argument(
        "--demo", action="store_true",
        help="Generate synthetic data and plot without running the benchmark",
    )
    p.add_argument(
        "--fixed-procs", type=int, default=8,
        help="Fixed process count for intensity / comparison plots (default: 8)",
    )
    p.add_argument(
        "--fixed-ds", type=int, default=0,
        help="Fixed data size (KB) for intensity / comparison plots (default: 0)",
    )
    return p.parse_args()


def main():
    args   = parse_args()
    outdir = Path(args.output_dir)

    if args.demo:
        print("  [demo] Generating synthetic benchmark data …")
        df = generate_demo_data()
    else:
        csv_path = Path(args.input)
        if not csv_path.exists():
            print(f"✗  CSV not found: {csv_path}")
            print("   Run benchmark first: python3 benchmark.py")
            print("   Or use --demo for a demonstration with synthetic data.")
            sys.exit(1)
        df = pd.read_csv(csv_path)
        print(f"  Loaded {len(df)} rows from {csv_path}")

    # Drop failed measurements
    df = df[df["latency_us"] > 0].copy()
    if df.empty:
        print("✗  No valid measurements found in CSV.")
        sys.exit(1)

    # Find a fixed_procs value that exists in the data
    available_procs = sorted(df["num_procs"].unique())
    fixed_procs = args.fixed_procs
    if fixed_procs not in available_procs:
        fixed_procs = available_procs[len(available_procs) // 2]
        print(f"  ⚠  procs={args.fixed_procs} not in data; using {fixed_procs}")

    print(f"\n  Generating plots → {outdir}/\n")

    plot_latency_vs_procs(df, outdir)
    plot_latency_vs_intensity(df, outdir, fixed_procs=fixed_procs, fixed_ds=args.fixed_ds)
    plot_core_scaling_heatmap(df, outdir, workload="baseline", ds=args.fixed_ds)
    plot_workload_comparison(df, outdir, fixed_procs=fixed_procs, fixed_ds=args.fixed_ds)
    plot_scaling_summary(df, outdir)

    print(f"\n  ✔  All plots saved to: {outdir}/")


if __name__ == "__main__":
    main()
