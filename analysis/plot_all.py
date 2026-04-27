#!/usr/bin/env python3
"""
analysis/plot_all.py
====================
Master plotting script — reads all CSV results and generates publication-quality
matplotlib figures for the IEEE LaTeX report.

Focus: Context Switching & Scheduling in Multicore Systems

Figures produced:
  fig1_lmbench_scaling.pdf        — lmbench lat_ctx: processes vs latency
  fig2_lmbench_ws.pdf             — lmbench lat_ctx: working-set vs latency
  fig3_core_migration.pdf         — core migration penalty bar chart (renumbered from fig5)
  fig4_scaling.pdf                — process-count scaling (free scheduling, renumbered from fig6)
  fig5_sched_policy.pdf           — scheduler policy comparison (renumbered from fig7)
  fig6_proc_vs_thread.pdf         — process vs thread latency comparison (renumbered from fig8)

All figures are saved to:  report/figures/

Requirements:  pip install matplotlib numpy pandas scipy

Author: SSP Final Project, 2026
"""

import os
import sys
import warnings
import numpy  as np
import pandas as pd
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from   matplotlib.ticker import AutoMinorLocator
from   scipy.stats       import sem as scipy_sem

warnings.filterwarnings("ignore")

# ── Global style ──────────────────────────────────────────────────
matplotlib.rcParams.update({
    "font.family"     : "serif",
    "font.serif"      : ["Times New Roman", "DejaVu Serif"],
    "font.size"       : 10,
    "axes.titlesize"  : 11,
    "axes.labelsize"  : 10,
    "xtick.labelsize" : 9,
    "ytick.labelsize" : 9,
    "legend.fontsize" : 9,
    "figure.dpi"      : 150,
    "savefig.dpi"     : 300,
    "savefig.bbox"    : "tight",
    "axes.grid"       : True,
    "grid.alpha"      : 0.35,
    "grid.linestyle"  : "--",
})

# ── Paths ─────────────────────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR   = os.path.dirname(SCRIPT_DIR)
RESDIR     = os.path.join(ROOT_DIR, "results")
FIGDIR     = os.path.join(ROOT_DIR, "report", "figures")
os.makedirs(FIGDIR, exist_ok=True)

def savefig(fig, name):
    path = os.path.join(FIGDIR, name)
    fig.savefig(path)
    print(f"  Saved: {path}")
    plt.close(fig)

def load_csv(filename):
    path = os.path.join(RESDIR, filename)
    if not os.path.exists(path):
        print(f"  [WARN] Missing: {path}  (skipping this figure)")
        return None
    df = pd.read_csv(path)
    # Remove duplicate header rows (rows where first column matches header)
    if len(df) > 0 and len(df.columns) > 0:
        first_col = df.columns[0]
        # Filter out rows where the first column value equals the header
        df = df[df[first_col].astype(str) != first_col].copy()
    return df

# ── Cache boundary annotations (Intel i5-1035G1) ──────────────────
L1_KB = 48
L2_KB = 512
L3_KB = 6144

# ==========================================================================
# FIG 1 — lmbench lat_ctx: Number of Processes vs Latency
# Shows scheduler overhead growth as process count increases.
# ==========================================================================
def fig1_lmbench_scaling():
    df = load_csv("lmbench_lat_ctx.csv")
    if df is None: return
    df.columns = df.columns.str.strip()
    df = df[df["latency"] != "N/A"].copy()
    df["latency"] = pd.to_numeric(df["latency"], errors="coerce")
    df = df.dropna(subset=["latency"])

    # ws=0 only — isolate the scheduling overhead
    d = df[df["ws"] == 0].groupby("n_procs")["latency"].agg(["mean","std"]).reset_index()

    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    ax.errorbar(d["n_procs"], d["mean"], yerr=d["std"],
                fmt="-o", color="#1f77b4", capsize=4, linewidth=2,
                markersize=6, label="lmbench lat_ctx (WS=0)")
    ax.set_xlabel("Number of Processes")
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 1: Scheduler Scaling — Latency vs Process Count")
    ax.xaxis.set_minor_locator(AutoMinorLocator())
    ax.legend()
    fig.tight_layout()
    savefig(fig, "fig1_lmbench_scaling.pdf")

