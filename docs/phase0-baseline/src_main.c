#include <libdragon.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define BANK_PATH "/dmwoods.s64b"
#define NEAR_Z 128
#define FAR_Z 65536
#define FOCAL 192
#define HORIZON 108
#define MAX_RENDER_WALLS 512
#define SHADOW64_SOFTWARE_RENDERER_ONLY 1

/*
 * R11 deliberately stays on the CPU/software renderer path. No libdragon
 * preview branch, no OpenGL, no RDP triangle path. This keeps the baseline
 * close to Build/JFBuild: project walls, draw vertical columns, sample ART
 * tiles in their native column-major layout, and keep top-down debugging.
 */

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t flags;
    uint32_t palette_offset;
    uint32_t palette_size;
    uint32_t map_offset;
    uint32_t map_size;
    uint32_t tile_dir_offset;
    uint32_t tile_count;
    uint32_t tile_data_offset;
    uint32_t tile_data_size;
    int32_t start_x;
    int32_t start_y;
    int32_t start_z;
    int32_t start_reserved;
    uint16_t start_ang;
    uint16_t start_sector;
    uint16_t num_sectors;
    uint16_t num_walls;
    uint32_t num_sprites;
    uint32_t sector_size;
    uint32_t wall_size;
    uint32_t sprite_size;
    uint32_t tile_entry_size;
    uint32_t map_crc32;
    uint32_t tile_crc32;
    uint32_t reserved0;
    uint8_t reserved1[32];
} s64_bank_header_t;

typedef struct __attribute__((packed)) {
    int16_t wallptr, wallnum;
    int32_t ceilingz, floorz;
    uint16_t ceilingstat, floorstat;
    int16_t ceilingpicnum, ceilingheinum;
    int8_t ceilingshade;
    uint8_t ceilingpal, ceilingxpanning, ceilingypanning;
    int16_t floorpicnum, floorheinum;
    int8_t floorshade;
    uint8_t floorpal, floorxpanning, floorypanning;
    uint8_t visibility, filler;
    int16_t lotag, hitag, extra;
} s64_sector_t;

typedef struct __attribute__((packed)) {
    int32_t x, y;
    int16_t point2, nextwall, nextsector, cstat, picnum, overpicnum;
    int8_t shade;
    uint8_t pal, xrepeat, yrepeat, xpanning, ypanning;
    int16_t lotag, hitag, extra;
} s64_wall_t;

typedef struct __attribute__((packed)) {
    int32_t x, y, z;
    int16_t cstat, picnum;
    int8_t shade;
    uint8_t pal, clipdist, filler, xrepeat, yrepeat;
    int8_t xoffset, yoffset;
    int16_t sectnum, statnum, ang, owner, xvel, yvel, zvel, lotag, hitag, extra;
} s64_sprite_t;

typedef struct __attribute__((packed)) {
    uint16_t picnum;
    uint16_t w;
    uint16_t h;
    uint16_t reserved;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t picanm;
} s64_tile_t;

typedef struct {
    int sx1, sx2;
    int yceil1, yceil2;
    int yfloor1, yfloor2;
    int zavg;
    uint16_t picnum;
    uint8_t pal;
    uint8_t portal;
} render_wall_t;

_Static_assert(sizeof(s64_bank_header_t) == 128, "bad bank header size");
_Static_assert(sizeof(s64_sector_t) == 40, "bad sector size");
_Static_assert(sizeof(s64_wall_t) == 32, "bad wall size");
_Static_assert(sizeof(s64_sprite_t) == 44, "bad sprite size");
_Static_assert(sizeof(s64_tile_t) == 20, "bad tile entry size");

static uint8_t *g_bank;
static const s64_bank_header_t *g_hdr;
static const uint8_t *g_palette;
static const s64_sector_t *g_sectors;
static const s64_wall_t *g_walls;
static const s64_sprite_t *g_sprites;
static const s64_tile_t *g_tiles;
static const uint8_t *g_tile_pixels;
static uint32_t g_pal_rgba[256];

static int32_t cam_x, cam_y, cam_z;
static uint16_t cam_ang;
static int zoom = 6;
static int view_mode = 0; /* 0 = first person, 1 = map */
static int show_help = 1;
static int g_last_render_walls = 0;

/* 0..90 degree sine, 33 samples, scaled by 1024. */
static const int16_t sin_quarter_1024[33] = {
    0, 50, 100, 150, 200, 249, 297, 345, 392, 438, 483, 526, 569, 610, 650, 688,
    724, 759, 792, 822, 851, 878, 903, 926, 946, 964, 980, 993, 1004, 1013, 1019, 1023, 1024
};

