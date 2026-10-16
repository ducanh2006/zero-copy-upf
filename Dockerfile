# SKYT UPF zero-copy: multi-stage (builder + slim runtime).
# Distro DPDK is 23.11.x, NOT third_party 24.11 (recorded at runtime in
# output/dpdk_version.txt); the chained-mbuf APIs used are stable.
ARG UBUNTU=24.04

FROM ubuntu:${UBUNTU} AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libdpdk-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . /app
# pkg-config finds distro libdpdk; record version for runtime export.
RUN pkg-config --modversion libdpdk > /app/dpdk_version.txt \
    && cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_DPDK=ON \
    && cmake --build build \
    && rm -rf build/CMakeFiles build/*.cmake build/Makefile build/cmake_install.cmake

FROM ubuntu:${UBUNTU} AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    tcpdump \
    libnuma1 \
    libbsd0 \
    libfdt1 \
    libpcap0.8 \
    libmd0 \
    && rm -rf /var/lib/apt/lists/*
# DPDK shared libs + PMD drivers (EAL autoloads $libdir/dpdk).
COPY --from=builder /usr/lib/x86_64-linux-gnu/librte_* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/dpdk/ /usr/lib/x86_64-linux-gnu/dpdk/
# Ship ONLY the plugins this project uses (pcap/ring/null/mempool_ring).
# Autoloading the full PMD set drags vendor deps (mlx5->verbs, idxd->isal).
# (Distro layout nests them under dpdk/pmds-<ABI>/.)
RUN find /usr/lib/x86_64-linux-gnu/dpdk/ -name '*.so*' -printf '%f\n' \
    | grep -v -E "^librte_(net_pcap|net_ring|net_null|mempool_ring)" \
    | while read -r f; do find /usr/lib/x86_64-linux-gnu/dpdk/ -name "$f" -delete; done \
    && find /usr/lib/x86_64-linux-gnu/dpdk/ -name '*.so*' -printf '%f\n'
WORKDIR /app
COPY --from=builder /app /app
# NOTE: plot.py uses its ASCII fallback here (no matplotlib in runtime).
VOLUME ["/app/output"]
ENTRYPOINT ["/bin/bash", "docker/entrypoint.sh"]
