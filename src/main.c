#include <libdragon.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define VIEW_TOP 0
#define VIEW_BOTTOM 218
#define VIEW_H (VIEW_BOTTOM - VIEW_TOP)
#define HORIZON 112
#define FOCAL 150.0f
#define NEAR_PLANE 48.0f
#define BUILD_ANGLE_COUNT 2048
#define BUILD_PI 3.14159265358979323846f
#define MAP_PATH "rom:/sw_first.map"
#define TEX_PATH "rom:/sw_r13_tex.bin"
#define PLAYER_EYE_OFFSET (40 * 256)
#define PLAYER_CROUCH_EYE_OFFSET (24 * 256)
#define PLAYER_STEP_HEIGHT (24 * 256)
#define PLAYER_HEIGHT (48 * 256)
#define MAX_VISIBLE_SECTORS 128
#define MAX_PORTAL_DEPTH 10
#define SWORD_REST 2080
#define SWORD_SWING0 2081
#define SWORD_SWING1 2082
#define SWORD_SWING2 2083
#define TILE_TRANSPARENT 255

/* Shadow Warrior tags / tile IDs used by R13. */
#define TAG_DOOR_ROTATE 112
#define TAG_DOOR_ROTATE_POS 113
#define TAG_DOOR_ROTATE_NEG 114
#define TAG_DOOR_SLIDING 115
#define TAG_LEVEL_EXIT_SWITCH 116
#define TAG_VATOR 206
#define TAG_SPRITE_SWITCH_VATOR 206
#define TAG_SWITCH_EVERYTHING 211
#define TAG_SWITCH_EVERYTHING_ONCE 212
#define TAG_SO_EVENT_SWITCH 215
#define TAG_ROTATOR 218
#define TAG_SLIDOR 220
#define SWITCH_LOCKED 29

#define ICON_UZI 1797
#define ICON_UZIFLOOR 1807
#define ICON_LG_UZI_AMMO 1799
#define ICON_SM_MEDKIT 1802
#define ICON_MEDKIT 1803
#define ICON_ARMOR 3030
#define KEY_TILE_FIRST 1765
#define KEY_TILE_LAST 1780

#define UZI_REST 2004
#define UZI_FIRE_0 2006
#define UZI_FIRE_1 2008
#define NINJA_RUN_R0 4096
#define NINJA_CRAWL_R0 4162
#define NINJA_PAIN_R0 4219
#define NINJA_DEAD 4227
#define COOLIE_RUN_R0 1400
#define COOLIE_PAIN_R0 1420
#define COOLIE_DEAD 4268

enum { WPN_SWORD = 0, WPN_UZI = 1 };

/* Build v7/v8 fields used by R13. Layout follows jfbuild/include/build.h. */
typedef struct {
    int16_t wallptr, wallnum;
    int32_t ceilingz, floorz;
    int16_t ceilingstat, floorstat;
    int16_t ceilingpicnum, ceilingheinum;
    int8_t ceilingshade;
    uint8_t ceilingpal, ceilingxpanning, ceilingypanning;
    int16_t floorpicnum, floorheinum;
    int8_t floorshade;
    uint8_t floorpal, floorxpanning, floorypanning;
    uint8_t visibility, filler;
    int16_t lotag, hitag, extra;
} map_sector_t;

typedef struct {
    int32_t x, y;
    int16_t point2, nextwall, nextsector, cstat;
    int16_t picnum, overpicnum;
    int8_t shade;
    uint8_t pal, xrepeat, yrepeat, xpanning, ypanning;
    int16_t lotag, hitag, extra;
    int16_t owner_sector;
} map_wall_t;

typedef struct {
    int32_t x, y, z;
    int16_t cstat, picnum;
    int8_t shade;
    uint8_t pal, clipdist, filler, xrepeat, yrepeat;
    int8_t xoffset, yoffset;
    int16_t sectnum, statnum, ang, owner, xvel, yvel, zvel, lotag, hitag, extra;
    int8_t hp;
    bool dead;
    bool pickup_taken;
    uint8_t attack_cooldown;
    uint8_t state_timer;
    int16_t base_picnum;
} map_sprite_t;

typedef struct {
    int version;
    int32_t start_x, start_y, start_z;
    int16_t start_angle, start_sector;
    int16_t sector_count, wall_count, sprite_count;
    map_sector_t *sectors;
    map_wall_t *walls;
    map_sprite_t *sprites;
    int32_t min_x, min_y, max_x, max_y;
} map_data_t;

typedef struct {
    int32_t x, y, z;
    int16_t angle, sector;
    int health;
    int armor;
    int kills;
    int pickups;
    int weapon_timer;
    int fire_cooldown;
    int damage_flash;
    int uzi_ammo;
    int weapon;
    int pitch;
    int32_t vertical_vel;
    uint8_t keys;
    bool grounded;
    bool crouching;
    bool exited;
    bool paused;
} player_t;

typedef struct {
    uint16_t id, w, h, flags;
    uint32_t picanm;
    uint32_t data_offset, data_size;
    const uint8_t *pixels;
    uint8_t avg_index;
} texture_t;

typedef struct {
    uint8_t *blob;
    size_t blob_size;
    uint8_t palette[768];
    uint16_t palette16[32][256];
    texture_t *tiles;
    uint32_t tile_count;
} texture_bank_t;

typedef struct { int16_t sector; uint8_t depth; } sector_queue_t;

static map_data_t g_map;
static texture_bank_t g_tex;
static player_t g_player;
static bool g_overhead = false;
static char g_status[192] = "boot";
static char g_message[80] = "";
static int g_message_timer = 0;
static float g_zbuf[SCREEN_W * VIEW_H];
static bool g_sector_visible[4096];
static bool g_sector_open[4096];
static int32_t g_sector_floor_base[4096];
static int32_t g_sector_ceiling_base[4096];
static int32_t g_sector_floor_target[4096];
static uint32_t g_frame = 0;

static int16_t le_i16(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint16_t le_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static int32_t le_i32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }
static uint32_t le_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static bool read_exact(FILE *fp, void *dst, size_t size) { return fread(dst, 1, size, fp) == size; }
static bool read_i16(FILE *fp, int16_t *out) { uint8_t b[2]; if (!read_exact(fp,b,2)) return false; *out=le_i16(b); return true; }
static bool read_i32(FILE *fp, int32_t *out) { uint8_t b[4]; if (!read_exact(fp,b,4)) return false; *out=le_i32(b); return true; }

static uint16_t pack_rgb16(int r, int g, int b) {
    if (r < 0) r = 0;
    if (r > 31) r = 31;
    if (g < 0) g = 0;
    if (g > 31) g = 31;
    if (b < 0) b = 0;
    if (b > 31) b = 31;
    return (uint16_t)((r << 11) | (g << 6) | (b << 1) | 1);
}

static int shade_level(int8_t shade) {
    int level = 28 - ((int)shade / 4);
    if (level < 3) level = 3;
    if (level > 31) level = 31;
    return level;
}

static void free_map(map_data_t *m) {
    free(m->sectors); free(m->walls); free(m->sprites); memset(m, 0, sizeof(*m));
}
static void free_textures(texture_bank_t *t) {
    free(t->tiles); free(t->blob); memset(t, 0, sizeof(*t));
}

static bool fail_load(FILE *fp, const char *reason) {
    if (fp) fclose(fp);
    snprintf(g_status, sizeof(g_status), "LOAD ERROR: %s", reason);
    debugf("Shadow64 R13: %s\n", g_status);
    return false;
}

