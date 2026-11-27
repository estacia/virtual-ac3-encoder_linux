#!/usr/bin/env bash
set -euo pipefail

OWNER="${GITHUB_OWNER:-estacia}"
REPO="${GITHUB_REPO:-virtual-ac3-encoder_linux}"
REMOTE_URL="https://github.com/${OWNER}/${REPO}.git"

command -v git >/dev/null 2>&1 || { echo "git is required." >&2; exit 1; }
command -v gh >/dev/null 2>&1 || { echo "GitHub CLI (gh) is required." >&2; exit 1; }
gh auth status >/dev/null

if [[ ! -d .git ]]; then
  git init -b main
fi
if ! git config user.name >/dev/null || ! git config user.email >/dev/null; then
  echo "Set git user.name and user.email before committing." >&2
  exit 1
fi
if ! git remote get-url origin >/dev/null 2>&1; then
  git remote add origin "${REMOTE_URL}"
fi

git add -A
if ! git diff --cached --quiet; then
  git commit -m "Update Linux-native PipeWire AC-3 encoder"
fi
git push -u origin main
printf '\nPushed to: %s\n' "${REMOTE_URL}"
