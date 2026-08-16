#include <libdragon.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define BUILD_ANGLE_COUNT 2048
#define BUILD_PI 3.14159265358979323846f
#define MAP_PATH "rom:/sw_first.map"
#define NEAR_PLANE 64.0f
#define PLAYER_EYE_OFFSET (40 * 256)

typedef struct {
    int16_t wallptr;
    int16_t wallnum;
    int32_t ceilingz;
    int32_t floorz;
} map_sector_t;

typedef struct {
    int32_t x;
    int32_t y;
    int16_t point2;
    int16_t nextwall;
    int16_t nextsector;
    int16_t cstat;
    int16_t owner_sector;
} map_wall_t;

typedef struct {
    int version;
    int32_t start_x;
    int32_t start_y;
    int32_t start_z;
    int16_t start_angle;
    int16_t start_sector;
    int16_t sector_count;
    int16_t wall_count;
    int16_t sprite_count;
    map_sector_t *sectors;
    map_wall_t *walls;
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
} map_data_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
    int16_t angle;
    int16_t sector;
} player_t;

static map_data_t g_map;
static player_t g_player;
static bool g_overhead = false;
static char g_status[160] = "boot";

static int16_t le_i16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int32_t le_i32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] |
                     ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}

static bool read_exact(FILE *fp, void *dst, size_t size) {
    return fread(dst, 1, size, fp) == size;
}

static bool read_i16(FILE *fp, int16_t *out) {
    uint8_t raw[2];
    if (!read_exact(fp, raw, sizeof(raw))) return false;
    *out = le_i16(raw);
    return true;
}

static bool read_i32(FILE *fp, int32_t *out) {
    uint8_t raw[4];
    if (!read_exact(fp, raw, sizeof(raw))) return false;
    *out = le_i32(raw);
    return true;
}

static void free_map(map_data_t *map) {
    free(map->sectors);
    free(map->walls);
    memset(map, 0, sizeof(*map));
}

static bool fail_load(FILE *fp, map_data_t *map, const char *reason) {
    if (fp) fclose(fp);
    free_map(map);
    snprintf(g_status, sizeof(g_status), "MAP ERROR: %s", reason);
    debugf("Shadow64 R11B: %s\n", g_status);
    return false;
}

static bool load_map(const char *path, map_data_t *map) {
    uint8_t sector_raw[40];
    uint8_t wall_raw[32];
    FILE *fp = NULL;
    int32_t version = 0;
    int16_t count = 0;

    free_map(map);
    fp = fopen(path, "rb");
    if (!fp) return fail_load(NULL, map, "rom:/sw_first.map not found");

    if (!read_i32(fp, &version)) return fail_load(fp, map, "short version header");
    if (version != 7 && version != 8) return fail_load(fp, map, "only Build v7/v8 maps supported");
    map->version = version;

    if (!read_i32(fp, &map->start_x) ||
        !read_i32(fp, &map->start_y) ||
        !read_i32(fp, &map->start_z) ||
        !read_i16(fp, &map->start_angle) ||
        !read_i16(fp, &map->start_sector)) {
        return fail_load(fp, map, "short player start header");
    }

    if (!read_i16(fp, &count)) return fail_load(fp, map, "missing sector count");
    if (count <= 0 || count > (version == 7 ? 1024 : 4096)) {
        return fail_load(fp, map, "invalid sector count");
    }
    map->sector_count = count;
    map->sectors = calloc((size_t)count, sizeof(*map->sectors));
    if (!map->sectors) return fail_load(fp, map, "sector allocation failed");

    for (int i = 0; i < map->sector_count; ++i) {
        if (!read_exact(fp, sector_raw, sizeof(sector_raw))) {
            return fail_load(fp, map, "short sector table");
        }
        map->sectors[i].wallptr = le_i16(&sector_raw[0]);
        map->sectors[i].wallnum = le_i16(&sector_raw[2]);
        map->sectors[i].ceilingz = le_i32(&sector_raw[4]);
        map->sectors[i].floorz = le_i32(&sector_raw[8]);
    }

    if (!read_i16(fp, &count)) return fail_load(fp, map, "missing wall count");
    if (count <= 0 || count > (version == 7 ? 8192 : 16384)) {
        return fail_load(fp, map, "invalid wall count");
    }
    map->wall_count = count;
    map->walls = calloc((size_t)count, sizeof(*map->walls));
    if (!map->walls) return fail_load(fp, map, "wall allocation failed");

    map->min_x = INT32_MAX;
    map->min_y = INT32_MAX;
    map->max_x = INT32_MIN;
    map->max_y = INT32_MIN;

    for (int i = 0; i < map->wall_count; ++i) {
        map_wall_t *wall = &map->walls[i];
        if (!read_exact(fp, wall_raw, sizeof(wall_raw))) {
            return fail_load(fp, map, "short wall table");
        }
        wall->x = le_i32(&wall_raw[0]);
        wall->y = le_i32(&wall_raw[4]);
        wall->point2 = le_i16(&wall_raw[8]);
        wall->nextwall = le_i16(&wall_raw[10]);
        wall->nextsector = le_i16(&wall_raw[12]);
        wall->cstat = le_i16(&wall_raw[14]);
        wall->owner_sector = -1;

        if (wall->x < map->min_x) map->min_x = wall->x;
        if (wall->x > map->max_x) map->max_x = wall->x;
        if (wall->y < map->min_y) map->min_y = wall->y;
        if (wall->y > map->max_y) map->max_y = wall->y;
    }

    if (!read_i16(fp, &map->sprite_count)) {
        return fail_load(fp, map, "missing sprite count");
    }
    if (map->sprite_count < 0 || map->sprite_count > (version == 7 ? 4096 : 16384)) {
        return fail_load(fp, map, "invalid sprite count");
    }

    for (int sector_index = 0; sector_index < map->sector_count; ++sector_index) {
        const map_sector_t *sector = &map->sectors[sector_index];
        const int first = sector->wallptr;
        const int end = first + sector->wallnum;
        if (first < 0 || sector->wallnum < 0 || end > map->wall_count) {
            return fail_load(fp, map, "sector wall range outside wall table");
        }
        for (int wall_index = first; wall_index < end; ++wall_index) {
            map->walls[wall_index].owner_sector = (int16_t)sector_index;
        }
    }

    for (int i = 0; i < map->wall_count; ++i) {
        if (map->walls[i].point2 < 0 || map->walls[i].point2 >= map->wall_count) {
            return fail_load(fp, map, "wall point2 outside wall table");
        }
    }

    fclose(fp);
    snprintf(g_status, sizeof(g_status), "v%d  sec:%d wall:%d spr:%d",
             map->version, map->sector_count, map->wall_count, map->sprite_count);
    debugf("Shadow64 R11B loaded %s (%s)\n", path, g_status);
    return true;
}

