# KỊCH BẢN VIDEO DEMO 2–3 PHÚT (VI) — ZERO-COPY CHAINED-MBUF UPF

Tổng thời lượng: ~2:30. Quay terminal thực chạy `demo/run_demo.sh` + `bench/compare` ASCII.

## 0:00–0:20 — Mở đầu & bài toán
> "Chào Ban giám khảo, em trình bày sáng kiến Zero-Copy Chained-mbuf cho Redundant
> Transmission N3/N9. Với URLLC, mỗi gói 800 byte phải nhân bản thành hai luồng tới
> hai trạm gốc, mỗi luồng một Outer Header 50 byte khác nhau. Deep-copy phải sao chép
> tới 1700 byte, gây nghẽn cache và lỡ SLA dưới một mili-giây."

(Hiển thị `docs/plan/plan1.md` §3.3 layout 50B trên màn hình.)

## 0:20–1:00 — Giải pháp chain
> "Thay vì copy payload, chúng em cấp hai header-mbuf nằm sẵn trong L1 cache, tăng
> refcnt payload từ một lên ba rồi nối bằng con trỏ next. Card mạng tự ghép khi phát.
> Prepend thất bại vì một headroom duy nhất, clone thất bại vì chung buffer — chỉ
> chained-mbuf đạt zero-copy thực sự. Lệnh grep memcpy dưới đây chứng minh chỉ copy
> header 50 byte, không chạm payload."

(Chạy: `grep -rn memcpy src/zerocopy.c`, `ctest`.)

## 1:00–2:00 — Benchmark thực
> "Bộ đo dùng rdtscp cộng mfence và clock monotonic, ghim CPU3, hai chế độ hot L1 và
> stream 65536 gói khoảng 55 megabyte vượt L3 30 megabyte. Kết quả thực đo một trăm
> nghìn vòng, payload tám trăm byte: hot deepcopy 784.5 cycles, 423.5 ns, 2.361 Mpps
> so với zerocopy 429.6 cycles, 279.9 ns, 3.573 Mpps; stream deepcopy 788.6 cycles,
> 423.5 ns, 2.361 Mpps so với zerocopy 433.8 cycles, 281.5 ns, 3.552 Mpps. Tốc độ
> tăng 1.83 lần hot và 1.82 lần stream, lượng copy giảm từ 1700 xuống 100 byte."

(Chạy: `./build/bench_upf --mode hot/stream`, `cat bench/results.csv`.)

## 2:00–2:30 — Triển khai & kết luận
> "Adapter DPDK thật ánh xạ một-một sang rte_pktmbuf_chain và refcnt_update, EAL
> no-pci cộng net_ring cho laptop, server VHT cần hugepages và MULTI_SEGS. Giải pháp
> giữ độ trễ mức microsecond cho URLLC và sẵn sàng thử tải line-rate. Em xin cảm ơn!"

(Hiển thị `docs/dpdk-deployment-guide.md` + `git log --oneline`.)
