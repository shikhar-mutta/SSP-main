# Context Switches & Scheduling in Multicore Processors

## 🎯 Project Title
**"Microarchitectural and Scaling Analysis of Context Switching and Scheduling in Multicore Systems using lmbench"**

---

## 📋 Project Overview

This project provides a **focused, systematic analysis** of context switching behavior and scheduler performance in modern multicore systems. Using the industry-standard **lmbench** benchmarking suite, we measure:

- **Context switch latency** under varying workloads
- **Scheduler behavior** across cores (FIFO, RR, normal scheduling)
- **Scaling effects** as process count increases
- **Microarchitectural impact** (cache, TLB, CPU affinity)
- **Process vs Thread** context switch cost comparison
- **Cross-core migration penalty** due to cache invalidation

---

## 🧪 Experimental Phases

### **Phase 1: lmbench Baseline Measurement**
Install and run lmbench to establish baseline context switch latency measurements.

**Key Benchmarks:**
- `lat_ctx` → Context switching latency (varies process count & working set)
- `lat_proc` → Process creation overhead
- `lat_syscall` → System call baseline

**Expected Output:** `results/lmbench_lat_ctx.csv`

```bash
cd lmbench
make results
```

---

### **Phase 2: Core Migration Penalty Analysis**
Measure the cost of migrating processes/threads across CPU cores due to:
- L1/L2/L3 cache invalidation
- TLB flush overhead
- Scheduler load balancing

**Run:**
```bash
make phase2
```

**Expected Output:** `results/core_migration.csv`

**Key Insights:**
- Same-core switches vs cross-core switches
- Scaling behavior with process count
- Free scheduling vs pinned scenarios

---

### **Phase 3: Scheduler Policy Comparison**
Compare different Linux scheduler behaviors:
- FIFO (real-time, deterministic)
- Round-robin (real-time, time-sliced)
- Normal (CFS - completely fair scheduler)

**Run:**
```bash
make phase3  # or with sudo for RT policies
```

**Expected Output:** `results/sched_policy.csv`

---

### **Phase 4: Process vs Thread Context Switch**
Measure the difference between process and thread context switches.

**Key Difference:**
- **Process switch:** Full memory context change (TLB, page tables)
- **Thread switch:** Lightweight (shared memory, TLB reuse)

**Run:**
```bash
make phase4
```

**Expected Output:** `results/proc_vs_thread.csv`

---

## 🚀 Novelty & Research Contributions

### **Novelty 1: Context Switch Cost Model**
Build a mathematical model linking context switch latency to:
- Working set size
- Cache residency (L1, L2, L3, DRAM)
- Number of processes competing for scheduler

**Plot:** Latency = f(working_set, num_processes)

---

### **Novelty 2: Core Migration Penalty Quantification**
Empirically measure and model the cost of cross-core scheduling:

```
Migration Cost = L1_eviction_cost + L2_eviction_cost + TLB_flush_cost
```

Show why modern Linux schedulers use **cache-aware scheduling** to minimize migrations.

---

### **Novelty 3: Scheduler Fairness vs Latency Tradeoff**
Demonstrate the fundamental tradeoff:
- ↑ More processes → ↑ Fairness (each gets time) BUT ↑ Latency (scheduling overhead)
- Plot: Process count (2→64) vs Context switch latency

**Key insight:** Linux CFS scheduler is near-optimal but has fundamental scalability limits.

---

### **Novelty 4: Microarchitectural Analysis (Optional: eBPF Integration)**
Use eBPF/bpftrace to correlate:
- Actual kernel-level context switches
- lmbench measured latency
- Hardware cache misses (if perf counters available)

```bash
sudo bpftrace -e 'tracepoint:sched:sched_switch { @[comm] = count(); }'
```

---

### **Novelty 5: Scalability Beyond Prototype Systems**
Run identical experiments on:
- **Local:** Ubuntu laptop/desktop
- **Cloud:** AWS EC2 instances
- **NUMA systems** (if available)

Show how virtualization overhead affects context switching and scheduler effectiveness.

---

## 📊 Analysis & Visualization

All analysis scripts generate publication-quality figures saved to `report/figures/`.

**Generated Figures:**

| Figure | Title | File |
|--------|-------|------|
| 1 | Scheduler Scaling — Latency vs Process Count | `fig1_lmbench_scaling.pdf` |
| 2 | Working-Set Effect on Context-Switch Latency | `fig2_lmbench_ws.pdf` |
| 3 | Core Migration Penalty: Same vs Cross Core | `fig3_core_migration.pdf` |
| 4 | Scaling Analysis — Process Count vs Latency | `fig4_scaling.pdf` |
| 5 | Scheduler Policy Comparison | `fig5_sched_policy.pdf` |
| 6 | Process vs Thread Context Switch | `fig6_proc_vs_thread.pdf` |

**Run analysis:**
```bash
make plots
```

---

## 📄 Research Papers (REQUIRED REFERENCES)

### **1. Original lmbench Paper**
- **Title:** "lmbench: Portable Tools for Performance Analysis"
- **Authors:** McVoy, Staelin
- **Use:** Explain benchmark methodology and validity
- **Link:** https://www.usenix.org/legacy/publications/library/proceedings/usenix99/full_papers/mcvoy/mcvoy.pdf

### **2. Linux Scheduler Evolution**
- **Title:** "The Linux Scheduler: a Decade of Evolution"
- **Authors:** Darren Hart et al.
- **Use:** Background on CFS, scheduling policies, multicore challenges
- **Link:** https://www.kernel.org/doc/ols/2007/ols2007v2-pages-95-108.pdf

