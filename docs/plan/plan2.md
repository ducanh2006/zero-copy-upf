# DETAILED IMPLEMENTATION PLAN (V4.0 WORKTREE + SUBAGENT): ZERO-COPY CHAINED-MBUF FOR 5G UPF N3/N9
### Technology Innovation Initiative / Tech Idea Contest – Viettel Digital Talent 2026 (Phase 2)
**Domain:** Software Engineering (SE) – High Performance Computing (HPC)  
**Entity:** Viettel High Tech (VHT) – Key Technology Center – Center 2 – Big Data & High-Performance Computing Department (CNXLDL)  
**Mentor:** Nguyen Ngoc Dung (`dungnn11@viettel.com.vn`)  
**Target Environment:** Ubuntu 24.04 LTS, Intel Core i7-13700HX (16 cores/24 threads, L3 Cache 30MB), GCC 13.3.0, DPDK 24.11 local (`third_party/dpdk-24.11/build`)

> **Inheritance:** This document inherits all technical specifications from `plan1.md` (V3.0) — High-fidelity Mbuf, 50B Outer Header layout, 3-way technical comparison (chain vs clone vs deepcopy), hot/stream benchmark, DPDK adapter, and deliverables.  
> **Unique addition in plan2:** Standardizes the **Git Worktree + Subagent + Auto-Review + Auto-Cleanup Operational Workflow** as required: each worktree has its own branch; once implemented, it undergoes automated review; only merged if review passes (PASS); once merged, both the branch and the worktree are immediately deleted.

---

## 1. TECHNICAL DIRECTION SIGN-OFF (Inherited from plan1, confirmed with project owner)

1. **Real DPDK (No Mocking):** Utilize existing `third_party/dpdk-24.11/build` (`.a` + `meson-uninstalled/libdpdk-uninstalled.pc`). Compile via `PKG_CONFIG_PATH`. EAL runs with `--no-pci` + virtual PMD `net_ring/net_null` because the laptop does not have a DPDK-bound NIC (WiFi `iwlwifi`, Killer `r8169`). Hugepages are already mounted (`/dev/hugepages`, ~896 pages free), so EAL can initialize. `MULTI_SEGS` + physical NIC configurations are documented in `deployment-guide` for production VHT servers.
2. **Slides/Video:** Markdown + demo script (no manual PPTX). Marp Markdown slides exported to PDF/HTML, demo = `run_demo.sh` with colored terminal output + live bench execution.
3. **Metrics:** Target metrics in plan1 converted into *design hypotheses*; finalized numbers will be gathered from actual runs in `bench/results.csv`. With `perf_event_paranoid=4`, the primary benchmark harness uses `__rdtscp` + `clock_gettime`, with `perf stat` as an auxiliary tool when sudo is available. `matplotlib` not currently installed → Phase 0 `pip install` or fallback to ASCII chart.
4. **Deadline:** Separate MVP registration form `docs/dang-ky-skyt.md` to submit to Mentor immediately from the full deliverable package due on October 18.

---

## 2. GIT WORKTREE + SUBAGENT + AUTO-REVIEW WORKFLOW (MANDATORY REQUIREMENT)

### 2.1. Worktree Lifecycle Diagram

```text
[ORCHESTRATOR] ──1. git worktree add .worktrees/<branch> -b <branch> main──▶ [WORKER SUBAGENT]
                                                                              │ Code + commit ONLY inside .worktrees/<branch>
                                                                              │ on its designated <branch>
                                                                              ▼
[ORCHESTRATOR] ◀──2. Worker reports DONE (lists files + commits)── [WORKER]
       │
       ▼ 3. Spawn independent REVIEW SUBAGENT (read-only, does NOT modify code)
[REVIEWER] ── build + ctest + checklist §5 ──▶ PASS / FAIL
       │
       ├── PASS ──▶ 4. Orchestrator: checkout main → merge --no-ff <branch>
       │            5. git branch -d <branch> (delete merged branch)
       │            6. git worktree remove --force .worktrees/<branch>
       │            7. git worktree prune
       │
       └── FAIL ──▶ Return error logs to worker, PRESERVE branch + worktree for fixing, FORBID merge/delete
```

### 2.2. Invariants (Violation results in Review FAIL)

1. **1 worktree = 1 separate branch**, directory `.worktrees/<branch-name>`. Never code directly on `main`, never code across worktrees.
2. **Worker only commits on its own branch, within its own worktree.** At the end of each WT, the worker must have at least 1 clean commit (`git status` in the worktree must be clean before requesting review).
3. **Worker must never self-merge and never delete branch/worktree.** Merge + cleanup is strictly performed by the orchestrator after the reviewer outputs PASS.
4. **Reviewer only verifies, never modifies code.** Reviewer can build/test/grep/inspect logs, but must not `edit/write/commit`.
5. **Delete if and only if:** reviewer concludes `PASS` + orchestrator merges `--no-ff` successfully. Deletion sequence: `merge → branch -d → worktree remove → prune`. On FAIL, keep the scene intact.
6. `.worktrees/` must be ignored (`build/`, `.worktrees/`, `*.o`, `*.csv`, `*.png` in `.gitignore`), while `third_party/*` remains ignored to avoid accidentally committing gigabytes of DPDK source code.

