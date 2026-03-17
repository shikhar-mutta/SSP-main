#!/usr/bin/env python3
"""
analyze_results.py
==================
Parses lmbench result files and generates publication-quality plots for
context-switch scaling, microarchitecture analysis, and all other benchmarks.
"""

import os, re, sys
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.patches import FancyBboxPatch
import matplotlib.ticker as mticker
import warnings
warnings.filterwarnings('ignore')

# ── Paths ──────────────────────────────────────────────────────────────────────
BASE        = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(BASE, '..', 'results')
PLOTS_DIR   = os.path.join(BASE, '..', 'plots')
os.makedirs(PLOTS_DIR, exist_ok=True)

# ── Style ──────────────────────────────────────────────────────────────────────
PALETTE = {
    '0KB':   '#4FC3F7',
    '16KB':  '#FF8A65',
    '64KB':  '#A5D6A7',
    'accent': '#E040FB',
}
BG_DARK  = '#0F1117'
BG_CARD  = '#1A1D27'
FG_TEXT  = '#E0E0E0'
FG_DIMM  = '#9E9E9E'
GRID_CLR = '#2A2D3A'

def set_dark_style():
    plt.rcParams.update({
        'figure.facecolor':  BG_DARK,
        'axes.facecolor':    BG_CARD,
        'axes.edgecolor':    GRID_CLR,
        'axes.labelcolor':   FG_TEXT,
        'axes.titlecolor':   FG_TEXT,
        'axes.grid':         True,
        'grid.color':        GRID_CLR,
        'grid.linewidth':    0.6,
        'xtick.color':       FG_DIMM,
        'ytick.color':       FG_DIMM,
        'text.color':        FG_TEXT,
        'legend.facecolor':  BG_CARD,
        'legend.edgecolor':  GRID_CLR,
        'legend.labelcolor': FG_TEXT,
        'font.family':       'DejaVu Sans',
        'font.size':         10,
        'axes.titlesize':    12,
        'axes.labelsize':    11,
    })

set_dark_style()

# ══════════════════════════════════════════════════════════════════════════════
# 1. PARSE RESULT FILES
# ══════════════════════════════════════════════════════════════════════════════

def parse_context_switch(path):
    """Returns dict: {(datasize_kb, procs): latency_us}"""
    records = []
    ds = None
    with open(path) as f:
        for line in f:
            line = line.strip()
            m_ds = re.match(r'=== Data Size: (\d+) KB ===', line)
            if m_ds:
                ds = int(m_ds.group(1))
                continue
            m = re.match(r'procs=(\d+)\s+ds=\d+KB\s+lat=([\d.]+)us', line)
            if m and ds is not None:
                records.append({'ds_kb': ds, 'procs': int(m.group(1)),
                                'lat_us': float(m.group(2))})
    return pd.DataFrame(records)

def parse_kv(path, key_pat, val_pat):
    """General key=value parser"""
    records = {}
    with open(path) as f:
        for line in f:
            if line.startswith('#'): continue
            km = re.search(key_pat, line)
            vm = re.search(val_pat, line)
            if km and vm:
                records[km.group(1)] = float(vm.group(1))
    return records

def parse_syscalls(path):
    return parse_kv(path,
                    r'syscall=(\w+)',
                    r'lat=([\d.]+)us')

def parse_signals(path):
    return parse_kv(path,
                    r'sig=(\w+)',
                    r'lat=([\d.]+)us')

def parse_process(path):
    return parse_kv(path,
                    r'proc=(\w+)',
                    r'lat=([\d.]+)us')

def parse_mem_latency(path):
    """lat_mem_rd output: size_MB  latency_ns"""
    records = []
    with open(path) as f:
        for line in f:
            if line.startswith('#') or line.strip() == '': continue
            parts = line.split()
            if len(parts) == 2:
                try:
                    size, lat = float(parts[0]), float(parts[1])
                    records.append({'size_mb': size, 'lat_ns': lat})
                except:
                    pass
    return pd.DataFrame(records)

# ── Load Data ──────────────────────────────────────────────────────────────────
ctx_df  = parse_context_switch(os.path.join(RESULTS_DIR, 'context_switch.txt'))
sc_data = parse_syscalls(os.path.join(RESULTS_DIR, 'syscalls.txt'))
sig_data = parse_signals(os.path.join(RESULTS_DIR, 'signals.txt'))
proc_data = parse_process(os.path.join(RESULTS_DIR, 'process.txt'))
mem_df   = parse_mem_latency(os.path.join(RESULTS_DIR, 'mem_latency.txt'))

