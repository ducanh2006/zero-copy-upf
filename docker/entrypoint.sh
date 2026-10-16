#!/usr/bin/env bash
# docker/entrypoint.sh - run inside the container; artifacts -> /app/output.
# Usage: entrypoint.sh capture [--packets 200] | perf [--packets 100000]
set -u

MODE="${1:-capture}"
shift || true

cd /app
mkdir -p output data/pcap

echo "=== libdpdk (container): $(pkg-config --modversion libdpdk 2>/dev/null || echo unknown) ==="
pkg-config --modversion libdpdk > output/dpdk_version.txt 2>/dev/null || echo unknown > output/dpdk_version.txt

echo "=== unit tests (no ctest in slim runtime; binaries run directly) ==="
./build/test_chain && ./build/test_lifecycle && ./build/test_header && ./build/test_zerocopy

echo "=== bench_upf (microbench) ==="
rm -f bench/results.csv
./build/bench_upf --mode hot --iters 50000 --payload 800
./build/bench_upf --mode stream --iters 50000 --payload 800
python3 bench/plot.py || true

echo "=== pcap replay ($MODE) ==="
rm -f bench/results_pcap.csv
if [ "$MODE" = "capture" ]; then
  # --no-huge: no hugetlbfs mount visible inside stock containers.
  ./build/dpdk_pcap_upf --no-pci --no-huge --file-prefix=upf_docker -l 0-1 \
    --vdev "net_pcap0,rx_pcap=data/pcap/sample_5g_traffic.pcap,infinite_rx=1" \
    --vdev "net_pcap1,tx_pcap=output/out_path1_gnb1.pcap" \
    --vdev "net_pcap2,tx_pcap=output/out_path2_gnb2.pcap" \
    -- --mode capture $@
  tcpdump -nn -r output/out_path1_gnb1.pcap -c 1 2>&1 | head -n 3 || true
else
  ./build/dpdk_pcap_upf --no-pci --no-huge --file-prefix=upf_docker -l 0-1 \
    --vdev "net_pcap0,rx_pcap=data/pcap/sample_5g_traffic.pcap,infinite_rx=1" \
    --vdev "net_null0" \
    --vdev "net_null1" \
    -- --mode perf $@
fi

cp -f bench/results.csv bench/compare.png bench/results_pcap.csv output/ 2>/dev/null || true
echo "=== output/ ==="
ls -la output/
echo "DONE."
