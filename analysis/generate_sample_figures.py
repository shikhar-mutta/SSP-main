#!/usr/bin/env python3
"""
analysis/generate_sample_figures.py
=====================================
Generates SAMPLE placeholder figures using synthetic data that matches
the expected shape of real results.

Use this to:
  1. Compile the LaTeX report BEFORE running the full experiments
  2. Verify the report layout and figure positioning
  3. Replace with real data later by running:  python3 analysis/plot_all.py

Author: SSP Final Project, 2026
"""

import os
import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.ticker import AutoMinorLocator

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

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR   = os.path.dirname(SCRIPT_DIR)
FIGDIR     = os.path.join(ROOT_DIR, "report", "figures")
os.makedirs(FIGDIR, exist_ok=True)

# Cache sizes for i5-1035G1
L1_KB, L2_KB, L3_KB = 48, 512, 6144

def save(fig, name):
    path = os.path.join(FIGDIR, name)
    fig.savefig(path)
    print(f"  Saved: {path}")
    plt.close(fig)

# ── Fig 1: lmbench scaling ───────────────────────────────────────
def fig1():
    procs = np.array([2, 4, 8, 16, 32])
    # Realistic values: ~3.2µs at 2 procs, growing super-linearly
    mean  = np.array([3.2, 3.8, 4.6, 6.1, 8.7])
    std   = np.array([0.4, 0.5, 0.6, 0.9, 1.3])

    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    ax.errorbar(procs, mean, yerr=std, fmt="-o", color="#1f77b4",
                capsize=4, linewidth=2, markersize=6,
                label="lmbench lat_ctx (WS=0)")
    ax.set_xlabel("Number of Processes")
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 1: Scheduler Scaling — Latency vs Process Count")
    ax.xaxis.set_minor_locator(AutoMinorLocator())
    ax.legend()
    ax.annotate("O(log N) CFS overhead", xy=(16, 6.1),
                xytext=(10, 7.5), arrowprops=dict(arrowstyle="->", color="gray"),
                fontsize=8, color="gray")
    fig.tight_layout()
    save(fig, "fig1_lmbench_scaling.pdf")

# ── Fig 2: lmbench working-set effect ───────────────────────────
def fig2():
    ws = np.array([0, 4, 8, 16, 32, 48, 64, 128, 256, 512,
                   1024, 2048, 4096, 8192])
    # Staircase shape: flat in L1, rises at L1→L2, again at L2→L3
    lat = np.array([3.2, 3.3, 3.3, 3.4, 3.5, 3.6,  # < L1
                    4.2, 5.0, 5.6, 5.9,              # L1→L2
                    8.1, 11.2, 18.4, 22.1])          # L2→DRAM

    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    ax.plot(ws, lat, "-s", color="#d62728", linewidth=2,
            markersize=5, label="2 processes")

    for kb, label, color in [(L1_KB, "L1 (48KB)", "#2ca02c"),
                              (L2_KB, "L2 (512KB)", "#ff7f0e"),
                              (L3_KB, "L3 (6MB)", "#9467bd")]:
        ax.axvline(x=kb, linestyle=":", color=color, linewidth=1.5, label=label)

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Working-Set Size (KB)  [log₂ scale]")
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 2: Working-Set Effect on Context-Switch Latency (lmbench)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    save(fig, "fig2_lmbench_ws.pdf")

# ── Fig 3: Cache pollution model ────────────────────────────────
def fig3():
    ws   = np.array([0, 4, 8, 16, 32, 48, 64, 96, 128, 192, 256,
                     384, 512, 768, 1024, 2048, 3072, 4096, 5120,
                     6144, 8192, 12288, 16384])
    mean = np.array([2.8, 2.9, 3.0, 3.1, 3.3, 3.6,
                     4.1, 4.6, 5.0, 5.3, 5.6,
                     5.8, 6.0, 7.2, 8.1, 9.0, 9.4, 11.2, 14.8,
                     18.3, 20.1, 21.8, 22.4])
    std  = mean * 0.08
    p99  = mean + 2.0 * std + np.linspace(0, 8, len(mean))

    fig, ax = plt.subplots(figsize=(6, 3.8))

    regions = [(0, L1_KB, "#d1e8d1", "L1-resident"),
               (L1_KB, L2_KB, "#fde8c8", "L2-resident"),
               (L2_KB, L3_KB, "#fdd0d0", "L3-resident"),
               (L3_KB, 20000, "#e0d0f0", "DRAM")]
    for x0, x1, c, lbl in regions:
        ax.axvspan(x0, x1, alpha=0.25, color=c, label=lbl)

    ax.plot(ws, mean, "-o", color="#1f77b4", linewidth=2,
            markersize=4, label="Mean latency", zorder=5)
    ax.fill_between(ws, mean - std, mean + std, alpha=0.2,
                    color="#1f77b4", label="±1 σ")
    ax.plot(ws, p99, "--", color="#d62728", linewidth=1.2,
            label="p99 latency", zorder=4)

    for kb, lbl in [(L1_KB, "L1"), (L2_KB, "L2"), (L3_KB, "L3")]:
        ax.axvline(x=kb, color="black", linewidth=0.8, linestyle="--")
        ax.text(kb * 1.05, 23, lbl, fontsize=8, va="top")

    ax.set_xscale("log", base=2)
    ax.set_xlabel("Working-Set Size (KB)  [log₂ scale]")
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 3: Cache Pollution Model — Latency vs Working-Set Size")
    ax.legend(fontsize=7.5, loc="upper left")
    fig.tight_layout()
    save(fig, "fig3_cache_pollution.pdf")

