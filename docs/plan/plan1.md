# KẾ HOẠCH CHI TIẾT (V3.0 HOÀN CHỈNH): ZERO-COPY CHAINED-MBUF CHO 5G UPF N3/N9
### Sáng kiến Ý tưởng / Sáng kiến Công nghệ – Viettel Digital Talent 2026 (Giai đoạn 2)
**Lĩnh vực:** Software Engineering (SE) – High Performance Computing (HPC)  
**Đơn vị:** Viettel High Tech (VHT) – TT CNCM – K2 – Phòng CNXLDL  
**Mentor hướng dẫn:** Nguyễn Ngọc Dũng (`dungnn11@viettel.com.vn`)  
**Môi trường thực tế:** Ubuntu 24.04 LTS, Intel Core i7-13700HX (16 cores/24 threads, L3 Cache 30MB), GCC 13.3.0

---

## 🧭 1. TỔNG KẾT RÀ SOÁT & ĐIỀU CHỈNH CỐT LÕI (GIẢI QUYẾT 7 ĐIỂM CRITIQUE)

Bảng đối chiếu và giải pháp khắc phục triệt để các rủi ro P0/P1 trước khi triển khai:

| Vấn đề rà soát (Critique) | Mức độ | Hiện trạng thực tế kiểm tra | Giải pháp chuẩn hóa trong Plan v3.0 |
| :--- | :---: | :--- | :--- |
| **1. Repo rỗng + `.gitignore` sai** | **P0** | `docs/*` đang bị ignore, mọi file `idea.md`, `requirement.md`, `plan1.md` đều untracked. | **Phase 0 làm trước:** Cập nhật `.gitignore` (chỉ ignore `build/`, `.worktrees/`, `*.o`, `*.csv`, `*.png`, giữ `third_party/*`), khởi tạo commit đầu tiên trên `main` tracking toàn bộ tài liệu. |
| **2. Ảo tưởng DPDK có sẵn** | **P0** | Không có NIC vật lý hỗ trợ DPDK trên laptop; `third_party/dpdk-24.11` là source + build uninstalled, thiếu quyền root cho hugepages. | **Chốt chiến lược:** Trọng tâm thực nghiệm là **bộ mô phỏng C11 High-Fidelity + Ring Mempool O(1) + Hardware TSC Timing**. Tầng `dpdk-real/` đóng vai trò **compile-check / API mapping** (`rte_pktmbuf_chain`, `rte_pktmbuf_refcnt_update`, `RTE_ETH_TX_OFFLOAD_MULTI_SEGS`) và viết **Deployment Guide** cho UPF server thật. |
| **3. Mô hình Mbuf thiếu fidelity** | **P1** | Plan cũ thiếu `buf_addr, data_off, nb_segs`, refcnt chưa rõ ràng theo per-mbuf. | Bổ sung đầy đủ trường của `rte_mbuf` thật (`buf_addr`, `data_off = 128B headroom`, `data_len`, `pkt_len`, `nb_segs`, `refcnt`). Làm rõ quyền sở hữu: `orig_payload` tăng `refcnt` lên 3, mỗi header mbuf có `refcnt=1` riêng. |
| **4. Chưa chốt `chain` vs `clone/attach`** | **P1** | Thiếu so sánh vì sao không dùng `rte_pktmbuf_clone()` hay `prepend()`. | **Bổ sung luận điểm 3-chiều:** `prepend()` thất bại vì mbuf chỉ có 1 headroom; `clone()` thất bại vì chia sẻ chung headroom, không thể ghi 2 outer headers khác nhau. **Chained-mbuf là giải pháp Zero-Copy duy nhất khả thi cho Redundant Transmission.** |
| **5. Thiếu spec Outer Header** | **P1** | Ước lượng 64B chung chung, chưa có byte layout. | **Khóa layout chuẩn 3GPP N3/N9:** Eth (14B) + IPv4 (20B) + UDP (8B, Port 2152) + GTP-U (8B, Msg=255) = **50 Bytes** (hoặc **54 Bytes** nếu kèm GTP-U Extension/VLAN). Định nghĩa rõ IP & TEID Path 1 vs Path 2. |
| **6. Số liệu hứa trước + L3/PMU** | **P1** | `lscpu` thực tế là **L3 30MB** (không phải 32MB); `perf_event_paranoid = 4` chặn PMU nếu không có root. | Chuyển cam kết thành **"Giả thuyết thiết kế (Design Hypothesis)"**. Xây dựng **Dual-Harness Benchmark:** Đo chính xác chu kỳ bằng `__rdtscp` + `mfence` và `clock_gettime(CLOCK_MONOTONIC_RAW)` chạy không cần root; script `run_perf.sh` phụ trợ khi có quyền sudo. |
| **7. Deadline & Tooling hồ sơ** | **P1** | Hạn nộp đăng ký là ngay lập tức (15/10 đã qua, 18/10 nộp Ban ngành); thiếu công cụ xuất Slide/Demo. | Tách ngay **`docs/dang-ky-skyt.md`** hoàn thiện ở Phase 0 để nộp Mentor Dũng. Slide dùng chuẩn **Marp Markdown** xuất trực tiếp sang HTML/PDF/PPTX; demo script tự động in màu chuyên nghiệp. |

