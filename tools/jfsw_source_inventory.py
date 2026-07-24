#!/usr/bin/env python3
"""Create a quick source inventory for a vendored JFSW tree.

This does not understand the whole source; it finds likely platform, renderer,
file I/O, audio, input, Build-engine, and Shadow Warrior game-code areas so the
next porting pass can be grounded in the actual upstream tree.
"""
from __future__ import annotations
import argparse
import hashlib
import os
import re
from pathlib import Path
from datetime import datetime, timezone

TEXT_EXTS = {'.c', '.cc', '.cpp', '.h', '.hpp', '.hh', '.inc', '.S', '.s', '.mak', '.mk', '.txt', '.md'}
SKIP_DIRS = {'.git', 'build', 'obj', 'objs', '.github'}
BUCKETS = {
    'platform_sdl': re.compile(r'\b(SDL|sdl_|SDL_)'),
    'video_renderer': re.compile(r'\b(draw|render|screen|video|polymost|gl_|OpenGL|setgamemode|printext|rotatesprite)', re.I),
    'audio_music': re.compile(r'\b(sound|audio|midi|music|voc|fx_|cd_|ogg|mix)', re.I),
    'input': re.compile(r'\b(input|keyboard|mouse|joystick|controller|key|button)', re.I),
    'filesystem_cache': re.compile(r'\b(file|open|read|write|grp|cache|kopen|kread|load|save)', re.I),
    'build_engine': re.compile(r'\b(sector|wall|sprite|picnum|tilesiz|sintable|numsectors|numwalls|board|engine)', re.I),
    'gameplay': re.compile(r'\b(actor|weapon|player|enemy|sectorobject|ST[0-9]|Do[A-Z]|Spawn|Kill|Inventory)', re.I),
}

def is_text(path: Path) -> bool:
    return path.suffix.lower() in TEXT_EXTS

def file_sha1(path: Path) -> str:
    h=hashlib.sha1()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('jfsw_root', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    root = args.jfsw_root.resolve()
    if not root.exists():
        raise SystemExit(f'JFSW root not found: {root}')
    files=[]
    bucket_hits={k:[] for k in BUCKETS}
    total_bytes=0
    text_count=0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        d=Path(dirpath)
        for name in filenames:
            p=d/name
            rel=p.relative_to(root).as_posix()
            try:
                size=p.stat().st_size
            except OSError:
                continue
            total_bytes += size
            files.append((rel,size))
            if is_text(p):
                text_count += 1
                try:
                    data=p.read_text(errors='ignore')
                except Exception:
                    data=''
                sample = rel + '\n' + data[:200000]
                for bucket, rx in BUCKETS.items():
                    if rx.search(sample):
                        bucket_hits[bucket].append((rel,size))
    files.sort()
    out=[]
    out.append('# JFSW Source Inventory')
    out.append('')
    out.append(f'Generated UTC: {datetime.now(timezone.utc).isoformat()}')
    out.append(f'Root: {root}')
    commit_file=root.parent/'JFSW_SOURCE_COMMIT.txt'
    if commit_file.exists():
        out.append(f'Upstream commit: {commit_file.read_text().strip()}')
    out.append(f'Files: {len(files)}')
    out.append(f'Text/source-like files: {text_count}')
    out.append(f'Total bytes: {total_bytes}')
    out.append('')
    out.append('## Top-level files')
    for rel,size in files[:200]:
        if '/' not in rel:
            out.append(f'- {rel} ({size} bytes)')
    out.append('')
    for bucket,hits in bucket_hits.items():
        hits=sorted(hits, key=lambda x: (x[0].count('/'), x[0]))
        out.append(f'## {bucket} candidates ({len(hits)})')
        for rel,size in hits[:80]:
            out.append(f'- {rel} ({size} bytes)')
        if len(hits)>80:
            out.append(f'- ... {len(hits)-80} more')
        out.append('')
    out.append('## Largest files')
    for rel,size in sorted(files, key=lambda x:x[1], reverse=True)[:80]:
        out.append(f'- {rel} ({size} bytes)')
    out.append('')
    out.append('## All source-like files')
    for rel,size in files:
        if Path(rel).suffix.lower() in TEXT_EXTS:
            out.append(f'- {rel} ({size} bytes)')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(out) + '\n')
    print(f'wrote {args.output}')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