### 2.3. Naming Conventions (Mandatory for automated scripts)

| WT | Branch | Worktree Path |
|----|--------|---------------|
| WT0 infra | `infra/skeleton` | `.worktrees/infra-skeleton` (or done directly on `main` if repo has 0 commits) |
| WT1 mbuf | `feat/mbuf-mempool` | `.worktrees/feat-mbuf-mempool` |
| WT2 deepcopy | `feat/deepcopy-baseline` | `.worktrees/feat-deepcopy-baseline` |
| WT3 zerocopy | `feat/zerocopy-chain` | `.worktrees/feat-zerocopy-chain` |
| WT4 bench | `feat/bench-harness` | `.worktrees/feat-bench-harness` |
| WT5 dpdk | `feat/dpdk-adapter` | `.worktrees/feat-dpdk-adapter` |
| WT6 docs | `docs/deliverables-bundle` | `.worktrees/docs-deliverables-bundle` |

### 2.4. Standard Orchestrator Commands for all WTs

```bash
# Create (from clean main):
git checkout main && git pull --ff-only
git worktree add .worktrees/<branch> -b <branch> main

# After reviewer PASS:
git checkout main
git merge --no-ff <branch> -m "Merge <branch>: <summary> (reviewed PASS)"
git branch -d <branch>
git worktree remove --force .worktrees/<branch>
git worktree prune
git log --oneline -5 && git worktree list
```

### 2.5. Standard Prompts for Subagent Spawning (Copy-Paste)

**Worker Prompt Template:**
> You are the worker for `<branch>` in worktree `.worktrees/<branch>`. Work ONLY within that directory, commit ONLY on branch `<branch>`. Task: <WT description + files + acceptance criteria §4>. When done: build + test locally, `git add` appropriate files, `git commit`, then report DONE with list of files/commits. Do NOT merge, do NOT delete branch/worktree, do NOT touch `main` or other worktrees.

**Reviewer Prompt Template:**
> You are the independent reviewer for `<branch>` at `.worktrees/<branch>`. You must NOT modify code or commit. Please: (1) `cmake -B build && cmake --build build`, (2) `ctest --output-on-failure`, (3) execute checklist §5 corresponding to this WT, (4) verify `git status/log` of the branch. Return strictly one of two statuses: `PASS` (with suggested merge command) or `FAIL` (with specific error log + file:line). Do NOT merge, do NOT delete.

### 2.6. Parallelization Strategy

- WT1 → WT2 → WT3 have code dependencies (mbuf → deepcopy/zerocopy), so they run sequentially, yet each WT strictly follows the worker→review→merge→delete lifecycle.
- WT4 (bench) waits for WT2+WT3 to merge. WT5 (dpdk) and WT6 (docs skeleton) can run in parallel after WT3, each maintaining its own branch/worktree.
- Maximum 2 workers running in parallel to prevent CPU/i7 contention and merge conflicts into `main` (merges are always sequential).

---

## 3. TECHNICAL ARCHITECTURE (Inherited verbatim from plan1 — summarized so workers do not need to reopen old files)

- **High-fidelity `Mbuf`** (`src/mbuf_model.h`): `buf_addr, data_off (=128 headroom), data_len, pkt_len, nb_segs, _Atomic refcnt, *next, pool_id`. Fixed Ring Mempool O(1) with atomic `head/tail`.
- **Refcnt Lifecycle:** `P(refcnt 1→3)` when chaining 2 headers; `free(h1)` → `P:3→2` retained; `free(h2)` → `P:2→1` retained; `free(P)` → `1→0` reclaimed. No leaks/double-frees/use-after-frees.
- **50B Outer Header:** Eth 14B + IPv4 20B + UDP 8B (dport 2152) + GTP-U 8B (type 255). Path1 `192.168.10.101/0x10000001`, Path2 `192.168.20.102/0x20000002`. VLAN +4B = 54B still comfortably fits in a 64B cache line.
- **3-Way Architectural Comparison:** `prepend()` fails (single headroom cannot accommodate 2 distinct headers), `clone()` fails (shares buffer/headroom), only `chain + refcnt + NIC scatter-gather` achieves true zero-copy payload.
- **Dual-Harness Benchmark:** `__rdtscp` + `CLOCK_MONOTONIC_RAW`, `--mode hot` (L1) and `--mode stream` (65536 pkts ~55MB > L3 30MB triggering cache thrashing for deepcopy). `run_perf.sh` + `plot.py` as auxiliary tools.
- **Real DPDK:** `dpdk_chain.c` maps `rte_pktmbuf_chain/refcnt_update`, `USE_DPDK=ON` links against `libdpdk-uninstalled.pc`, EAL `--no-pci`, virtual PMD, deployment guide for UPF servers.

---

## 4. DETAILED WORKTREE BREAKDOWN (Each item = 1 branch + 1 worktree + 1 worker + 1 reviewer)

