#!/usr/bin/env bash
# run_docker.sh - build image and run one service (capture|bench).
# Usage: bash scripts/run_docker.sh [capture|bench]
set -u
SVC="${1:-capture}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p output
docker compose up --build "$SVC"