static bool load_map(const char *path, map_data_t *m) {
    uint8_t raw[44]; FILE *fp = NULL; int32_t version; int16_t count;
    free_map(m);
    fp = fopen(path, "rb"); if (!fp) return fail_load(NULL, "map missing");
    if (!read_i32(fp, &version) || (version != 7 && version != 8)) return fail_load(fp, "bad map version");
    m->version = version;
    if (!read_i32(fp,&m->start_x)||!read_i32(fp,&m->start_y)||!read_i32(fp,&m->start_z)||!read_i16(fp,&m->start_angle)||!read_i16(fp,&m->start_sector)) return fail_load(fp,"short map header");
    if (!read_i16(fp,&count) || count<=0 || count>(version==7?1024:4096)) return fail_load(fp,"bad sector count");
    m->sector_count=count; m->sectors=calloc((size_t)count,sizeof(*m->sectors)); if(!m->sectors) return fail_load(fp,"sector alloc");
    for(int i=0;i<count;i++){
        if(!read_exact(fp,raw,40)) return fail_load(fp,"short sectors");
        map_sector_t *s=&m->sectors[i];
        s->wallptr=le_i16(raw+0); s->wallnum=le_i16(raw+2); s->ceilingz=le_i32(raw+4); s->floorz=le_i32(raw+8);
        s->ceilingstat=le_i16(raw+12); s->floorstat=le_i16(raw+14); s->ceilingpicnum=le_i16(raw+16); s->ceilingheinum=le_i16(raw+18);
        s->ceilingshade=(int8_t)raw[20]; s->ceilingpal=raw[21]; s->ceilingxpanning=raw[22]; s->ceilingypanning=raw[23];
        s->floorpicnum=le_i16(raw+24); s->floorheinum=le_i16(raw+26); s->floorshade=(int8_t)raw[28]; s->floorpal=raw[29]; s->floorxpanning=raw[30]; s->floorypanning=raw[31];
        s->visibility=raw[32]; s->filler=raw[33]; s->lotag=le_i16(raw+34); s->hitag=le_i16(raw+36); s->extra=le_i16(raw+38);
    }
    if(!read_i16(fp,&count)||count<=0||count>(version==7?8192:16384)) return fail_load(fp,"bad wall count");
    m->wall_count=count; m->walls=calloc((size_t)count,sizeof(*m->walls)); if(!m->walls) return fail_load(fp,"wall alloc");
    m->min_x=INT32_MAX; m->min_y=INT32_MAX; m->max_x=INT32_MIN; m->max_y=INT32_MIN;
    for(int i=0;i<count;i++){
        if(!read_exact(fp,raw,32)) return fail_load(fp,"short walls");
        map_wall_t *w=&m->walls[i];
        w->x=le_i32(raw+0); w->y=le_i32(raw+4); w->point2=le_i16(raw+8); w->nextwall=le_i16(raw+10); w->nextsector=le_i16(raw+12); w->cstat=le_i16(raw+14);
        w->picnum=le_i16(raw+16); w->overpicnum=le_i16(raw+18); w->shade=(int8_t)raw[20]; w->pal=raw[21]; w->xrepeat=raw[22]; w->yrepeat=raw[23]; w->xpanning=raw[24]; w->ypanning=raw[25];
        w->lotag=le_i16(raw+26); w->hitag=le_i16(raw+28); w->extra=le_i16(raw+30); w->owner_sector=-1;
        if(w->x<m->min_x)m->min_x=w->x;
        if(w->x>m->max_x)m->max_x=w->x;
        if(w->y<m->min_y)m->min_y=w->y;
        if(w->y>m->max_y)m->max_y=w->y;
    }
    if(!read_i16(fp,&count)||count<0||count>(version==7?4096:16384)) return fail_load(fp,"bad sprite count");
    m->sprite_count=count; m->sprites=calloc((size_t)count,sizeof(*m->sprites)); if(count && !m->sprites) return fail_load(fp,"sprite alloc");
    for(int i=0;i<count;i++){
        if(!read_exact(fp,raw,44)) return fail_load(fp,"short sprites");
        map_sprite_t *s=&m->sprites[i];
        s->x=le_i32(raw+0); s->y=le_i32(raw+4); s->z=le_i32(raw+8); s->cstat=le_i16(raw+12); s->picnum=le_i16(raw+14); s->shade=(int8_t)raw[16]; s->pal=raw[17]; s->clipdist=raw[18]; s->filler=raw[19];
        s->xrepeat=raw[20]; s->yrepeat=raw[21]; s->xoffset=(int8_t)raw[22]; s->yoffset=(int8_t)raw[23]; s->sectnum=le_i16(raw+24); s->statnum=le_i16(raw+26); s->ang=le_i16(raw+28); s->owner=le_i16(raw+30); s->xvel=le_i16(raw+32); s->yvel=le_i16(raw+34); s->zvel=le_i16(raw+36); s->lotag=le_i16(raw+38); s->hitag=le_i16(raw+40); s->extra=le_i16(raw+42);
        s->base_picnum=s->picnum;
        if(s->picnum==NINJA_RUN_R0 || s->picnum==NINJA_CRAWL_R0) s->hp=8;
        else if(s->picnum==COOLIE_RUN_R0 || s->picnum==1441) s->hp=12;
        else s->hp=0;
    }
    fclose(fp);
    for(int si=0;si<m->sector_count;si++){
        map_sector_t *s=&m->sectors[si]; int first=s->wallptr, end=first+s->wallnum;
        if(first<0||s->wallnum<0||end>m->wall_count) return fail_load(NULL,"sector wall range");
        for(int wi=first;wi<end;wi++)m->walls[wi].owner_sector=(int16_t)si;
    }
    for(int i=0;i<m->wall_count;i++) if(m->walls[i].point2<0||m->walls[i].point2>=m->wall_count) return fail_load(NULL,"bad point2");
    snprintf(g_status,sizeof(g_status),"v%d sec:%d wall:%d spr:%d",m->version,m->sector_count,m->wall_count,m->sprite_count);
    return true;
}

static const texture_t *find_texture(uint16_t id) {
    int lo=0, hi=(int)g_tex.tile_count-1;
    while(lo<=hi){int mid=(lo+hi)>>1; uint16_t v=g_tex.tiles[mid].id; if(v==id)return &g_tex.tiles[mid]; if(v<id)lo=mid+1; else hi=mid-1;}
    return NULL;
}

static bool load_texture_bank(const char *path, texture_bank_t *t) {
    free_textures(t); FILE *fp=fopen(path,"rb"); if(!fp)return fail_load(NULL,"texture bank missing");
    if(fseek(fp,0,SEEK_END)!=0)return fail_load(fp,"texture seek");
    long sz=ftell(fp);
    if(sz<28)return fail_load(fp,"texture bank short");
    rewind(fp);
    t->blob=malloc((size_t)sz); if(!t->blob)return fail_load(fp,"texture bank alloc"); t->blob_size=(size_t)sz;
    if(!read_exact(fp,t->blob,t->blob_size)){free_textures(t);return fail_load(fp,"texture bank read");} fclose(fp);
    const uint8_t *p=t->blob; if(memcmp(p,"S64TX13\0",8)!=0){free_textures(t);return fail_load(NULL,"texture magic");}
    uint32_t version=le_u32(p+8), count=le_u32(p+12), palbytes=le_u32(p+16), descbytes=le_u32(p+20), pixbytes=le_u32(p+24);
    if(version!=1||palbytes!=768||descbytes!=count*20u||28u+palbytes+descbytes+pixbytes>t->blob_size){free_textures(t);return fail_load(NULL,"texture bank layout");}
    memcpy(t->palette,p+28,768); t->tile_count=count; t->tiles=calloc(count,sizeof(*t->tiles)); if(count&&!t->tiles){free_textures(t);return fail_load(NULL,"texture desc alloc");}
    const uint8_t *d=p+28+768;
    for(uint32_t i=0;i<count;i++,d+=20){
        texture_t *x=&t->tiles[i]; x->id=le_u16(d); x->w=le_u16(d+2); x->h=le_u16(d+4); x->flags=le_u16(d+6); x->picanm=le_u32(d+8); x->data_offset=le_u32(d+12); x->data_size=le_u32(d+16);
        if((size_t)x->data_offset+x->data_size>t->blob_size || x->data_size!=(uint32_t)x->w*x->h){free_textures(t);return fail_load(NULL,"texture descriptor bounds");}
        x->pixels=t->blob+x->data_offset; uint32_t sum=0,n=0; for(uint32_t k=0;k<x->data_size;k++){uint8_t q=x->pixels[k]; if(q!=255){sum+=q;n++;}} x->avg_index=(uint8_t)(n?sum/n:0);
    }
    for(int level=0;level<32;level++)for(int i=0;i<256;i++){
        int r=(t->palette[i*3+0]&63)>>1, g=(t->palette[i*3+1]&63)>>1, b=(t->palette[i*3+2]&63)>>1;
        r=r*level/31; g=g*level/31; b=b*level/31; t->palette16[level][i]=pack_rgb16(r,g,b);
    }
    return true;
}