### WT0 — `infra/skeleton`
- **Branch/Worktree:** `infra/skeleton` / `.worktrees/infra-skeleton` (if `main` has 0 commits, perform directly on `main` without worktree).
- **Tasks:** Fix `.gitignore`, `CMakeLists.txt` C11 `-O3 -Wall -Wextra`, stubs for `src/mbuf_model.h` + `bench/bench.c`, `docs/dang-ky-skyt.md`, `scripts/worktree-runner.sh` + `scripts/review-merge.sh`, `pip install matplotlib` (or document fallback).
- **Completion Criteria:** `cmake -B build && cmake --build build` passes; clean `git status`; commit on `main`.

### WT1 — `feat/mbuf-mempool`
- **Tasks:** `src/mbuf_model.{h,c}`, `tests/test_chain.c`, `tests/test_lifecycle.c`, `mbuf_chain/refcnt_update/free` strictly conforming to §3.
- **Acceptance Criteria:** CTest 100% pass, 1M alloc/free without memory leaks, correct refcnt lifecycle.
- **Review complete PASS → merge → delete branch + worktree.**

### WT2 — `feat/deepcopy-baseline`
- **Tasks:** `src/outer_header.{h,c}`, `src/deepcopy.{h,c}`, `tests/test_header.c`. `duplicate_deepcopy()` allocates 2×(50B+800B) + memcpy, returns bytes copied.
- **Acceptance Criteria:** 2 memory-independent packets; mutating packet 1 does not affect packet 2.

### WT3 — `feat/zerocopy-chain` (Core)
- **Tasks:** `src/zerocopy.{h,c}`, `tests/test_zerocopy.c`. Header pool + `refcnt+2` + `chain(h1,P)`, `chain(h2,P)`.
- **Acceptance Criteria:** 0 byte payload copy (`grep memcpy zerocopy.c` shows header only), `P->refcnt==3`, `h1->next==P`, `h2->next==P`, safe sequence deallocation.

### WT4 — `feat/bench-harness`
- **Tasks:** `bench/bench.c` (rdtscp, Mpps, p50/p95/p99, hot/stream, `results.csv`), `bench/run_perf.sh`, `bench/plot.py` → `compare.png`.
- **Acceptance Criteria:** Real execution generates `results.csv` + `compare.png`, no hardcoded figures.

### WT5 — `feat/dpdk-adapter` (Real DPDK)
- **Tasks:** `src/dpdk-real/dpdk_chain.c`, `CMake -DUSE_DPDK=ON` using `PKG_CONFIG_PATH=third_party/dpdk-24.11/build/meson-uninstalled`, `docs/dpdk-deployment-guide.md` (hugepages, VFIO, `MULTI_SEGS` for VHT servers, noting laptop uses `--no-pci` + ring PMD).
- **Acceptance Criteria:** `USE_DPDK=ON` build passes, EAL init + chain/refcnt operational on virtual PMD.

### WT6 — `docs/deliverables-bundle`
- **Tasks:** `report/technical-report-vi.md`, `slides/slides-5p.md` (Marp), `demo/video-script-vi.md` + `demo/run_demo.sh`, `README.md` + `ARCHITECTURE.md`.
- **Acceptance Criteria:** Numbers in report/slides match `results.csv` 100%.

---

## 5. REVIEWER CHECKLIST (Executed for every WT, adding WT3/WT4 lines when applicable)

```bash
# 1. Clean Build (inside .worktrees/<branch>):
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --clean-first
# PASS: 0 errors, 0 warnings with -Wall -Wextra

# 2. Tests:
ctest --test-dir build --output-on-failure
# PASS: 100% passed

# 3. Zero-Copy Purity (WT3):
grep -rn "memcpy" src/zerocopy.c
# PASS: 50B header only, payload untouched

# 4. Memory Safety (WT1/WT2/WT3): 1M loop + correct refcnt free, no segfault/leak/double-free

# 5. Benchmark Integrity (WT4/WT6): results.csv generated from real run, numbers in docs/slides match CSV

# 6. Git Hygiene: correct branch name, clean commit inside worktree, no leftover build/csv/png artifacts, no main pollution
```

Reviewer may only return `PASS` if all applicable checks are green. A single red check = `FAIL`, preserve worktree, forbid merge/delete.

---

## 6. TIMELINE (Oct 16 – Oct 18)

| Time | Tasks | Subagent |
|------|-------|----------|
| Evening Oct 16 | WT0 skeleton + `dang-ky-skyt.md` submitted to Mentor | Direct orchestrator execution |
| Night Oct 16 – Morning Oct 17 | WT1 → WT2 → WT3 sequentially, each following worker→review→merge→delete | 1 worker + 1 reviewer per round |
| Afternoon Oct 17 | WT4 bench, collecting actual numbers on i7-13700HX | 1 worker + 1 reviewer |
| Night Oct 17 – Morning Oct 18 | WT5 + WT6 (can run in parallel), tag `v1.0-skyt` | 2 workers + reviewers, sequential merge |
