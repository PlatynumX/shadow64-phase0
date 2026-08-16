#!/usr/bin/env python3
"""Build a compact Shadow64 R12 texture bank directly from the user's SW.GRP.

Only tiles referenced by the selected map plus the sword HUD frames are packed.
No proprietary game data is distributed with Shadow64; this tool derives the bank
locally from the user's own SW.GRP.
"""
from __future__ import annotations

import argparse
import io
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

SIGNATURE = b"KenSilverman"
ENTRY_SIZE = 16
NAME_SIZE = 12
FIRST_LEVEL = "$bullet.map"
MAGIC = b"S64TX12\0"
BANK_VERSION = 1
MAX_TILE_DIM = 64
EXTRA_TILES = {2080, 2081, 2082, 2083}  # Shadow Warrior sword HUD frames (conpic.h)

@dataclass(frozen=True)
class GrpEntry:
    name: str
    size: int
    offset: int

@dataclass
class Tile:
    tile_id: int
    width: int
    height: int
    picanm: int
    pixels: bytes  # Build column-major indexed pixels, possibly downsampled


def read_exact(fh: BinaryIO, n: int) -> bytes:
    b = fh.read(n)
    if len(b) != n:
        raise ValueError(f"unexpected end of file: wanted {n} bytes, got {len(b)}")
    return b


def parse_grp(path: Path) -> tuple[list[GrpEntry], dict[str, GrpEntry]]:
    size = path.stat().st_size
    with path.open("rb") as fh:
        if read_exact(fh, 12) != SIGNATURE:
            raise ValueError("not a KenSilverman GRP archive")
        count = struct.unpack("<I", read_exact(fh, 4))[0]
        if count <= 0 or count > 100000:
            raise ValueError(f"invalid GRP entry count: {count}")
        table_end = 16 + count * ENTRY_SIZE
        if table_end > size:
            raise ValueError("truncated GRP directory")
        raw: list[tuple[str, int]] = []
        for _ in range(count):
            name = read_exact(fh, 12).split(b"\0", 1)[0].decode("ascii", "replace")
            esize = struct.unpack("<I", read_exact(fh, 4))[0]
            raw.append((name, esize))
    cursor = table_end
    entries: list[GrpEntry] = []
    for name, esize in raw:
        end = cursor + esize
        if end > size:
            raise ValueError(f"GRP entry {name!r} extends past EOF")
        entries.append(GrpEntry(name, esize, cursor))
        cursor = end
    by_name = {e.name.casefold(): e for e in entries}
    return entries, by_name


def read_entry(path: Path, entry: GrpEntry) -> bytes:
    with path.open("rb") as fh:
        fh.seek(entry.offset)
        return read_exact(fh, entry.size)


def get_entry(by_name: dict[str, GrpEntry], name: str) -> GrpEntry:
    try:
        return by_name[name.casefold()]
    except KeyError:
        raise ValueError(f"required GRP member missing: {name}") from None


def collect_map_tiles(map_data: bytes) -> set[int]:
    # Build v7/v8 map layout: header, sectors(40), walls(32), sprites(44).
    fh = io.BytesIO(map_data)
    version = struct.unpack("<i", read_exact(fh, 4))[0]
    if version not in (7, 8):
        raise ValueError(f"unsupported map version {version}")
    read_exact(fh, 16)  # x,y,z,ang,sect
    nsec = struct.unpack("<h", read_exact(fh, 2))[0]
    if not 0 < nsec <= (1024 if version == 7 else 4096):
        raise ValueError(f"invalid sector count {nsec}")
    tiles: set[int] = set()
    for _ in range(nsec):
        s = read_exact(fh, 40)
        ceilingpicnum = struct.unpack_from("<h", s, 16)[0]
        floorpicnum = struct.unpack_from("<h", s, 24)[0]
        if ceilingpicnum >= 0: tiles.add(ceilingpicnum)
        if floorpicnum >= 0: tiles.add(floorpicnum)
    nwall = struct.unpack("<h", read_exact(fh, 2))[0]
    if not 0 < nwall <= (8192 if version == 7 else 16384):
        raise ValueError(f"invalid wall count {nwall}")
    for _ in range(nwall):
        w = read_exact(fh, 32)
        picnum, overpicnum = struct.unpack_from("<hh", w, 16)
        if picnum >= 0: tiles.add(picnum)
        if overpicnum >= 0: tiles.add(overpicnum)
    nspr = struct.unpack("<h", read_exact(fh, 2))[0]
    if not 0 <= nspr <= (4096 if version == 7 else 16384):
        raise ValueError(f"invalid sprite count {nspr}")
    for _ in range(nspr):
        s = read_exact(fh, 44)
        picnum = struct.unpack_from("<h", s, 14)[0]
        if picnum >= 0: tiles.add(picnum)
    tiles.update(EXTRA_TILES)
    return tiles