static uint8_t texel(const texture_t *t, int u, int v) {
    if(!t||!t->pixels||t->w==0||t->h==0)return 0;
    u%=t->w;
    v%=t->h;
    if(u<0)u+=t->w;
    if(v<0)v+=t->h;
    return t->pixels[u*t->h+v];
}

static inline void raw_pixel(surface_t *s,int x,int y,uint16_t c){ if((unsigned)x>=SCREEN_W||(unsigned)y>=SCREEN_H)return; uint16_t *row=(uint16_t*)((uint8_t*)s->buffer+(size_t)y*s->stride); row[x]=c; }
static inline void zpixel(surface_t *s,int x,int y,float z,uint16_t c){ if((unsigned)x>=SCREEN_W||y<VIEW_TOP||y>=VIEW_BOTTOM)return; int idx=(y-VIEW_TOP)*SCREEN_W+x; if(z<g_zbuf[idx]){g_zbuf[idx]=z;raw_pixel(s,x,y,c);} }

static void camera_basis(float*fx,float*fy,float*rx,float*ry);

static void set_message(const char *msg) {
    snprintf(g_message, sizeof(g_message), "%s", msg ? msg : "");
    g_message_timer = 120;
}

static bool sector_tag_is_door(int16_t tag) {
    return tag == TAG_DOOR_ROTATE || tag == TAG_DOOR_ROTATE_POS ||
           tag == TAG_DOOR_ROTATE_NEG || tag == TAG_DOOR_SLIDING;
}

static bool sector_tag_is_operator(int16_t tag) {
    return sector_tag_is_door(tag) || tag == TAG_VATOR ||
           tag == TAG_ROTATOR || tag == TAG_SLIDOR;
}

static int32_t sector_slope_z(int si, int32_t x, int32_t y, bool floor_plane) {
    if (si < 0 || si >= g_map.sector_count) return 0;
    const map_sector_t *sec = &g_map.sectors[si];
    int32_t base = floor_plane ? sec->floorz : sec->ceilingz;
    int16_t stat = floor_plane ? sec->floorstat : sec->ceilingstat;
    int16_t heinum = floor_plane ? sec->floorheinum : sec->ceilingheinum;
    if (!(stat & 2) || sec->wallnum <= 0) return base;
    const map_wall_t *w = &g_map.walls[sec->wallptr];
    const map_wall_t *w2 = &g_map.walls[w->point2];
    int64_t dx = (int64_t)w2->x - w->x;
    int64_t dy = (int64_t)w2->y - w->y;
    double len = sqrt((double)(dx * dx + dy * dy));
    if (len < 1.0) return base;
    int64_t cross = dx * ((int64_t)y - w->y) - dy * ((int64_t)x - w->x);
    double dz = ((double)heinum * (double)cross) / (len * 256.0);
    return base + (int32_t)dz;
}

static int32_t sector_floor_z(int si, int32_t x, int32_t y) {
    return sector_slope_z(si, x, y, true);
}

static int32_t sector_ceiling_z(int si, int32_t x, int32_t y) {
    return sector_slope_z(si, x, y, false);
}

static bool point_inside_sector(const map_data_t *m,int32_t x,int32_t y,int si){
    if(si<0||si>=m->sector_count)return false;
    const map_sector_t *s=&m->sectors[si];
    bool inside=false;
    for(int n=0;n<s->wallnum;n++){const map_wall_t *a=&m->walls[s->wallptr+n],*b=&m->walls[a->point2]; if((a->y>y)!=(b->y>y)){double ix=(double)(b->x-a->x)*(double)(y-a->y)/(double)(b->y-a->y)+(double)a->x; if((double)x<ix)inside=!inside;}}
    return inside;
}
static int16_t find_sector(const map_data_t *m,int32_t x,int32_t y,int16_t hint){
    if(point_inside_sector(m,x,y,hint))return hint;
    if(hint>=0&&hint<m->sector_count){const map_sector_t*s=&m->sectors[hint];for(int n=0;n<s->wallnum;n++){int16_t ns=m->walls[s->wallptr+n].nextsector;if(ns>=0&&point_inside_sector(m,x,y,ns))return ns;}}
    for(int i=0;i<m->sector_count;i++)if(point_inside_sector(m,x,y,i))return(int16_t)i;
    return -1;
}
static int64_t orient2d(int32_t ax,int32_t ay,int32_t bx,int32_t by,int32_t cx,int32_t cy){return(int64_t)(bx-ax)*(cy-ay)-(int64_t)(by-ay)*(cx-ax);}
static bool crosses_wall(int32_t x0,int32_t y0,int32_t x1,int32_t y1,const map_wall_t*w){const map_wall_t*e=&g_map.walls[w->point2];int64_t a=orient2d(x0,y0,x1,y1,w->x,w->y),b=orient2d(x0,y0,x1,y1,e->x,e->y),c=orient2d(w->x,w->y,e->x,e->y,x0,y0),d=orient2d(w->x,w->y,e->x,e->y,x1,y1);return((a>0&&b<0)||(a<0&&b>0))&&((c>0&&d<0)||(c<0&&d>0));}

static bool door_sector_blocks(int si) {
    if (si < 0 || si >= g_map.sector_count) return false;
    return sector_tag_is_door(g_map.sectors[si].lotag) && !g_sector_open[si];
}

static bool wall_is_open_portal(const map_wall_t *w) {
    if (w->nextsector < 0 || w->nextsector >= g_map.sector_count) return false;
    if (w->cstat & 1) return false;
    if (door_sector_blocks(w->owner_sector) || door_sector_blocks(w->nextsector)) return false;
    return true;
}

static bool move_allowed(int32_t x0,int32_t y0,int32_t x1,int32_t y1,int16_t cur,int16_t*newsec){
    int16_t cand=find_sector(&g_map,x1,y1,cur); if(cand<0)return false;
    if(cur<0||cur>=g_map.sector_count){*newsec=cand;return true;}
    const map_sector_t*s=&g_map.sectors[cur];
    for(int n=0;n<s->wallnum;n++){
        const map_wall_t*w=&g_map.walls[s->wallptr+n];
        if(!crosses_wall(x0,y0,x1,y1,w))continue;
        if(!wall_is_open_portal(w)||cand!=w->nextsector)return false;
    }
    if(cand != cur){
        int32_t cur_floor=sector_floor_z(cur,x0,y0);
        int32_t next_floor=sector_floor_z(cand,x1,y1);
        int32_t next_ceil=sector_ceiling_z(cand,x1,y1);
        if(next_floor < cur_floor-PLAYER_STEP_HEIGHT)return false;
        if(next_floor-next_ceil < PLAYER_HEIGHT)return false;
    }
    *newsec=cand;return true;
}