static bool point_inside_sector(const map_data_t *map, int32_t x, int32_t y, int sector_index) {
    if (sector_index < 0 || sector_index >= map->sector_count) return false;
    const map_sector_t *sector = &map->sectors[sector_index];
    bool inside = false;

    for (int n = 0; n < sector->wallnum; ++n) {
        const int wall_index = sector->wallptr + n;
        const map_wall_t *a = &map->walls[wall_index];
        const map_wall_t *b = &map->walls[a->point2];
        const bool crosses = ((a->y > y) != (b->y > y));
        if (crosses) {
            const double intersect_x = (double)(b->x - a->x) * (double)(y - a->y) /
                                       (double)(b->y - a->y) + (double)a->x;
            if ((double)x < intersect_x) inside = !inside;
        }
    }
    return inside;
}

static int16_t find_sector(const map_data_t *map, int32_t x, int32_t y, int16_t hint) {
    if (point_inside_sector(map, x, y, hint)) return hint;

    if (hint >= 0 && hint < map->sector_count) {
        const map_sector_t *sector = &map->sectors[hint];
        for (int n = 0; n < sector->wallnum; ++n) {
            const map_wall_t *wall = &map->walls[sector->wallptr + n];
            if (wall->nextsector >= 0 && point_inside_sector(map, x, y, wall->nextsector)) {
                return wall->nextsector;
            }
        }
    }

    for (int i = 0; i < map->sector_count; ++i) {
        if (point_inside_sector(map, x, y, i)) return (int16_t)i;
    }
    return -1;
}

static void reset_player(void) {
    g_player.x = g_map.start_x;
    g_player.y = g_map.start_y;
    g_player.z = g_map.start_z;
    g_player.angle = (int16_t)(g_map.start_angle & (BUILD_ANGLE_COUNT - 1));
    g_player.sector = find_sector(&g_map, g_player.x, g_player.y, g_map.start_sector);
    if (g_player.sector < 0) g_player.sector = g_map.start_sector;
}

static int64_t orient2d(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy) {
    return (int64_t)(bx - ax) * (int64_t)(cy - ay) -
           (int64_t)(by - ay) * (int64_t)(cx - ax);
}