def parse_art(blob: bytes) -> dict[int, Tile]:
    fh = io.BytesIO(blob)
    version, numtiles, start, end = struct.unpack("<iiii", read_exact(fh, 16))
    if version != 1:
        raise ValueError(f"ART version {version} is not supported")
    if start < 0 or end < start or end > 65535:
        raise ValueError("invalid ART tile range")
    count = end - start + 1
    xs = list(struct.unpack("<" + "h" * count, read_exact(fh, count * 2)))
    ys = list(struct.unpack("<" + "h" * count, read_exact(fh, count * 2)))
    picanm = list(struct.unpack("<" + "i" * count, read_exact(fh, count * 4)))
    out: dict[int, Tile] = {}
    for i in range(count):
        w, h = xs[i], ys[i]
        if w < 0 or h < 0:
            raise ValueError("negative ART tile dimension")
        pixels = read_exact(fh, w * h)
        out[start + i] = Tile(start + i, w, h, picanm[i], pixels)
    return out


def downsample_column_major(tile: Tile, max_dim: int = MAX_TILE_DIM) -> Tile:
    w, h = tile.width, tile.height
    if w <= 0 or h <= 0:
        return tile
    scale = min(1.0, max_dim / float(max(w, h)))
    nw = max(1, int(round(w * scale)))
    nh = max(1, int(round(h * scale)))
    if nw == w and nh == h:
        return tile
    src = tile.pixels
    dst = bytearray(nw * nh)
    for x in range(nw):
        sx = min(w - 1, int(x * w / nw))
        for y in range(nh):
            sy = min(h - 1, int(y * h / nh))
            dst[x * nh + y] = src[sx * h + sy]
    return Tile(tile.tile_id, nw, nh, tile.picanm, bytes(dst))


def build_bank(grp: Path, map_name: str, output: Path, report: Path | None) -> None:
    entries, by_name = parse_grp(grp)
    map_entry = get_entry(by_name, map_name)
    map_data = read_entry(grp, map_entry)
    wanted = collect_map_tiles(map_data)

    pal_entry = get_entry(by_name, "PALETTE.DAT")
    palette = read_entry(grp, pal_entry)
    if len(palette) < 768:
        raise ValueError("PALETTE.DAT is shorter than the 768-byte VGA palette")
    palette = palette[:768]

    art_entries = sorted(
        (e for e in entries if e.name.upper().startswith("TILES") and e.name.upper().endswith(".ART")),
        key=lambda e: e.name.upper(),
    )
    if not art_entries:
        raise ValueError("SW.GRP contains no TILES###.ART files")

    found: dict[int, Tile] = {}
    for entry in art_entries:
        for tid, tile in parse_art(read_entry(grp, entry)).items():
            if tid in wanted:
                found[tid] = downsample_column_major(tile)

    missing = sorted(t for t in wanted if t not in found)
    if missing:
        # Missing zero-sized/undefined tiles can legitimately be referenced by maps.
        # Only fail if one of our required HUD tiles is absent.
        required_missing = sorted(EXTRA_TILES.intersection(missing))
        if required_missing:
            raise ValueError(f"required sword tiles missing from ART: {required_missing}")

    tiles = [found[t] for t in sorted(found)]
    output.parent.mkdir(parents=True, exist_ok=True)
    # header: magic, version, count, palette_bytes, descriptor_bytes, pixel_bytes
    desc_size = 20
    pixel_bytes = sum(len(t.pixels) for t in tiles)
    header_size = 8 + 5 * 4
    desc_bytes = len(tiles) * desc_size
    data_base = header_size + 768 + desc_bytes
    with output.open("wb") as fh:
        fh.write(MAGIC)
        fh.write(struct.pack("<IIIII", BANK_VERSION, len(tiles), 768, desc_bytes, pixel_bytes))
        fh.write(palette)
        cursor = 0
        for tile in tiles:
            flags = 1 if tile.width > 0 and tile.height > 0 else 0
            # id,u16 w,u16 h,u16 flags,u16 picanm,u32 data_offset,u32 data_size,u32
            fh.write(struct.pack("<HHHHIII", tile.tile_id, tile.width, tile.height, flags,
                                 tile.picanm & 0xFFFFFFFF, data_base + cursor, len(tile.pixels)))
            cursor += len(tile.pixels)
        for tile in tiles:
            fh.write(tile.pixels)

    if report:
        report.parent.mkdir(parents=True, exist_ok=True)
        lines = [
            "Shadow64 R12 texture bank report",
            f"map={map_name}",
            f"map_tiles_requested={len(wanted)}",
            f"tiles_packed={len(tiles)}",
            f"missing_nonfatal={len(missing)}",
            f"bank_bytes={output.stat().st_size}",
            f"max_tile_dim={MAX_TILE_DIM}",
            "",
            "packed tile IDs:",
            " ".join(str(t.tile_id) for t in tiles),
        ]
        if missing:
            lines += ["", "missing tile IDs:", " ".join(str(t) for t in missing)]
        report.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("grp", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--map", default=FIRST_LEVEL)
    ap.add_argument("--report", type=Path)
    args = ap.parse_args()
    try:
        build_bank(args.grp, args.map, args.output, args.report)
    except (OSError, ValueError, struct.error) as exc:
        print(f"ERROR: {exc}")
        return 1
    print(f"Built texture bank: {args.output} ({args.output.stat().st_size} bytes)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