static void try_move_player(int32_t dx,int32_t dy){
    if(!dx&&!dy)return;
    int16_t oldsec=g_player.sector;
    int32_t ox=g_player.x,oy=g_player.y,wx=ox+dx,wy=oy+dy;
    int16_t sec=g_player.sector;
    if(move_allowed(ox,oy,wx,wy,g_player.sector,&sec)){g_player.x=wx;g_player.y=wy;g_player.sector=sec;}
    else{
        if(move_allowed(ox,oy,wx,oy,g_player.sector,&sec)){g_player.x=wx;g_player.sector=sec;}
        if(move_allowed(g_player.x,oy,g_player.x,wy,g_player.sector,&sec)){g_player.y=wy;g_player.sector=sec;}
    }
    if(g_player.sector!=oldsec && g_player.sector>=0 && g_player.sector<g_map.sector_count){
        int16_t tag=g_map.sectors[g_player.sector].lotag;
        if(tag==TAG_LEVEL_EXIT_SWITCH){g_player.exited=true;set_message("LEVEL EXIT TRIGGERED");}
    }
}

static int32_t nearest_adjacent_floor(int si) {
    const map_sector_t *s=&g_map.sectors[si];
    int32_t base=g_sector_floor_base[si],best=base;
    int64_t bestdiff=INT64_MAX;
    for(int n=0;n<s->wallnum;n++){
        const map_wall_t *w=&g_map.walls[s->wallptr+n];
        int ns=w->nextsector;
        if(ns<0||ns>=g_map.sector_count)continue;
        int32_t z=g_sector_floor_base[ns];
        int64_t d=llabs((long long)z-base);
        if(d>256 && d<bestdiff){best=z;bestdiff=d;}
    }
    return best;
}

static void toggle_sector_operator(int si) {
    if(si<0||si>=g_map.sector_count)return;
    map_sector_t *sec=&g_map.sectors[si];
    if(!sector_tag_is_operator(sec->lotag))return;
    g_sector_open[si]=!g_sector_open[si];
    if(sec->lotag==TAG_VATOR){
        g_sector_floor_target[si]=g_sector_open[si]?nearest_adjacent_floor(si):g_sector_floor_base[si];
    }
}

static void activate_match(int16_t match) {
    if(match==0)return;
    int changed=0;
    for(int i=0;i<g_map.sector_count;i++){
        if(g_map.sectors[i].hitag==match && sector_tag_is_operator(g_map.sectors[i].lotag)){
            toggle_sector_operator(i);changed++;
        }
    }
    for(int i=0;i<g_map.sprite_count;i++){
        map_sprite_t *sp=&g_map.sprites[i];
        if(sp->hitag!=match)continue;
        if(sp->lotag==TAG_LEVEL_EXIT_SWITCH){g_player.exited=true;changed++;}
    }
    if(changed)set_message("SWITCH ACTIVATED");
}

static bool switch_picture(int pic) {
    switch(pic){
        case 551:case 552:case 553:case 554:case 558:case 559:case 561:case 562:
        case 563:case 564:case 565:case 566:case 567:case 568:case 569:case 570:
        case 571:case 572:case 573:case 574:case 575:case 576:case 577:case 578:
        case 579:case 580:case 581:case 582:case 583:case 584:case 2470:case 2471:
            return true;
        default:return false;
    }
}

static void animate_switch_sprite(map_sprite_t *sp) {
    if(!switch_picture(sp->picnum))return;
    switch(sp->picnum){
        case 552:case 554:case 559:case 562:case 564:case 566:case 568:case 570:
        case 572:case 574:case 576:case 578:case 580:case 582:case 584:case 2471:
            sp->picnum--;
            break;
        default:sp->picnum++;break;
    }
}

static bool operate_sprite(map_sprite_t *sp) {
    if(!sp||sp->pickup_taken)return false;
    if(sp->lotag==TAG_LEVEL_EXIT_SWITCH){
        animate_switch_sprite(sp);g_player.exited=true;set_message("SEPPUKU STATION COMPLETE");return true;
    }
    if(sp->lotag==SWITCH_LOCKED){
        int key=sp->hitag;
        if(key>=1&&key<=8&&(g_player.keys&(1u<<(key-1)))){
            animate_switch_sprite(sp);
            for(int i=0;i<g_map.sector_count;i++)if(sector_tag_is_door(g_map.sectors[i].lotag))g_sector_open[i]=true;
            set_message("KEY LOCK OPENED");
        }else set_message("YOU NEED THE KEY");
        return true;
    }
    if(sp->lotag==TAG_SPRITE_SWITCH_VATOR||sp->lotag==TAG_SWITCH_EVERYTHING||
       sp->lotag==TAG_SWITCH_EVERYTHING_ONCE||sp->lotag==TAG_SO_EVENT_SWITCH){
        animate_switch_sprite(sp);activate_match(sp->hitag);
        if(sp->lotag==TAG_SWITCH_EVERYTHING_ONCE){sp->lotag=0;sp->hitag=0;}
        return true;
    }
    if(switch_picture(sp->picnum)&&sp->hitag){animate_switch_sprite(sp);activate_match(sp->hitag);return true;}
    return false;
}

static bool operate_nearby(void) {
    float fx,fy,rx,ry;camera_basis(&fx,&fy,&rx,&ry);(void)rx;(void)ry;
    int best=-1;float bestdist=1800.0f;
    for(int i=0;i<g_map.sprite_count;i++){
        map_sprite_t *sp=&g_map.sprites[i];
        if(sp->pickup_taken||sp->dead)continue;
        float dx=(float)(sp->x-g_player.x),dy=(float)(sp->y-g_player.y);
        float forward=dx*fx+dy*fy;if(forward<0)continue;
        float dist=sqrtf(dx*dx+dy*dy);
        if(dist<bestdist&&(sp->lotag!=0||switch_picture(sp->picnum))){best=i;bestdist=dist;}
    }
    if(best>=0&&operate_sprite(&g_map.sprites[best]))return true;
    int si=g_player.sector;
    if(si>=0&&si<g_map.sector_count&&sector_tag_is_operator(g_map.sectors[si].lotag)){
        toggle_sector_operator(si);set_message(g_sector_open[si]?"OPEN":"CLOSED");return true;
    }
    if(si>=0&&si<g_map.sector_count){
        const map_sector_t *sec=&g_map.sectors[si];
        int bestsec=-1;float bestwall=1800.0f;
        for(int n=0;n<sec->wallnum;n++){
            const map_wall_t *w=&g_map.walls[sec->wallptr+n],*e=&g_map.walls[w->point2];
            float mx=((float)w->x+e->x)*0.5f,my=((float)w->y+e->y)*0.5f;
            float dx=mx-g_player.x,dy=my-g_player.y,forward=dx*fx+dy*fy;
            float dist=sqrtf(dx*dx+dy*dy);
            if(forward>0&&dist<bestwall&&w->nextsector>=0&&sector_tag_is_operator(g_map.sectors[w->nextsector].lotag)){
                bestwall=dist;bestsec=w->nextsector;
            }
        }
        if(bestsec>=0){toggle_sector_operator(bestsec);set_message(g_sector_open[bestsec]?"OPEN":"CLOSED");return true;}
    }
    set_message("NOTHING TO USE");return false;
}

static void update_sector_motion(void) {
    for(int i=0;i<g_map.sector_count;i++){
        int32_t target=g_sector_floor_target[i];
        int32_t cur=g_map.sectors[i].floorz;
        if(cur==target)continue;
        int32_t step=256;
        if(cur<target){cur+=step;if(cur>target)cur=target;}
        else{cur-=step;if(cur<target)cur=target;}
        g_map.sectors[i].floorz=cur;
    }
}