static bool movement_crosses_wall(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                  const map_wall_t *wall) {
    const map_wall_t *end = &g_map.walls[wall->point2];
    const int64_t a = orient2d(x0, y0, x1, y1, wall->x, wall->y);
    const int64_t b = orient2d(x0, y0, x1, y1, end->x, end->y);
    const int64_t c = orient2d(wall->x, wall->y, end->x, end->y, x0, y0);
    const int64_t d = orient2d(wall->x, wall->y, end->x, end->y, x1, y1);
    return ((a > 0 && b < 0) || (a < 0 && b > 0)) &&
           ((c > 0 && d < 0) || (c < 0 && d > 0));
}

static bool movement_allowed(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                             int16_t current_sector, int16_t *new_sector) {
    const int16_t candidate = find_sector(&g_map, x1, y1, current_sector);
    if (candidate < 0) return false;
    if (current_sector < 0 || current_sector >= g_map.sector_count) {
        *new_sector = candidate;
        return true;
    }

    const map_sector_t *sector = &g_map.sectors[current_sector];
    for (int n = 0; n < sector->wallnum; ++n) {
        const map_wall_t *wall = &g_map.walls[sector->wallptr + n];
        if (!movement_crosses_wall(x0, y0, x1, y1, wall)) continue;
        const bool open_portal = wall->nextsector >= 0 && (wall->cstat & 1) == 0;
        if (!open_portal || candidate != wall->nextsector) return false;
    }

    *new_sector = candidate;
    return true;
}

static void try_move(int32_t dx, int32_t dy) {
    if (dx == 0 && dy == 0) return;

    const int32_t old_x = g_player.x;
    const int32_t old_y = g_player.y;
    const int32_t wanted_x = old_x + dx;
    const int32_t wanted_y = old_y + dy;
    int16_t sector = g_player.sector;

    if (movement_allowed(old_x, old_y, wanted_x, wanted_y, g_player.sector, &sector)) {
        g_player.x = wanted_x;
        g_player.y = wanted_y;
        g_player.sector = sector;
    } else {
        if (movement_allowed(old_x, old_y, wanted_x, old_y, g_player.sector, &sector)) {
            g_player.x = wanted_x;
            g_player.sector = sector;
        }
        if (movement_allowed(g_player.x, old_y, g_player.x, wanted_y, g_player.sector, &sector)) {
            g_player.y = wanted_y;
            g_player.sector = sector;
        }
    }

    if (g_player.sector >= 0 && g_player.sector < g_map.sector_count) {
        const int32_t desired_eye = g_map.sectors[g_player.sector].floorz - PLAYER_EYE_OFFSET;
        g_player.z += (desired_eye - g_player.z) / 8;
    }
}

static int outcode(int x, int y) {
    int code = 0;
    if (x < 0) code |= 1;
    else if (x >= SCREEN_W) code |= 2;
    if (y < 0) code |= 4;
    else if (y >= SCREEN_H) code |= 8;
    return code;
}

static void draw_clipped_line(surface_t *surface, int x0, int y0, int x1, int y1, uint32_t color) {
    int code0 = outcode(x0, y0);
    int code1 = outcode(x1, y1);

    while (true) {
        if ((code0 | code1) == 0) {
            graphics_draw_line(surface, x0, y0, x1, y1, color);
            return;
        }
        if ((code0 & code1) != 0) return;

        const int code = code0 ? code0 : code1;
        int x = 0;
        int y = 0;

        if (code & 8) {
            if (y1 == y0) return;
            x = x0 + (x1 - x0) * (SCREEN_H - 1 - y0) / (y1 - y0);
            y = SCREEN_H - 1;
        } else if (code & 4) {
            if (y1 == y0) return;
            x = x0 + (x1 - x0) * (0 - y0) / (y1 - y0);
            y = 0;
        } else if (code & 2) {
            if (x1 == x0) return;
            y = y0 + (y1 - y0) * (SCREEN_W - 1 - x0) / (x1 - x0);
            x = SCREEN_W - 1;
        } else {
            if (x1 == x0) return;
            y = y0 + (y1 - y0) * (0 - x0) / (x1 - x0);
            x = 0;
        }

        if (code == code0) {
            x0 = x;
            y0 = y;
            code0 = outcode(x0, y0);
        } else {
            x1 = x;
            y1 = y;
            code1 = outcode(x1, y1);
        }
    }
}

