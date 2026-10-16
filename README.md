# SKYT UPF Zero-Copy (Chained-mbuf) — 5G N3/N9 Redundant Transmission

High-fidelity C11 model + `__rdtscp` benchmark proving **1700B → 100B copy (0B payload)**
for URLLC duplicate. Maps 1-1 to real DPDK 24.11 (`rte_pktmbuf_chain`).

Measured (100000 iters, 800B payload, `bench/results.csv`):

| Mode | Method | avg_cycles | avg_ns | Mpps | p50 | bytes |
|---|---|---|---|---|---|---|
| hot | deepcopy | 784.5 | 423.5 | 2.361 | 664 | 1700 |
| hot | zerocopy | 429.6 | 279.9 | 3.573 | 328 | 100 |
| stream | deepcopy | 788.6 | 423.5 | 2.361 | 768 | 1700 |
| stream | zerocopy | 433.8 | 281.5 | 3.552 | 336 | 100 |

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --clean-first
ctest --test-dir build --output-on-failure
```

## Bench

```bash
./build/bench_upf --mode hot --iters 100000 --payload 800
./build/bench_upf --mode stream --iters 100000 --payload 800
cat bench/results.csv
python3 bench/plot.py        # matplotlib or ASCII fallback
bash bench/run_perf.sh       # + perf stat PMU when available
bash demo/run_demo.sh        # 1-click live demo
```

## DPDK ON/OFF

```bash
# OFF (default, no DPDK needed):
cmake -B build && cmake --build build && ctest --test-dir build

# ON (compile-check vs real DPDK 24.11):
export PKG_CONFIG_PATH=$PWD/third_party/dpdk-24.11/build/meson-uninstalled:$PKG_CONFIG_PATH
cmake -B build-dpdk -DUSE_DPDK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-dpdk --clean-first
ctest --test-dir build-dpdk --output-on-failure
```

Laptop EAL: `--no-pci --vdev=net_ring0`; VHT servers need hugepages + `vfio-pci` +
`RTE_ETH_TX_OFFLOAD_MULTI_SEGS` (see `docs/dpdk-deployment-guide.md`).

## Worktree workflow

One worktree = one branch under `.worktrees/<branch>` (see `plan2.md` §2).
Never code on `master` except WT0. Worker commits only on its branch; never
self-merges. Merge + delete only by orchestrator after review PASS:

```bash
./scripts/worktree-runner.sh create feat/<name>
# ... work, commit inside .worktrees/feat-<name> ...
./scripts/review-merge.sh feat/<name>
# if PASS:
git checkout master && git merge --no-ff feat/<name>
git worktree remove --force .worktrees/feat-<name>
git branch -d feat/<name> && git worktree prune
```

## Repo tree

```
src/mbuf_model.{h,c}  ring mempool O(1), chain/refcnt
src/outer_header.{h,c} 50B N3/N9 header (dport 2152, GTP type 255)
src/deepcopy.{h,c}     baseline 1700B memcpy
src/zerocopy.{h,c}     core 100B header-only chain
src/dpdk-real/         real rte_chain wrapper (USE_DPDK)
bench/bench.c          rdtscp hot/stream harness -> results.csv
bench/run_perf.sh bench/plot.py
tests/                 chain/lifecycle/header/zerocopy
report/technical-report-vi.md slides/slides-5p.md
demo/video-script-vi.md demo/run_demo.sh
docs/dang-ky-skyt.md docs/dpdk-deployment-guide.md
```