# ── Fig 4: Box plot by cache region ─────────────────────────────
def fig4():
    np.random.seed(42)
    groups = [
        np.random.normal(2.8, 0.2, 30),   # no WS
        np.random.normal(3.4, 0.3, 60),   # L1
        np.random.normal(5.5, 0.6, 50),   # L2
        np.random.normal(9.0, 1.2, 40),   # L3
        np.random.normal(21.0, 5.0, 30),  # DRAM
    ]
    labels = ["No WS", "L1\n(≤48KB)", "L2\n(≤512KB)", "L3\n(≤6MB)", "DRAM\n(>6MB)"]
    colors = ["#aec7e8", "#d1e8d1", "#fde8c8", "#fdd0d0", "#e0d0f0"]

    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    bp = ax.boxplot(groups, patch_artist=True, notch=False)
    for patch, color in zip(bp["boxes"], colors):
        patch.set_facecolor(color)
    ax.set_xticklabels(labels)
    ax.set_xlabel("Cache Region")
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 4: Latency Distribution by Cache Residency Region")
    fig.tight_layout()
    save(fig, "fig4_cache_regions.pdf")

# ── Fig 5: Core migration penalty ───────────────────────────────
def fig5():
    scenarios = ["Same Core\n(HT siblings)", "Diff Core\n(cross-L2)", "Free\n(no pin)"]
    means     = [2.4, 4.9, 3.6]
    p99s      = [3.8, 7.2, 6.1]
    stds      = [0.3, 0.7, 0.9]
    colors    = ["#2ca02c", "#d62728", "#ff7f0e"]

    x = np.arange(len(scenarios))
    w = 0.35

    fig, ax = plt.subplots(figsize=(5.5, 3.8))
    b1 = ax.bar(x - w/2, means, w, label="Mean ± σ", color=colors,
                alpha=0.85, yerr=stds, capsize=5,
                error_kw={"elinewidth": 1.5})
    ax.bar(x + w/2, p99s, w, label="p99", color=colors,
           alpha=0.4, edgecolor="black", linewidth=0.8)
    ax.set_xticks(x)
    ax.set_xticklabels(scenarios)
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 5: Core Migration Penalty — Same vs Cross Core")
    ax.legend(["Mean ± σ", "p99"])
    for bar in b1:
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.1,
                f"{bar.get_height():.1f}", ha="center", va="bottom", fontsize=8)
    fig.tight_layout()
    save(fig, "fig5_core_migration.pdf")

# ── Fig 6: Process-count scaling ────────────────────────────────
def fig6():
    procs   = np.array([2, 4, 6, 8, 10, 12, 16])
    mean    = np.array([3.4, 3.6, 3.8, 4.0, 4.9, 5.8, 7.1])
    std     = np.array([0.4, 0.4, 0.5, 0.5, 0.7, 0.8, 1.1])
    p99     = np.array([5.2, 5.6, 6.0, 6.5, 8.2, 9.8, 13.4])

    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    ax.errorbar(procs, mean, yerr=std, fmt="-o", color="#1f77b4",
                capsize=4, linewidth=2, markersize=6, label="Mean ± σ")
    ax.plot(procs, p99, "--s", color="#d62728", linewidth=1.5,
            markersize=5, label="p99")
    ax.axvline(x=8, color="gray", linestyle=":", linewidth=1.5,
               label="#vCPUs (8)")
    ax.set_xlabel("Number of Processes")
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 6: Scaling Analysis — Latency vs Process Count")
    ax.legend()
    ax.xaxis.set_minor_locator(AutoMinorLocator())
    fig.tight_layout()
    save(fig, "fig6_scaling.pdf")

