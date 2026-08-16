#!/usr/bin/env python3
"""Validate/list/extract Build MAP files from a KenSilverman GRP archive.

Shadow64 never redistributes game data. This utility reads the user's local
SW.GRP, validates the entire directory against the archive length, and can
extract one selected MAP into the DragonFS tree.
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterable

SIGNATURE = b"KenSilverman"
ENTRY_SIZE = 16
NAME_SIZE = 12
FIRST_LEVEL = "$bullet.map"

@dataclass(frozen=True)
class GrpEntry:
    name: str
    size: int
    offset: int

def read_exact(fh: BinaryIO, size: int) -> bytes:
    data = fh.read(size)
    if len(data) != size:
        raise ValueError(f"unexpected end of file: wanted {size} bytes, got {len(data)}")
    return data

def parse_grp(path: Path) -> list[GrpEntry]:
    archive_size = path.stat().st_size
    if archive_size < len(SIGNATURE) + 4:
        raise ValueError(f"archive is only {archive_size} bytes; SW.GRP is empty or truncated")
    with path.open("rb") as fh:
        signature = read_exact(fh, len(SIGNATURE))
        if signature != SIGNATURE:
            raise ValueError("not a KenSilverman GRP archive (signature mismatch)")
        count = struct.unpack("<I", read_exact(fh, 4))[0]
        if count == 0:
            raise ValueError("GRP contains zero entries")
        if count > 1_000_000:
            raise ValueError(f"unreasonable GRP entry count: {count}")
        table_end = len(SIGNATURE) + 4 + count * ENTRY_SIZE
        if table_end > archive_size:
            raise ValueError(f"GRP directory is truncated: needs {table_end} bytes, archive has {archive_size}")

        raw_entries: list[tuple[str, int]] = []
        for _ in range(count):
            raw_name = read_exact(fh, NAME_SIZE)
            size = struct.unpack("<I", read_exact(fh, 4))[0]
            name = raw_name.split(b"\0", 1)[0].decode("ascii", errors="replace")
            raw_entries.append((name, size))

        cursor = table_end
        entries: list[GrpEntry] = []
        for name, size in raw_entries:
            end = cursor + size
            if end > archive_size:
                raise ValueError(f"entry {name!r} extends past end of archive ({end} > {archive_size})")
            entries.append(GrpEntry(name=name, size=size, offset=cursor))
            cursor = end
        if cursor != archive_size:
            # Extra bytes are tolerated by some GRP variants, but make it visible.
            print(f"WARNING: GRP has {archive_size - cursor} trailing bytes", file=sys.stderr)
        return entries

def choose_entry(entries: Iterable[GrpEntry], requested: str | None) -> GrpEntry:
    maps = [entry for entry in entries if entry.name.lower().endswith(".map")]
    if not maps:
        raise ValueError("the GRP contains no .MAP entries")
    wanted = requested or FIRST_LEVEL
    for entry in maps:
        if entry.name.casefold() == wanted.casefold():
            return entry
    available = ", ".join(entry.name for entry in maps[:20])
    raise ValueError(f"requested map {wanted!r} was not found; first MAP entries: {available}")

def extract(path: Path, entry: GrpEntry, output: Path) -> str:
    output.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    remaining = entry.size
    with path.open("rb") as src, output.open("wb") as dst:
        src.seek(entry.offset)
        while remaining:
            chunk = src.read(min(1024 * 1024, remaining))
            if not chunk:
                raise ValueError(f"unexpected end of archive while extracting {entry.name}")
            dst.write(chunk)
            digest.update(chunk)
            remaining -= len(chunk)
    return digest.hexdigest()

def write_inventory(path: Path, entries: list[GrpEntry]) -> None:
    maps = [entry for entry in entries if entry.name.lower().endswith(".map")]
    lines = ["Shadow64 R11B SW.GRP map inventory", "", f"MAP count: {len(maps)}", ""]
    for entry in maps:
        lines.append(f"{entry.name}\t{entry.size}\t0x{entry.offset:08x}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("grp", type=Path, help="path to the user's SW.GRP")
    parser.add_argument("output", type=Path, nargs="?", help="output MAP path")
    parser.add_argument("--name", default=FIRST_LEVEL, help="GRP member name (default: $bullet.map)")
    parser.add_argument("--inventory", type=Path, help="optional text inventory of all MAP entries")
    parser.add_argument("--validate-only", action="store_true", help="validate archive and selected map without extracting")
    args = parser.parse_args()
    if not args.grp.is_file():
        parser.error(f"GRP not found: {args.grp}")
    try:
        entries = parse_grp(args.grp)
        selected = choose_entry(entries, args.name)
        if args.inventory:
            write_inventory(args.inventory, entries)
        if args.validate_only:
            print(f"VALID GRP: {args.grp}")
            print(f"Archive bytes: {args.grp.stat().st_size}")
            print(f"Entries: {len(entries)}")
            print(f"Selected map: {selected.name} ({selected.size} bytes)")
            return 0
        if args.output is None:
            raise ValueError("output path is required unless --validate-only is used")
        sha256 = extract(args.grp, selected, args.output)
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"Selected: {selected.name}")
    print(f"Size:     {selected.size}")
    print(f"SHA256:   {sha256}")
    print(f"Output:   {args.output}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
