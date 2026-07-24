#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v libdragon >/dev/null 2>&1; then
  echo "libdragon CLI is not installed. Use GitHub Actions or run: npm install -g libdragon" >&2
  exit 1
fi
# IMPORTANT: libdragon init may refresh template files. The GitHub workflow backs up
# and restores this project after init; for local manual builds, init once first.
libdragon make
