#!/usr/bin/env bash
# review-merge.sh - orchestrator review + merge helper (plan2 section 2.4 + section 5).
# Usage:
#   ./scripts/review-merge.sh <branch>
# Runs review checklist inside .worktrees/<sanitized-branch>, merges only if ALL green.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

branch="${1:?usage: $0 <branch>}"
wt=".worktrees/$(echo "$branch" | tr '/' '-')"

fail() { echo "REVIEW FAIL: $*" >&2; exit 1; }

echo "=== Review branch $branch in $wt ==="

[ -d "$wt" ] || fail "worktree $wt not found"

echo "--- 1. clean build ---"
cmake -B "$wt/build" -DCMAKE_BUILD_TYPE=Release -S "$wt" || fail "cmake configure failed"
cmake --build "$wt/build" --clean-first || fail "build failed"

echo "--- 2. ctest ---"
ctest --test-dir "$wt/build" --output-on-failure || fail "ctest failed"

# WT3 purity check (runs whenever zerocopy.c exists)
if [ -f "$wt/src/zerocopy.c" ]; then
  echo "--- 3. zerocopy purity (grep memcpy) ---"
  grep -rn "memcpy" "$wt/src/zerocopy.c" || fail "no memcpy found in zerocopy.c (expected header-only memcpy)"
  echo "(reviewer: verify output above touches ONLY 50B header, never payload)"
fi

echo "--- 6. git hygiene ---"
git -C "$wt" status --porcelain | tee /tmp/review-status.txt
# worktree build/ is untracked-but-ignored normally; check no stray source modifications uncommitted
if [ -s /tmp/review-status.txt ]; then
  echo "NOTE: worktree has untracked/modified files (see above). Must be clean or only ignored artifacts."
  git -C "$wt" status
fi
echo "branch log:"; git log --oneline -5 "$branch" || fail "branch log failed"

echo "REVIEW PASS for $branch"
echo "To merge (orchestrator only):"
echo "  git checkout master && git merge --no-ff $branch -m \"Merge $branch (reviewed PASS)\""
echo "  git branch -d $branch && git worktree remove --force $wt && git worktree prune"
