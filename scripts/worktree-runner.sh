#!/usr/bin/env bash
# worktree-runner.sh - helpers implementing plan2 section 2.4 commands.
# Usage:
#   ./scripts/worktree-runner.sh create <branch>
#   ./scripts/worktree-runner.sh list
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

sanitize() {
  # feat/mbuf-mempool -> feat-mbuf-mempool
  echo "$1" | tr '/' '-'
}

cmd="${1:-help}"
case "$cmd" in
  create)
    branch="${2:?usage: $0 create <branch>}"
    wt=".worktrees/$(sanitize "$branch")"
    git checkout master
    git worktree add "$wt" -b "$branch" master
    echo "Created worktree $wt on branch $branch"
    ;;
  list)
    git worktree list
    git branch -a
    ;;
  *)
    echo "usage: $0 {create <branch>|list}"
    exit 1
    ;;
esac