### **3. Scalability Analysis**
- **Title:** "An Analysis of Linux Scalability to Many Cores"
- **Authors:** Boyd-Wickizer et al. (MIT)
- **Use:** Document scalability bottlenecks, lock contention, cache effects
- **Link:** https://www.usenix.org/legacy/event/osdi10/tech/full_papers/Boyd-Wickizer.pdf

### **4. Memory Hierarchy & CPU**
- **Title:** "What Every Programmer Should Know About Memory"
- **Authors:** Ulrich Drepper
- **Use:** Explain cache, TLB, memory latency effects on scheduling
- **Link:** https://people.freebsd.org/~lstewart/articles/cpumemory.pdf

### **5. Multicore Scheduling Deep Dive**
- **Title:** "A Study of Linux Scheduler Scalability"
- **Authors:** Various (Linux Plumbers Conf)
- **Use:** Practical scheduler tuning, load balancing, core affinity
- **Link:** https://dl.acm.org/doi/10.1145/1629575.1629596

---

## 🛠️ Tools & Setup

### **Required Tools**
```bash
sudo apt update
sudo apt install build-essential git python3 matplotlib numpy pandas -y

# For optional advanced analysis
sudo apt install linux-tools-generic bpftrace -y
```

### **CPU Affinity Tools**
```bash
# Pin processes to specific cores
taskset -c 0 ./bin/x86_64-linux-gnu/lat_ctx -s 0 2

# NUMA-aware execution (if multi-socket)
numactl --cpunodebind=0 --membind=0 ./bin/x86_64-linux-gnu/lat_ctx -s 0 2
```

### **Scheduler Control**
```bash
# Real-time FIFO priority (requires root)
sudo chrt -f 99 ./bin/x86_64-linux-gnu/lat_ctx -s 0 2

# Normal (CFS) scheduler
sudo chrt -o 0 ./bin/x86_64-linux-gnu/lat_ctx -s 0 2
```

---

## 🔥 Viva Preparation

### **Expected Questions & Answers**

**Q1: Why is context switch latency NOT constant?**
> Answer: Context switch latency depends on:
> - **Working set size:** Larger WS → more cache misses → higher latency
> - **Number of processes:** More contention → scheduler overhead
> - **CPU topology:** Cross-core switches require cache invalidation
> - **Kernel version:** Different CFS tuning parameters affect scheduling

**Q2: What is the role of cache and TLB in scheduling?**
> Answer:
> - **L1/L2 caches:** Process state often fits in L1/L2. Core migration flushes these.
> - **TLB (Translation Lookaside Buffer):** Caches virtual→physical address mappings. TLB miss → expensive page table walk.
> - **L3 (shared):** Last-level cache is shared; processes compete for space.

**Q3: Why does multicore increase complexity?**
> Answer:
> - **Cache coherency:** MESI/MOESI protocols ensure consistency but add latency
> - **Load balancing:** Scheduler must decide which core gets next task
> - **Inter-core communication:** Process migration → cache line bouncing
> - **NUMA (Multi-socket):** Remote memory access is 10-100× slower

**Q4: Process vs Thread context switch — what's the difference?**
> Answer:
> - **Process:** Requires switching memory address space, page tables, TLB flush → expensive (1-10 µs)
> - **Thread:** Stays in same address space, only saves/restores registers → cheap (0.1-1 µs)

---

## 📦 Project Structure

```
SSP-main/
├── README_FOCUSED.md          # This file
├── Makefile                   # Build & experiment orchestration
├── src/                       # Benchmark source code
│   ├── ctx_switch_bench.c     # Core context switch microbenchmark
│   ├── core_migration_penalty.c
│   ├── sched_policy_bench.c
│   └── process_vs_thread.c
├── bin/                       # Compiled benchmarks
├── lmbench/                   # Official lmbench suite
├── scripts/
│   ├── run_all.sh             # Master experiment runner
│   └── phase1_lmbench_baseline.sh
├── results/                   # CSV outputs
│   ├── lmbench_lat_ctx.csv
│   ├── core_migration.csv
│   ├── sched_policy.csv
│   └── proc_vs_thread.csv
├── analysis/
│   └── plot_all.py            # Figure generation
├── report/
│   ├── main.tex               # LaTeX document
│   └── figures/               # Generated PDFs
```

---

## 🚀 Quick Start

### **1. Clone & Build**
```bash
cd /home/shikhar/Sem\ 2/SSP/SSP-main
make all
```

### **2. Run All Experiments**
```bash
make run                  # Takes ~10 minutes
```

### **3. Generate Figures**
```bash
make plots
```

### **4. View Results**
```bash
ls results/*.csv          # View raw data
ls report/figures/        # View generated plots
```

### **5. Compile LaTeX Report**
```bash
make report
```

---

## 📋 Checklist for Final Submission

- [ ] All 4 phases executed successfully
- [ ] CSV outputs generated in `results/`
- [ ] 6 figures generated in `report/figures/`
- [ ] LaTeX report compiled to PDF
- [ ] All 5 research papers cited in references
- [ ] Viva questions answered in presentation
- [ ] Novelty clearly explained in report

---

## 🔗 References & External Resources

- **lmbench GitHub:** https://github.com/intel/lmbench
- **Linux Kernel Scheduler:** https://www.kernel.org/doc/html/latest/scheduler/index.html
- **CFS Documentation:** https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html

---

**Last Updated:** April 27, 2026  
**Project Status:** ✅ Focused & Ready for Execution