# ── Fig 7: Scheduler policy comparison ──────────────────────────
def fig7():
    policies = ["SCHED_OTHER\n(CFS)", "SCHED_BATCH", "SCHED_IDLE",
                "SCHED_FIFO\n(RT)", "SCHED_RR\n(RT)"]
    means    = [3.4, 4.1, 8.7, 2.1, 2.3]
    p99s     = [6.8, 7.9, 18.2, 3.4, 3.9]
    stds     = [1.2, 1.5, 4.3, 0.4, 0.5]
    colors   = ["#1f77b4", "#ff7f0e", "#aec7e8", "#d62728", "#2ca02c"]

    x = np.arange(len(policies))

    fig, ax = plt.subplots(figsize=(6, 3.8))
    bars = ax.bar(x, means, 0.4, color=colors, alpha=0.85,
                  yerr=stds, capsize=5, error_kw={"elinewidth": 1.5})
    ax.plot(x, p99s, "D", color="black", markersize=6, label="p99", zorder=5)
    ax.set_xticks(x)
    ax.set_xticklabels(policies, fontsize=8)
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 7: Scheduler Policy Comparison")
    ax.legend(["p99"])
    for bar in bars:
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.15,
                f"{bar.get_height():.1f}", ha="center", va="bottom", fontsize=7.5)
    fig.tight_layout()
    save(fig, "fig7_sched_policy.pdf")

# ── Fig 8: Process vs thread ─────────────────────────────────────
def fig8():
    labels = ["Process", "Thread"]
    means  = [3.6, 2.3]
    p99s   = [6.8, 4.5]
    stds   = [0.9, 0.5]
    colors = ["#d62728", "#2ca02c"]

    x = np.arange(2)
    w = 0.35

    fig, ax = plt.subplots(figsize=(4.5, 3.5))
    b1 = ax.bar(x - w/2, means, w, color=colors, alpha=0.85,
                yerr=stds, capsize=6, error_kw={"elinewidth": 1.8},
                label="Mean ± σ")
    ax.bar(x + w/2, p99s, w, color=colors, alpha=0.4,
           edgecolor="black", label="p99")
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=10)
    ax.set_ylabel("Context Switch Latency (µs)")
    ax.set_title("Fig 8: Process vs Thread Context Switch")
    ax.legend()
    speedup = means[0] / means[1]
    ax.annotate(f"Thread is\n{speedup:.2f}× faster\n(TLB flush\navoided)",
                xy=(0.5, means[1] + 0.3), xycoords="data",
                ha="center", fontsize=8, color="#444444",
                bbox=dict(boxstyle="round,pad=0.3", fc="lightyellow",
                          ec="gray", alpha=0.8))
    fig.tight_layout()
    save(fig, "fig8_proc_vs_thread.pdf")

# ── Fig 9: Syscall overhead ──────────────────────────────────────
def fig9():
    syscalls = ["getpid()", "getppid()", "read(/dev/null)",
                "write(/dev/null)", "sched_yield()",
                "gettimeofday() [VDSO]", "clock_gettime [VDSO]"]
    latency_ns = [245, 185, 228, 235, 413, 18, 17]
    # Sort ascending
    pairs = sorted(zip(latency_ns, syscalls))
    latency_ns, syscalls = zip(*pairs)

    import matplotlib.cm as cm
    colors = cm.viridis(np.linspace(0.2, 0.85, len(syscalls)))  # type: ignore

    fig, ax = plt.subplots(figsize=(6, 3.8))
    bars = ax.barh(syscalls, latency_ns, color=colors, alpha=0.85)
    ax.set_xlabel("Latency (ns)")
    ax.set_title("Fig 9: System Call Overhead Baseline (RDTSC measured)")
    ax.xaxis.set_minor_locator(AutoMinorLocator())
    for bar, val in zip(bars, latency_ns):
        ax.text(val + 3, bar.get_y() + bar.get_height()/2,
                f"{val} ns", va="center", fontsize=8)
    fig.tight_layout()
    save(fig, "fig9_syscall_overhead.pdf")

if __name__ == "__main__":
    print("=" * 55)
    print("  Generating SAMPLE figures (synthetic data)")
    print(f"  Output dir: {FIGDIR}")
    print("  Replace with real data after: make run && make plots")
    print("=" * 55)
    fig1(); fig2(); fig3(); fig4(); fig5()
    fig6(); fig7(); fig8(); fig9()
    print("\n✅  All 9 sample figures generated.")
    print("    Now run:  make report")