# ==========================================================================
# FIG 2 — lmbench lat_ctx: Working-Set Size vs Latency (2 processes)
# ==========================================================================
def fig2_lmbench_ws():
    df = load_csv("lmbench_lat_ctx.csv")
    if df is None: return
    df.columns = df.columns.str.strip()
    df = df[df["latency"] != "N/A"].copy()
    df["latency"] = pd.to_numeric(df["latency"], errors="coerce")
    df = df.dropna(subset=["latency"])

    d = df[df["n_procs"] == 2].groupby("ws")["latency"].mean().reset_index()

    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    ax.plot(d["ws"], d["latency"], "-s", color="#d62728",
            linewidth=2, markersize=5, label="2 processes")

    # Cache boundary vertical lines
    for kb, label, color in [(L1_KB,"L1 (48KB)","#2ca02c"),
                              (L2_KB,"L2 (512KB)","#ff7f0e"),
                              (L3_KB,"L3 (6MB)","#9467bd")]:
        ax.axvline(x=kb, linestyle=":", color=color, linewidth=1.5, label=label)

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Working-Set Size (KB)  [log₂ scale]")
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 2: Working-Set Effect on Context-Switch Latency (lmbench)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    savefig(fig, "fig2_lmbench_ws.pdf")

# ==========================================================================
# FIG 5 — Core Migration Penalty (bar chart: same-core vs diff-core vs free)
# ==========================================================================
def fig5_core_migration():
    df = load_csv("core_migration.csv")
    if df is None: return

    # Extract the 3 fixed scenarios (non-scaling rows)
    fixed = df[df["scenario"].isin(
        ["same_core_ht", "diff_core", "free_sched"])].copy()

    # The CSV can contain repeated exports of the same rows; collapse them so
    # the figure shows one entry per scenario instead of many overlapping bars.
    fixed["mean_us"] = pd.to_numeric(fixed["mean_us"], errors="coerce")
    fixed["p99_us"] = pd.to_numeric(fixed["p99_us"], errors="coerce")
    fixed["stddev_us"] = pd.to_numeric(fixed["stddev_us"], errors="coerce")
    fixed = (
        fixed.dropna(subset=["mean_us", "p99_us", "stddev_us"])
             .groupby("scenario", as_index=False)[["mean_us", "p99_us", "stddev_us"]]
             .mean()
    )

    labels  = {"same_core_ht": "Same Core\n(HT siblings)",
               "diff_core"   : "Diff Core\n(cross-L2)",
               "free_sched"  : "Free\n(no pin)"}
    colors  = ["#2ca02c", "#d62728", "#ff7f0e"]

    names   = [labels[s] for s in fixed["scenario"]]
    means   = fixed["mean_us"].values
    p99s    = fixed["p99_us"].values
    stds    = fixed["stddev_us"].values

    x = np.arange(len(names))
    w = 0.35

    fig, ax = plt.subplots(figsize=(5.5, 3.8))
    b1 = ax.bar(x - w/2, means, w, label="Mean", color=colors, alpha=0.85,
                yerr=stds, capsize=5, error_kw={"elinewidth":1.5})
    b2 = ax.bar(x + w/2, p99s,  w, label="p99",  color=colors, alpha=0.45,
                edgecolor="black", linewidth=0.8)

    ax.set_xticks(x)
    ax.set_xticklabels(names)
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 3: Core Migration Penalty — Same vs Cross Core")
    ax.legend(["Mean ± σ", "p99"])

    # Annotate bars
    for bar in b1:
        ax.text(bar.get_x()+bar.get_width()/2, bar.get_height()+0.1,
                f"{bar.get_height():.2f}", ha="center", va="bottom", fontsize=8)

    fig.tight_layout()
    savefig(fig, "fig3_core_migration.pdf")

# ==========================================================================
# FIG 6 — Process-count scaling (free scheduling)
# ==========================================================================
def fig6_scaling():
    df = load_csv("core_migration.csv")
    if df is None: return

    d = df[df["scenario"] == "scaling_free"].copy()
    d["n_procs"] = pd.to_numeric(d["n_procs"], errors="coerce")
    d["mean_us"] = pd.to_numeric(d["mean_us"], errors="coerce")
    d["p99_us"] = pd.to_numeric(d["p99_us"], errors="coerce")
    d["stddev_us"] = pd.to_numeric(d["stddev_us"], errors="coerce")
    d = d.dropna(subset=["n_procs", "mean_us", "p99_us", "stddev_us"])
    d = d.groupby("n_procs", as_index=False)[["mean_us", "p99_us", "stddev_us"]].mean()
    d = d.sort_values("n_procs")

    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    ax.errorbar(d["n_procs"], d["mean_us"], yerr=d["stddev_us"],
                fmt="-o", color="#1f77b4", capsize=4, linewidth=2, markersize=6,
                label="Mean ± σ")
    ax.plot(d["n_procs"], d["p99_us"], "--s", color="#d62728",
            linewidth=1.5, markersize=5, label="p99")

    # Mark logical CPU count as a reference
    ncpu = os.cpu_count() or 8
    ax.axvline(x=ncpu, color="gray", linestyle=":", linewidth=1.5,
               label=f"#vCPUs ({ncpu})")

    ax.set_xlabel("Number of Processes")
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 4: Scaling Analysis — Latency vs Process Count")
    ax.legend()
    ax.xaxis.set_minor_locator(AutoMinorLocator())
    fig.tight_layout()
    savefig(fig, "fig4_scaling.pdf")

