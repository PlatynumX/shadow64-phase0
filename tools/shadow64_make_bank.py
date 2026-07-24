#!/usr/bin/env python3
"""
Shadow64 Phase 0 asset bank builder.

Input:
  SW.GRP from Shadow Warrior registered/full data.

Output:
  dmwoods.s64b            N64-native big-endian map/tile/palette bank
  dmwoods_summary.json    counts/offsets/debug info

This intentionally does not try to make the N64 parse the whole 45 MiB GRP.
It extracts one test map and only the referenced ART tiles so the N64 side
can prove the data path and viewer before we port the real renderer.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path


def read_grp(path: Path):
    data = path.read_bytes()
    if data[:12] != b"KenSilverman":
        raise SystemExit("Not a KenSilverman GRP archive")

    count = struct.unpack_from("<I", data, 12)[0]
    entries = []
    off = 16 + count * 16
    for i in range(count):
        raw = data[16 + i * 16 : 16 + i * 16 + 12]
        name = raw.split(b"\0", 1)[0].decode("ascii", errors="replace")
        size = struct.unpack_from("<I", data, 16 + i * 16 + 12)[0]
        entries.append({"name": name, "size": size, "offset": off})
        off += size

    if off != len(data):
        raise SystemExit(f"GRP directory size mismatch: expected end {off}, got {len(data)}")

    return data, entries


def get_entry(data: bytes, entries: list[dict], name: str):
    for e in entries:
        if e["name"].upper() == name.upper():
            return data[e["offset"] : e["offset"] + e["size"]], e
    raise KeyError(name)


def parse_map(buf: bytes):
    o = 0
    version, posx, posy, posz = struct.unpack_from("<iiii", buf, o)
    o += 16
    ang, cursect = struct.unpack_from("<hh", buf, o)
    o += 4
    numsectors = struct.unpack_from("<h", buf, o)[0]
    o += 2

    sectors = []
    for _ in range(numsectors):
        sectors.append(struct.unpack_from("<hhii hhhh bBBB hh bBBB BB hhh", buf, o))
        o += 40

    numwalls = struct.unpack_from("<h", buf, o)[0]
    o += 2
    walls = []
    for _ in range(numwalls):
        walls.append(struct.unpack_from("<ii hhhhhh bBBBBB hhh", buf, o))
        o += 32

    numsprites = struct.unpack_from("<h", buf, o)[0]
    o += 2
    sprites = []
    for _ in range(numsprites):
        sprites.append(struct.unpack_from("<iii hh bBBBBB bb hhhhhhhhhh", buf, o))
        o += 44

    if o != len(buf):
        raise ValueError(f"MAP parse ended at {o}, file is {len(buf)} bytes")

    return {
        "version": version,
        "start": (posx, posy, posz),
        "ang": ang,
        "cursect": cursect,
        "sectors": sectors,
        "walls": walls,
        "sprites": sprites,
    }


def parse_art(buf: bytes):
    artversion, numtiles, start, end = struct.unpack_from("<iiii", buf, 0)
    n = end - start + 1
    if n <= 0:
        return {}

    o = 16
    xs = list(struct.unpack_from("<" + "h" * n, buf, o))
    o += 2 * n
    ys = list(struct.unpack_from("<" + "h" * n, buf, o))
    o += 2 * n
    picanm = list(struct.unpack_from("<" + "i" * n, buf, o))
    o += 4 * n

    tiles = {}
    pixoff = o
    for idx in range(n):
        w, h = xs[idx], ys[idx]
        size = max(0, w * h)
        tiles[start + idx] = {
            "w": w,
            "h": h,
            "picanm": picanm[idx],
            "pixels": buf[pixoff : pixoff + size],
        }
        pixoff += size

    if pixoff != len(buf):
        raise ValueError(f"ART parse ended at {pixoff}, file is {len(buf)} bytes")

    return tiles


def align_bytes(b: bytes | bytearray, n: int = 16) -> bytes:
    b = bytes(b)
    return b + b"\0" * ((n - len(b) % n) % n)


def pack_sector(s):
    return struct.pack(
        ">hhiiHHhhbBBBhhbBBBBBhhh",
        s[0], s[1], s[2], s[3],
        s[4] & 0xFFFF, s[5] & 0xFFFF,
        s[6], s[7],
        s[8], s[9], s[10], s[11],
        s[12], s[13],
        s[14], s[15], s[16], s[17],
        s[18], s[19],
        s[20], s[21], s[22],
    )


def pack_wall(w):
    return struct.pack(
        ">iihhhhhhbBBBBBhhh",
        w[0], w[1],
        w[2], w[3], w[4], w[5], w[6], w[7],
        w[8], w[9], w[10], w[11], w[12], w[13],
        w[14], w[15], w[16],
    )


def pack_sprite(sp):
    return struct.pack(
        ">iiihhbBBBBBbbhhhhhhhhhh",
        sp[0], sp[1], sp[2],
        sp[3], sp[4],
        sp[5], sp[6], sp[7], sp[8], sp[9], sp[10],
        sp[11], sp[12],
        sp[13], sp[14], sp[15], sp[16], sp[17], sp[18], sp[19], sp[20], sp[21], sp[22],
    )


def build_bank(grp_path: Path, map_name: str, out_dir: Path):
    grp, entries = read_grp(grp_path)

    map_bytes, map_entry = get_entry(grp, entries, map_name)
    mp = parse_map(map_bytes)

    alltiles = {}
    tile_source = {}
    art_entries = sorted(
        [e for e in entries if e["name"].upper().startswith("TILES") and e["name"].upper().endswith(".ART")],
        key=lambda e: int(re.search(r"(\d+)", e["name"]).group(1)),
    )

    for e in art_entries:
        tiles = parse_art(grp[e["offset"] : e["offset"] + e["size"]])
        for pic, tile in tiles.items():
            alltiles[pic] = tile
            tile_source[pic] = e["name"]

    palette_bytes, _ = get_entry(grp, entries, "PALETTE.DAT")
    palette = palette_bytes[:768]

    sectors = mp["sectors"]
    walls = mp["walls"]
    sprites = mp["sprites"]

    used = set()
    for s in sectors:
        used.add(s[6] & 0xFFFF)
        used.add(s[12] & 0xFFFF)
    for w in walls:
        used.add(w[6] & 0xFFFF)
        over = w[7] & 0xFFFF
        if over:
            used.add(over)
    for sp in sprites:
        used.add(sp[4] & 0xFFFF)

    used_present = [
        p for p in sorted(used)
        if p in alltiles and alltiles[p]["w"] > 0 and alltiles[p]["h"] > 0
    ]
    used_missing = [
        p for p in sorted(used)
        if p not in alltiles or alltiles[p]["w"] <= 0 or alltiles[p]["h"] <= 0
    ]

    map_conv = (
        b"".join(pack_sector(s) for s in sectors)
        + b"".join(pack_wall(w) for w in walls)
        + b"".join(pack_sprite(sp) for sp in sprites)
    )

    tile_dir = bytearray()
    tile_data = bytearray()
    for pic in used_present:
        t = alltiles[pic]
        data_offset = len(tile_data)
        pix = t["pixels"]
        tile_data.extend(pix)
        # picnum, width, height, reserved, offset, size, picanm
        tile_dir.extend(struct.pack(">HHHHIII", pic, t["w"], t["h"], 0, data_offset, len(pix), t["picanm"] & 0xFFFFFFFF))

    chunks = bytearray(b"\0" * 128)
    palette_offset = len(chunks)
    chunks.extend(palette)
    chunks = bytearray(align_bytes(chunks))

    map_offset = len(chunks)
    chunks.extend(map_conv)
    chunks = bytearray(align_bytes(chunks))

    tile_dir_offset = len(chunks)
    chunks.extend(tile_dir)
    chunks = bytearray(align_bytes(chunks))

    tile_data_offset = len(chunks)
    chunks.extend(tile_data)
    chunks = bytearray(align_bytes(chunks))

    startx, starty, startz = mp["start"]
    hdr = struct.pack(
        ">4sHHIIIIIIIIiiiiHHHHIIIIIIII",
        b"S64B", 1, 1,
        palette_offset, len(palette),
        map_offset, len(map_conv),
        tile_dir_offset, len(used_present),
        tile_data_offset, len(tile_data),
        startx, starty, startz, 0,
        mp["ang"] & 0xFFFF,
        mp["cursect"] & 0xFFFF,
        len(sectors),
        len(walls),
        len(sprites),
        40, 32, 44, 20,
        zlib.crc32(map_bytes) & 0xFFFFFFFF,
        zlib.crc32(bytes(tile_data)) & 0xFFFFFFFF,
        0,
    )
    hdr += b"\0" * (128 - len(hdr))
    chunks[0:128] = hdr
    bank = bytes(chunks)

    out_dir.mkdir(parents=True, exist_ok=True)
    bank_path = out_dir / "dmwoods.s64b"
    bank_path.write_bytes(bank)

    summary = {
        "source_grp": grp_path.name,
        "source_grp_sha1": hashlib.sha1(grp).hexdigest(),
        "bank_sha1": hashlib.sha1(bank).hexdigest(),
        "bank_size": len(bank),
        "map_name": map_name,
        "map_size": len(map_bytes),
        "map_version": mp["version"],
        "start": {"x": startx, "y": starty, "z": startz, "ang": mp["ang"], "cursect": mp["cursect"]},
        "counts": {"sectors": len(sectors), "walls": len(walls), "sprites": len(sprites)},
        "used_picnums": len(used),
        "packed_tiles": len(used_present),
        "missing_or_empty_picnums": used_missing,
        "tile_data_size": len(tile_data),
        "offsets": {
            "palette_offset": palette_offset,
            "palette_size": len(palette),
            "map_offset": map_offset,
            "map_size": len(map_conv),
            "tile_dir_offset": tile_dir_offset,
            "tile_count": len(used_present),
            "tile_data_offset": tile_data_offset,
            "tile_data_size": len(tile_data),
        },
        "tile_sources": {str(pic): tile_source.get(pic) for pic in used_present},
    }
    (out_dir / "dmwoods_summary.json").write_text(json.dumps(summary, indent=2))
    return summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("grp", type=Path, help="Path to SW.GRP")
    ap.add_argument("--map", default="$DMWOODS.MAP", help="MAP lump to pack")
    ap.add_argument("--out", type=Path, default=Path("assets"), help="Output asset directory")
    args = ap.parse_args()

    summary = build_bank(args.grp, args.map, args.out)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
