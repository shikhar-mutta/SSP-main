#!/usr/bin/env python3
"""
SSP Benchmark Results Visualization
Generates 6 publication-quality graph types from workload_benchmark.csv.

Outputs (all saved to results/):
  scaling_efficiency.png    – mean latency vs core count, one line per workload
  latency_vs_intensity.png  – 2x2 grid: latency vs intensity per workload,
                              separate line per core count
  workload_comparison.png   – grouped bar chart: workload x intensity at each
                              core count
  tail_latency_<wl>.png     – per-workload tail latency (P50/P95/P99) by intensity
  latency_heatmap.png       – 2x2 heatmap: intensity x cores for each workload
  summary_statistics.png    – table: mean / std / min / max per workload
"""

import os
import sys
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
from matplotlib.colors import LogNorm

plt.rcParams.update({
    'figure.dpi': 150,
    'font.size': 11,
    'axes.labelsize': 13,
    'axes.titlesize': 14,
    'axes.titleweight': 'bold',
    'legend.fontsize': 10,
    'xtick.labelsize': 11,
    'ytick.labelsize': 11,
    'axes.spines.top': False,
    'axes.spines.right': False,
})

COLORS = ['#2E86AB', '#A23B72', '#F18F01', '#C73E1D']
WORKLOAD_ORDER = ['cpu', 'memory', 'io', 'mixed']
WORKLOAD_LABELS = {'cpu': 'CPU', 'memory': 'Memory', 'io': 'I/O', 'mixed': 'Mixed'}


def load_data(csv_path):
    if not os.path.exists(csv_path):
        print(f"ERROR: CSV file not found: {csv_path}", file=sys.stderr)
        sys.exit(1)
    df = pd.read_csv(csv_path)
    df.columns = df.columns.str.strip()
    print(f"Loaded {len(df)} records from {csv_path}")
    return df


def save(fig, path):
    fig.savefig(path, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"  Saved: {path}")


# ── Graph 1: Scaling Efficiency ───────────────────────────────────────────────
def plot_scaling_efficiency(df, out_dir):
    fig, ax = plt.subplots(figsize=(9, 5))
    workloads = [w for w in WORKLOAD_ORDER if w in df['workload_type'].unique()]
    cores_avail = sorted(df['active_cores'].unique())
    for wl, col in zip(workloads, COLORS):
        sub = df[df['workload_type'] == wl]
        # Use per-cycle latency for I/O, normalized latency for others
        metric = 'io_cycle_latency_us' if wl == 'io' else 'latency_us'
        grp = sub.groupby('active_cores')[metric].agg(['mean', 'std']).reindex(cores_avail)
        ax.errorbar(grp.index, grp['mean'], yerr=grp['std'],
                    marker='o', markersize=7, linewidth=2, capsize=4,
                    label=WORKLOAD_LABELS[wl], color=col)
    ax.set_yscale('log')
    ax.set_xlabel('Active Core Count')
    ax.set_ylabel('Mean Latency (us)  [log scale]')
    ax.set_title('Scaling Efficiency: Core Count vs. Latency per Workload')
    ax.set_xticks(cores_avail)
    ax.get_yaxis().set_major_formatter(mticker.ScalarFormatter())
    ax.legend(loc='upper right')
    ax.grid(True, which='both', alpha=0.25)
    fig.tight_layout()
    save(fig, os.path.join(out_dir, 'scaling_efficiency.png'))