static int16_t isin1024(uint16_t angle) {
    uint16_t a = angle & 2047;
    int sign = 1;
    if (a >= 1024) { a -= 1024; sign = -1; }
    if (a > 512) a = 1024 - a;
    int idx = a >> 4;
    if (idx >= 32) return sign * 1024;
    int frac = a & 15;
    int v0 = sin_quarter_1024[idx];
    int v1 = sin_quarter_1024[idx + 1];
    return (int16_t)(sign * (v0 + ((v1 - v0) * frac >> 4)));
}

static int16_t icos1024(uint16_t angle) {
    return isin1024(angle + 512);
}

static uint32_t make_pal_color(uint8_t idx) {
    const uint8_t *p = &g_palette[idx * 3];
    int r = p[0], g = p[1], b = p[2];
    if (r <= 63 && g <= 63 && b <= 63) {
        r <<= 2; g <<= 2; b <<= 2;
    }
    return graphics_make_color(r, g, b, 255);
}

static void rebuild_palette_cache(void) {
    for (int i = 0; i < 256; i++) {
        g_pal_rgba[i] = make_pal_color((uint8_t)i);
    }
}

static const s64_tile_t *find_tile(uint16_t picnum) {
    for (uint32_t i = 0; i < g_hdr->tile_count; i++) {
        if (g_tiles[i].picnum == picnum) return &g_tiles[i];
    }
    return NULL;
}

static uint8_t tile_sample(const s64_tile_t *t, int x, int y) {
    if (!t || !t->w || !t->h) return 0;
    x %= t->w; if (x < 0) x += t->w;
    y %= t->h; if (y < 0) y += t->h;
    const uint8_t *pix = g_tile_pixels + t->data_offset;
    /* Build ART tiles are column-major: offset = x * height + y. */
    return pix[x * t->h + y];
}

static void draw_tile_preview(surface_t *disp, int x0, int y0, uint16_t picnum, int scale) {
    const s64_tile_t *t = find_tile(picnum);
    if (!t) return;

    int tw = t->w;
    int th = t->h;
    if (tw > 64) tw = 64;
    if (th > 64) th = 64;

    for (int y = 0; y < th; y++) {
        for (int x = 0; x < tw; x++) {
            uint8_t idx = tile_sample(t, x, y);
            uint32_t c = g_pal_rgba[idx];
            for (int yy = 0; yy < scale; yy++) {
                for (int xx = 0; xx < scale; xx++) {
                    int sx = x0 + x * scale + xx;
                    int sy = y0 + y * scale + yy;
                    if (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < SCREEN_H) {
                        graphics_draw_pixel(disp, sx, sy, c);
                    }
                }
            }
        }
    }
}

static int map_to_screen_x(int32_t x) {
    return ((x - cam_x) >> zoom) + SCREEN_W / 2;
}

static int map_to_screen_y(int32_t y) {
    return ((y - cam_y) >> zoom) + SCREEN_H / 2;
}

static void render_topdown(surface_t *disp) {
    uint32_t wall_color = graphics_make_color(210, 210, 210, 255);
    uint32_t portal_color = graphics_make_color(80, 150, 255, 255);
    uint32_t player_color = graphics_make_color(255, 60, 60, 255);
    uint32_t sprite_color = graphics_make_color(255, 220, 80, 255);

    for (uint32_t i = 0; i < g_hdr->num_walls; i++) {
        const s64_wall_t *w = &g_walls[i];
        if (w->point2 < 0 || w->point2 >= g_hdr->num_walls) continue;
        const s64_wall_t *w2 = &g_walls[w->point2];
        int x1 = map_to_screen_x(w->x);
        int y1 = map_to_screen_y(w->y);
        int x2 = map_to_screen_x(w2->x);
        int y2 = map_to_screen_y(w2->y);
        graphics_draw_line(disp, x1, y1, x2, y2, w->nextsector >= 0 ? portal_color : wall_color);
    }

    for (uint32_t i = 0; i < g_hdr->num_sprites; i++) {
        int x = map_to_screen_x(g_sprites[i].x);
        int y = map_to_screen_y(g_sprites[i].y);
        graphics_draw_box(disp, x - 1, y - 1, 3, 3, sprite_color);
    }

    int px = map_to_screen_x(cam_x);
    int py = map_to_screen_y(cam_y);
    graphics_draw_box(disp, px - 2, py - 2, 5, 5, player_color);

    int16_t cs = icos1024(cam_ang);
    int16_t sn = isin1024(cam_ang);
    int dx = (cs * 28) >> 10;
    int dy = (sn * 28) >> 10;
    graphics_draw_line(disp, px, py, px + dx, py + dy, player_color);
}