static void reset_player(void){
    memset(g_sector_open,0,sizeof(g_sector_open));
    for(int i=0;i<g_map.sector_count&&i<4096;i++){
        g_sector_floor_base[i]=g_map.sectors[i].floorz;
        g_sector_ceiling_base[i]=g_map.sectors[i].ceilingz;
        g_sector_floor_target[i]=g_map.sectors[i].floorz;
    }
    g_player.x=g_map.start_x;g_player.y=g_map.start_y;g_player.z=g_map.start_z;
    g_player.angle=g_map.start_angle&(BUILD_ANGLE_COUNT-1);
    g_player.sector=find_sector(&g_map,g_player.x,g_player.y,g_map.start_sector);if(g_player.sector<0)g_player.sector=g_map.start_sector;
    g_player.health=100;g_player.armor=0;g_player.kills=0;g_player.pickups=0;g_player.weapon_timer=0;g_player.fire_cooldown=0;g_player.damage_flash=0;
    g_player.uzi_ammo=0;g_player.weapon=WPN_SWORD;g_player.pitch=0;g_player.vertical_vel=0;g_player.keys=0;g_player.grounded=true;g_player.crouching=false;g_player.exited=false;g_player.paused=false;
    g_overhead=false;g_message[0]='\0';g_message_timer=0;
    for(int i=0;i<g_map.sprite_count;i++){
        map_sprite_t *sp=&g_map.sprites[i];
        sp->dead=false;sp->pickup_taken=false;sp->attack_cooldown=0;sp->state_timer=0;sp->picnum=sp->base_picnum;
        if(sp->base_picnum==NINJA_RUN_R0||sp->base_picnum==NINJA_CRAWL_R0)sp->hp=8;
        else if(sp->base_picnum==COOLIE_RUN_R0||sp->base_picnum==1441)sp->hp=12;
        else sp->hp=0;
    }
}

static void camera_basis(float*fx,float*fy,float*rx,float*ry){float a=(float)g_player.angle*BUILD_PI/1024.0f;*fx=cosf(a);*fy=sinf(a);*rx=-*fy;*ry=*fx;}
static int view_horizon(void){return HORIZON+g_player.pitch;}
static int project_y(int32_t wz,float depth){return(int)(view_horizon()+((float)(wz-g_player.z)/16.0f)*FOCAL/depth);}

typedef struct {float x1,z1,x2,z2,t1,t2;} clipped_wall_t;
static bool project_wall_segment(const map_wall_t*a,const map_wall_t*b,clipped_wall_t*out){
    float fx,fy,rx,ry;camera_basis(&fx,&fy,&rx,&ry);float dx1=(float)(a->x-g_player.x),dy1=(float)(a->y-g_player.y),dx2=(float)(b->x-g_player.x),dy2=(float)(b->y-g_player.y);
    float x1=dx1*rx+dy1*ry,z1=dx1*fx+dy1*fy,x2=dx2*rx+dy2*ry,z2=dx2*fx+dy2*fy,t1=0,t2=1;
    if(z1<=NEAR_PLANE&&z2<=NEAR_PLANE)return false;
    if(z1<=NEAR_PLANE){float q=(NEAR_PLANE-z1)/(z2-z1);x1+=(x2-x1)*q;t1+=(t2-t1)*q;z1=NEAR_PLANE;}
    if(z2<=NEAR_PLANE){float q=(NEAR_PLANE-z2)/(z1-z2);x2+=(x1-x2)*q;t2+=(t1-t2)*q;z2=NEAR_PLANE;}
    out->x1=x1;out->z1=z1;out->x2=x2;out->z2=z2;out->t1=t1;out->t2=t2;return true;
}

static void render_plane_background(surface_t*s){
    if(g_player.sector<0||g_player.sector>=g_map.sector_count)return;
    const map_sector_t*sec=&g_map.sectors[g_player.sector];
    const texture_t*ceil=find_texture((uint16_t)sec->ceilingpicnum),*floor=find_texture((uint16_t)sec->floorpicnum);
    float fx,fy,rx,ry;camera_basis(&fx,&fy,&rx,&ry);
    int horizon=view_horizon();
    for(int y=VIEW_TOP;y<VIEW_BOTTOM;y++){
        bool is_floor=y>horizon;if(y==horizon)continue;const texture_t*t=is_floor?floor:ceil;int shade=shade_level(is_floor?sec->floorshade:sec->ceilingshade);int panx=is_floor?sec->floorxpanning:sec->ceilingxpanning,pany=is_floor?sec->floorypanning:sec->ceilingypanning;
        float denom=(float)(y-horizon);
        int32_t planez=is_floor?sector_floor_z(g_player.sector,g_player.x,g_player.y):sector_ceiling_z(g_player.sector,g_player.x,g_player.y);
        float depth=((float)(planez-g_player.z)/16.0f)*FOCAL/denom;if(depth<NEAR_PLANE)depth=NEAR_PLANE;
        for(int x=0;x<SCREEN_W;x++){
            float camx=((float)x-160.0f)*depth/FOCAL;
            float wx=(float)g_player.x+fx*depth+rx*camx,wy=(float)g_player.y+fy*depth+ry*camx;
            int32_t slopez=is_floor?sector_floor_z(g_player.sector,(int32_t)wx,(int32_t)wy):sector_ceiling_z(g_player.sector,(int32_t)wx,(int32_t)wy);
            float d2=((float)(slopez-g_player.z)/16.0f)*FOCAL/denom;if(d2>NEAR_PLANE){depth=d2;camx=((float)x-160.0f)*depth/FOCAL;wx=(float)g_player.x+fx*depth+rx*camx;wy=(float)g_player.y+fy*depth+ry*camx;}
            uint8_t idx=t?texel(t,(int)(wx/64.0f)+panx,(int)(wy/64.0f)+pany):0;raw_pixel(s,x,y,g_tex.palette16[shade][idx]);
        }
    }
}

static void draw_textured_span(surface_t*s,const texture_t*t,const map_wall_t*w,float z,float wall_t,int x,int y0,int y1,int full_top,int full_bottom){
    if(y0>y1){int q=y0;y0=y1;y1=q;}if(y0<VIEW_TOP)y0=VIEW_TOP;if(y1>=VIEW_BOTTOM)y1=VIEW_BOTTOM-1;if(y0>y1)return;int shade=shade_level(w->shade);float repeats=(w->xrepeat?w->xrepeat:8)/8.0f;int u=t?(int)(wall_t*(float)t->w*repeats)+(int)w->xpanning:0;
    int span=full_bottom-full_top;if(span==0)span=1;for(int y=y0;y<=y1;y++){int v=t?(int)(((float)(y-full_top)/(float)span)*(float)t->h*((w->yrepeat?w->yrepeat:8)/8.0f))+(int)w->ypanning:0;uint8_t idx=t?texel(t,u,v):0;if(idx==255&&((w->cstat&16)!=0))continue;zpixel(s,x,y,z,g_tex.palette16[shade][idx]);}
}

static bool wall_frustum_visible(const map_wall_t*w){clipped_wall_t c;if(!project_wall_segment(w,&g_map.walls[w->point2],&c))return false;float sx1=160.0f+c.x1*FOCAL/c.z1,sx2=160.0f+c.x2*FOCAL/c.z2;return !(sx1<-48.0f&&sx2<-48.0f)&&!(sx1>368.0f&&sx2>368.0f);}