# ── Graph 2: Latency vs Intensity (2x2 grid) ──────────────────────────────────
def plot_latency_vs_intensity(df, out_dir):
    workloads = [w for w in WORKLOAD_ORDER if w in df['workload_type'].unique()]
    cores_list = sorted(df['active_cores'].unique())
    intensities = sorted(df['intensity_pct'].unique())
    n = len(workloads)
    ncols = 2
    nrows = (n + 1) // ncols
    fig, axes = plt.subplots(nrows, ncols, figsize=(12, 5 * nrows), sharex=True)
    axes = np.array(axes).flatten()
    for idx, wl in enumerate(workloads):
        ax = axes[idx]
        sub = df[df['workload_type'] == wl]
        # Use per-cycle latency for I/O, normalized latency for others
        metric = 'io_cycle_latency_us' if wl == 'io' else 'latency_us'
        for c_idx, cores in enumerate(cores_list):
            csub = sub[sub['active_cores'] == cores]
            grp = csub.groupby('intensity_pct')[metric].agg(['mean', 'std']).reindex(intensities)
            ax.errorbar(grp.index, grp['mean'], yerr=grp['std'],
                        marker='s', markersize=6, linewidth=2, capsize=4,
                        label=f'{cores} core{"s" if cores > 1 else ""}',
                        color=COLORS[c_idx % len(COLORS)])
        ax.set_title(f'{WORKLOAD_LABELS[wl]} Workload')
        ax.set_xlabel('Intensity (%)')
        ax.set_ylabel('Mean Latency (us)')
        ax.set_xticks(intensities)
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.25)
    for idx in range(len(workloads), len(axes)):
        axes[idx].set_visible(False)
    fig.suptitle('Latency vs. Intensity for Each Workload Type', fontsize=15, fontweight='bold')
    fig.tight_layout()
    save(fig, os.path.join(out_dir, 'latency_vs_intensity.png'))


