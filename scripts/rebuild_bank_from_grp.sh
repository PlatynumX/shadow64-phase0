#!/usr/bin/env bash
set -euo pipefail

GRP_PATH="${1:-SW.GRP}"
OUT_DIR="${2:-assets}"

python3 tools/shadow64_make_bank.py "$GRP_PATH" --out "$OUT_DIR" --map '$DMWOODS.MAP'