---

## ⚡ 2. CƠ CHẾ SUBAGENT + GIT WORKTREE + AUTO-REVIEW MERGE

Để tối đa hóa tốc độ phát triển và tính độc lập của từng module, toàn bộ dự án được triển khai bằng cơ chế **Git Worktree + Subagent Độc Lập**:

```text
                                [ ORCHESTRATOR AGENT ]
                                          │
                     1. Tạo worktree: git worktree add .worktrees/<branch> -b <branch> main
                                          │
                                          ▼
                                 [ CODE SUBAGENT ]
               (Làm việc và commit CHỈ trong thư mục .worktrees/<branch>)
                                          │
                        2. Báo cáo hoàn thành tác vụ
                                          │
                                          ▼
                                [ REVIEW SUBAGENT ]
                 (Spawn độc lập, chỉ đọc + build + chạy verify checklist)
                                          │
                           ┌──────────────┴──────────────┐
                        [ PASS ]                      [ FAIL ]
                           │                             │
            3. Merge vào main (--no-ff)          Trả về Code Subagent
            4. Xóa branch (git branch -d)        kèm log lỗi chi tiết
            5. Xóa worktree (.worktrees/...)     (Giữ nguyên worktree)
            6. git worktree prune
```

### Quy tắc bất biến (Invariants):
1. Mỗi tác vụ là **1 nhánh riêng** và **1 thư mục worktree riêng** nằm trong `.worktrees/<ten-nhanh>`.
2. Code Subagent **tuyệt đối không tự merge** vào `main`.
3. Review Subagent **chỉ kiểm tra tính đúng đắn**, nếu đạt yêu cầu (PASS) thì bàn giao Orchestrator merge và dọn sạch worktree ngay lập tức để giữ repo luôn tinh gọn.

---

## 🏛️ 3. KIẾN TRÚC KỸ THUẬT & ĐẶC TẢ CHI TIẾT (SPECIFICATIONS)

### 3.1. Đặc tả Cấu trúc `Mbuf` High-Fidelity (`src/mbuf_model.h`)

Mô phỏng chân thực kiến trúc của `rte_mbuf` trong DPDK:

```c
#ifndef MBUF_MODEL_H
#define MBUF_MODEL_H

#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>

#define RTE_PKTMBUF_HEADROOM 128
#define MBUF_DEFAULT_BUF_SIZE 2048

typedef struct Mbuf {
    void *buf_addr;              // Con trỏ tới vùng đệm vật lý (buffer raw)
    uint16_t data_off;          // Headroom offset (mặc định RTE_PKTMBUF_HEADROOM)
    uint16_t data_len;          // Độ dài dữ liệu có hiệu lực trong segment này
    uint32_t pkt_len;           // Tổng độ dài toàn bộ gói tin (toàn chuỗi segments)
    uint16_t nb_segs;           // Số lượng segments cấu thành gói tin
    _Atomic uint16_t refcnt;    // Reference count độc lập của chính mbuf này
    struct Mbuf *next;          // Con trỏ trỏ tới segment tiếp theo (NULL nếu là segment cuối)
    uint16_t pool_id;           // Phân loại: 0 = Payload Pool, 1 = Header Pool
} Mbuf;

// Ring Mempool tĩnh O(1)
typedef struct Mempool {
    Mbuf *storage;              // Mảng tĩnh chứa các mbuf
    uint8_t *raw_buffers;       // Mảng bộ đệm dữ liệu liên tục
    uint32_t capacity;          // Sức chứa tối đa
    _Atomic uint32_t head;      // Vị trí lấy mbuf (alloc)
    _Atomic uint32_t tail;      // Vị trí trả mbuf (free)
} Mempool;

Mempool* mempool_create(uint32_t capacity, uint16_t pool_id);
void     mempool_destroy(Mempool *mp);

Mbuf*    mbuf_alloc(Mempool *mp);
void     mbuf_free(Mbuf *m);
void     mbuf_refcnt_update(Mbuf *m, int16_t delta);
int      mbuf_chain(Mbuf *head, Mbuf *tail); // Ghép tail vào cuối chuỗi head
uint8_t* mbuf_prepend(Mbuf *m, uint16_t len); // Thêm header nếu đủ headroom

#endif
```

---

### 3.2. Sơ đồ Quyền sở hữu và Vòng đời Bộ đếm Tham chiếu (Refcnt Lifecycle)

Khi một gói tin từ Server (`orig_payload`, `refcnt = 1`) cần nhân bản thành 2 luồng URLLC gửi tới 2 trạm gốc:

```text
                                [ Server Data Payload ]
                                (Mbuf P, refcnt: 1 -> 3)
                                          ▲   ▲
                       ┌──────────────────┘   └──────────────────┐
        chain (next)   │                                         │ chain (next)
             ┌─────────┴─────────┐                     ┌─────────┴─────────┐
             │   Header Mbuf 1   │                     │   Header Mbuf 2   │
             │     (Path 1)      │                     │     (Path 2)      │
             │   refcnt: 1       │                     │   refcnt: 1       │
             │ TEID: 0x10000001  │                     │ TEID: 0x20000002  │
             │ IP: 192.168.10.101│                     │ IP: 192.168.20.102│
             └───────────────────┘                     └───────────────────┘
```

**Quy tắc giải phóng chuỗi (`mbuf_free`):**
1. Khi NIC phát xong Path 1 $\rightarrow$ gọi `mbuf_free(h1)`:
   * Segment đầu (`h1`) được trả về `Header Pool`.
   * Duyệt theo `next` tới `P` $\rightarrow$ gọi `mbuf_refcnt_update(P, -1)`. Refcnt của `P` giảm từ 3 xuống 2. Vì `refcnt > 0`, **`P` không bị thu hồi**.
2. Khi NIC phát xong Path 2 $\rightarrow$ gọi `mbuf_free(h2)`:
   * Segment đầu (`h2`) được trả về `Header Pool`.
   * Duyệt theo `next` tới `P` $\rightarrow$ gọi `mbuf_refcnt_update(P, -1)`. Refcnt của `P` giảm từ 2 xuống 1.
3. Khi luồng xử lý gốc hoàn tất $\rightarrow$ gọi `mbuf_free(P)`:
   * Refcnt của `P` giảm từ 1 về 0 $\rightarrow$ `P` được thu hồi về `Payload Pool`.
   * **Bảo đảm 100% không rò rỉ bộ nhớ, không double free, không use-after-free.**

---

### 3.3. Đặc tả Layout Outer Header 3GPP Chuẩn N3/N9

Định dạng gói tin Outer Layer chèn vào trước payload 800 bytes:

```text
+-----------------------+-------------------------+--------------------+------------------------+
| Ethernet II (14B)     | IPv4 Header (20B)       | UDP Header (8B)    | GTP-U Header (8B)      |
| DMAC(6) | SMAC(6) |   | Src: 10.0.0.1           | SPort: FlowHash    | Ver:1, PT:1, Type:255  |
| Type: 0x0800 (2B)     | Dst: IP_gNodeB          | DPort: 2152        | Length: 800 + Ext(0)   |
|                       | Proto: 17 (UDP)         | Len: 816B          | TEID: 4 Bytes          |
+-----------------------+-------------------------+--------------------+------------------------+
|<--------------------------------- TỔNG: 50 BYTES -------------------------------------------->|
```

