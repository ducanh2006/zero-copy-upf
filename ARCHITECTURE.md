# ARCHITECTURE — Zero-Copy Chained-mbuf UPF

## Components

```
Server payload (800B, P refcnt 1)
        |
  duplicate_zerocopy() [src/zerocopy.c]
   |- alloc h1,h2 from header_pool (50B each, L1-hot)
   |- memcpy ONLY 50B outer header (never payload)
   |- refcnt_update(P,+2) => 1->3
   |- chain(h1,P), chain(h2,P) => 2-seg packets, pkt_len 850, nb_segs 2
        |
NIC Scatter-Gather TX (2 segs as 1 packet)
        |
  mbuf_free(h1) -> P 3->2 | mbuf_free(h2) -> P 2->1 | mbuf_free(P) -> 0
```

Baseline `duplicate_deepcopy()` [src/deepcopy.c] instead allocs 2×850B and
`memcpy`s header + payload (1700B), producing independent packets.

## Memory

- `Mbuf`: `buf_addr/data_off(128)/data_len/pkt_len/nb_segs/atomic refcnt/next/pool_id`
- `Mempool`: `storage/raw_buffers(cap×2048)/capacity/atomic head/tail` + per-pool
  ring FIFO (`g_rings[pool_id]`, pre-filled `0..cap-1`, `tail-head` = free count).
- Pools: `0` payload, `1` header. `mbuf_prepend` checks `data_off>=len`.

## Headers

`OuterHeader` 50B packed: Eth14 + IPv4 20 (10.0.0.1→192.168.10.101/.20.102,
total 836, proto 17) + UDP8 (sport hash, dport 2152, len 816) + GTP-U8
(0x30/255/800/TEID 0x10000001/0x20000002). Fits one 64B cache line.

## Benchmark

`bench/bench.c`: pinned CPU3, `__rdtscp`+`mfence` per duplicate (frees excluded),
wall `CLOCK_MONOTONIC_RAW` total, p50/p95/p99 via `qsort`. Hot = 1 payload reused
(L1); stream = 65536 ring (~55MB > L3 30MB). CSV:
`mode,method,iters,avg_cycles,avg_ns,mpps,p50,p95,p99,bytes_copied`.
Current live numbers (100000 iters):

- hot deepcopy 1139.9cyc/623.2ns/1.605Mpps/p50 618/1700B
- hot zerocopy 411.0cyc/268.2ns/3.728Mpps/p50 318/100B
- stream deepcopy 767.6cyc/413.7ns/2.417Mpps/p50 746/1700B
- stream zerocopy 585.8cyc/388.8ns/2.572Mpps/p50 322/100B

## DPDK mapping

`src/dpdk-real/dpdk_chain.c`: `rte_pktmbuf_refcnt_update(P,1)`×2 +
`rte_pktmbuf_chain()`, `rte_eal_init(--no-pci,--vdev=net_ring0)`.
Port requires `RTE_ETH_TX_OFFLOAD_MULTI_SEGS` for 2-seg TX. `USE_DPDK=OFF`
uses the C11 model (same lifecycle); `ON` links `libdpdk` via pkg-config.

## Testing

`test_chain` (next/pkt_len/nb_segs + prepend + exhaustion),
`test_lifecycle` (1→3→2→1→0 + 1M loop),
`test_header` (50B layout + 1700B independence),
`test_zerocopy` (refcnt 3, shared pointer, header divergence, safe free).
All must pass under `-O3 -Wall -Wextra` Release with zero warnings.
