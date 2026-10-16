#!/usr/bin/env bash
# run_perf.sh - auxiliary Hardware PMU wrapper (plan1 section 3.4 / WT4).
# Primary harness uses __rdtscp + CLOCK_MONOTONIC_RAW (no root needed).
# This script adds `perf stat` (cycles, cache-misses, instructions) when available.
set -u

BIN="./build/bench_upf"
ITERS_HOT="${1:-50000}"
ITERS_STREAM="${2:-50000}"

if [ ! -x "$BIN" ]; then
  echo "building bench_upf..."
  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --clean-first
fi

echo "=== bench hot (rdtscp) ==="
"$BIN" --mode hot --iters "$ITERS_HOT" --payload 800

echo ""
echo "=== bench stream (rdtscp) ==="
"$BIN" --mode stream --iters "$ITERS_STREAM" --payload 800

echo ""
echo "=== perf stat (auxiliary, may need sudo / lower paranoid) ==="
if command -v perf >/dev/null 2>&1; then
  perf stat -e cycles,instructions,cache-misses,cache-references \
    "$BIN" --mode hot --iters 10000 --payload 800 2>&1 | tail -20 || \
    echo "perf failed (try: sudo sysctl -w kernel.perf_event_paranoid=1)"
else
  echo "perf not found, skipping PMU (rdtscp results above are authoritative)"
fi

echo ""
echo "=== results.csv ==="
cat bench/results.csv 2>/dev/null || echo "(no bench/results.csv)"