# ==========================================================================
# FIG 7 — Scheduler Policy Comparison
# ==========================================================================
def fig7_sched_policy():
    df = load_csv("sched_policy.csv")
    if df is None: return
    df = df[df["mean_us"] != "N/A"].copy()
    df["mean_us"]   = pd.to_numeric(df["mean_us"],   errors="coerce")
    df["p99_us"]    = pd.to_numeric(df["p99_us"],    errors="coerce")
    df["stddev_us"] = pd.to_numeric(df["stddev_us"], errors="coerce")
    df = df.dropna(subset=["mean_us"])

    policy_colors = {
        "SCHED_OTHER (CFS)": "#1f77b4",
        "SCHED_BATCH"      : "#ff7f0e",
        "SCHED_IDLE"       : "#aec7e8",
        "SCHED_FIFO  (RT)" : "#d62728",
        "SCHED_RR    (RT)" : "#2ca02c",
    }

    x      = np.arange(len(df))
    colors = [policy_colors.get(p, "#999999") for p in df["policy"]]
    w      = 0.4

    fig, ax = plt.subplots(figsize=(6, 3.8))
    bars = ax.bar(x, df["mean_us"], w, color=colors, alpha=0.85,
                  yerr=df["stddev_us"], capsize=5, error_kw={"elinewidth":1.5},
                  label="Mean ± σ")
    ax.plot(x, df["p99_us"], "D", color="black", markersize=6,
            label="p99", zorder=5)

    ax.set_xticks(x)
    ax.set_xticklabels(df["policy"], rotation=15, ha="right", fontsize=8)
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 5: Scheduler Policy Comparison")
    ax.legend()

    for bar in bars:
        ax.text(bar.get_x()+bar.get_width()/2, bar.get_height()+0.1,
                f"{bar.get_height():.2f}", ha="center", va="bottom", fontsize=7.5)

    fig.tight_layout()
    savefig(fig, "fig5_sched_policy.pdf")

# ==========================================================================
# FIG 8 — Process vs Thread Switch
# ==========================================================================
def fig8_proc_vs_thread():
    df = load_csv("proc_vs_thread.csv")
    if df is None: return
    df["mean_us"]   = pd.to_numeric(df["mean_us"],   errors="coerce")
    df["p99_us"]    = pd.to_numeric(df["p99_us"],    errors="coerce")
    df["stddev_us"] = pd.to_numeric(df["stddev_us"], errors="coerce")

    labels = df["switch_type"].str.capitalize().tolist()
    means  = df["mean_us"].values
    p99s   = df["p99_us"].values
    stds   = df["stddev_us"].values

    x = np.arange(len(labels))
    w = 0.35
    colors = ["#d62728", "#2ca02c"]

    fig, ax = plt.subplots(figsize=(4.5, 3.5))
    b1 = ax.bar(x - w/2, means, w, label="Mean ± σ", color=colors, alpha=0.85,
                yerr=stds, capsize=6, error_kw={"elinewidth":1.8})
    ax.bar(x + w/2, p99s, w, label="p99",
           color=colors, alpha=0.4, edgecolor="black")

    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=10)
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 6: Process vs Thread Context Switch")
    ax.legend()

    if len(means) == 2 and means[1] > 0:
        speedup = means[0] / means[1]
        ax.annotate(f"Thread is\n{speedup:.2f}× faster",
                    xy=(0.5, max(means)*0.7), xycoords="data",
                    ha="center", fontsize=9, color="#555555",
                    bbox=dict(boxstyle="round,pad=0.3", fc="lightyellow",
                              ec="gray", alpha=0.8))

    fig.tight_layout()
    savefig(fig, "fig6_proc_vs_thread.pdf")

# ==========================================================================
# Main
# ==========================================================================
if __name__ == "__main__":
    print("=" * 60)
    print("  Generating figures for SSP Project Report")
    print(f"  Results dir : {RESDIR}")
    print(f"  Output dir  : {FIGDIR}")
    print("=" * 60)

    fig1_lmbench_scaling()
    fig2_lmbench_ws()
    fig5_core_migration()
    fig6_scaling()
    fig7_sched_policy()
    fig8_proc_vs_thread()

    print("\n✅  All figures generated.")
    print("    Now run:  make report")
