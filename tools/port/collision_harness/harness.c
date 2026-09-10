/**
 * Collision harness (tools/port/collision_harness/run.sh).
 *
 * Builds a synthetic big-endian hit file (a floor fan around the origin, a
 * one-sided gate, a diagonal house wall and a two-sided wall), loads it through
 * the real load_hit_data(), then runs the real ray tests (src/collision.c), the
 * real trig helpers (src/43F0.c), the port's collision diagnostics
 * (port/testing_bridge.c) and a copy of the player movement code from
 * src/77480.c. The output must match expected.txt; run.sh builds it for the
 * host and, with the NDK and qemu-user, as a static aarch64 Android executable,
 * so a difference between the two shows up as a diff rather than on a phone.
 */
#include "common.h"
#include "map.h"
#include "port/port_endian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct HitFile { u32 collisionOffset; u32 zoneOffset; } HitFile;
void load_hit_data(s32 idx, HitFile* hit);
u16 _wrap_trig_lookup_value(f32 theta);

// bionic static executables need a 64-byte aligned TLS segment
static __thread volatile char tls_pad[64] __attribute__((aligned(64), used));
// ---- stubs for symbols collision.c / 43F0.c pull in --------------------------------------
Vec3s gEntityColliderFaces[] = {
    { 0, 1, 2 }, { 0, 2, 3 }, { 0, 4, 5 }, { 0, 5, 1 }, { 1, 5, 6 }, { 1, 6, 2 },
    { 2, 6, 7 }, { 2, 7, 3 }, { 3, 7, 4 }, { 3, 4, 0 }, { 4, 7, 6 }, { 4, 6, 5 },
};
Vec3f gEntityColliderNormals[] = {
    { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { -1, 0, 0 }, { -1, 0, 0 },
    { 0, 0, -1 }, { 0, 0, -1 }, { 1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 1, 0 },
};

static MapSettings sMap;
MapSettings* get_current_map_settings(void) { return &sMap; }

// The game heap never zeroes memory; fill with garbage to mimic a reused collision heap.
void* collision_heap_malloc(s32 size) { void* p = malloc(size); memset(p, 0xCD, size); return p; }
s32 collision_heap_free(void* p) { free(p); return 0; }
void* general_heap_malloc(s32 size) { return collision_heap_malloc(size); }
s32 general_heap_free(void* p) { free(p); return 0; }
void* heap_malloc(s32 size) { return collision_heap_malloc(size); }
s32 heap_free(void* p) { free(p); return 0; }
s32 collision_heap_create(void) { return 0; }
Entity* get_entity_by_index(s32 index) { (void)index; return NULL; }
struct Model* get_model_from_list_index(s32 index) { (void)index; return NULL; }
void* load_asset_by_name(const char* name, u32* size) { (void)name; *size = 0; return NULL; }
void decode_yay0(void* src, void* dst) { (void)src; (void)dst; }
void guMtxXFMF(Matrix4f m, f32 x, f32 y, f32 z, f32* ox, f32* oy, f32* oz) { (void)m; *ox = x; *oy = y; *oz = z; }
void guRotateF(Matrix4f m, f32 a, f32 x, f32 y, f32 z) { (void)m; (void)a; (void)x; (void)y; (void)z; }
void guMtxCatF(Matrix4f a, Matrix4f b, Matrix4f c) { (void)a; (void)b; (void)c; }
void guTranslateF(Matrix4f m, f32 x, f32 y, f32 z) { (void)m; (void)x; (void)y; (void)z; }
void guMtxL2F(Matrix4f m, Mtx* src) { (void)m; (void)src; }

#include "testing_bridge.h"
#include "game_modes.h"
#include "model.h"
Camera gCameras[4];
s32 gCurrentCameraID;
CollisionStatus gCollisionStatus;
PlayerStatus gPlayerStatus;
ShapeFile gMapShapeData;
TriggerList* gCurrentTriggerListPtr;
GameStatus sGameStatus;
AreaConfig gAreas[29];
PlayerData gPlayerData;
GameStatus* gGameStatusPtr = &sGameStatus;
s16 get_game_mode(void) { return GAME_MODE_WORLD; }

// port/os_stubs.c trig (copied verbatim)
s16 sins(u16 angle) { return (s16)(sinf(angle * (2.0f * 3.14159265f / 65536.0f)) * 32767.0f); }
s16 coss(u16 angle) { return (s16)(cosf(angle * (2.0f * 3.14159265f / 65536.0f)) * 32767.0f); }

// ---- synthetic hit file ------------------------------------------------------------------
typedef struct { s16 x, y, z; } V;
static u8 sFile[1 << 16];
static u32 sPos;
static void put16(s32 v) { sFile[sPos++] = (v >> 8) & 0xFF; sFile[sPos++] = v & 0xFF; }
static void put32(u32 v) { sFile[sPos++] = v >> 24; sFile[sPos++] = (v >> 16) & 0xFF; sFile[sPos++] = (v >> 8) & 0xFF; sFile[sPos++] = v & 0xFF; }
static void putf(f32 f) { u32 u; memcpy(&u, &f, 4); put32(u); }

#define NV 16
#define NC 4
static V sVerts[NV] = {
    // floor fan around the origin (0..3)
    { 0, 0, 0 }, { 500, 0, 207 }, { 207, 0, 500 }, { -500, 0, -500 },
    // gate: quad at x = -273, z from -59 to 3, facing +x (4..7)
    { -273, 0, -59 }, { -273, 100, -59 }, { -273, 100, 3 }, { -273, 0, 3 },
    // house wall: diagonal quad from (59,-165) to (116,-132), height 88 (8..11)
    { 59, 0, -165 }, { 59, 88, -165 }, { 116, 88, -132 }, { 116, 0, -132 },
    // two-sided wall along z at x = 305 (12..15)
    { 305, 0, 181 }, { 305, 100, 181 }, { 305, 100, 301 }, { 305, 0, 301 },
};
// triangles as (i, j, k, oneSided); loader takes v1 = i, v2 = j, v3 = k
static s32 sTris[NC][4][4] = {
    { { 0, 2, 1, 1 }, { 0, 3, 2, 1 }, { -1 } },              // floor: 2 tris
    { { 4, 5, 6, 1 }, { 4, 6, 7, 1 }, { -1 } },              // gate
    { { 8, 10, 9, 1 }, { 8, 11, 10, 1 }, { -1 } },           // house door wall
    { { 12, 13, 14, 0 }, { 12, 14, 15, 0 }, { -1 } },        // two sided
};
static const char* sNames[NC] = { "floor", "gate", "door", "twosided" };

static void build_file(void) {
    s32 i, j;
    u32 hdr, colOff, vertOff, triOff[NC], aabbOff;
    s32 numTris[NC];
    f32 mins[NC][3], maxs[NC][3];

    sPos = 0;
    put32(8); put32(0);            // HitFile: collisionOffset=8, zoneOffset=0
    hdr = sPos;                    // HitFileHeader placeholder (0x18 bytes)
    sPos += 0x18;

    for (i = 0; i < NC; i++) {
        numTris[i] = 0;
        mins[i][0] = mins[i][1] = mins[i][2] = 1e9f;
        maxs[i][0] = maxs[i][1] = maxs[i][2] = -1e9f;
        for (j = 0; j < 4 && sTris[i][j][0] >= 0; j++) {
            s32 k;
            numTris[i]++;
            for (k = 0; k < 3; k++) {
                V* v = &sVerts[sTris[i][j][k]];
                if (v->x < mins[i][0]) mins[i][0] = v->x; if (v->x > maxs[i][0]) maxs[i][0] = v->x;
                if (v->y < mins[i][1]) mins[i][1] = v->y; if (v->y > maxs[i][1]) maxs[i][1] = v->y;
                if (v->z < mins[i][2]) mins[i][2] = v->z; if (v->z > maxs[i][2]) maxs[i][2] = v->z;
            }
        }
    }

    // triangles
    for (i = 0; i < NC; i++) {
        triOff[i] = sPos;
        for (j = 0; j < numTris[i]; j++) {
            u32 packed = (u32)(sTris[i][j][0] & 0x3FF) | ((u32)(sTris[i][j][1] & 0x3FF) << 10) |
                         ((u32)(sTris[i][j][2] & 0x3FF) << 20) | ((u32)(sTris[i][j][3] & 1) << 30);
            put32(packed);
        }
    }
    // vertices
    vertOff = sPos;
    for (i = 0; i < NV; i++) { put16(sVerts[i].x); put16(sVerts[i].y); put16(sVerts[i].z); }
    // aabbs: 7 words each
    aabbOff = sPos;
    for (i = 0; i < NC; i++) {
        putf(mins[i][0]); putf(mins[i][1]); putf(mins[i][2]);
        putf(maxs[i][0]); putf(maxs[i][1]); putf(maxs[i][2]);
        put32(0);                  // flagsForCollider
    }
    // colliders
    colOff = sPos;
    for (i = 0; i < NC; i++) {
        put16(i * 7);              // boundingBoxOffset in u32 words
        put16(i + 1 < NC ? i + 1 : -1);  // nextSibling
        put16(-1);                 // firstChild
        put16(numTris[i]);
        put32(triOff[i]);
    }
    // header
    {
        u32 end = sPos;
        sPos = hdr;
        put16(NC); put16(0); put32(colOff);
        put16(NV); put16(0); put32(vertOff);
        put16(NC * 7); put16(0); put32(aabbOff);
        sPos = end;
    }
    sMap.hitAssetCollisionOffset = 8;
    sMap.hitAssetZoneOffset = 0;
}

// ---- copies of the player raycast code (src/77480.c) -------------------------------------
static HitID h_player_raycast_general(s32 mode, f32 startX, f32 startY, f32 startZ, f32 dirX, f32 dirY, f32 dirZ, f32* hitX,
                            f32* hitY, f32* hitZ, f32* hitDepth, f32*hitNx, f32* hitNy, f32* hitNz) {
    f32 nAngleX;
    f32 nAngleZ;
    s32 entityID;
    s32 colliderID;
    Entity* entity;
    s32 ignoreFlags;
    s32 ret;

    entityID = test_ray_entities(startX, startY, startZ, dirX, dirY, dirZ, hitX, hitY, hitZ, hitDepth, hitNx, hitNy,
                                hitNz);
    ret = NO_COLLIDER;
    if (entityID > NO_COLLIDER) {
        entity = get_entity_by_index(entityID);
        if (entity->alpha < 255) {
            entity->collisionTimer = 0;
            entity->flags |= ENTITY_FLAG_CONTINUOUS_COLLISION;
        } else {
            ret = entityID | COLLISION_WITH_ENTITY_BIT;
        }
    } else if (mode == PLAYER_COLLISION_HAMMER) {
        ret = test_ray_colliders(COLLIDER_FLAG_IGNORE_SHELL, startX, startY, startZ, dirX, dirY, dirZ,
            hitX, hitY, hitZ, hitDepth, hitNx, hitNy, hitNz);
    }

    if (mode == PLAYER_COLLISION_1 || mode == PLAYER_COLLISION_HAMMER) {
        return ret;
    }

    if (mode == PLAYER_COLLISION_4) {
        ignoreFlags = COLLIDER_FLAG_DOCK_WALL;
    } else {
        ignoreFlags = COLLIDER_FLAG_IGNORE_PLAYER;
    }

    colliderID = test_ray_colliders(ignoreFlags, startX, startY, startZ, dirX, dirY, dirZ,
        hitX, hitY, hitZ, hitDepth, hitNx, hitNy, hitNz);

    if (ret <= NO_COLLIDER) {
        ret = colliderID;
    }

    if (ret > NO_COLLIDER) {
        nAngleZ = 180.0f - atan2(0, 0, *hitNz * 100.0, *hitNy * 100.0);
        nAngleX = 180.0f - atan2(0, 0, *hitNx * 100.0, *hitNy * 100.0);
        printf("    angle filter: nAngleZ=%.4f nAngleX=%.4f\n", nAngleZ, nAngleX);

        if (!((nAngleZ == 90.0f && nAngleX == 90.0f) || fabs(nAngleZ) >= 30.0 || fabs(nAngleX) >= 30.0)) {
            ret = NO_COLLIDER;
        }
    }

    return ret;
}

static void h_player_get_slip_vector(f32* outX, f32* outY, f32 x, f32 y, f32 nX, f32 nY) {
    f32 projectionLength = (x * nX) + (y * nY);

    *outX = (x - projectionLength * nX) * 0.5f;
    *outY = (y - projectionLength * nY) * 0.5f;
}

HitID player_test_move_with_slipping(PlayerStatus* playerStatus, f32* x, f32* y, f32* z, f32 length, f32 yaw) {
    f32 sinTheta;
    f32 cosTheta;
    f32 hitX;
    f32 hitY;
    f32 hitZ;
    f32 hitDepth;
    f32 hitNx;
    f32 hitNy;
    f32 hitNz;
    f32 slipDx;
    f32 slipDz;
    f32 radius;
    f32 height;
    s32 hitID;
    f32 targetDx, targetDz;
    f32 dx, dz;
    f32 depthDiff;
    s32 ret = NO_COLLIDER;

    height = 0.0f;
    if (!(playerStatus->flags & (PS_FLAG_JUMPING | PS_FLAG_FALLING))) {
        height = 10.01f;
    }
    radius = playerStatus->colliderDiameter * 0.5f;

    sin_cos_rad(DEG_TO_RAD(yaw), &sinTheta, &cosTheta);
    cosTheta = -cosTheta;
    hitDepth = length + radius;

    targetDx = length * sinTheta;
    targetDz = length * cosTheta;

    hitID = h_player_raycast_general(PLAYER_COLLISION_0, *x, *y + height, *z, sinTheta, 0, cosTheta, &hitX, &hitY, &hitZ, &hitDepth, &hitNx, &hitNy, &hitNz);
    if (hitID > NO_COLLIDER && (depthDiff = hitDepth, depthDiff <= length + radius)) {
        depthDiff -= (length + radius);
        dx = depthDiff * sinTheta;
        dz = depthDiff * cosTheta;
        h_player_get_slip_vector(&slipDx, &slipDz, targetDx, targetDz, hitNx, hitNz);
        *x += dx + slipDx;
        *z += dz + slipDz;
        ret = hitID;
    } else {
        height = playerStatus->colliderHeight * 0.75;
        hitID = h_player_raycast_general(PLAYER_COLLISION_0, *x, *y + height, *z, sinTheta, 0, cosTheta, &hitX, &hitY, &hitZ, &hitDepth, &hitNx, &hitNy, &hitNz);
        if (hitID > NO_COLLIDER && (depthDiff = hitDepth, depthDiff <= length + radius)) {
            depthDiff -= (length + radius);
            dx = depthDiff * sinTheta;
            dz = depthDiff * cosTheta;
            h_player_get_slip_vector(&slipDx, &slipDz, targetDx, targetDz, hitNx, hitNz);
            *x += dx + slipDx;
            *z += dz + slipDz;
            ret = hitID;
        }
    }

    *x += targetDx;
    *z += targetDz;
    return ret;
}

// ---- tests -------------------------------------------------------------------------------
static void dump_colliders(void) {
    s32 i, j;
    for (i = 0; i < gCollisionData.numColliders; i++) {
        Collider* c = &gCollisionData.colliderList[i];
        printf("  #%d %s flags=%08X tris=%d aabb=(%.1f %.1f %.1f)-(%.1f %.1f %.1f)\n", i, sNames[i], (unsigned)c->flags,
               c->numTriangles, c->aabb->min.x, c->aabb->min.y, c->aabb->min.z, c->aabb->max.x, c->aabb->max.y,
               c->aabb->max.z);
        for (j = 0; j < c->numTriangles; j++) {
            ColliderTriangle* t = &c->triangleTable[j];
            printf("     tri%d v1=(%.0f %.0f %.0f) v2=(%.0f %.0f %.0f) v3=(%.0f %.0f %.0f) n=(%.4f %.4f %.4f) oneSided=%d\n",
                   j, t->v1->x, t->v1->y, t->v1->z, t->v2->x, t->v2->y, t->v2->z, t->v3->x, t->v3->y, t->v3->z,
                   t->normal.x, t->normal.y, t->normal.z, t->oneSided);
        }
    }
}

static void ray(const char* what, s32 ignore, f32 sx, f32 sy, f32 sz, f32 dx, f32 dy, f32 dz, f32 depth) {
    f32 hx, hy, hz, nx, ny, nz;
    f32 d = depth;
    s32 id = test_ray_colliders(ignore, sx, sy, sz, dx, dy, dz, &hx, &hy, &hz, &d, &nx, &ny, &nz);
    if (id > NO_COLLIDER) {
        printf("  %-34s hit #%d (%s) depth=%.4f at (%.3f %.3f %.3f) n=(%.4f %.4f %.4f)\n", what, id, sNames[id], d, hx, hy, hz, nx, ny, nz);
    } else {
        printf("  %-34s miss (ret %d)\n", what, id);
    }
}

int main(void) {
    s32 i;
    f32 s, c;
    PlayerStatus ps;

    tls_pad[0] = 1;
    setvbuf(stdout, NULL, _IONBF, 0);   // keep stdout/stderr ordering deterministic
    printf("sizeof: Collider=%zu ColliderTriangle=%zu ColliderBoundingBox=%zu Vec3f=%zu HitID=%zu char signed=%d\n",
           sizeof(Collider), sizeof(ColliderTriangle), sizeof(ColliderBoundingBox), sizeof(Vec3f), sizeof(HitID),
           (int)((char)-1 < 0));

    build_file();
    load_hit_data(0, (HitFile*)sFile);
    printf("loaded %d colliders\n", gCollisionData.numColliders);
    dump_colliders();

    printf("trig:\n");
    for (i = 0; i < 8; i++) {
        f32 yaw = i * 45.0f;
        sin_cos_rad(DEG_TO_RAD(yaw), &s, &c);
        printf("  yaw %5.1f: wrap=%u sin=%.6f cos=%.6f atan2(0,0,sin,-cos)=%.4f\n", yaw,
               (unsigned)_wrap_trig_lookup_value(RAD_TO_BINANG(DEG_TO_RAD(yaw))), s, c, atan2(0, 0, s, -c));
    }
    sin_cos_rad(DEG_TO_RAD(-90.0f), &s, &c);
    printf("  yaw -90: sin=%.6f cos=%.6f\n", s, c);
    printf("  pm_round(2.5)=%d pm_round(-2.5)=%d pm_round(0.4)=%d\n", pm_round(2.5f), pm_round(-2.5f), pm_round(0.4f));

    printf("rays:\n");
    ray("down onto floor from (100,50,100)", COLLIDER_FLAG_IGNORE_PLAYER, 100, 50, 100, 0, -1, 0, 100);
    ray("west into gate from (-250,10,-28)", COLLIDER_FLAG_IGNORE_PLAYER, -250, 10.01f, -28, -1, 0, 0, 40);
    ray("west into gate, short ray (13)", COLLIDER_FLAG_IGNORE_PLAYER, -250, 10.01f, -28, -1, 0, 0, 13);
    ray("west into gate, from 12 away", COLLIDER_FLAG_IGNORE_PLAYER, -261, 10.01f, -28, -1, 0, 0, 13);
    ray("east from behind the gate", COLLIDER_FLAG_IGNORE_PLAYER, -290, 10.01f, -28, 1, 0, 0, 40);
    ray("diag ray into gate (general)", COLLIDER_FLAG_IGNORE_PLAYER, -250, 10.01f, -28, -0.9f, 0.1f, 0.1f, 40);
    ray("into door wall from (60,10,-120)", COLLIDER_FLAG_IGNORE_PLAYER, 60, 10.01f, -120, 0.5f, 0, -0.866f, 40);
    ray("into two-sided from west", COLLIDER_FLAG_IGNORE_PLAYER, 290, 10.01f, 240, 1, 0, 0, 40);
    ray("into two-sided from east", COLLIDER_FLAG_IGNORE_PLAYER, 320, 10.01f, 240, -1, 0, 0, 40);
    ray("gate with sin/cos of yaw 270", COLLIDER_FLAG_IGNORE_PLAYER, -250, 10.01f, -28, -1, 0, 0, 40);
    sin_cos_rad(DEG_TO_RAD(270.0f), &s, &c);
    ray("gate with real trig yaw 270", COLLIDER_FLAG_IGNORE_PLAYER, -250, 10.01f, -28, s, 0, -c, 40);

    printf("self-check:\n");
    port_testing_collision_selfcheck();
    printf("watchdog: move through the gate\n");
    memset(&gPlayerStatus, 0, sizeof(gPlayerStatus));
    gPlayerStatus.colliderDiameter = 26;
    gPlayerStatus.colliderHeight = 37;
    gPlayerStatus.pos.x = -280; gPlayerStatus.pos.z = -28;
    port_testing_watch_player_walls(-250, 0, -28);
    printf("watchdog: walk along the gate (no crossing expected)\n");
    gPlayerStatus.pos.x = -250; gPlayerStatus.pos.z = -40;
    port_testing_watch_player_walls(-250, 0, -28);
    printf("dump:\n");
    gPlayerStatus.pos.x = -255; gPlayerStatus.pos.z = -28;
    port_testing_dump_collision();
    printf("walk west into the gate (player radius 13, 2 units per step):\n");
    memset(&ps, 0, sizeof(ps));
    ps.colliderDiameter = 26;
    ps.colliderHeight = 37;
    {
        f32 x = -230, y = 0, z = -28;
        for (i = 0; i < 20; i++) {
            HitID hit = player_test_move_with_slipping(&ps, &x, &y, &z, 2.0f, 270.0f);
            printf("  step %2d: x=%.4f z=%.4f hit=%d\n", i, x, z, hit);
        }
    }
    printf("inspect from x=-255 facing west (length = radius):\n");
    {
        f32 x = -255, y = 0, z = -28;
        HitID hit = player_test_move_with_slipping(&ps, &x, &y, &z, 13.0f, 270.0f);
        printf("  x=%.4f z=%.4f hit=%d\n", x, z, hit);
    }
    printf("walk into the diagonal door wall (yaw 330):\n");
    {
        f32 x = 60, y = 0, z = -110;
        for (i = 0; i < 20; i++) {
            HitID hit = player_test_move_with_slipping(&ps, &x, &y, &z, 2.0f, 330.0f);
            printf("  step %2d: x=%.4f z=%.4f hit=%d\n", i, x, z, hit);
        }
    }
    return 0;
}