* **Path 1 (Đường 1):** Dst IP = `192.168.10.101`, TEID = `0x10000001`.
* **Path 2 (Đường 2):** Dst IP = `192.168.20.102`, TEID = `0x20000002`.
* *Tùy chọn:* Nếu có 802.1Q VLAN Tag (4B), tổng là **54 Bytes** (vẫn nằm trọn trong 1 Cache Line 64B).

---

### 3.4. Luận điểm Kiến trúc 3-Chiều (Bảo vệ trước Hội đồng HPC)

| Phương pháp | Cơ chế chèn Header Path 1 & Path 2 | Chi phí sao chép Payload (800B) | Hiện tượng Cache Miss (L1/L3) | Đánh giá tính khả thi |
| :--- | :--- | :---: | :---: | :--- |
| **1. Deep-Copy (Truyền thống)** | `alloc` 2 mbuf mới (864B) + `memcpy` Header (64B) + `memcpy` Payload (800B). | **1600 Bytes** (2 x 800B) | **Rất cao (Cache Thrashing):** CPU phải nạp 800B từ RAM lên Cache. | Chậm, lãng phí chu kỳ CPU và băng thông bộ nhớ. |
| **2. Indirect / Clone (`rte_pktmbuf_clone`)** | Tạo 2 shadow mbuf trỏ vào buffer gốc. Dùng `prepend()` để thêm outer header. | 0 Byte | Không áp dụng được | **BẤT KHẢ THI:** Cả 2 bản clone cùng chia sẻ chung một vùng đệm và chung một headroom; không thể ghi 2 outer headers khác nhau cùng lúc. |
| **3. Chained-mbuf (Giải pháp đề xuất)** | Cấp phát 2 Header mbuf riêng (64B trong L1). Con trỏ `next` nối vào Payload gốc. NIC Scatter-Gather tự ghép. | **0 Byte** | **Rất thấp:** CPU chỉ ghi đè 64B Header đã nằm sẵn trong L1 Cache. | **TỐI ƯU DUY NHẤT:** Đáp ứng hoàn hảo chuẩn URLLC. |

---

## 🛠️ 4. PHÂN CHIA WORKTREE & CHI TIẾT TỪNG NHÁNH TRIỂN KHAI

### WT0 — `infra/skeleton` (Làm trước trên `main` – Nền tảng)
* **Mục tiêu:** Sửa `.gitignore`, thiết lập build CMake C11, stub header và tạo form đăng ký SKYT.
* **Tác vụ cụ thể:**
  1. Cập nhật `.gitignore`: Bỏ `docs/*`, thêm `build/`, `.worktrees/`, `*.o`, `*.csv`, `*.png`. Giữ `third_party/*`.
  2. Tạo biểu mẫu đăng ký `docs/dang-ky-skyt.md` theo template Mục 5 của BTC (điền sẵn Mentor, Đơn vị, Tên sáng kiến).
  3. Tạo `CMakeLists.txt` chuẩn C11 (`-O3 -Wall -Wextra -pthread`).
  4. Tạo stub `src/mbuf_model.h` và `bench/bench.c`.
  5. Tạo scripts quản lý worktree: `scripts/worktree-runner.sh`.
* **Nghiệm thu:** `cmake -B build && cmake --build build` thành công; commit đầu tiên trên `main`.

---

### WT1 — `feat/mbuf-mempool` (Nền tảng Quản lý Bộ nhớ Mô phỏng)
* **Branch & Worktree:** `branch-wt1-mbuf` tại `.worktrees/wt1-mbuf`
* **Mục tiêu:** Cài đặt `Mbuf` high-fidelity, Fixed Ring Mempool O(1), `mbuf_chain`, `mbuf_refcnt_update`, `mbuf_free`.
* **Files:** `src/mbuf_model.h`, `src/mbuf_model.c`, `tests/test_chain.c`, `tests/test_lifecycle.c`.
* **Nghiệm thu:** CTest pass 100%, vòng lặp 1.000.000 mbuf không rò rỉ bộ nhớ, kiểm thử refcnt lifecycle chính xác.
* **Review:** Kiểm tra độ an toàn atomic và không có memory leak. Pass -> Merge `main`, xóa WT1.