print("=== Parsed Data ===")
print("Context Switch:\n", ctx_df)
print("\nSyscalls:",  sc_data)
print("\nSignals:",   sig_data)
print("\nProcess:",   proc_data)
print("\nMem latency rows:", len(mem_df))


# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 1: Context Switch Scaling (main focus)
# ══════════════════════════════════════════════════════════════════════════════

def plot_ctx_scaling():
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))
    fig.patch.set_facecolor(BG_DARK)
    fig.suptitle('Context Switch Latency — Scaling Analysis\n'
                 'Intel Core i5-1035G1 (Ice Lake) · lmbench lat_ctx',
                 color=FG_TEXT, fontsize=14, fontweight='bold', y=1.01)

    # ── Left: Latency vs Processes (all data sizes) ──────────────────────────
    ax = axes[0]
    colors = ['#4FC3F7', '#FF8A65', '#A5D6A7']
    markers = ['o', 's', '^']
    for (ds, col, mk) in zip([0, 16, 64], colors, markers):
        sub = ctx_df[ctx_df['ds_kb'] == ds].sort_values('procs')
        ax.plot(sub['procs'], sub['lat_us'], marker=mk, color=col, lw=2.5,
                ms=7, label=f'{ds} KB working set', zorder=5)
        ax.fill_between(sub['procs'], sub['lat_us'],
                        alpha=0.12, color=col)

    # Mark physical core boundary (4 cores / 8 threads)
    ax.axvline(x=8, ls='--', color='#FFD54F', lw=1.5, alpha=0.7,
               label='HT boundary (8 threads)')
    ax.axvline(x=4, ls=':', color='#CE93D8', lw=1.5, alpha=0.7,
               label='Physical cores (4)')

    ax.set_xlabel('Number of Processes')
    ax.set_ylabel('Context Switch Latency (µs)')
    ax.set_title('Latency vs. Process Count')
    ax.set_xscale('log', base=2)
    ax.xaxis.set_major_formatter(mticker.ScalarFormatter())
    ax.set_xticks([2, 4, 8, 16, 32, 64, 96])
    ax.legend(fontsize=9, loc='upper left')
    ax.set_ylim(bottom=0)

    # ── Right: Relative Overhead (normalised to 2-proc baseline) ─────────────
    ax2 = axes[1]
    for (ds, col, mk) in zip([0, 16, 64], colors, markers):
        sub = ctx_df[ctx_df['ds_kb'] == ds].sort_values('procs')
        baseline = sub['lat_us'].iloc[0]
        overhead = sub['lat_us'] / baseline
        ax2.plot(sub['procs'], overhead, marker=mk, color=col, lw=2.5,
                 ms=7, label=f'{ds} KB', zorder=5)
        ax2.fill_between(sub['procs'], overhead, 1, alpha=0.10, color=col)

    ax2.axhline(y=1.0, ls='--', color=FG_DIMM, lw=1, alpha=0.6)
    ax2.axvline(x=8, ls='--', color='#FFD54F', lw=1.5, alpha=0.7)
    ax2.axvline(x=4, ls=':', color='#CE93D8', lw=1.5, alpha=0.7)
    ax2.set_xscale('log', base=2)
    ax2.xaxis.set_major_formatter(mticker.ScalarFormatter())
    ax2.set_xticks([2, 4, 8, 16, 32, 64, 96])
    ax2.set_xlabel('Number of Processes')
    ax2.set_ylabel('Normalised Overhead (relative to 2-proc)')
    ax2.set_title('Relative Scheduling Overhead')
    ax2.legend(fontsize=9)

    for ax_ in axes:
        ax_.spines['top'].set_visible(False)
        ax_.spines['right'].set_visible(False)

    plt.tight_layout()
    out = os.path.join(PLOTS_DIR, 'ctx_scaling.pdf')
    out_png = os.path.join(PLOTS_DIR, 'ctx_scaling.png')
    plt.savefig(out,     bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.savefig(out_png, bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.close()
    print(f"[OK] {out_png}")


# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 2: Microarchitecture — Memory Latency Hierarchy
# ══════════════════════════════════════════════════════════════════════════════

# Ice Lake cache sizes
CACHE_BOUNDARIES = {
    'L1d (48KB)':  0.048,
    'L2 (512KB)':  0.512,
    'L3 (6MB)':    6.0,
}

def plot_mem_hierarchy():
    fig, ax = plt.subplots(figsize=(11, 6))
    fig.patch.set_facecolor(BG_DARK)

    ax.plot(mem_df['size_mb'] * 1024, mem_df['lat_ns'], color='#4FC3F7',
            lw=2.5, marker='o', ms=5, zorder=5, label='Measured latency')

    # Shade cache regions
    colors_cache = ['#1B5E2044', '#E65100AA', '#1A237E44']
    prev = 0
    for (name, boundary_mb), shade in zip(CACHE_BOUNDARIES.items(), colors_cache):
        ax.axvspan(prev, boundary_mb * 1024, alpha=0.15, color=shade.replace('44','').replace('AA',''))
        ax.axvline(x=boundary_mb * 1024, ls='--', color='#FFD54F', lw=1.2, alpha=0.8)
        ax.text(boundary_mb * 1024, ax.get_ylim()[1] if hasattr(ax, '_ylim_set') else 100,
                name, color='#FFD54F', fontsize=8, va='top', ha='right', rotation=90)
        prev = boundary_mb * 1024

    # Re-annotate after we know y-range
    ymax = mem_df['lat_ns'].max() * 1.1
    ax.set_ylim(0, ymax)
    prev = 0
    for (name, boundary_mb) in CACHE_BOUNDARIES.items():
        ax.text(boundary_mb * 1024 * 0.95, ymax * 0.92, name,
                color='#FFD54F', fontsize=8.5, va='top', ha='right', rotation=90,
                fontweight='bold')

    ax.set_xscale('log', base=2)
    ax.set_xlabel('Array Size (KB)')
    ax.set_ylabel('Access Latency (ns)')
    ax.set_title('Memory Hierarchy Latency Profile\n'
                 'Intel Core i5-1035G1 — lat_mem_rd (stride=128B)',
                 color=FG_TEXT)
    ax.legend()
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    plt.tight_layout()
    out_png = os.path.join(PLOTS_DIR, 'mem_hierarchy.png')
    plt.savefig(out_png, bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    out_pdf = os.path.join(PLOTS_DIR, 'mem_hierarchy.pdf')
    plt.savefig(out_pdf, bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.close()
    print(f"[OK] {out_png}")


# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 3: Syscall Overhead — Horizontal Bar Chart
# ══════════════════════════════════════════════════════════════════════════════

def plot_syscall_bars():
    labels = list(sc_data.keys())
    values = [sc_data[k] for k in labels]
    # Sort ascending
    pairs = sorted(zip(values, labels))
    values, labels = zip(*pairs)

    fig, ax = plt.subplots(figsize=(9, 5))
    fig.patch.set_facecolor(BG_DARK)

    bar_colors = plt.cm.cool(np.linspace(0.2, 0.9, len(labels)))
    bars = ax.barh(labels, values, color=bar_colors, edgecolor='none', height=0.55)

    for bar, val in zip(bars, values):
        ax.text(val + 0.03, bar.get_y() + bar.get_height()/2,
                f'{val:.4f} µs', va='center', color=FG_TEXT, fontsize=9)

    ax.set_xlabel('Latency (µs)')
    ax.set_title('System Call Overhead\n'
                 'Intel Core i5-1035G1 — lmbench lat_syscall',
                 color=FG_TEXT)
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.set_xlim(0, max(values) * 1.35)

    plt.tight_layout()
    out_png = os.path.join(PLOTS_DIR, 'syscall_overhead.png')
    plt.savefig(out_png, bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.savefig(out_png.replace('.png','.pdf'), bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.close()
    print(f"[OK] {out_png}")


# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 4: Process Creation — Stacked / grouped bars
# ══════════════════════════════════════════════════════════════════════════════

def plot_process_creation():
    labels = ['fork', 'exec', 'shell']
    values = [proc_data.get(k, 0) for k in labels]
    # Incremental cost
    increments = [values[0],
                  values[1] - values[0],
                  values[2] - values[1]]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))
    fig.patch.set_facecolor(BG_DARK)

    # Left: absolute
    cols = ['#29B6F6', '#AB47BC', '#FF7043']
    bars = ax1.bar(labels, values, color=cols, width=0.45, edgecolor='none')
    for bar, val in zip(bars, values):
        ax1.text(bar.get_x() + bar.get_width()/2, val + 20,
                 f'{val:.0f}µs', ha='center', color=FG_TEXT, fontsize=10)
    ax1.set_ylabel('Latency (µs)')
    ax1.set_title('Process Creation Latency (Absolute)\nlat_proc', color=FG_TEXT)
    ax1.spines['top'].set_visible(False)
    ax1.spines['right'].set_visible(False)

    # Right: stacked incremental
    stack_labels = ['fork\n(base)', '+exec\noverhead', '+shell\noverhead']
    bottoms = [0, values[0], values[1]]
    inc_cols = ['#29B6F6', '#AB47BC', '#FF7043']
    for i, (sl, inc, bot, c) in enumerate(zip(stack_labels, increments, bottoms, inc_cols)):
        ax2.bar(0.4, inc, bottom=bot, width=0.45, color=c, edgecolor='none', label=sl)
        ax2.text(0.4, bot + inc/2, f'{inc:.0f}µs\n({sl})',
                 ha='center', va='center', color='white', fontsize=9, fontweight='bold')
    ax2.set_ylabel('Latency (µs)')
    ax2.set_title('Incremental Cost Decomposition\n(fork → exec → shell)', color=FG_TEXT)
    ax2.set_xlim(-0.1, 0.9)
    ax2.set_xticks([])
    ax2.legend(loc='upper right', fontsize=9)
    ax2.spines['top'].set_visible(False)
    ax2.spines['right'].set_visible(False)

    plt.tight_layout()
    out_png = os.path.join(PLOTS_DIR, 'process_creation.png')
    plt.savefig(out_png, bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.savefig(out_png.replace('.png','.pdf'), bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.close()
    print(f"[OK] {out_png}")


# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 5: Signal Handling
# ══════════════════════════════════════════════════════════════════════════════

def plot_signals():
    labels = list(sig_data.keys())
    values = [sig_data[k] for k in labels]

    fig, ax = plt.subplots(figsize=(8, 4.5))
    fig.patch.set_facecolor(BG_DARK)

    cols = ['#80DEEA', '#FFCC80', '#EF9A9A']
    bars = ax.bar(labels, values, color=cols, width=0.4, edgecolor='none')
    for bar, val in zip(bars, values):
        ax.text(bar.get_x() + bar.get_width()/2, val + max(values)*0.02,
                f'{val:.4f}µs', ha='center', color=FG_TEXT, fontsize=10)

    ax.set_ylabel('Latency (µs)')
    ax.set_title('Signal Handling Latency\nlat_sig — i5-1035G1', color=FG_TEXT)
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    plt.tight_layout()
    out_png = os.path.join(PLOTS_DIR, 'signal_handling.png')
    plt.savefig(out_png, bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.savefig(out_png.replace('.png','.pdf'), bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.close()
    print(f"[OK] {out_png}")


# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 6: Context Switch — Microarchitecture Heatmap
# ══════════════════════════════════════════════════════════════════════════════

def plot_ctx_heatmap():
    pivot = ctx_df.pivot(index='ds_kb', columns='procs', values='lat_us')

    fig, ax = plt.subplots(figsize=(11, 4))
    fig.patch.set_facecolor(BG_DARK)

    im = ax.imshow(pivot.values, aspect='auto', cmap='plasma',
                   interpolation='nearest')

    ax.set_xticks(range(len(pivot.columns)))
    ax.set_xticklabels(pivot.columns)
    ax.set_yticks(range(len(pivot.index)))
    ax.set_yticklabels([f'{v}KB' for v in pivot.index])
    ax.set_xlabel('Number of Processes')
    ax.set_ylabel('Working Set Size')
    ax.set_title('Context Switch Latency Heatmap (µs)\n'
                 'Processes × Working-Set Size — i5-1035G1', color=FG_TEXT)

    # Annotate cells
    for i in range(len(pivot.index)):
        for j in range(len(pivot.columns)):
            val = pivot.values[i, j]
            ax.text(j, i, f'{val:.2f}', ha='center', va='center',
                    color='white', fontsize=9, fontweight='bold')

    cbar = fig.colorbar(im, ax=ax, shrink=0.9)
    cbar.set_label('Latency (µs)', color=FG_TEXT)
    cbar.ax.yaxis.set_tick_params(color=FG_DIMM)
    plt.setp(cbar.ax.yaxis.get_ticklabels(), color=FG_DIMM)

    plt.tight_layout()
    out_png = os.path.join(PLOTS_DIR, 'ctx_heatmap.png')
    plt.savefig(out_png, bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.savefig(out_png.replace('.png','.pdf'), bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.close()
    print(f"[OK] {out_png}")


# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 7: Combined Overview Dashboard
# ══════════════════════════════════════════════════════════════════════════════

def plot_dashboard():
    fig = plt.figure(figsize=(18, 10))
    fig.patch.set_facecolor(BG_DARK)
    fig.suptitle('SSP Benchmarking Dashboard — Intel Core i5-1035G1 (Ice Lake)\n'
                 'lmbench Microbenchmark Suite · Ubuntu · Kernel 6.17.0-14-generic',
                 color=FG_TEXT, fontsize=14, fontweight='bold')

    gs = gridspec.GridSpec(2, 3, figure=fig, hspace=0.45, wspace=0.38)

    # ── Subplot 1: Context switch ─────────────────────────────────────────────
    ax1 = fig.add_subplot(gs[0, :2])
    colors = ['#4FC3F7', '#FF8A65', '#A5D6A7']
    for (ds, col) in zip([0, 16, 64], colors):
        sub = ctx_df[ctx_df['ds_kb'] == ds].sort_values('procs')
        ax1.plot(sub['procs'], sub['lat_us'], marker='o', color=col, lw=2,
                 ms=5, label=f'{ds}KB working set')
    ax1.axvline(x=8, ls='--', color='#FFD54F', lw=1.2, alpha=0.7)
    ax1.set_xscale('log', base=2)
    ax1.xaxis.set_major_formatter(mticker.ScalarFormatter())
    ax1.set_xticks([2, 4, 8, 16, 32, 64, 96])
    ax1.set_xlabel('Processes'); ax1.set_ylabel('µs')
    ax1.set_title('Context Switch Latency Scaling')
    ax1.legend(fontsize=8)
    ax1.spines['top'].set_visible(False); ax1.spines['right'].set_visible(False)

    # ── Subplot 2: Memory Hierarchy ───────────────────────────────────────────
    ax2 = fig.add_subplot(gs[0, 2])
    ax2.plot(mem_df['size_mb'] * 1024, mem_df['lat_ns'],
             color='#4FC3F7', lw=2, marker='o', ms=3)
    for boundary_mb, name in [(0.048, 'L1'), (0.512, 'L2'), (6.0, 'L3')]:
        ax2.axvline(x=boundary_mb * 1024, ls='--', color='#FFD54F', lw=1)
        ypos = mem_df['lat_ns'].max() * 0.9
        ax2.text(boundary_mb * 1024 * 0.85, ypos, name,
                 color='#FFD54F', fontsize=7, rotation=90, va='top')
    ax2.set_xscale('log', base=2)
    ax2.set_xlabel('Array (KB)'); ax2.set_ylabel('Latency (ns)')
    ax2.set_title('Memory Hierarchy')
    ax2.spines['top'].set_visible(False); ax2.spines['right'].set_visible(False)

    # ── Subplot 3: Syscall ────────────────────────────────────────────────────
    ax3 = fig.add_subplot(gs[1, 0])
    skeys = list(sc_data.keys())[::-1]
    svals = [sc_data[k] for k in skeys]
    cols3 = plt.cm.cool(np.linspace(0.2, 0.9, len(skeys)))
    ax3.barh(skeys, svals, color=cols3, height=0.5)
    ax3.set_xlabel('µs'); ax3.set_title('Syscall Overhead')
    ax3.spines['top'].set_visible(False); ax3.spines['right'].set_visible(False)

    # ── Subplot 4: Signals ────────────────────────────────────────────────────
    ax4 = fig.add_subplot(gs[1, 1])
    sglabs = list(sig_data.keys())
    sgvals = [sig_data[k] for k in sglabs]
    ax4.bar(sglabs, sgvals, color=['#80DEEA','#FFCC80','#EF9A9A'], width=0.4)
    ax4.set_ylabel('µs'); ax4.set_title('Signal Handling')
    ax4.spines['top'].set_visible(False); ax4.spines['right'].set_visible(False)

    # ── Subplot 5: Process Creation ───────────────────────────────────────────
    ax5 = fig.add_subplot(gs[1, 2])
    plabs = list(proc_data.keys())
    pvals = [proc_data[k] for k in plabs]
    ax5.bar(plabs, pvals, color=['#29B6F6','#AB47BC','#FF7043'], width=0.4)
    ax5.set_ylabel('µs'); ax5.set_title('Process Creation')
    ax5.spines['top'].set_visible(False); ax5.spines['right'].set_visible(False)

    for ax in [ax1, ax2, ax3, ax4, ax5]:
        ax.set_facecolor(BG_CARD)

    out_png = os.path.join(PLOTS_DIR, 'dashboard.png')
    plt.savefig(out_png, bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.savefig(out_png.replace('.png','.pdf'), bbox_inches='tight', dpi=150, facecolor=BG_DARK)
    plt.close()
    print(f"[OK] {out_png}")


# ══════════════════════════════════════════════════════════════════════════════
# Run all plots
# ══════════════════════════════════════════════════════════════════════════════

if __name__ == '__main__':
    print("\n=== Generating Plots ===")
    plot_ctx_scaling()
    plot_mem_hierarchy()
    plot_syscall_bars()
    plot_process_creation()
    plot_signals()
    plot_ctx_heatmap()
    plot_dashboard()
    print("\n=== All plots saved to", PLOTS_DIR, "===")