static void render_overhead(surface_t *surface, uint32_t solid, uint32_t portal, uint32_t player_color) {
    const float span_x = (float)(g_map.max_x - g_map.min_x);
    const float span_y = (float)(g_map.max_y - g_map.min_y);
    float scale_x = span_x > 0.0f ? 300.0f / span_x : 1.0f;
    float scale_y = span_y > 0.0f ? 200.0f / span_y : 1.0f;
    const float scale = scale_x < scale_y ? scale_x : scale_y;
    const float center_x = ((float)g_map.min_x + (float)g_map.max_x) * 0.5f;
    const float center_y = ((float)g_map.min_y + (float)g_map.max_y) * 0.5f;

    for (int i = 0; i < g_map.wall_count; ++i) {
        const map_wall_t *a = &g_map.walls[i];
        const map_wall_t *b = &g_map.walls[a->point2];
        const int x0 = (int)(160.0f + ((float)a->x - center_x) * scale);
        const int y0 = (int)(120.0f + ((float)a->y - center_y) * scale);
        const int x1 = (int)(160.0f + ((float)b->x - center_x) * scale);
        const int y1 = (int)(120.0f + ((float)b->y - center_y) * scale);
        draw_clipped_line(surface, x0, y0, x1, y1, a->nextsector >= 0 ? portal : solid);
    }

    const int px = (int)(160.0f + ((float)g_player.x - center_x) * scale);
    const int py = (int)(120.0f + ((float)g_player.y - center_y) * scale);
    graphics_draw_box(surface, px - 2, py - 2, 5, 5, player_color);
    const float angle = (float)g_player.angle * BUILD_PI / 1024.0f;
    const int hx = px + (int)(cosf(angle) * 12.0f);
    const int hy = py + (int)(sinf(angle) * 12.0f);
    draw_clipped_line(surface, px, py, hx, hy, player_color);
}

static bool clip_near(float *x1, float *z1, float *x2, float *z2) {
    if (*z1 <= NEAR_PLANE && *z2 <= NEAR_PLANE) return false;
    if (*z1 <= NEAR_PLANE) {
        const float t = (NEAR_PLANE - *z1) / (*z2 - *z1);
        *x1 += (*x2 - *x1) * t;
        *z1 = NEAR_PLANE;
    }
    if (*z2 <= NEAR_PLANE) {
        const float t = (NEAR_PLANE - *z2) / (*z1 - *z2);
        *x2 += (*x1 - *x2) * t;
        *z2 = NEAR_PLANE;
    }
    return true;
}

static int project_y(int32_t world_z, float depth) {
    const float vertical = (float)(world_z - g_player.z) / 16.0f;
    return (int)(120.0f + vertical * 120.0f / depth);
}

static void render_first_person(surface_t *surface, uint32_t solid, uint32_t portal, uint32_t horizon) {
    graphics_draw_line(surface, 0, 120, SCREEN_W - 1, 120, horizon);

    const float angle = (float)g_player.angle * BUILD_PI / 1024.0f;
    const float forward_x = cosf(angle);
    const float forward_y = sinf(angle);
    const float right_x = -forward_y;
    const float right_y = forward_x;

    for (int i = 0; i < g_map.wall_count; ++i) {
        const map_wall_t *wall = &g_map.walls[i];
        const map_wall_t *end = &g_map.walls[wall->point2];
        const int owner = wall->owner_sector;
        if (owner < 0 || owner >= g_map.sector_count) continue;

        const float dx1 = (float)(wall->x - g_player.x);
        const float dy1 = (float)(wall->y - g_player.y);
        const float dx2 = (float)(end->x - g_player.x);
        const float dy2 = (float)(end->y - g_player.y);
        float cam_x1 = dx1 * right_x + dy1 * right_y;
        float cam_z1 = dx1 * forward_x + dy1 * forward_y;
        float cam_x2 = dx2 * right_x + dy2 * right_y;
        float cam_z2 = dx2 * forward_x + dy2 * forward_y;
        if (!clip_near(&cam_x1, &cam_z1, &cam_x2, &cam_z2)) continue;

        const int sx1 = (int)(160.0f + cam_x1 * 150.0f / cam_z1);
        const int sx2 = (int)(160.0f + cam_x2 * 150.0f / cam_z2);
        const map_sector_t *sector = &g_map.sectors[owner];
        const int ceil_y1 = project_y(sector->ceilingz, cam_z1);
        const int ceil_y2 = project_y(sector->ceilingz, cam_z2);
        const int floor_y1 = project_y(sector->floorz, cam_z1);
        const int floor_y2 = project_y(sector->floorz, cam_z2);
        const uint32_t color = wall->nextsector >= 0 ? portal : solid;

        draw_clipped_line(surface, sx1, ceil_y1, sx2, ceil_y2, color);
        draw_clipped_line(surface, sx1, floor_y1, sx2, floor_y2, color);
        if (wall->nextsector < 0 || (wall->cstat & 1) != 0) {
            draw_clipped_line(surface, sx1, ceil_y1, sx1, floor_y1, color);
            draw_clipped_line(surface, sx2, ceil_y2, sx2, floor_y2, color);
        } else if (wall->nextsector < g_map.sector_count) {
            const map_sector_t *next = &g_map.sectors[wall->nextsector];
            const int next_ceil_y1 = project_y(next->ceilingz, cam_z1);
            const int next_ceil_y2 = project_y(next->ceilingz, cam_z2);
            const int next_floor_y1 = project_y(next->floorz, cam_z1);
            const int next_floor_y2 = project_y(next->floorz, cam_z2);
            if (next->ceilingz > sector->ceilingz) {
                draw_clipped_line(surface, sx1, next_ceil_y1, sx2, next_ceil_y2, color);
            }
            if (next->floorz < sector->floorz) {
                draw_clipped_line(surface, sx1, next_floor_y1, sx2, next_floor_y2, color);
            }
        }
    }

    graphics_draw_line(surface, 156, 120, 164, 120, solid);
    graphics_draw_line(surface, 160, 116, 160, 124, solid);
}