---

### WT2 — `feat/deepcopy-baseline` (Baseline Truyền thống)
* **Branch & Worktree:** `branch-wt2-deepcopy` tại `.worktrees/wt2-deepcopy`
* **Mục tiêu:** Hiện thực giải pháp nhân bản truyền thống dùng `memcpy` toàn bộ header và payload.
* **Files:** `src/deepcopy.h`, `src/deepcopy.c`, `src/outer_header.h`, `src/outer_header.c`, `tests/test_header.c`.
* **Nghiệm thu:** Hàm `duplicate_deepcopy()` tạo 2 bản tin độc lập, copy đúng 1700B (2 x 50B header + 2 x 800B payload). Sửa payload bản 1 không ảnh hưởng bản 2.
* **Review:** Kiểm tra tính độc lập bộ nhớ. Pass -> Merge `main`, xóa WT2.

---

### WT3 — `feat/zerocopy-chain` (Giải pháp Sáng kiến Cốt lõi)
* **Branch & Worktree:** `branch-wt3-zerocopy` tại `.worktrees/wt3-zerocopy`
* **Mục tiêu:** Hiện thực cơ chế Chained-mbuf Zero-Copy, Header Pool, và tăng refcnt payload.
* **Files:** `src/zerocopy.h`, `src/zerocopy.c`, `tests/test_zerocopy.c`.
* **Nghiệm thu:** 
  * Hàm `duplicate_zerocopy()` hoàn thành với **0 byte payload copy** (`grep memcpy zerocopy.c` tuyệt đối không chạm tới payload).
  * `payload->refcnt == 3`. `h1->next == payload`, `h2->next == payload`.
  * Giải phóng tuần tự `h1` rồi `h2` thu hồi đúng vòng đời.
* **Review:** Kiểm tra tính tinh khiết Zero-Copy. Pass -> Merge `main`, xóa WT3.

---

### WT4 — `feat/bench-harness` (Đo Lường Chu kỳ Lệnh & Cache Thrashing)
* **Branch & Worktree:** `branch-wt4-bench` tại `.worktrees/wt4-bench`
* **Mục tiêu:** Harness đo kiểm công bằng, xuất CSV, biểu đồ phân tích và script đo `perf`.
* **Files:**
  * `bench/bench.c`: Đo chu kỳ lệnh bằng `__rdtscp` (`constant_tsc`), tính Throughput Mpps, Latency p50/p95/p99.
  * Hỗ trợ 2 chế độ:
    * `--mode hot`: Đo raw cycles trong cache L1.
    * `--mode stream`: Xoay vòng qua 65.536 packets (~55MB > L3 Cache 30MB) để mô phỏng tải thực tế và kích hoạt Cache Thrashing.
  * `bench/run_perf.sh`: Script wrapper đo Hardware PMU (khi chạy quyền sudo hoặc khi hạ paranoid).
  * `bench/plot.py`: Sinh biểu đồ `bench/compare.png` chất lượng cao.
* **Nghiệm thu:** Chạy lệnh `bench` xuất ra file `bench/results.csv` và `bench/compare.png` không lỗi font.
* **Review:** Kiểm tra số liệu được đo thực tế, không hard-code. Pass -> Merge `main`, xóa WT4.

---

### WT5 — `feat/dpdk-adapter` (Adapter Tích hợp DPDK Thật & Hướng Dẫn)
* **Branch & Worktree:** `branch-wt5-dpdk` tại `.worktrees/wt5-dpdk`
* **Mục tiêu:** Chứng minh code ánh xạ 1-1 sang API DPDK thật và tài liệu hướng dẫn triển khai thực tế.
* **Files:**
  * `src/dpdk-real/dpdk_chain.c`: Wrapper gọi `rte_pktmbuf_chain()` và `rte_pktmbuf_refcnt_update()`.
  * `CMakeLists.txt`: Hỗ trợ `-DUSE_DPDK=ON` link tới `third_party/dpdk-24.11/build` qua `libdpdk-uninstalled.pc`.
  * `docs/dpdk-deployment-guide.md`: Hướng dẫn cấu hình Hugepages 2MB/1GB, cờ port `RTE_ETH_TX_OFFLOAD_MULTI_SEGS`, binding VFIO trên UPF server Viettel.
