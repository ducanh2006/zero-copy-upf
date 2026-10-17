# Tối ưu hóa nhân bản gói tin 5G UPF bằng kỹ thuật Zero-Copy trên DPDK
> Đề tài Sáng kiến Công nghệ — Viettel Digital Talent 2026 (Lĩnh vực SE-HPC, Viettel High Tech)

Dự án ứng dụng kỹ thuật Chained-mbuf trên DPDK kết hợp tính năng Scatter-Gather của card mạng để nhân bản gói tin trong tính năng Redundant Transmission trên giao diện N3/N9 cho luồng 5G URLLC. Khối lượng dữ liệu CPU sao chép giảm từ **1700B xuống 100B (0B payload copy)**.

## Kết quả thực nghiệm (`bench/results.csv`, 100.000 chu kỳ đo, payload 800B)

| Chế độ | Phương pháp | Chu kỳ (cycles) | Độ trễ (ns) | Thông lượng (Mpps) | Trung vị p50 | Dữ liệu copy |
|---|---|---|---|---|---|---|
| hot (L1) | Deep-Copy | 1139.9 | 623.2 | 1.605 | 618 | 1700 B |
| hot (L1) | **Zero-Copy** | **411.0** | **268.2** | **3.728** | **318** | **100 B** |
| stream (L3) | Deep-Copy | 767.6 | 413.7 | 2.417 | 746 | 1700 B |
| stream (L3) | **Zero-Copy** | **585.8** | **388.8** | **2.572** | **322** | **100 B** |

- **Hot Cache:** Tốc độ tăng **2.77 lần**, thông lượng tăng từ 1.605 lên 3.728 Mpps.
- **Stream Mode (55MB > L3 30MB):** Độ trễ trung vị p50 giảm **2.32 lần** (từ 746 xuống 322 cycles), chống nghẽn bộ nhớ đệm hiệu quả.

## Hướng dẫn biên dịch và kiểm thử

```bash
# Biên dịch Release tối ưu C11 -O3
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --clean-first

# Chạy toàn bộ 4 bộ kiểm thử đơn vị
ctest --test-dir build --output-on-failure
```

## Chạy Benchmark và Demo 1-Click

```bash
# Chạy demo tự động: biên dịch, kiểm thử, xác thực độ thuần khiết và đo benchmark
bash demo/run_demo.sh

# Hoặc chạy đo kiểm thủ công
./build/bench_upf --mode hot --iters 100000 --payload 800
./build/bench_upf --mode stream --iters 100000 --payload 800
python3 bench/plot.py        # Xuất biểu đồ so sánh ra bench/compare.png
```

## Tái hiện lưu lượng 5G (PCAP Replay với DPDK)

```bash
# Nhân bản lưu lượng PCAP mẫu ra 2 file pcap riêng biệt cho 2 trạm gốc
bash scripts/run_pcap_dpdk.sh capture --packets 200

# Đo thông lượng tối đa chuyển tiếp với net_null
bash scripts/run_pcap_dpdk.sh perf --packets 100000

# Kiểm tra cấu trúc gói tin đầu ra
tcpdump -nn -r data/pcap/out_path1_gnb1.pcap -c 1
```

## Chạy với Docker

```bash
# Dựng và chạy đo kiểm tự động trong container
docker compose up --build bench
```

## Cấu trúc thư mục dự án

```text
src/mbuf_model.{h,c}    Mô hình mbuf, Ring Mempool O(1) phi khóa
src/outer_header.{h,c}  Đóng gói Outer Header 50B chuẩn N3/N9 (cổng 2152, GTP-U)
src/deepcopy.{h,c}      Phương pháp đối chứng sao chép toàn phần 1700B
src/zerocopy.{h,c}      Lõi nhân bản Chained-mbuf Zero-Copy 100B
src/dpdk-real/          Bộ điều hợp tương thích chuẩn DPDK (rte_pktmbuf_chain)
bench/                  Chương trình đo chu kỳ xung nhịp rdtscp và vẽ đồ thị
data/pcap/              Lưu lượng 5G mẫu và các gói tin nhân bản đầu ra
demo/                   Kịch bản thuyết minh và script chạy tự động demo
tests/                  Bộ kiểm thử đơn vị chuỗi mbuf, vòng đời refcnt, tiêu đề
```