static void update_input(void) {
    joypad_poll();
    const joypad_inputs_t input = joypad_get_inputs(JOYPAD_PORT_1);
    const joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    if (pressed.start) reset_player();
    if (pressed.a) g_overhead = !g_overhead;

    int turn = input.stick_x;
    if (input.btn.d_left) turn -= 55;
    if (input.btn.d_right) turn += 55;
    g_player.angle = (int16_t)((g_player.angle + turn / 5) & (BUILD_ANGLE_COUNT - 1));

    int forward = input.stick_y;
    if (input.btn.d_up) forward += 70;
    if (input.btn.d_down) forward -= 70;
    int strafe = 0;
    if (input.btn.c_left || input.btn.l) strafe -= 70;
    if (input.btn.c_right || input.btn.r) strafe += 70;

    if (abs(forward) < 8) forward = 0;
    if (abs(strafe) < 8) strafe = 0;
    const int speed = input.btn.z ? 18 : 10;
    const float angle = (float)g_player.angle * BUILD_PI / 1024.0f;
    const float forward_x = cosf(angle);
    const float forward_y = sinf(angle);
    const float right_x = -forward_y;
    const float right_y = forward_x;
    const int32_t dx = (int32_t)((forward_x * (float)forward + right_x * (float)strafe) * (float)speed);
    const int32_t dy = (int32_t)((forward_y * (float)forward + right_y * (float)strafe) * (float)speed);
    try_move(dx, dy);
}

static void render_frame(void) {
    surface_t *surface = display_get();
    const uint32_t black = color_to_packed16(RGBA32(0, 0, 0, 255));
    const uint32_t white = color_to_packed16(RGBA32(230, 230, 230, 255));
    const uint32_t red = color_to_packed16(RGBA32(255, 70, 70, 255));
    const uint32_t green = color_to_packed16(RGBA32(70, 255, 120, 255));
    const uint32_t blue = color_to_packed16(RGBA32(80, 150, 255, 255));
    const uint32_t gray = color_to_packed16(RGBA32(70, 70, 70, 255));

    graphics_fill_screen(surface, black);
    if (g_map.walls) {
        if (g_overhead) render_overhead(surface, white, blue, green);
        else render_first_person(surface, white, blue, gray);
    }

    graphics_set_color(g_map.walls ? green : red, black);
    graphics_draw_text(surface, 8, 8, "SHADOW64 R11 MAP CORE");
    graphics_set_color(white, black);
    graphics_draw_text(surface, 8, 20, g_status);

    char line[96];
    snprintf(line, sizeof(line), "X:%ld Y:%ld ANG:%d SEC:%d %s",
             (long)g_player.x, (long)g_player.y, g_player.angle,
             g_player.sector, g_overhead ? "MAP" : "3D");
    graphics_draw_text(surface, 8, 220, line);
    display_show(surface);
}

int main(void) {
    debug_init_isviewer();
    debug_init_usblog();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    joypad_init();

    const int dfs_result = dfs_init(DFS_DEFAULT_LOCATION);
    if (dfs_result != DFS_ESUCCESS) {
        snprintf(g_status, sizeof(g_status), "DFS ERROR: %s", dfs_strerror(dfs_result));
    } else if (load_map(MAP_PATH, &g_map)) {
        reset_player();
    }

    while (true) {
        if (g_map.walls) update_input();
        render_frame();
    }
}