static int cmp_render_wall(const void *a, const void *b) {
    const render_wall_t *wa = (const render_wall_t*)a;
    const render_wall_t *wb = (const render_wall_t*)b;
    return wb->zavg - wa->zavg; /* far to near */
}

static void add_projected_wall(render_wall_t *list, int *count, const s64_sector_t *s, const s64_wall_t *w) {
    if (*count >= MAX_RENDER_WALLS) return;
    if (w->point2 < 0 || w->point2 >= g_hdr->num_walls) return;

    const s64_wall_t *w2 = &g_walls[w->point2];
    int16_t cs = icos1024(cam_ang);
    int16_t sn = isin1024(cam_ang);

    int32_t dx1 = w->x - cam_x;
    int32_t dy1 = w->y - cam_y;
    int32_t dx2 = w2->x - cam_x;
    int32_t dy2 = w2->y - cam_y;

    int32_t x1 = ((int64_t)-dx1 * sn + (int64_t)dy1 * cs) >> 10;
    int32_t z1 = ((int64_t) dx1 * cs + (int64_t)dy1 * sn) >> 10;
    int32_t x2 = ((int64_t)-dx2 * sn + (int64_t)dy2 * cs) >> 10;
    int32_t z2 = ((int64_t) dx2 * cs + (int64_t)dy2 * sn) >> 10;

    if (z1 <= NEAR_Z && z2 <= NEAR_Z) return;
    if (z1 > FAR_Z && z2 > FAR_Z) return;

    if (z1 < NEAR_Z) {
        int32_t dz = z2 - z1;
        if (dz == 0) return;
        int64_t num = NEAR_Z - z1;
        x1 = x1 + (int32_t)(((int64_t)(x2 - x1) * num) / dz);
        z1 = NEAR_Z;
    }
    if (z2 < NEAR_Z) {
        int32_t dz = z1 - z2;
        if (dz == 0) return;
        int64_t num = NEAR_Z - z2;
        x2 = x2 + (int32_t)(((int64_t)(x1 - x2) * num) / dz);
        z2 = NEAR_Z;
    }

    int sx1 = SCREEN_W / 2 + (int)(((int64_t)x1 * FOCAL) / z1);
    int sx2 = SCREEN_W / 2 + (int)(((int64_t)x2 * FOCAL) / z2);
    if (sx1 == sx2) return;
    if ((sx1 < 0 && sx2 < 0) || (sx1 >= SCREEN_W && sx2 >= SCREEN_W)) return;

    int32_t cz = s->ceilingz - cam_z;
    int32_t fz = s->floorz - cam_z;
    int yceil1 = HORIZON + (int)((((int64_t)cz >> 8) * FOCAL) / z1);
    int yfloor1 = HORIZON + (int)((((int64_t)fz >> 8) * FOCAL) / z1);
    int yceil2 = HORIZON + (int)((((int64_t)cz >> 8) * FOCAL) / z2);
    int yfloor2 = HORIZON + (int)((((int64_t)fz >> 8) * FOCAL) / z2);

    render_wall_t *rw = &list[*count];
    rw->sx1 = sx1;
    rw->sx2 = sx2;
    rw->yceil1 = yceil1;
    rw->yceil2 = yceil2;
    rw->yfloor1 = yfloor1;
    rw->yfloor2 = yfloor2;
    rw->zavg = (z1 + z2) >> 1;
    rw->picnum = (uint16_t)w->picnum;
    rw->pal = w->pal;
    rw->portal = w->nextsector >= 0 ? 1 : 0;
    (*count)++;
}

