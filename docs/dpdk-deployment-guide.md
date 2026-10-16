# DPDK Deployment Guide — Zero-Copy Chained-mbuf UPF (N3/N9)
# Hướng dẫn triển khai DPDK — UPF Zero-Copy Chained-mbuf (N3/N9)

This guide maps the high-fidelity C11 model (`src/mbuf_model.*`, `src/zerocopy.*`)
1-1 to real DPDK 24.11 APIs and covers bring-up on both laptops (no physical NIC)
and Viettel High Tech UPF servers (physical NIC + `MULTI_SEGS`).

Tài liệu này ánh xạ mô hình C11 (`src/mbuf_model.*`, `src/zerocopy.*`) sang API DPDK
24.11 thật theo tỉ lệ 1-1, và hướng dẫn bring-up trên cả laptop (không có NIC vật lý)
lẫn server UPF của Viettel High Tech (NIC vật lý + `MULTI_SEGS`).

## 1. API mapping (model -> real DPDK)

| Model (C11) | Real DPDK 24.11 | Notes |
|---|---|---|
| `mbuf_alloc()` | `rte_pktmbuf_alloc(pool)` | Header pool (64B) + payload pool (800B+) |
| `mbuf_refcnt_update(P,+2)` | `rte_pktmbuf_refcnt_update(P,1)` x2 | 1 -> 3 shared ownership |
| `mbuf_chain(h,P)` | `rte_pktmbuf_chain(h,P)` | Appends P, updates `pkt_len`/`nb_segs` |
| `mbuf_free(h)` | `rte_pktmbuf_free(h)` | Returns head seg, decrements chained refcnt |
| `build_outer_header()` | Fill `rte_ether/ipv4/udp/gtpu` structs | 50B: Eth14 + IPv4 20 + UDP8 (dport 2152) + GTP-U8 (type 255) |

See `src/dpdk-real/dpdk_chain.c:dpdk_zerocopy_duplicate()` for the exact wrapper.

## 2. EAL bring-up

### 2.1 Laptop (no DPDK-bound NIC, iwlwifi/r8169) — what we used

```bash
# No hugepage reservation strictly needed for --no-pci + ring/null PMDs,
# but EAL still prefers hugetlbfs if mounted (/dev/hugepages).
./build-dpdk/bench_upf --no-pci --vdev=net_ring0 --vdev=net_null0
# In code: dpdk_eal_init_no_pci() uses exactly these args.
```

Build:

```bash
export PKG_CONFIG_PATH=$PWD/third_party/dpdk-24.11/build/meson-uninstalled:$PKG_CONFIG_PATH
cmake -B build-dpdk -DUSE_DPDK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-dpdk --clean-first
ctest --test-dir build-dpdk --output-on-failure
```

### 2.2 VHT UPF server (physical NIC, production)

```bash
# 1) Hugepages: 2MB x 1024 (or 1GB x 4 for 100G NICs)
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
sudo mkdir -p /dev/hugepages && sudo mount -t hugetlbfs nodev /dev/hugepages

# 2) Bind NIC to vfio-pci (example 0000:5e:00.0)
sudo modprobe vfio-pci
sudo dpdk-devbind.py --bind=vfio-pci 0000:5e:00.0

# 3) Run with required offload for chained TX:
#    RTE_ETH_TX_OFFLOAD_MULTI_SEGS must be set in rte_eth_conf.txmode.offloads,
#    otherwise multi-segment TX (our 2-seg chain) is dropped.
./upf_app -l 0-7 -n 4 -- -p 0x1 --tx-offloads=0x8000
```

`RTE_ETH_TX_OFFLOAD_MULTI_SEGS` — bắt buộc cho TX chained-mbuf nhiều segment;
nếu thiếu, NIC PMD sẽ từ chối gói 2-segment (header + payload).

## 3. Laptop vs VHT server

| Item | Laptop (dev/bench) | VHT server (prod) |
|---|---|---|
| NIC | WiFi `iwlwifi` / Killer `r8169` (no DPDK) | Mellanox/Intel 25G/100G bound to `vfio-pci` |
| EAL args | `--no-pci --vdev=net_ring0 --vdev=net_null0` | `-l 0-7 -n 4 -a 0000:5e:00.0` |
| PMD | `net_ring` / `net_null` (functional verify) | `mlx5` / `ice` / `i40e` (line-rate) |
| Hugepages | Optional (hugetlbfs if present) | Mandatory: 2MB x1024 or 1GB x4 |
| TX offload | Ignored (virtual) | `RTE_ETH_TX_OFFLOAD_MULTI_SEGS` required |
| Measurement | `__rdtscp` + `CLOCK_MONOTONIC_RAW` (no root) | Same + `perf stat` PMU (cycles, cache-misses) |

## 4. Troubleshooting

- `EAL: No available hugepages`: create hugetlbfs or run with `--no-huge` (functional only, not perf).
- `TX dropped, nb_segs>1`: enable `MULTI_SEGS` (see §2.2).
- `rte_pktmbuf_chain: segs already chained`: ensure header mbufs are fresh (`refcnt==1`, `next==NULL`).
- `perf_event_paranoid=4`: primary bench needs no root (`__rdtscp`); use `run_perf.sh` with sudo only for PMU counters.