static int collect_visible_sectors(int16_t start,sector_queue_t*out){
    memset(g_sector_visible,0,sizeof(g_sector_visible));if(start<0||start>=g_map.sector_count)return 0;int head=0,tail=0;out[tail++]=(sector_queue_t){start,0};g_sector_visible[start]=true;
    while(head<tail&&tail<MAX_VISIBLE_SECTORS){sector_queue_t q=out[head++];const map_sector_t*s=&g_map.sectors[q.sector];if(q.depth>=MAX_PORTAL_DEPTH)continue;for(int n=0;n<s->wallnum;n++){const map_wall_t*w=&g_map.walls[s->wallptr+n];int ns=w->nextsector;if(ns<0||ns>=g_map.sector_count||!wall_is_open_portal(w)||g_sector_visible[ns])continue;if(!wall_frustum_visible(w))continue;g_sector_visible[ns]=true;out[tail++]=(sector_queue_t){(int16_t)ns,(uint8_t)(q.depth+1)};if(tail>=MAX_VISIBLE_SECTORS)break;}}
    return tail;
}

static void render_wall(surface_t*s,const map_wall_t*w){
    const map_wall_t*e=&g_map.walls[w->point2];int owner=w->owner_sector;if(owner<0||owner>=g_map.sector_count)return;clipped_wall_t c;if(!project_wall_segment(w,e,&c))return;float sx1f=160.0f+c.x1*FOCAL/c.z1,sx2f=160.0f+c.x2*FOCAL/c.z2;if((sx1f<0&&sx2f<0)||(sx1f>=SCREEN_W&&sx2f>=SCREEN_W))return;
    float x1=sx1f,x2=sx2f,z1=c.z1,z2=c.z2,t1=c.t1,t2=c.t2;if(x1>x2){float q=x1;x1=x2;x2=q;q=z1;z1=z2;z2=q;q=t1;t1=t2;t2=q;}int ix1=(int)ceilf(x1),ix2=(int)floorf(x2);if(ix1<0)ix1=0;if(ix2>=SCREEN_W)ix2=SCREEN_W-1;if(ix1>ix2)return;const texture_t*tex=find_texture((uint16_t)w->picnum);
    for(int x=ix1;x<=ix2;x++){
        float a=(x2!=x1)?((float)x-x1)/(x2-x1):0;float inv=(1-a)/z1+a/z2;if(inv<=0)continue;float z=1.0f/inv;float wt=(((1-a)*t1/z1)+(a*t2/z2))/inv;
        int32_t wx=w->x+(int32_t)((float)(e->x-w->x)*wt),wy=w->y+(int32_t)((float)(e->y-w->y)*wt);
        int32_t owner_ceil=sector_ceiling_z(owner,wx,wy),owner_floor=sector_floor_z(owner,wx,wy);
        int ct=project_y(owner_ceil,z),fb=project_y(owner_floor,z);
        if(wall_is_open_portal(w)){
            int nsidx=w->nextsector;int32_t next_ceil=sector_ceiling_z(nsidx,wx,wy),next_floor=sector_floor_z(nsidx,wx,wy);
            int nct=project_y(next_ceil,z),nfb=project_y(next_floor,z);
            if(next_ceil>owner_ceil)draw_textured_span(s,tex,w,z,wt,x,ct,nct,ct,fb);
            if(next_floor<owner_floor)draw_textured_span(s,tex,w,z,wt,x,nfb,fb,ct,fb);
        }else draw_textured_span(s,tex,w,z,wt,x,ct,fb,ct,fb);
    }
}

static void render_sprite(surface_t*s,const map_sprite_t*sp){
    if(sp->pickup_taken||(sp->cstat&0x8000)||((sp->cstat>>4)&3)!=0)return;
    if(sp->sectnum>=0&&sp->sectnum<g_map.sector_count&&!g_sector_visible[sp->sectnum])return;
    const texture_t*t=find_texture((uint16_t)sp->picnum);
    if(!t||!t->w||!t->h)return;
    float fx,fy,rx,ry;camera_basis(&fx,&fy,&rx,&ry);float dx=(float)(sp->x-g_player.x),dy=(float)(sp->y-g_player.y),cx=dx*rx+dy*ry,z=dx*fx+dy*fy;if(z<=NEAR_PLANE)return;float sx=160.0f+cx*FOCAL/z;float worldw=(float)t->w*(float)(sp->xrepeat?sp->xrepeat:32)*0.5f,worldh=(float)t->h*(float)(sp->yrepeat?sp->yrepeat:32)*0.5f;float sw=worldw*FOCAL/z,sh=worldh*FOCAL/z;if(sw<1||sh<1||sw>800||sh>800)return;int left=(int)(sx-sw*0.5f),right=(int)(sx+sw*0.5f),bottom=project_y(sp->z,z),top=bottom-(int)sh;int shade=shade_level(sp->shade);
    if(left<0)left=0;
    if(right>=SCREEN_W)right=SCREEN_W-1;
    if(top<VIEW_TOP)top=VIEW_TOP;
    if(bottom>=VIEW_BOTTOM)bottom=VIEW_BOTTOM-1;
    if(left>right||top>bottom)return;
    for(int x=left;x<=right;x++){int u=(int)(((float)(x-(int)(sx-sw*0.5f))/sw)*t->w);if(sp->cstat&4)u=t->w-1-u;for(int y=top;y<=bottom;y++){int v=(int)(((float)(y-(bottom-(int)sh))/sh)*t->h);if(sp->cstat&8)v=t->h-1-v;uint8_t idx=texel(t,u,v);if(idx==255)continue;zpixel(s,x,y,z,g_tex.palette16[shade][idx]);}}
}

static void render_world(surface_t*s){
    for(int i=0;i<SCREEN_W*VIEW_H;i++)g_zbuf[i]=1.0e30f;
    render_plane_background(s);
    sector_queue_t q[MAX_VISIBLE_SECTORS];
    int n=collect_visible_sectors(g_player.sector,q);
    for(int i=0;i<n;i++){const map_sector_t*sec=&g_map.sectors[q[i].sector];for(int w=0;w<sec->wallnum;w++)render_wall(s,&g_map.walls[sec->wallptr+w]);}
    for(int i=0;i<g_map.sprite_count;i++)render_sprite(s,&g_map.sprites[i]);
}

static void render_hud_tile(surface_t*s,int tile_id,int x,int y,int scale,bool flip){const texture_t*t=find_texture((uint16_t)tile_id);if(!t)return;for(int tx=0;tx<t->w;tx++){int sx=flip?t->w-1-tx:tx;for(int ty=0;ty<t->h;ty++){uint8_t idx=texel(t,sx,ty);if(idx==255)continue;uint16_t c=g_tex.palette16[31][idx];for(int yy=0;yy<scale;yy++)for(int xx=0;xx<scale;xx++)raw_pixel(s,x+tx*scale+xx,y+ty*scale+yy,c);}}}

static void render_weapon(surface_t*s){
    int tile=SWORD_REST;
    if(g_player.weapon==WPN_UZI){
        tile=UZI_REST;
        if(g_player.weapon_timer>0)tile=(g_player.weapon_timer&2)?UZI_FIRE_0:UZI_FIRE_1;
    }else if(g_player.weapon_timer>0){
        if(g_player.weapon_timer>8)tile=SWORD_SWING0;else if(g_player.weapon_timer>4)tile=SWORD_SWING1;else tile=SWORD_SWING2;
    }
    const texture_t*t=find_texture((uint16_t)tile);if(!t)return;int scale=2;if(t->w*3<180&&t->h*3<180)scale=3;
    int x=SCREEN_W-(int)t->w*scale+18,y=SCREEN_H-(int)t->h*scale+8;render_hud_tile(s,tile,x,y,scale,false);
}

