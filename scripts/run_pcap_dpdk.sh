#!/usr/bin/env bash
# run_pcap_dpdk.sh - native DPDK PCAP replay (capture | perf).
# Provenance: EAL/driver workaround from hpc-spi-classifier/run_project_native.sh
# (NTFS /mnt is world-writable; EAL refuses plugins from unsafe paths).
#
# Usage:
#   bash scripts/run_pcap_dpdk.sh capture [--packets 200]   # TX -> out pcaps
#   bash scripts/run_pcap_dpdk.sh perf [--packets 100000]   # TX -> net_null
set -u

MODE="${1:-capture}"
shift || true

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Locate the prebuilt DPDK tree (git-ignored, so linked worktrees don't have it).
DPDK_BUILD="${DPDK_BUILD:-$ROOT/third_party/dpdk-24.11/build}"
if [ ! -d "$DPDK_BUILD" ]; then
  MAIN_WT="$(git -C "$ROOT" worktree list --porcelain 2>/dev/null | awk '/^worktree /{print $2; exit}')"
  if [ -n "$MAIN_WT" ] && [ -d "$MAIN_WT/third_party/dpdk-24.11/build" ]; then
    DPDK_BUILD="$MAIN_WT/third_party/dpdk-24.11/build"
  fi
fi
if [ ! -d "$DPDK_BUILD" ]; then
  echo "DPDK build not found (tried $DPDK_BUILD). Set DPDK_BUILD env." >&2
  exit 1
fi
echo "DPDK build: $DPDK_BUILD"

BIN="./build/dpdk_pcap_upf"
SAMPLE="data/pcap/sample_5g_traffic.pcap"
OUT1="data/pcap/out_path1_gnb1.pcap"
OUT2="data/pcap/out_path2_gnb2.pcap"

if [ ! -f "$SAMPLE" ]; then
  echo "generating $SAMPLE ..."
  python3 scripts/gen_sample_pcap.py --packets 256
fi

if [ ! -x "$BIN" ]; then
  echo "building $BIN (USE_DPDK=ON) ..."
  export PKG_CONFIG_PATH="$DPDK_BUILD/meson-uninstalled:${PKG_CONFIG_PATH:-}"
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_DPDK=ON
  cmake --build build
fi

# 1. Stage dynamic PMDs somewhere EAL trusts: NOT /tmp or /dev/shm
# (mode 1777 world-writable -> EAL "insecure" refusal) and not the NTFS
# mount (world-writable). $HOME is ext4, mode 750.
TMP_DRIVER_DIR="${UPF_DRIVER_DIR:-$HOME/.upf_dpdk_drivers}"
mkdir -p "$TMP_DRIVER_DIR"
for so in librte_net_pcap.so librte_mempool_ring.so librte_net_ring.so librte_net_null.so; do
  src="$DPDK_BUILD/drivers/$so"
  if [ -f "$src" ] && [ ! -f "$TMP_DRIVER_DIR/$so" ]; then
    cp "$src" "$TMP_DRIVER_DIR/"
  fi
done

export LD_LIBRARY_PATH="$DPDK_BUILD/lib:$DPDK_BUILD/drivers:${LD_LIBRARY_PATH:-}"
export UPF_RX_PCAP="$ROOT/$SAMPLE"

# --no-huge: laptop runs as non-root without hugetlbfs write access;
# functional/capture runs don't need hugepage-backed pools (perf numbers
# on VHT servers MUST use hugepages; see docs/dpdk-deployment-guide.md).
COMMON=(--file-prefix=upf_pcap --no-pci --no-huge -d "$TMP_DRIVER_DIR" -l 0-3)

if [ "$MODE" = "capture" ]; then
  rm -f "$OUT1" "$OUT2"
  # shellcheck disable=SC2068
  ./build/dpdk_pcap_upf "${COMMON[@]}" -n 4 \
    --vdev "net_pcap0,rx_pcap=$SAMPLE,infinite_rx=1" \
    --vdev "net_pcap1,tx_pcap=$OUT1" \
    --vdev "net_pcap2,tx_pcap=$OUT2" \
    -- --mode capture $@
  echo "=== verify path1 (first packet) ==="
  tcpdump -nn -r "$OUT1" -c 1 2>&1 | head -n 5
  echo "=== sizes ==="
  ls -la "$OUT1" "$OUT2"
elif [ "$MODE" = "perf" ]; then
  # TX sinks: net_null (no disk I/O at Mpps). RX still infinite pcap replay.
  # shellcheck disable=SC2068
  ./build/dpdk_pcap_upf "${COMMON[@]}" -n 4 \
    --vdev "net_pcap0,rx_pcap=$SAMPLE,infinite_rx=1" \
    --vdev "net_null0" \
    --vdev "net_null1" \
    -- --mode perf $@
else
  echo "unknown mode: $MODE (expected capture|perf)" >&2
  exit 1
fi