# ── Graph 3: Workload Comparison (grouped bar) ────────────────────────────────
def plot_workload_comparison(df, out_dir):
    workloads = [w for w in WORKLOAD_ORDER if w in df['workload_type'].unique()]
    intensities = sorted(df['intensity_pct'].unique())
    cores_list = sorted(df['active_cores'].unique())
    ref_cores = cores_list[len(cores_list) // 2]
    sub = df[df['active_cores'] == ref_cores]
    x = np.arange(len(workloads))
    n_int = len(intensities)
    width = 0.7 / n_int
    int_colors = ['#2E86AB', '#F18F01', '#C73E1D', '#6A0572']
    fig, ax = plt.subplots(figsize=(10, 5))
    for i, intensity in enumerate(intensities):
        vals, errs = [], []
        for wl in workloads:
            grp = sub[(sub['workload_type'] == wl) & (sub['intensity_pct'] == intensity)]
            # Use per-cycle latency for I/O, normalized latency for others
            metric = 'io_cycle_latency_us' if wl == 'io' else 'latency_us'
            vals.append(grp[metric].mean())
            errs.append(grp[metric].std())
        offset = (i - n_int / 2 + 0.5) * width
        ax.bar(x + offset, vals, width, yerr=errs, capsize=3,
               label=f'Intensity {intensity}%',
               color=int_colors[i % len(int_colors)],
               edgecolor='grey', linewidth=0.7, alpha=0.9)
    ax.set_yscale('log')
    ax.set_xlabel('Workload Type')
    ax.set_ylabel('Mean Latency (us)  [log scale]')
    ax.set_title(f'Workload Comparison at {ref_cores} Core{"s" if ref_cores > 1 else ""}')
    ax.set_xticks(x)
    ax.set_xticklabels([WORKLOAD_LABELS[w] for w in workloads])
    ax.get_yaxis().set_major_formatter(mticker.ScalarFormatter())
    ax.legend()
    ax.grid(True, which='both', axis='y', alpha=0.25)
    fig.tight_layout()
    save(fig, os.path.join(out_dir, 'workload_comparison.png'))


# ── Graph 4: Tail Latency per Workload ────────────────────────────────────────
def plot_tail_latency(df, out_dir):
    workloads = [w for w in WORKLOAD_ORDER if w in df['workload_type'].unique()]
    intensities = sorted(df['intensity_pct'].unique())
    pct_labels = ['P50', 'P95', 'P99']
    pct_values = [50, 95, 99]
    pct_colors = ['#2E86AB', '#F18F01', '#C73E1D']
    width = 0.22
    for wl in workloads:
        sub = df[df['workload_type'] == wl]
        # Use per-cycle latency for I/O, normalized latency for others
        metric = 'io_cycle_latency_us' if wl == 'io' else 'latency_us'
        x = np.arange(len(intensities))
        fig, ax = plt.subplots(figsize=(9, 5))
        for j, (plabel, pval, pcol) in enumerate(zip(pct_labels, pct_values, pct_colors)):
            vals = [np.percentile(sub[sub['intensity_pct'] == i][metric], pval)
                    for i in intensities]
            offset = (j - 1) * width
            ax.bar(x + offset, vals, width, label=plabel,
                   color=pcol, edgecolor='grey', linewidth=0.7, alpha=0.9)
        ax.set_xlabel('Intensity (%)')
        ax.set_ylabel('Latency (us)')
        ax.set_title(f'Tail Latency Analysis: {WORKLOAD_LABELS[wl]} Workload')
        ax.set_xticks(x)
        ax.set_xticklabels([f'{i}%' for i in intensities])
        ax.legend()
        ax.grid(True, axis='y', alpha=0.25)
        fig.tight_layout()
        save(fig, os.path.join(out_dir, f'tail_latency_{wl}.png'))


# ── Graph 5: Latency Heatmap ──────────────────────────────────────────────────
def plot_latency_heatmap(df, out_dir):
    workloads = [w for w in WORKLOAD_ORDER if w in df['workload_type'].unique()]
    intensities = sorted(df['intensity_pct'].unique())
    cores_list = sorted(df['active_cores'].unique())
    n = len(workloads)
    ncols = 2
    nrows = (n + 1) // ncols
    fig, axes = plt.subplots(nrows, ncols, figsize=(11, 5 * nrows))
    axes = np.array(axes).flatten()
    for idx, wl in enumerate(workloads):
        ax = axes[idx]
        # Use per-cycle latency for I/O, normalized latency for others
        metric = 'io_cycle_latency_us' if wl == 'io' else 'latency_us'
        matrix = np.zeros((len(intensities), len(cores_list)))
        for ri, intens in enumerate(intensities):
            for ci, cores in enumerate(cores_list):
                cell = df[(df['workload_type'] == wl) &
                          (df['intensity_pct'] == intens) &
                          (df['active_cores'] == cores)][metric]
                matrix[ri, ci] = cell.mean() if len(cell) > 0 else np.nan
        vmin, vmax = np.nanmin(matrix), np.nanmax(matrix)
        norm = LogNorm(vmin=max(vmin, 1e-3), vmax=vmax) if vmax > vmin else None
        im = ax.imshow(matrix, aspect='auto', cmap='YlOrRd', norm=norm)
        fig.colorbar(im, ax=ax, label='Mean Latency (us)')
        ax.set_title(f'{WORKLOAD_LABELS[wl]}')
        ax.set_xlabel('Active Cores')
        ax.set_ylabel('Intensity (%)')
        ax.set_xticks(range(len(cores_list)))
        ax.set_xticklabels(cores_list)
        ax.set_yticks(range(len(intensities)))
        ax.set_yticklabels([f'{i}%' for i in intensities])
        for ri in range(len(intensities)):
            for ci in range(len(cores_list)):
                val = matrix[ri, ci]
                if not np.isnan(val):
                    ax.text(ci, ri, f'{val:.0f}', ha='center', va='center',
                            fontsize=9, color='black')
    for idx in range(len(workloads), len(axes)):
        axes[idx].set_visible(False)
    fig.suptitle('Latency Heatmap: Intensity x Core Count per Workload',
                 fontsize=15, fontweight='bold')
    fig.tight_layout()
    save(fig, os.path.join(out_dir, 'latency_heatmap.png'))


# ── Graph 6: Summary statistics table ─────────────────────────────────────────
def plot_summary_statistics(df, out_dir):
    workloads = [w for w in WORKLOAD_ORDER if w in df['workload_type'].unique()]
    rows = []
    for wl in workloads:
        # Use per-cycle latency for I/O, normalized latency for others
        metric = 'io_cycle_latency_us' if wl == 'io' else 'latency_us'
        sub = df[df['workload_type'] == wl][metric]
        rows.append([
            WORKLOAD_LABELS[wl],
            f'{sub.mean():.1f}',
            f'{sub.std():.1f}',
            f'{sub.min():.1f}',
            f'{sub.max():.1f}',
            f'{np.percentile(sub, 50):.1f}',
            f'{np.percentile(sub, 95):.1f}',
            f'{np.percentile(sub, 99):.1f}',
        ])
    col_labels = ['Workload', 'Mean (us)', 'Std Dev', 'Min (us)', 'Max (us)',
                  'P50 (us)', 'P95 (us)', 'P99 (us)']
    fig, ax = plt.subplots(figsize=(14, 1.2 + 0.55 * len(rows)))
    ax.axis('off')
    tbl = ax.table(cellText=rows, colLabels=col_labels,
                   loc='center', cellLoc='center')
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(11)
    tbl.scale(1, 1.6)
    header_color = '#2E86AB'
    alt_color = '#EAF4FB'
    for (r, c), cell in tbl.get_celld().items():
        if r == 0:
            cell.set_facecolor(header_color)
            cell.set_text_props(color='white', fontweight='bold')
        elif r % 2 == 0:
            cell.set_facecolor(alt_color)
        cell.set_edgecolor('#CCCCCC')
    ax.set_title('Summary Statistics by Workload Type',
                 fontsize=14, fontweight='bold', pad=15)
    fig.tight_layout()
    save(fig, os.path.join(out_dir, 'summary_statistics.png'))


# ── Main ──────────────────────────────────────────────────────────────────────

def plot_scheduling_analysis(df, out_dir):
    """
    Plot scheduling contention vs latency.
    Visualizes correlation between context switches and latency variability.
    (FIX for Issue 3: Scheduling insights)
    """
    if 'sched_contention' not in df.columns:
        print("  ⊘ Scheduling contention data not available, skipping...")
        return
    
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))
    
    # Left: Contention vs Latency scatter
    for i, wl in enumerate(WORKLOAD_ORDER):
        wdf = df[df['workload_type'] == wl]
        if len(wdf) > 0:
            ax1.scatter(wdf['sched_contention'], wdf['latency_us'],
                       label=WORKLOAD_LABELS.get(wl, wl),
                       color=COLORS[i], s=100, alpha=0.6, edgecolors='black', linewidth=0.5)
    ax1.set_xlabel('Scheduling Contention Score', fontsize=12)
    ax1.set_ylabel('Latency (µs)', fontsize=12)
    ax1.set_title('Scheduling Contention vs Latency', fontsize=13, fontweight='bold')
    ax1.legend(loc='best', fontsize=10)
    ax1.grid(True, alpha=0.3, linestyle='--')
    
    # Right: Contention by core count
    for wl in WORKLOAD_ORDER:
        wdf = df[df['workload_type'] == wl]
        if len(wdf) > 0:
            grouped = wdf.groupby('active_cores')['sched_contention'].mean()
            ax2.plot(grouped.index, grouped.values, marker='o', 
                    label=WORKLOAD_LABELS.get(wl, wl), linewidth=2, markersize=7)
    ax2.set_xlabel('Active Cores', fontsize=12)
    ax2.set_ylabel('Mean Scheduling Contention', fontsize=12)
    ax2.set_title('Scheduling Contention vs Core Count', fontsize=13, fontweight='bold')
    ax2.legend(loc='best', fontsize=10)
    ax2.grid(True, alpha=0.3, linestyle='--')
    
    fig.tight_layout()
    save(fig, os.path.join(out_dir, 'scheduling_analysis.png'))


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    csv_path = 'results/workload_benchmark.csv'
    out_dir  = 'results'
    print('=' * 60)
    print('SSP Benchmark Results Visualization')
    print('=' * 60)
    df = load_data(csv_path)
    print('\nGenerating graphs...')
    print('-' * 40)
    plot_scaling_efficiency(df, out_dir)
    plot_latency_vs_intensity(df, out_dir)
    plot_workload_comparison(df, out_dir)
    plot_tail_latency(df, out_dir)
    plot_latency_heatmap(df, out_dir)
    plot_summary_statistics(df, out_dir)
    plot_scheduling_analysis(df, out_dir)
    print('-' * 40)
    print(f'\nAll graphs saved to: {out_dir}/')
    print('=' * 60)

if __name__ == '__main__':
    main()
