# Shadow64 R11: JFSW porting baseline

R11 changes the project direction from a standalone Shadow Warrior asset/viewer experiment into a JFSW-based N64 port branch.

## Base source policy

The upload script vendors a fresh snapshot of JonoF's Shadow Warrior source port into:

```text
third_party/jfsw/
```

The exact upstream commit is written to:

```text
third_party/JFSW_SOURCE_COMMIT.txt
docs/JFSW_SOURCE_INVENTORY.txt
```

The source zip does not include JFSW because the snapshot is fetched during the Android/Termux upload workflow. The repo must remain private because this project also contains a game-derived Shadow Warrior asset bank for `$DMWOODS.MAP`.

## Why JFSW now

JFSW is the useful midpoint between original DOS Shadow Warrior and modern VoidSW/EDuke32:

- closer to original game/Build-era structure
- already separated enough to run on non-DOS platforms
- SDL-era platform layer gives us places to cut and replace
- much lighter than VoidSW for an N64 target

## Porting rule

Do not try to compile all of JFSW for N64 in one jump.

Use JFSW as the behavior/source base and carve it in this order:

1. Build/map data structs and fixed-point math assumptions.
2. MAP/ART/palette loading behavior.
3. Build renderer visibility/clip/wall loop.
4. Shadow Warrior game state structs.
5. One weapon + one actor.
6. Audio and music much later.

## First code targets after R11

R11 should start extracting the renderer-facing pieces from JFSW/JFBuild into a libdragon-safe layer:

```text
src/jfsw_n64/
  n64_platform.h
  n64_files.c
  n64_video.c
  n64_input.c
  n64_audio_stub.c
  build_compat.h
```

The current R11 ROM still builds the R09 first-person debug renderer. The important R11 change is that the stable repo now contains the JFSW source snapshot and an inventory report so we can port against real files instead of guessing.

## Cut list for N64

Cut or stub first:

- SDL video/audio/input
- OpenGL/Polymost paths
- network/multiplayer
- CD audio/Ogg music
- host filesystem scanning
- editor tools
- voxel rendering
- large dynamic caches
- platform desktop config UI

Keep first:

- Build sector/wall/sprite structs
- MAP interpretation
- ART tile metadata rules
- palette/lookups
- fixed-point movement/rendering behavior
- actor and weapon state machines, later
