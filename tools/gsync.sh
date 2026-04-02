#!/usr/bin/env bash
set -e

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "[ERROR] not inside a git repository"
  exit 1
fi

BRANCH="$(git branch --show-current)"

if [ -z "$BRANCH" ]; then
  echo "[ERROR] can't find current branch"
  exit 1
fi

echo "[INFO] branch: $BRANCH"
echo "[INFO] local changes:"
git status --short

echo "[INFO] pulling from origin/$BRANCH ..."
if ! git pull --rebase origin "$BRANCH"; then
  echo "[ERROR] git pull --rebase failed"
  echo "[HINT] resolve conflicts, then run: git rebase --continue"
  echo "[HINT] or abort with: git rebase --abort"
  exit 1
fi

echo "[INFO] sync done"