static bool is_ninja(const map_sprite_t *sp){return sp->base_picnum==NINJA_RUN_R0||sp->base_picnum==NINJA_CRAWL_R0;}
static bool is_coolie(const map_sprite_t *sp){return sp->base_picnum==COOLIE_RUN_R0||sp->base_picnum==1441;}

static bool segment_blocked(int32_t x0,int32_t y0,int32_t x1,int32_t y1){
    for(int i=0;i<g_map.wall_count;i++){
        const map_wall_t*w=&g_map.walls[i];
        if(!crosses_wall(x0,y0,x1,y1,w))continue;
        if(!wall_is_open_portal(w))return true;
    }
    return false;
}

static void hurt_player(int damage){
    if(damage<=0||g_player.health<=0)return;
    if(g_player.armor>0){int absorb=(damage+1)/2;if(absorb>g_player.armor)absorb=g_player.armor;g_player.armor-=absorb;damage-=absorb;}
    g_player.health-=damage;if(g_player.health<0)g_player.health=0;g_player.damage_flash=6;
}

static void damage_enemy(map_sprite_t *sp,int damage){
    if(!sp||sp->dead||sp->hp<=0)return;
    sp->hp-=damage;
    if(sp->hp<=0){
        sp->hp=0;sp->dead=true;sp->state_timer=0;sp->picnum=is_ninja(sp)?NINJA_DEAD:COOLIE_DEAD;g_player.kills++;
    }else{
        sp->picnum=is_ninja(sp)?NINJA_PAIN_R0:COOLIE_PAIN_R0;sp->state_timer=8;
    }
}

static void sword_hit(void){
    float fx,fy,rx,ry;camera_basis(&fx,&fy,&rx,&ry);int best=-1;float bestz=1500.0f;
    for(int i=0;i<g_map.sprite_count;i++){
        map_sprite_t*sp=&g_map.sprites[i];if(sp->dead||sp->hp<=0)continue;
        float dx=(float)(sp->x-g_player.x),dy=(float)(sp->y-g_player.y),z=dx*fx+dy*fy,side=fabsf(dx*rx+dy*ry);
        if(z>0&&z<bestz&&side<z*0.45f&&!segment_blocked(g_player.x,g_player.y,sp->x,sp->y)){best=i;bestz=z;}
    }
    if(best>=0)damage_enemy(&g_map.sprites[best],3);
}

static void uzi_hitscan(void){
    float fx,fy,rx,ry;camera_basis(&fx,&fy,&rx,&ry);int best=-1;float bestz=12000.0f;
    for(int i=0;i<g_map.sprite_count;i++){
        map_sprite_t*sp=&g_map.sprites[i];if(sp->dead||sp->hp<=0)continue;
        float dx=(float)(sp->x-g_player.x),dy=(float)(sp->y-g_player.y),z=dx*fx+dy*fy,side=fabsf(dx*rx+dy*ry);
        if(z>0&&z<bestz&&side<80.0f+z*0.075f&&!segment_blocked(g_player.x,g_player.y,sp->x,sp->y)){best=i;bestz=z;}
    }
    if(best>=0)damage_enemy(&g_map.sprites[best],1);
}

static int key_number_from_tile(int pic){if(pic<KEY_TILE_FIRST||pic>KEY_TILE_LAST)return 0;return ((pic-KEY_TILE_FIRST)/4)+1;}

static void update_pickups(void){
    for(int i=0;i<g_map.sprite_count;i++){
        map_sprite_t*sp=&g_map.sprites[i];if(sp->pickup_taken||sp->dead)continue;
        int keynum=key_number_from_tile(sp->picnum);
        bool supported=sp->picnum==ICON_SM_MEDKIT||sp->picnum==ICON_MEDKIT||sp->picnum==ICON_ARMOR||
                       sp->picnum==ICON_UZI||sp->picnum==ICON_UZIFLOOR||sp->picnum==ICON_LG_UZI_AMMO||keynum>0;
        if(!supported)continue;
        int64_t dx=(int64_t)sp->x-g_player.x,dy=(int64_t)sp->y-g_player.y;if(dx*dx+dy*dy>=700LL*700LL)continue;
        if(keynum>0){g_player.keys|=(uint8_t)(1u<<(keynum-1));set_message("KEY ACQUIRED");}
        else if(sp->picnum==ICON_UZI||sp->picnum==ICON_UZIFLOOR){g_player.uzi_ammo+=50;if(g_player.uzi_ammo>200)g_player.uzi_ammo=200;g_player.weapon=WPN_UZI;set_message("UZI");}
        else if(sp->picnum==ICON_LG_UZI_AMMO){g_player.uzi_ammo+=50;if(g_player.uzi_ammo>200)g_player.uzi_ammo=200;set_message("UZI AMMO");}
        else if(sp->picnum==ICON_ARMOR){if(g_player.armor>=100)continue;g_player.armor=100;set_message("ARMOR");}
        else{if(g_player.health>=100)continue;g_player.health+=(sp->picnum==ICON_SM_MEDKIT)?20:25;if(g_player.health>100)g_player.health=100;set_message("HEALTH");}
        sp->pickup_taken=true;g_player.pickups++;
    }
}

static void update_enemy_states(void){
    for(int i=0;i<g_map.sprite_count;i++){
        map_sprite_t *sp=&g_map.sprites[i];if(sp->dead)continue;
        if(sp->state_timer){sp->state_timer--;if(!sp->state_timer&&sp->hp>0)sp->picnum=sp->base_picnum;}
    }
}

static void update_enemies(void){
    if(g_player.health<=0||g_player.exited)return;
    for(int i=0;i<g_map.sprite_count;i++){
        map_sprite_t*sp=&g_map.sprites[i];if(sp->dead||sp->hp<=0)continue;if(sp->attack_cooldown)sp->attack_cooldown--;
        int64_t dx64=(int64_t)g_player.x-sp->x,dy64=(int64_t)g_player.y-sp->y;float dist=sqrtf((float)(dx64*dx64+dy64*dy64));if(dist>10000.0f)continue;
        bool cansee=!segment_blocked(sp->x,sp->y,g_player.x,g_player.y);
        if(is_ninja(sp)&&cansee&&dist<7000.0f){
            if(!sp->attack_cooldown){hurt_player(3);sp->attack_cooldown=42;sp->picnum=4116;sp->state_timer=6;}
            if(dist<1800.0f)continue;
        }
        if(is_coolie(sp)&&dist<750.0f){if(!sp->attack_cooldown){hurt_player(8);sp->attack_cooldown=30;}continue;}
        if(!cansee)continue;
        float inv=1.0f/(dist>1?dist:1);float speed=is_coolie(sp)?42.0f:24.0f;
        int32_t nx=sp->x+(int32_t)((float)dx64*inv*speed),ny=sp->y+(int32_t)((float)dy64*inv*speed);int16_t sec;
        if(move_allowed(sp->x,sp->y,nx,ny,sp->sectnum,&sec)){sp->x=nx;sp->y=ny;sp->sectnum=sec;}
    }
}

static void select_weapon(int dir){
    if(dir==0)return;
    if(g_player.weapon==WPN_SWORD&&g_player.uzi_ammo>0)g_player.weapon=WPN_UZI;
    else g_player.weapon=WPN_SWORD;
    set_message(g_player.weapon==WPN_UZI?"UZI SELECTED":"SWORD SELECTED");
}

