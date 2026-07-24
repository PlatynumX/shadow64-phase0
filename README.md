# Shadow64 Phase 0 R11

This is the **Shadow Warrior → Nintendo 64 / Expansion Pak** Phase 0 R11 package.

R11 deliberately stays on the **software renderer** route. It does **not** use the libdragon preview branch, OpenGL, GL display lists, shaders, or an RDP triangle renderer. The N64 code still boots with standard libdragon trunk and draws through the CPU/software framebuffer path.

```text
SW.GRP -> $DMWOODS.MAP + ART tiles + PALETTE.DAT -> dmwoods.s64b -> DFS load -> CPU software wall columns
```

## Current target

```text
Map: $DMWOODS.MAP
Sectors: 65
Walls: 483
Sprites: 220
Packed tiles: 87
Bank size: 615,712 bytes
Tile pixel payload: 585,322 bytes
ROM output: shadow64_phase0_r11.z64
```

## What R11 should show

```text
- crude first-person software-rendered wall view
- real Shadow Warrior ART wall textures from dmwoods.s64b
- debug sky/floor fills
- debug text at top
- B toggles back to the top-down map viewer
```

This is still a renderer proof-of-concept, not gameplay. It does not yet have real Build sector recursion, floor/ceiling spans, sprite billboards, audio, weapons, enemies, collision, save/load, or full JFSW game loop integration.

## Controls

```text
D-pad up/down: move forward/back
D-pad left/right: strafe
C-left/C-right: turn
A: reset camera
B: toggle first-person/top-down view
C-up/C-down: zoom only while in top-down view
```

## Files

```text
tools/shadow64_make_bank.py      GRP/MAP/ART/PAL extractor
assets/dmwoods.s64b              generated Phase 0 N64 asset bank
filesystem/dmwoods.s64b          DFS copy packed into the ROM
src/main.c                       libdragon CPU software renderer
preview/dmwoods_minimap.png      host-side parsed-map preview
preview/dmwoods_used_tiles_contact_sheet.png
.github/workflows/build-shadow64.yml
scripts/validate_shadow64_package.sh
scripts/build_with_libdragon.sh
scripts/fetch_jfsw_reference.sh  optional private-repo JFSW source snapshot fetcher
```

## Android/Termux workflow

Use the separate script:

```text
upload_shadow64_to_github_r11.sh
```

It updates the stable private repo `shadow64-phase0`, starts the GitHub Actions libdragon build, and downloads `shadow64_phase0_r11.z64` back to Android Downloads.

## Local build on a PC/Linux box with libdragon

```bash
bash scripts/validate_shadow64_package.sh
bash scripts/build_with_libdragon.sh
```

## Notes

- Expansion Pak is required.
- Keep the GitHub repo private because `dmwoods.s64b` is game-derived data.
- ART tile pixels remain Build-style column-major: `offset = x * height + y`.
- JFSW remains the reference base, but R11 keeps the runnable ROM on the known-good software renderer skeleton.
