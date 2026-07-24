Shadow64 Phase 0 bank format: S64B v1

All numeric fields are big-endian / N64-native.

Header: 128 bytes
  magic[4] = "S64B"
  version = 1
  flags = 1
  palette_offset, palette_size
  map_offset, map_size
  tile_dir_offset, tile_count
  tile_data_offset, tile_data_size
  start_x, start_y, start_z
  start_ang, start_sector
  num_sectors, num_walls, num_sprites
  sector_size=40, wall_size=32, sprite_size=44, tile_entry_size=20
  crc32 values

Map block:
  sectors[num_sectors]   40 bytes each
  walls[num_walls]       32 bytes each
  sprites[num_sprites]   44 bytes each

Tile dir entry, 20 bytes:
  uint16 picnum
  uint16 width
  uint16 height
  uint16 reserved
  uint32 data_offset relative to tile_data_offset
  uint32 data_size
  uint32 picanm

Tile data:
  raw Build ART pixels, column-major.

Generated from:
  SW.GRP SHA1 4863226c01d0850c65ac0a3e20831e072b285425
  $DMWOODS.MAP CRC32 B5985E0F
  dmwoods.s64b SHA1 2b9acce284e6e7948025173fc57eb32bec2dd72b