* **Nghiệm thu:** `cmake -DUSE_DPDK=ON` biên dịch thành công (nếu bật DPDK); tài liệu triển khai mạch lạc.
* **Review:** Kiểm tra chuẩn API DPDK 24.11. Pass -> Merge `main`, xóa WT5.

---

### WT6 — `docs/deliverables-bundle` (Đóng Gói Hồ Sơ Cuộc Thi Chuẩn Bực)
* **Branch & Worktree:** `branch-wt6-docs` tại `.worktrees/wt6-docs`
* **Mục tiêu:** Hoàn thiện 100% hồ sơ cuộc thi theo quy chế VDT GĐ2.
* **Files:**
  1. `report/technical-report-vi.md`: Báo cáo kỹ thuật đầy đủ cấu trúc chuẩn GĐ1 Viettel.
  2. `slides/slides-5p.md`: Slide 5 trang định dạng **Marp Markdown** (hỗ trợ preview và xuất ra PDF/PPTX).
  3. `demo/video-script-vi.md`: Kịch bản video demo 2-3 phút có mốc thời gian chi tiết từng giây.
  4. `demo/run_demo.sh`: Script 1-click trình diễn demo live terminal in màu, chạy bench và hiển thị biểu đồ.
  5. `README.md` & `ARCHITECTURE.md`: Tài liệu kiến trúc và hướng dẫn cài đặt toàn diện.
* **Nghiệm thu:** Toàn bộ số liệu trong báo cáo và slide khớp 100% với `results.csv`.
* **Review:** Đọc soát nội dung Tiếng Việt và tính nhất quán số liệu. Pass -> Merge `main`, xóa WT6.

---

## 📋 5. CHECKLIST CỨNG CỦA REVIEW SUBAGENT

Trước khi bất kỳ nhánh nào được phép merge vào `main`, Review Subagent phải chạy kiểm tra và ký xác nhận:

```bash
# 1. Kiểm tra build sạch sẽ
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --clean-first
# Kết quả: 0 error, 0 warning (-Wall -Wextra)

# 2. Kiểm tra tính đúng đắn của toàn bộ Unit Tests
ctest --test-dir build --output-on-failure
# Kết quả: 100% tests passed

# 3. Kiểm tra tính tinh khiết Zero-Copy (đối với WT3)
grep -in "memcpy" src/zerocopy.c
# Kết quả: Chỉ copy Outer Header (50B/64B), TUYỆT ĐỐI KHÔNG COPY payload

# 4. Kiểm tra Memory Safety
# Không có segmentation fault, không rò rỉ mbuf trong vòng lặp 1.000.000 lần

# 5. Kiểm tra tính trung thực của dữ liệu
# Số liệu trong docs/slides phải lấy từ kết quả chạy thực của bench/results.csv
```

---

## ⏱️ 6. KẾ HOẠCH HÀNH ĐỘNG KHẨN CẤP (DEADLINE 18/10)

* **Ưu tiên 1 (Làm ngay tối nay 16/10):**
  * Khởi tạo Phase 0 trên `main`.
  * Hoàn thành và bàn giao file **`docs/dang-ky-skyt.md`** để bạn có thể điền thông tin cá nhân nộp gấp cho Mentor Nguyễn Ngọc Dũng.
* **Ưu tiên 2 (Tối 16/10 - Sáng 17/10):**
  * Chạy song song WT1 (Mbuf Mempool), WT2 (DeepCopy), WT3 (ZeroCopy) qua git worktree và auto-review merge.
* **Ưu tiên 3 (Chiều 17/10):**
  * Chạy WT4 (Benchmark harness + Streaming cache + Sinh biểu đồ). Thu thập số liệu đo thực tế trên i7-13700HX.
* **Ưu tiên 4 (Tối 17/10 - Sáng 18/10):**
  * Chạy WT5 (DPDK adapter) và WT6 (Báo cáo kỹ thuật, Slide Marp 5 trang, Kịch bản Video).
  * Review tổng thể, tạo git tag `v1.0-skyt` và đóng gói nộp Ban ngành dọc trước 18/10.