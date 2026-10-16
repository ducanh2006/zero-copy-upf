#!/usr/bin/env bash
# run_demo.sh - 1-click live demo: build + ctest + bench hot/stream + results.
set -u
GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[1;33m'; NC='\033[0m'

cecho() { printf "${2}%s${NC}\n" "$1"; }

cecho "=== [1/4] Build (Release, C11 -O3) ===" "$CYAN"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --clean-first

cecho "=== [2/4] Unit tests (chain/lifecycle/header/zerocopy) ===" "$CYAN"
ctest --test-dir build --output-on-failure

cecho "=== [3/4] Zero-copy purity ===" "$CYAN"
grep -rn "memcpy" src/zerocopy.c || true
cecho "(only 50B header copies, never payload)" "$YELLOW"

cecho "=== [4/4] Benchmark hot + stream (rdtscp, pinned CPU3) ===" "$CYAN"
rm -f bench/results.csv
./build/bench_upf --mode hot --iters 50000 --payload 800
./build/bench_upf --mode stream --iters 50000 --payload 800

cecho "=== results.csv ===" "$GREEN"
cat bench/results.csv

cecho "=== plot (matplotlib or ASCII fallback) ===" "$CYAN"
python3 bench/plot.py || true

cecho "Demo DONE. Numbers above are live from this machine." "$GREEN"
