---
marp: true
theme: default
paginate: true
---

# Zero-Copy Chained-mbuf cho 5G UPF N3/N9
## Viettel Digital Talent 2026 — SE/HPC — VHT CNXLDL — Mentor Nguyễn Ngọc Dũng
URLLC Redundant Transmission: 1700B copy → 100B copy, 0B payload

---

## 1. Bài toán: nhân bản URLLC trên N3/N9

- UPF chuyển tiếp hàng chục Gbps; URLLC yêu cầu duplicate mỗi gói 800B thành **2 luồng độc lập** tới 2 gNB (99,999%, <1ms)
- Mỗi luồng cần **Outer Header riêng 50B**: Eth14 + IPv4 20 (10.0.0.1 → 192.168.10.101 / .20.102) + UDP8 (dport 2152) + GTP-U8 (type 255, TEID 0x10000001 / 0x20000002)
- Deep-copy: alloc 2×850B + `memcpy` 1700B → CPU kéo 800B từ RAM → **cache-thrashing**, lỡ SLA

---

## 2. Giải pháp: chained-mbuf + refcnt + NIC Scatter-Gather

```text
        P(payload 800B, refcnt 1->3)
         ^                     ^
   next /                       \ next
h1(header 50B Path1)     h2(header 50B Path2)
refcnt 1, TEID ...01      refcnt 1, TEID ...02
```

- Cấp 2 header-mbuf L1-hot, `memcpy` **chỉ 50B** mỗi bản, `refcnt_update(P,+2)`, `chain(h,P)`
- `prepend` bất khả thi (1 headroom), `clone` bất khả thi (chung buffer) → chỉ chain đạt zero-copy
- `mbuf_free(h1)`: P 3→2 giữ; `free(h2)`: P 2→1; `free(P)`: 1→0 thu hồi

---

## 3. Header 50B + vòng đời refcnt

- Layout: `DMAC/SMAC/0x0800 | IPv4 836B/proto17 | UDP 816/dport2152 | GTP-U 0x30/255/800/TEID`
- Ring Mempool O(1): `head/tail` atomic, `storage` + `raw 2048B`, 1M alloc/free không leak
- Purity: `grep memcpy src/zerocopy.c` → chỉ 2 dòng `OUTER_HEADER_LEN`, không chạm payload

---

## 4. Kết quả thực đo (`bench/results.csv`, 100000 iters, 800B)

| Mode | Method | avg_cycles | avg_ns | Mpps | p50 | bytes |
|---|---|---|---|---|---|---|
| hot | deepcopy | 784.5 | 423.5 | 2.361 | 664 | 1700 |
| hot | zerocopy | 429.6 | 279.9 | 3.573 | 328 | 100 |
| stream | deepcopy | 788.6 | 423.5 | 2.361 | 768 | 1700 |
| stream | zerocopy | 433.8 | 281.5 | 3.552 | 336 | 100 |

- Speedup **1.83x hot / 1.82x stream** (cycles); stream chứng minh miễn nhiễm thrashing (55MB > L3 30MB)
- Tiếp theo: thử NIC vật lý VHT + `MULTI_SEGS` + `perf` PMU, microsecond URLLC ổn định