static void draw_projected_wall(surface_t *disp, const render_wall_t *rw) {
    int sx1 = rw->sx1;
    int sx2 = rw->sx2;
    int yc1 = rw->yceil1;
    int yc2 = rw->yceil2;
    int yf1 = rw->yfloor1;
    int yf2 = rw->yfloor2;
    int flip = 0;

    if (sx1 > sx2) {
        int t;
        t = sx1; sx1 = sx2; sx2 = t;
        t = yc1; yc1 = yc2; yc2 = t;
        t = yf1; yf1 = yf2; yf2 = t;
        flip = 1;
    }

    int width = sx2 - sx1;
    if (width <= 0) return;

    const s64_tile_t *tile = find_tile(rw->picnum);
    uint32_t fallback = graphics_make_color(80 + (rw->picnum & 63), 80 + ((rw->picnum >> 2) & 63), 100 + ((rw->picnum >> 4) & 95), 255);
    if (rw->portal) fallback = graphics_make_color(35, 70, 110, 255);

    int startx = sx1 < 0 ? 0 : sx1;
    int endx = sx2 >= SCREEN_W ? SCREEN_W - 1 : sx2;

    for (int x = startx; x <= endx; x++) {
        int local = x - sx1;
        int yceil = yc1 + (int)(((int64_t)(yc2 - yc1) * local) / width);
        int yfloor = yf1 + (int)(((int64_t)(yf2 - yf1) * local) / width);
        if (yceil > yfloor) {
            int tmp = yceil; yceil = yfloor; yfloor = tmp;
        }
        if (yfloor < 0 || yceil >= SCREEN_H) continue;
        int draw_y0 = yceil < 0 ? 0 : yceil;
        int draw_y1 = yfloor >= SCREEN_H ? SCREEN_H - 1 : yfloor;
        int wall_h = yfloor - yceil;
        if (wall_h <= 0) continue;

        int u;
        if (tile) {
            u = ((int64_t)local * tile->w) / width;
            if (flip) u = tile->w - 1 - u;
        } else {
            u = 0;
        }

        for (int y = draw_y0; y <= draw_y1; y++) {
            uint32_t color = fallback;
            if (tile) {
                int v = ((int64_t)(y - yceil) * tile->h) / wall_h;
                uint8_t idx = tile_sample(tile, u, v);
                color = g_pal_rgba[idx];
                if (rw->portal) {
                    /* Cheap darkening so pass-through walls read as debug portals. */
                    if (((x ^ y) & 3) == 0) color = graphics_make_color(30, 70, 120, 255);
                }
            }
            graphics_draw_pixel(disp, x, y, color);
        }
    }
}

static void render_first_person(surface_t *disp) {
    uint32_t sky = graphics_make_color(20, 24, 34, 255);
    uint32_t ground = graphics_make_color(30, 26, 20, 255);
    graphics_draw_box(disp, 0, 0, SCREEN_W, HORIZON, sky);
    graphics_draw_box(disp, 0, HORIZON, SCREEN_W, SCREEN_H - HORIZON, ground);

    render_wall_t list[MAX_RENDER_WALLS];
    int count = 0;
    for (uint32_t si = 0; si < g_hdr->num_sectors; si++) {
        const s64_sector_t *s = &g_sectors[si];
        int start = s->wallptr;
        int end = s->wallptr + s->wallnum;
        if (start < 0 || end > g_hdr->num_walls) continue;
        for (int wi = start; wi < end; wi++) {
            add_projected_wall(list, &count, s, &g_walls[wi]);
        }
    }

    qsort(list, count, sizeof(list[0]), cmp_render_wall);
    for (int i = 0; i < count; i++) {
        draw_projected_wall(disp, &list[i]);
    }
    g_last_render_walls = count;

    uint32_t cross = graphics_make_color(255, 255, 255, 255);
    graphics_draw_line(disp, SCREEN_W / 2 - 4, SCREEN_H / 2, SCREEN_W / 2 + 4, SCREEN_H / 2, cross);
    graphics_draw_line(disp, SCREEN_W / 2, SCREEN_H / 2 - 4, SCREEN_W / 2, SCREEN_H / 2 + 4, cross);
}

static void render_debug(surface_t *disp) {
    char line[128];
    graphics_draw_text(disp, 8, 8, "Shadow64 Phase 0 R11 SOFTWARE / $DMWOODS.MAP");
    snprintf(line, sizeof(line), "view:%s sectors:%u walls:%u sprites:%lu tiles:%lu",
        view_mode == 0 ? "first-person" : "top-down",
        g_hdr->num_sectors, g_hdr->num_walls,
        (unsigned long)g_hdr->num_sprites, (unsigned long)g_hdr->tile_count);
    graphics_draw_text(disp, 8, 20, line);

    snprintf(line, sizeof(line), "x:%ld y:%ld z:%ld ang:%u drawn:%d",
        (long)cam_x, (long)cam_y, (long)cam_z, cam_ang, g_last_render_walls);
    graphics_draw_text(disp, 8, 32, line);
    graphics_draw_text(disp, 8, 44, "renderer: software CPU columns / libdragon trunk / GL disabled");

    if (show_help) {
        graphics_draw_text(disp, 8, 204, "Dpad up/down move  Dpad left/right strafe");
        graphics_draw_text(disp, 8, 216, "C-left/C-right turn  A reset  B switch view");
        graphics_draw_text(disp, 8, 228, "R11 = CPU software renderer only; no OpenGL/preview path");
    }
}

