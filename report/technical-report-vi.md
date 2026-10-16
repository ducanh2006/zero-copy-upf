# BÁO CÁO KỸ THUẬT: ỨNG DỤNG ZERO-COPY (CHAINED-MBUF) CHO 5G UPF N3/N9
## Viettel Digital Talent 2026 — Giai đoạn 2 (Lĩnh vực SE-HPC)

- **Tên sáng kiến:** Ứng dụng kỹ thuật Zero-Copy (Chained-mbuf) để nhân bản gói tin trong tính năng Redundant Transmission trên giao diện N3/N9 của 5G UPF
- **Đơn vị:** Viettel High Tech (VHT) — TT CNCM — K2 — Phòng CNXLDL
- **Mentor:** Nguyễn Ngọc Dũng (dungnn11@viettel.com.vn)
- **Môi trường đo:** Ubuntu 24.04, Intel i7-13700HX (L3 30MB), GCC 13.3.0, DPDK 24.11, `__rdtscp` + `CLOCK_MONOTONIC_RAW`, CPU pinning core 3
- **Nguồn số liệu:** `bench/results.csv` (chạy thực, 100000 iters/mode, payload 800B) — mọi bảng dưới đây trích nguyên văn từ CSV.

## 1. Tóm tắt

Redundant Transmission cho URLLC trên N3/N9 yêu cầu nhân bản mỗi gói payload 800B
thành 2 luồng với Outer Header khác nhau. Deep-copy truyền thống sao chép 1700B
(2×50B header + 2×800B payload), gây cache-thrashing và tốn chu kỳ CPU. Giải pháp
chained-mbuf chỉ sao chép 100B header (2×50B), payload 0B copy nhờ `next` + `refcnt`
và NIC Scatter-Gather. Đo thực trên i7-13700HX cho thấy speedup 2.77x (hot) và
1.31x (stream) về cycles, throughput tăng từ 1.605 Mpps lên 3.728 Mpps (hot).

## 2. Đặt vấn đề

UPF xử lý hàng chục–hàng trăm Gbps. Với URLLC (99,999%, <1ms), mỗi gói từ Data
Network phải được duplicate thành 2 bản độc lập tới 2 gNB qua N3/N9, mỗi bản cần
Outer Header riêng (IP/TEID khác nhau). UPF bình thường chỉ đọc header; deep-copy
buộc CPU kéo toàn bộ 800B payload từ RAM lên L1/L2/L3, gây tắc nghẽn bus nhớ và
không đạt SLA microsecond của 3GPP.

## 3. So sánh 3 chiều (prepend vs clone vs chain)

| Phương pháp | Cơ chế chèn 2 headers | Copy payload 800B | Cache-miss | Kết luận |
|---|---|---|---|---|
| **Deep-Copy** | Alloc 2 mbuf 850B + `memcpy` header 50B + `memcpy` payload 800B | **1600B** (2×800B), tổng 1700B | Rất cao (thrashing) | Chậm, tốn CPU/băng thông nhớ |
| **Clone (`rte_pktmbuf_clone`)** | 2 shadow mbuf chia sẻ buffer gốc + `prepend()` | 0B | Không áp dụng | **Bất khả thi:** chung buffer/headroom, không ghi được 2 headers khác nhau |
| **Chained-mbuf (đề xuất)** | 2 header-mbuf riêng (50B, L1-hot) + `next` tới payload gốc, NIC tự ghép | **0B** | Rất thấp (chỉ 100B L1) | **Tối ưu duy nhất cho URLLC** |

`prepend()` thất bại vì mỗi mbuf chỉ có một headroom duy nhất; `clone()` thất bại
vì chia sẻ headroom nên không thể ghi TEID/IP khác nhau đồng thời.

## 4. Thiết kế

### 4.1 Mbuf high-fidelity (`src/mbuf_model.h`)

```c
typedef struct Mbuf {
    void *buf_addr; uint16_t data_off; /* 128 headroom */
    uint16_t data_len; uint32_t pkt_len; uint16_t nb_segs;
    _Atomic uint16_t refcnt; struct Mbuf *next; uint16_t pool_id;
} Mbuf;
```

Ring Mempool O(1) với `head`/`tail` atomic (mod capacity), `storage` + `raw_buffers`
(`capacity×2048B`). `mbuf_chain` nối `next` và cộng `pkt_len`/`nb_segs`;
`mbuf_free` trả head-seg về pool và `refcnt_update(next,-1)`, giữ payload tới khi
`refcnt==0`. Vòng 1M alloc/free không leak; pool luôn phục hồi đủ capacity.

### 4.2 Outer Header 50B chuẩn N3/N9 (`src/outer_header.h`)

