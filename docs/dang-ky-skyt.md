# ĐĂNG KÝ SÁNG KIẾN Ý TƯỞNG (SKYT) — VIETTEL DIGITAL TALENT 2026 (GIAI ĐOẠN 2)

> Nộp Mentor trước 15/10/2026; Ban ngành dọc trước 18/10/2026.

## 1. Thông tin chung

| Trường | Nội dung |
|---|---|
| **Tên sáng kiến** | Ứng dụng kỹ thuật Zero-Copy (Chained-mbuf) để nhân bản gói tin trong tính năng Redundant Transmission trên giao diện N3/N9 của 5G UPF |
| **Lĩnh vực — Chuyên ngành** | Software Engineering (SE) – High Performance Computing (HPC) / 5G Core |
| **Đơn vị** | Viettel High Tech (VHT) — TT CNCM — K2 — Phòng CNXLDL |
| **Địa điểm thực tập GĐ2** | VHT |
| **Mentor hướng dẫn** | Nguyễn Ngọc Dũng |
| **Email Mentor** | dungnn11@viettel.com.vn |
| **SĐT Mentor** | 0349 606 016 |
| **Mục tiêu URLLC** | Nhân bản gói tin N3/N9 cho luồng URLLC (độ tin cậy 99,999%, độ trễ < 1 ms) với 0 byte payload copy, giảm cache-miss, giữ throughput Mpps ổn định |

## 2. Đặt vấn đề

Trong kiến trúc 5G Core, UPF xử lý lưu lượng người dùng tốc độ hàng chục–hàng trăm Gbps.
Với dịch vụ URLLC, tính năng Redundant Transmission trên N3/N9 yêu cầu nhân bản mỗi gói
tin payload (~800 B) thành 2 luồng độc lập gửi qua 2 đường truyền tới gNB, mỗi luồng cần
một Outer Header riêng (Ethernet 14 B + IPv4 20 B + UDP 8 B + GTP-U 8 B = 50 B, TEID/IP khác nhau).

Phương pháp truyền thống (Deep-Copy) cấp phát 2 mbuf mới và `memcpy` toàn bộ header +
payload (1700 B cho 2 bản), buộc CPU kéo 800 B payload từ RAM lên cache, gây cache-thrashing,
tốn chu kỳ CPU và không đáp ứng SLA độ trễ siêu thấp của URLLC.

## 3. Giải pháp đề xuất (Tóm tắt ~200 từ)

Giải pháp sử dụng cơ chế Chained-mbuf và Scatter-Gather I/O của NIC hỗ trợ DPDK.
Thay vì sao chép payload, UPF cấp phát 2 header-mbuf nhỏ (50 B Outer Header cho Path 1/Path 2:
IP đích 192.168.10.101/192.168.20.102, TEID 0x10000001/0x20000002, UDP dport 2152, GTP-U type 255)
đã nằm sẵn trong L1 cache, tăng bộ đếm tham chiếu (refcnt) của mbuf payload gốc từ 1 lên 3,
rồi nối bằng con trỏ `next` (`rte_pktmbuf_chain` tương đương) thành 2 chuỗi 2-segment;
NIC tự ghép khi phát. `prepend()` bất khả thi vì một mbuf chỉ có một headroom duy nhất,
`clone()` bất khả thi vì hai bản clone chia sẻ chung buffer/headroom nên không thể ghi hai
outer header khác nhau; chỉ chained-mbuf đạt zero-copy payload thực sự. Mô phỏng C11
high-fidelity (`rte_mbuf` gồm buf_addr, data_off=128 headroom, data_len, pkt_len, nb_segs,
atomic refcnt, next, pool_id; Ring Mempool O(1)) kết hợp đo `__rdtscp` + `CLOCK_MONOTONIC_RAW`
ở hai chế độ hot (L1) và stream (55 MB > L3 30 MB) chứng minh CPU chỉ ghi 100 B header thay vì
1700 B, giảm mạnh cache-miss và chu kỳ lệnh, giữ độ trễ mức microsecond, đáp ứng chuẩn 3GPP
URLLC và sẵn sàng triển khai lên UPF server thật (hugepages, VFIO, MULTI_SEGS).

## 4. Hiệu quả kỳ vọng (giả thuyết thiết kế — số liệu thực đo ở `bench/results.csv`)

- Triệt tiêu chi phí copy payload: 0 byte payload copy (chỉ copy 2×50 B header).
- CPU chỉ chạm vùng header trong L1 cache; giảm cache-miss trong chế độ stream.
- Throughput (Mpps) và latency p50/p95/p99 cải thiện so với deep-copy; độ trễ ổn định mức µs.
- Ánh xạ 1-1 sang API DPDK thật (`rte_pktmbuf_chain`, `rte_pktmbuf_refcnt_update`,
  `RTE_ETH_TX_OFFLOAD_MULTI_SEGS`), kèm deployment guide cho server VHT.

## 5. Kế hoạch triển khai GĐ2 (16–18/10/2026)

- WT0: khung CMake C11 + form đăng ký (file này).
- WT1: Mbuf high-fidelity + Ring Mempool O(1) + refcnt lifecycle.
- WT2: Deep-copy baseline 50 B header + 800 B payload, 1700 B copy.
- WT3 (cốt lõi): Zero-copy chain, refcnt 1→3, header-only memcpy.
- WT4: Benchmark hot/stream (`__rdtscp`, Mpps, p50/p95/p99, `results.csv`, `compare.png`).
- WT5: Adapter DPDK thật (`USE_DPDK=ON`, EAL `--no-pci` + net_ring, deployment guide).
- WT6: Báo cáo kỹ thuật + Slide Marp 5 trang + video demo + README/ARCHITECTURE, tag `v1.0-skyt`.

## 6. Cam kết

Báo cáo SKYT được xây dựng từ dự án GĐ2 do thực tập sinh trực tiếp triển khai dưới sự
hướng dẫn của Mentor, có giải pháp kỹ thuật và kết quả đo lường rõ ràng (bench thực chạy
trên Intel i7-13700HX, Ubuntu 24.04, GCC 13.3.0, DPDK 24.11).
