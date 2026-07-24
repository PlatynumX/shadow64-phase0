#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
need() { [ -e "$1" ] || { echo "MISSING: $1" >&2; exit 1; }; }
need Makefile
need src/main.c
need assets/dmwoods.s64b
need filesystem/dmwoods.s64b
need assets/dmwoods_summary.json
need tools/shadow64_make_bank.py
need .github/workflows/build-shadow64.yml
need workflow-build-shadow64.yml
need package.json
need scripts/build_with_libdragon.sh

need tools/jfsw_source_inventory.py
need scripts/fetch_jfsw_reference.sh
need docs/JFSW_PORTING_BASELINE.md
need RELEASE_NOTES_R11.txt
need TERMUX_README_R11.txt
python3 - <<'PY'
from pathlib import Path
import struct, json, hashlib
bank = Path('assets/dmwoods.s64b').read_bytes()
fsbank = Path('filesystem/dmwoods.s64b').read_bytes()
assert bank == fsbank, 'assets and filesystem bank mismatch'
assert bank[:4] == b'S64B', 'bad bank magic'
version = struct.unpack_from('>H', bank, 4)[0]
assert version == 1, f'bad bank version {version}'
summary = json.loads(Path('assets/dmwoods_summary.json').read_text())
print('OK package')
print('bank bytes:', len(bank))
print('bank sha256:', hashlib.sha256(bank).hexdigest())
print('map:', summary.get('map_name') or summary.get('map'))
counts = summary.get('counts', {})
print('sectors:', counts.get('sectors'))
print('walls:', counts.get('walls'))
print('sprites:', counts.get('sprites'))
print('tiles:', summary.get('tile_count') or summary.get('packed_tiles'))
PY