static void load_bank(void) {
    dfs_init(DFS_DEFAULT_LOCATION);

    int fp = dfs_open(BANK_PATH);
    if (fp < 0) return;

    int size = dfs_size(fp);
    g_bank = malloc(size);
    if (!g_bank) {
        dfs_close(fp);
        return;
    }
    dfs_read(g_bank, 1, size, fp);
    dfs_close(fp);

    g_hdr = (const s64_bank_header_t*)g_bank;
    if (memcmp(g_hdr->magic, "S64B", 4) != 0 || g_hdr->version != 1) {
        free(g_bank);
        g_bank = NULL;
        g_hdr = NULL;
        return;
    }

    g_palette = g_bank + g_hdr->palette_offset;
    g_sectors = (const s64_sector_t*)(g_bank + g_hdr->map_offset);
    g_walls = (const s64_wall_t*)((const uint8_t*)g_sectors + g_hdr->num_sectors * sizeof(s64_sector_t));
    g_sprites = (const s64_sprite_t*)((const uint8_t*)g_walls + g_hdr->num_walls * sizeof(s64_wall_t));
    g_tiles = (const s64_tile_t*)(g_bank + g_hdr->tile_dir_offset);
    g_tile_pixels = g_bank + g_hdr->tile_data_offset;
    rebuild_palette_cache();

    cam_x = g_hdr->start_x;
    cam_y = g_hdr->start_y;
    cam_z = g_hdr->start_z;
    cam_ang = g_hdr->start_ang;
}

static void reset_camera(void) {
    if (!g_hdr) return;
    cam_x = g_hdr->start_x;
    cam_y = g_hdr->start_y;
    cam_z = g_hdr->start_z;
    cam_ang = g_hdr->start_ang;
    zoom = 6;
}

static void update_controls(void) {
    struct controller_data held = get_keys_held();
    struct controller_data down = get_keys_down();

    if (!g_hdr) return;

    int16_t cs = icos1024(cam_ang);
    int16_t sn = isin1024(cam_ang);
    int speed = 96;
    if (held.c[0].up) {
        cam_x += ((int32_t)cs * speed) >> 10;
        cam_y += ((int32_t)sn * speed) >> 10;
    }
    if (held.c[0].down) {
        cam_x -= ((int32_t)cs * speed) >> 10;
        cam_y -= ((int32_t)sn * speed) >> 10;
    }
    if (held.c[0].left) {
        cam_x += ((int32_t)sn * speed) >> 10;
        cam_y -= ((int32_t)cs * speed) >> 10;
    }
    if (held.c[0].right) {
        cam_x -= ((int32_t)sn * speed) >> 10;
        cam_y += ((int32_t)cs * speed) >> 10;
    }
    if (held.c[0].C_left) cam_ang -= 12;
    if (held.c[0].C_right) cam_ang += 12;
    if (held.c[0].C_up && view_mode == 1 && zoom > 2) zoom--;
    if (held.c[0].C_down && view_mode == 1 && zoom < 12) zoom++;

    if (down.c[0].A) reset_camera();
    if (down.c[0].B) view_mode = !view_mode;
}

int main(void) {
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    controller_init();

    bool expanded = get_memory_size() >= 8 * 1024 * 1024;
    load_bank();

    while (1) {
        controller_scan();
        update_controls();

        surface_t *disp = display_get();
        graphics_fill_screen(disp, graphics_make_color(0, 0, 0, 255));

        if (!expanded) {
            graphics_draw_text(disp, 40, 100, "Expansion Pak required.");
            graphics_draw_text(disp, 40, 116, "Shadow64 is 8MB-only.");
        } else if (!g_hdr) {
            graphics_draw_text(disp, 24, 100, "Could not load /dmwoods.s64b from DFS.");
            graphics_draw_text(disp, 24, 116, "Check Makefile/DFS asset packing.");
        } else {
            if (view_mode == 0) {
                render_first_person(disp);
            } else {
                render_topdown(disp);
                draw_tile_preview(disp, 248, 48, g_tiles[0].picnum, 1);
            }
            render_debug(disp);
        }

        display_show(disp);
    }

    return 0;
}