static void update_vertical(bool jump_pressed,bool crouch_held){
    if(g_player.sector<0||g_player.sector>=g_map.sector_count)return;
    g_player.crouching=crouch_held;
    int32_t floorz=sector_floor_z(g_player.sector,g_player.x,g_player.y);
    int32_t eyeoff=g_player.crouching?PLAYER_CROUCH_EYE_OFFSET:PLAYER_EYE_OFFSET;
    int32_t target=floorz-eyeoff;
    if(jump_pressed&&g_player.grounded&&!g_player.crouching){g_player.vertical_vel=-1050;g_player.grounded=false;}
    if(!g_player.grounded){g_player.z+=g_player.vertical_vel;g_player.vertical_vel+=86;if(g_player.z>=target){g_player.z=target;g_player.vertical_vel=0;g_player.grounded=true;}}
    else{g_player.z+=(target-g_player.z)/3;if(llabs((long long)target-g_player.z)<8)g_player.z=target;}
}

static void fire_weapon(bool pressed,bool held){
    if(g_player.health<=0||g_player.exited)return;
    if(g_player.weapon==WPN_SWORD){if(pressed&&g_player.weapon_timer==0){g_player.weapon_timer=12;sword_hit();}}
    else if(held&&g_player.fire_cooldown==0&&g_player.uzi_ammo>0){g_player.uzi_ammo--;g_player.weapon_timer=4;g_player.fire_cooldown=4;uzi_hitscan();}
}

static void update_input(void){
    joypad_poll();joypad_inputs_t in=joypad_get_inputs(JOYPAD_PORT_1);joypad_buttons_t p=joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if((g_player.health<=0||g_player.exited)&&p.start){reset_player();return;}
    if(p.start){g_player.paused=!g_player.paused;return;}
    if(g_player.paused)return;
    if(p.a)operate_nearby();
    if(p.d_left)select_weapon(-1);
    if(p.d_right)select_weapon(1);
    if(p.d_up||p.d_down)set_message("INVENTORY SLOT RESERVED");
    if(p.l)set_message("NO INVENTORY ITEM");
    int turn=in.stick_x;if(abs(turn)<6)turn=0;g_player.angle=(int16_t)((g_player.angle+turn/5)&2047);
    int look=in.stick_y;if(abs(look)<7)look=0;g_player.pitch+=look/7;if(g_player.pitch<-48)g_player.pitch=-48;if(g_player.pitch>48)g_player.pitch=48;
    int forward=0,strafe=0;if(in.btn.c_up)forward+=78;if(in.btn.c_down)forward-=78;if(in.btn.c_left)strafe-=78;if(in.btn.c_right)strafe+=78;
    float a=(float)g_player.angle*BUILD_PI/1024.0f,fx=cosf(a),fy=sinf(a),rx=-fy,ry=fx;int speed=g_player.crouching?7:11;
    try_move_player((int32_t)((fx*forward+rx*strafe)*speed),(int32_t)((fy*forward+ry*strafe)*speed));
    update_vertical(p.r,in.btn.b);fire_weapon(p.z,in.btn.z);
    if(g_player.weapon_timer>0)g_player.weapon_timer--;
    if(g_player.fire_cooldown>0)g_player.fire_cooldown--;
    if(g_player.damage_flash>0)g_player.damage_flash--;
    if(g_message_timer>0)g_message_timer--;
    update_sector_motion();update_pickups();update_enemy_states();update_enemies();
}

static int outcode(int x,int y){int c=0;if(x<0)c|=1;else if(x>=SCREEN_W)c|=2;if(y<0)c|=4;else if(y>=SCREEN_H)c|=8;return c;}
static void clipped_line(surface_t*s,int x0,int y0,int x1,int y1,uint32_t color){int c0=outcode(x0,y0),c1=outcode(x1,y1);while(true){if(!(c0|c1)){graphics_draw_line(s,x0,y0,x1,y1,color);return;}if(c0&c1)return;int c=c0?c0:c1,x=0,y=0;if(c&8){if(y1==y0)return;x=x0+(x1-x0)*(SCREEN_H-1-y0)/(y1-y0);y=SCREEN_H-1;}else if(c&4){if(y1==y0)return;x=x0+(x1-x0)*(0-y0)/(y1-y0);y=0;}else if(c&2){if(x1==x0)return;y=y0+(y1-y0)*(SCREEN_W-1-x0)/(x1-x0);x=SCREEN_W-1;}else{if(x1==x0)return;y=y0+(y1-y0)*(0-x0)/(x1-x0);x=0;}if(c==c0){x0=x;y0=y;c0=outcode(x0,y0);}else{x1=x;y1=y;c1=outcode(x1,y1);}}}
static void render_overhead(surface_t*s,uint32_t solid,uint32_t portal,uint32_t pc){float sx=(g_map.max_x>g_map.min_x)?300.0f/(float)(g_map.max_x-g_map.min_x):1,sy=(g_map.max_y>g_map.min_y)?200.0f/(float)(g_map.max_y-g_map.min_y):1,sc=sx<sy?sx:sy,cx=((float)g_map.min_x+g_map.max_x)*.5f,cy=((float)g_map.min_y+g_map.max_y)*.5f;for(int i=0;i<g_map.wall_count;i++){map_wall_t*a=&g_map.walls[i],*b=&g_map.walls[a->point2];clipped_line(s,(int)(160+(a->x-cx)*sc),(int)(120+(a->y-cy)*sc),(int)(160+(b->x-cx)*sc),(int)(120+(b->y-cy)*sc),a->nextsector>=0?portal:solid);}int px=(int)(160+(g_player.x-cx)*sc),py=(int)(120+(g_player.y-cy)*sc);graphics_draw_box(s,px-2,py-2,5,5,pc);}

static void render_frame(void){
    surface_t*s=display_get();
    uint16_t black=pack_rgb16(0,0,0),white=pack_rgb16(30,30,30),green=pack_rgb16(5,31,10),blue=pack_rgb16(5,12,31),red=pack_rgb16(31,3,3);
    graphics_fill_screen(s,black);
    if(g_map.walls&&g_tex.blob){if(g_overhead)render_overhead(s,white,blue,green);else{render_world(s);render_weapon(s);}}
    if(g_player.damage_flash){for(int y=0;y<SCREEN_H;y+=8)for(int x=0;x<SCREEN_W;x+=8)raw_pixel(s,x,y,red);}
    graphics_set_color(g_map.walls&&g_tex.blob?green:red,black);
    graphics_draw_text(s,8,8,"SHADOW64 R13 DUKE64 GAMEPLAY");
    graphics_set_color(white,black);
    char line[128];
    snprintf(line,sizeof(line),"HP:%d AR:%d UZI:%d K:%d KEY:%02X",g_player.health,g_player.armor,g_player.uzi_ammo,g_player.kills,g_player.keys);
    graphics_draw_text(s,8,20,line);
    if(g_message_timer>0&&g_message[0])graphics_draw_text(s,8,32,g_message);
    snprintf(line,sizeof(line),"SEC:%d STICK:LOOK C:MOVE Z:FIRE A:USE",g_player.sector);
    graphics_draw_text(s,8,216,line);
    graphics_draw_text(s,8,228,"R:JUMP B:CROUCH D-L/R:WEAPON START:PAUSE");
    if(g_player.paused)graphics_draw_text(s,132,104,"PAUSED");
    if(g_player.health<=0)graphics_draw_text(s,108,112,"YOU DIED - START");
    if(g_player.exited)graphics_draw_text(s,64,112,"SEPPUKU STATION COMPLETE - START");
    display_show(s);g_frame++;
}

int main(void){debug_init_isviewer();debug_init_usblog();display_init(RESOLUTION_320x240,DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);joypad_init();int dr=dfs_init(DFS_DEFAULT_LOCATION);if(dr!=DFS_ESUCCESS)snprintf(g_status,sizeof(g_status),"DFS ERROR: %s",dfs_strerror(dr));else if(load_map(MAP_PATH,&g_map)&&load_texture_bank(TEX_PATH,&g_tex))reset_player();while(true){if(g_map.walls&&g_tex.blob)update_input();render_frame();}return 0;}
