#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
URL="${JFSW_REPO_URL:-https://github.com/jonof/jfsw.git}"
REF="${JFSW_REF:-}"
DEST="third_party/jfsw"
mkdir -p third_party docs
rm -rf "$DEST"
echo "Fetching JFSW reference source from: $URL"
git clone --depth=1 "$URL" "$DEST"
if [ -n "$REF" ]; then
  git -C "$DEST" fetch --depth=1 origin "$REF"
  git -C "$DEST" checkout --detach FETCH_HEAD
fi
COMMIT="$(git -C "$DEST" rev-parse HEAD)"
echo "$COMMIT" > third_party/JFSW_SOURCE_COMMIT.txt
echo "Vendored JFSW commit: $COMMIT"
rm -rf "$DEST/.git"
python3 tools/jfsw_source_inventory.py "$DEST" docs/JFSW_SOURCE_INVENTORY.txt
cat > docs/JFSW_SOURCE_NOTICE.txt <<NOTICE
JFSW reference source vendored by scripts/fetch_jfsw_reference.sh
Upstream URL: $URL
Commit: $COMMIT

This source is included so Shadow64 can be ported against the real JFSW/JFBuild codebase instead of guessed structs and behavior. Keep the Shadow64 repo private while it also contains game-derived asset banks.
NOTICE