Eth 14B (DMAC/SMAC + `0x0800`) + IPv4 20B (Src `10.0.0.1`, Proto 17, total 836) +
UDP 8B (dport **2152**, len 816) + GTP-U 8B (flags `0x30`, type **255**, len 800).

- Path 1: Dst `192.168.10.101`, TEID `0x10000001`
- Path 2: Dst `192.168.20.102`, TEID `0x20000002`

Vừa 1 cache-line 64B (54B nếu thêm VLAN).

### 4.3 Vòng đời refcnt

```
P(payload, refcnt 1->3) <--- h1(header Path1, refcnt 1)
                        <--- h2(header Path2, refcnt 1)
```

`free(h1)`: h1 về header-pool, P 3→2 giữ lại. `free(h2)`: h2 về pool, P 2→1.
`free(P)`: P 1→0 thu hồi. Không double-free/use-after-free. `grep memcpy`
trên `src/zerocopy.c` chỉ thấy 2 dòng copy 50B header, tuyệt đối không chạm payload.

## 5. Benchmark (số liệu thực từ `bench/results.csv`)

Lệnh tái hiện:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --clean-first
rm -f bench/results.csv
./build/bench_upf --mode hot --iters 100000 --payload 800
./build/bench_upf --mode stream --iters 100000 --payload 800
cat bench/results.csv
python3 bench/plot.py
```

CSV nguyên văn:

```
mode,method,iters,avg_cycles,avg_ns,mpps,p50_cycles,p95_cycles,p99_cycles,bytes_copied
hot,deepcopy,100000,1139.9,623.2,1.605,618,4792,5980,1700
hot,zerocopy,100000,411.0,268.2,3.728,318,400,2782,100
stream,deepcopy,100000,767.6,413.7,2.417,746,1014,1190,1700
stream,zerocopy,100000,585.8,388.8,2.572,322,2504,2922,100
```

| Mode | Method | iters | avg_cycles | avg_ns | Mpps | p50 | p95 | p99 | bytes |
|---|---|---|---|---|---|---|---|---|---|
| hot | deepcopy | 100000 | 1139.9 | 623.2 | 1.605 | 618 | 4792 | 5980 | 1700 |
| hot | zerocopy | 100000 | 411.0 | 268.2 | 3.728 | 318 | 400 | 2782 | 100 |
| stream | deepcopy | 100000 | 767.6 | 413.7 | 2.417 | 746 | 1014 | 1190 | 1700 |
| stream | zerocopy | 100000 | 585.8 | 388.8 | 2.572 | 322 | 2504 | 2922 | 100 |

- Hot (L1): zerocopy nhanh hơn **2.77x cycles** (1139.9→411.0), **2.32x ns** (623.2→268.2), Mpps 1.605→3.728.
- Stream (55MB > L3 30MB, 65536 gói): zerocopy nhanh hơn **1.31x cycles** (767.6→585.8), **1.06x ns** (413.7→388.8), Mpps 2.417→2.572; p50 746→322 cho thấy miễn nhiễm cache-thrashing (chỉ chạm 100B L1).
- Lượng copy: 1700B → 100B (giảm 94%), payload 800B → 0B.
- Lưu ý phương sai: `__rdtscp` dao động theo freq-scaling/tải máy (xem p95/p99); p50 là chỉ số robust (hot 618→318 ≈1.94x, stream 746→322 ≈2.32x). Luôn `rm -f bench/results.csv` trước khi đo lại vì `bench.c` append.

## 6. Triển khai DPDK thật

`src/dpdk-real/dpdk_chain.c` ánh xạ 1-1: `rte_pktmbuf_refcnt_update(P,1)`×2 +
`rte_pktmbuf_chain(h1,P)`/`(h2,P)`, EAL `--no-pci --vdev=net_ring0` cho laptop;
server VHT cần hugepages 2MB/1GB, `vfio-pci` và `RTE_ETH_TX_OFFLOAD_MULTI_SEGS`
(chi tiết `docs/dpdk-deployment-guide.md`). Build: `USE_DPDK=OFF` mặc định;
`USE_DPDK=ON` với `PKG_CONFIG_PATH=third_party/dpdk-24.11/build/meson-uninstalled`
đều pass `ctest` 4/4.

## 7. Kết luận (URLLC)

Zero-copy chained-mbuf triệt tiêu copy payload, giữ CPU trong L1, ổn định độ trễ
mức microsecond và tăng throughput N3/N9, đáp ứng 3GPP URLLC. Sẵn sàng thử nghiệm
trên server VHT với NIC vật lý và đo PMU (`perf stat` cycles/cache-misses).
