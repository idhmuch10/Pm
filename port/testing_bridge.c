/**
 * testing_bridge.c - Testing shortcuts backed by the game's own save/load and
 * map-change code paths (see testing_bridge.h).
 */
#include "common.h"
#include "game_modes.h"
#include "fio.h"
#include "map.h"
#include "model.h"

extern ShapeFile gMapShapeData;
extern TriggerList* gCurrentTriggerListPtr;
#include "testing_bridge.h"
#include "port_paths.h"
#include <limits.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#ifdef __ANDROID__
#include "android/AndroidPort.h"
#endif

int port_testing_in_world(void) {
    return get_game_mode() == GAME_MODE_WORLD;
}

int port_testing_game_mode(void) {
    return get_game_mode();
}

static int valid_area(int area) {
    return area >= 0 && area < (int)ARRAY_COUNT(gAreas);
}

const char* port_testing_current_map(void) {
    int area = gGameStatusPtr->areaID;
    int map = gGameStatusPtr->mapID;
    if (!valid_area(area) || map < 0 || map >= gAreas[area].mapCount) {
        return "?";
    }
    return gAreas[area].maps[map].id;
}

int port_testing_current_entry(void) {
    return gGameStatusPtr->entryID;
}

int port_testing_current_slot(void) {
    return gGameStatusPtr->saveSlot;
}

int port_testing_current_area(void) {
    return gGameStatusPtr->areaID;
}

int port_testing_current_map_index(void) {
    return gGameStatusPtr->mapID;
}

int port_testing_story_progress(void) {
    return evt_get_variable(NULL, GB_StoryProgress);
}

void port_testing_set_story_progress(int value) {
    evt_set_variable(NULL, GB_StoryProgress, value);
}

int port_testing_story_intro(void) {
    return STORY_INTRO;
}

int port_testing_area_count(void) {
    return (int)ARRAY_COUNT(gAreas);
}

const char* port_testing_area_id(int area) {
    return valid_area(area) ? gAreas[area].id : "?";
}

int port_testing_map_count(int area) {
    return valid_area(area) ? gAreas[area].mapCount : 0;
}

const char* port_testing_map_id(int area, int map) {
    if (!valid_area(area) || map < 0 || map >= gAreas[area].mapCount) {
        return "?";
    }
    return gAreas[area].maps[map].id;
}

int port_testing_quick_save(char* msg, size_t msgSize) {
    if (!port_testing_in_world()) {
        snprintf(msg, msgSize, "Quick save only works while exploring a map.");
        return 0;
    }
    // Same as a save block: remember the position, then serialise into the active slot.
    gGameStatusPtr->savedPos.x = (s16)gPlayerStatusPtr->pos.x;
    gGameStatusPtr->savedPos.y = (s16)gPlayerStatusPtr->pos.y;
    gGameStatusPtr->savedPos.z = (s16)gPlayerStatusPtr->pos.z;
    fio_save_game(gGameStatusPtr->saveSlot);
    snprintf(msg, msgSize, "Saved slot %d at %s (entry %d).", gGameStatusPtr->saveSlot, port_testing_current_map(),
             gGameStatusPtr->entryID);
    return 1;
}

int port_testing_quick_load(char* msg, size_t msgSize) {
    if (!port_testing_in_world()) {
        snprintf(msg, msgSize, "Quick load only works while exploring a map.");
        return 0;
    }
    if (!fio_load_game(gGameStatusPtr->saveSlot)) {
        snprintf(msg, msgSize, "The active save slot is empty.");
        return 0;
    }
    // Same path as picking a file in the file menu: re-enter the world from the save.
    set_game_mode(GAME_MODE_ENTER_WORLD);
    snprintf(msg, msgSize, "Loading save slot %d...", gGameStatusPtr->saveSlot);
    return 1;
}

int port_testing_warp(int area, int map, int entry, char* msg, size_t msgSize) {
    if (!port_testing_in_world()) {
        snprintf(msg, msgSize, "Warping only works while exploring a map.");
        return 0;
    }
    if (!valid_area(area) || map < 0 || map >= gAreas[area].mapCount) {
        snprintf(msg, msgSize, "Invalid area/map.");
        return 0;
    }
    // Same as the GotoMap script command.
    gGameStatusPtr->areaID = (s16)area;
    gGameStatusPtr->mapID = (s16)map;
    gGameStatusPtr->entryID = (s16)entry;
    set_map_transition_effect(TRANSITION_STANDARD);
    set_game_mode(GAME_MODE_CHANGE_MAP);
    snprintf(msg, msgSize, "Warping to %s entry %d.", gAreas[area].maps[map].id, entry);
    return 1;
}

int port_testing_warp_by_name(const char* mapName, int entry, int story, char* msg, size_t msgSize) {
    s16 areaID = 0;
    s16 mapID = 0;
    if (!get_map_IDs_by_name(mapName, &areaID, &mapID)) {
        snprintf(msg, msgSize, "Unknown map %s.", mapName);
        return 0;
    }
    if (story != INT_MIN) {
        evt_set_variable(NULL, GB_StoryProgress, story);
    }
    return port_testing_warp(areaID, mapID, entry, msg, msgSize);
}

static int append_status(char* buf, size_t size, int n, const char* fmt, ...) {
    va_list ap;
    int written;
    if (n < 0 || (size_t)n >= size) {
        return n;
    }
    va_start(ap, fmt);
    written = vsnprintf(buf + n, size - (size_t)n, fmt, ap);
    va_end(ap);
    return written < 0 ? n : n + written;
}

void port_testing_status_text(char* buf, size_t size) {
    PlayerStatus* ps = &gPlayerStatus;
    CollisionStatus* cs = &gCollisionStatus;
    int n = 0;

    if (size == 0) {
        return;
    }
    buf[0] = '\0';
    n = append_status(buf, size, n, "Mode %d, map %s entry %d, story %d, partner %d, load type %d\n", get_game_mode(),
                      port_testing_current_map(), gGameStatusPtr->entryID, evt_get_variable(NULL, GB_StoryProgress),
                      gPlayerData.curPartner, gGameStatusPtr->loadType);
    n = append_status(buf, size, n, "Player: pos %.1f %.1f %.1f  state %d  speed %.2f\n", ps->pos.x, ps->pos.y,
                      ps->pos.z, ps->actionState, ps->curSpeed);
    n = append_status(buf, size, n, "  anim %08X  flags %08X  animFlags %08X  collider h%d d%d\n", (unsigned)ps->anim,
                      (unsigned)ps->flags, (unsigned)ps->animFlags, ps->colliderHeight, ps->colliderDiameter);
    n = append_status(buf, size, n, "  noStaticCollision %d (nesting %d)  cutsceneMove %d  inputDisabled %d  invisible %d\n",
                      (ps->flags & PS_FLAG_NO_STATIC_COLLISION) != 0, ps->enableCollisionOverlapsCheck,
                      (ps->flags & PS_FLAG_CUTSCENE_MOVEMENT) != 0, (ps->flags & PS_FLAG_INPUT_DISABLED) != 0,
                      (ps->animFlags & PA_FLAG_INVISIBLE) != 0);
    n = append_status(buf, size, n, "Collision: floor %d  wall %d  inspect %d  pushing %d  floorBelow %d\n",
                      cs->curFloor, cs->curWall, cs->curInspect, cs->pushingAgainstWall, cs->floorBelow);
    n = append_status(buf, size, n, "  map colliders %d, zones %d\n", gCollisionData.numColliders,
                      gZoneCollisionData.numColliders);
    if (get_game_mode() == GAME_MODE_WORLD) {
        Npc* partner = get_npc_safe(NPC_PARTNER);
        if (partner != NULL) {
            n = append_status(buf, size, n,
                              "Partner: pos %.1f %.1f %.1f  floor %d  wall %d  anim %08X  alpha %d  flags %08X\n",
                              partner->pos.x, partner->pos.y, partner->pos.z, partner->curFloor, partner->curWall,
                              (unsigned)partner->curAnim, partner->alpha, (unsigned)partner->flags);
        } else {
            n = append_status(buf, size, n, "Partner: none\n");
        }
    }
    (void)n;
}

/* The on-demand report is written to its own file as well as to the log. The log
 * reaches the dialog through a pipe and a pump thread, so its final lines can still
 * be in flight when Java reads the file; this copy is closed before the dialog opens. */
static FILE* sReportFile = NULL;

static void rp(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (sReportFile != NULL) {
        va_start(ap, fmt);
        vfprintf(sReportFile, fmt, ap);
        va_end(ap);
    }
}

/* Collider state as the map loaded it, so the report can show what changed since. */
typedef struct ColliderSnapshot {
    s32 flags;
    s16 numTriangles;
    s16 hasAabb;
    f32 min[3];
    f32 max[3];
} ColliderSnapshot;

static ColliderSnapshot sLoadState[512];
static int sLoadStateCount = 0;

static f32 aabb_distance_xz(ColliderBoundingBox* aabb, f32 x, f32 z) {
    f32 dx = 0.0f;
    f32 dz = 0.0f;
    if (x < aabb->min.x) {
        dx = aabb->min.x - x;
    } else if (x > aabb->max.x) {
        dx = x - aabb->max.x;
    }
    if (z < aabb->min.z) {
        dz = aabb->min.z - z;
    } else if (z > aabb->max.z) {
        dz = z - aabb->max.z;
    }
    return sqrtf(dx * dx + dz * dz);
}

/* The port keeps a map's name tables as the raw N64 arrays: big-endian 32-bit
 * addresses (0x80210000-based) into the shape buffer gMapShapeData. */
static const char* name_from_table(const void* list, int index) {
    const u8* base = (const u8*)&gMapShapeData;
    const u8* table;
    u32 n64Addr;
    u32 offset;
    const char* name;
    int i;

    if (list == NULL || index < 0) {
        return "?";
    }
    table = (const u8*)list + (size_t)index * 4;
    n64Addr = ((u32)table[0] << 24) | ((u32)table[1] << 16) | ((u32)table[2] << 8) | (u32)table[3];
    if (n64Addr < 0x80210000u) {
        return "?";
    }
    offset = n64Addr - 0x80210000u;
    if (offset >= 0x100000u) {
        return "?";
    }
    name = (const char*)(base + offset);
    for (i = 0; i < 32; i++) {
        if (name[i] == '\0') {
            return i > 0 ? name : "?";
        }
        if (name[i] < 0x20 || name[i] > 0x7E) {
            return "?";
        }
    }
    return "?";
}

static const char* collider_name(int index) {
    MapSettings* settings = get_current_map_settings();
    if (settings == NULL || index >= gCollisionData.numColliders) {
        return "?";
    }
    return name_from_table(settings->colliderNameList, index);
}

static const char* model_name(int treeIndex) {
    MapSettings* settings = get_current_map_settings();
    if (settings == NULL || treeIndex >= MAX_MODELS) {
        return "?";
    }
    return name_from_table(settings->modelNameList, treeIndex);
}

/* Log every map model that carries a script transform (TranslateModel/RotateModel/
 * ScaleModel or a transform group): flags, matrix state and the translation and
 * diagonal of the user matrix. Doors, gates and cutscene props are drawn through
 * this path, so a report saved while one looks wrong shows what the game asked for. */
void port_testing_dump_models(void) {
    int i, printed = 0, total = 0, hidden = 0;

    if (gCurrentModels == NULL) {
        rp("[models] no model list\n");
        return;
    }
    for (i = 0; i < MAX_MODELS; i++) {
        Model* m = (*gCurrentModels)[i];
        f32 (*u)[4];
        int transformed;
        if (m == NULL) {
            continue;
        }
        total++;
        if (m->flags & MODEL_FLAG_HIDDEN) {
            hidden++;
        }
        u = m->userTransformMtx;
        transformed = (m->flags & (MODEL_FLAG_HAS_TRANSFORM | MODEL_FLAG_MATRIX_DIRTY | MODEL_FLAG_TRANSFORM_GROUP_MEMBER)) != 0 ||
                      u[3][0] != 0.0f || u[3][1] != 0.0f || u[3][2] != 0.0f ||
                      u[0][0] != 1.0f || u[1][1] != 1.0f || u[2][2] != 1.0f ||
                      u[0][1] != 0.0f || u[0][2] != 0.0f || u[1][0] != 0.0f || u[1][2] != 0.0f || u[2][0] != 0.0f || u[2][1] != 0.0f;
        if (!transformed || printed >= 48) {
            continue;
        }
        printed++;
        rp("[models]  #%d %s id %d flags %04X fresh %d baked %d final %s center (%.0f %.0f %.0f) user T=(%.1f %.1f %.1f) diag=(%.2f %.2f %.2f) rot=(%.2f %.2f %.2f)\n",
                i, model_name(m->modelID), m->modelID, m->flags, m->matrixFreshness, m->bakedMtx != NULL,
                m->finalMtx == NULL ? "null" : (m->finalMtx == &m->savedMtx ? "saved" : "stack"),
                m->center.x, m->center.y, m->center.z, u[3][0], u[3][1], u[3][2], u[0][0], u[1][1], u[2][2],
                u[0][2], u[1][0], u[2][0]);
    }
    rp("[models] %d models, %d hidden, %d with a transform listed\n", total, hidden, printed);
}

static f32 unit_xz(f32* dx, f32* dz) {
    f32 len = sqrtf(*dx * *dx + *dz * *dz);
    if (len > 0.0f) {
        *dx /= len;
        *dz /= len;
    }
    return len;
}

/* Cast the game's own wall ray (test_ray_colliders, horizontal, ignoring
 * COLLIDER_FLAG_IGNORE_PLAYER) and describe the outcome in one line. */
static void log_wall_ray(const char* label, f32 sx, f32 sy, f32 sz, f32 dx, f32 dz, f32 depth) {
    f32 hx, hy, hz, nx, ny, nz;
    f32 d = depth;
    s32 hit = test_ray_colliders(COLLIDER_FLAG_IGNORE_PLAYER, sx, sy, sz, dx, 0.0f, dz, &hx, &hy, &hz, &d, &nx, &ny,
                                 &nz);
    if (hit >= 0) {
        rp("    %s: hit #%d (%s) at depth %.2f of %.2f, point (%.1f %.1f %.1f) n=(%.2f %.2f %.2f)\n",
                label, hit, collider_name(hit), d, depth, hx, hy, hz, nx, ny, nz);
    } else {
        rp("    %s: no hit within %.2f (ret %d)\n", label, depth, hit);
    }
}

/* Runs every wall and floor triangle of the freshly loaded map through the game's
 * own ray tests: a short ray cast from just in front of each triangle's centre
 * straight at it must hit that triangle's collider. Called from load_hit_data(). */
void port_testing_collision_selfcheck(void) {
    CollisionData* cd = &gCollisionData;
    int walls = 0, wallHits = 0, wallOther = 0;
    int floors = 0, floorHits = 0, floorOther = 0;
    int degenerate = 0, printed = 0;
    int i, t;

    sLoadStateCount = cd->numColliders;
    if (sLoadStateCount > (int)(sizeof(sLoadState) / sizeof(sLoadState[0]))) {
        sLoadStateCount = (int)(sizeof(sLoadState) / sizeof(sLoadState[0]));
    }
    for (i = 0; i < sLoadStateCount; i++) {
        Collider* c = &cd->colliderList[i];
        ColliderSnapshot* snap = &sLoadState[i];
        snap->flags = c->flags;
        snap->numTriangles = c->numTriangles;
        snap->hasAabb = (c->numTriangles != 0 && c->aabb != NULL);
        if (snap->hasAabb) {
            snap->min[0] = c->aabb->min.x; snap->min[1] = c->aabb->min.y; snap->min[2] = c->aabb->min.z;
            snap->max[0] = c->aabb->max.x; snap->max[1] = c->aabb->max.y; snap->max[2] = c->aabb->max.z;
        }
    }

    for (i = 0; i < cd->numColliders; i++) {
        Collider* c = &cd->colliderList[i];
        if (c->numTriangles == 0 || c->aabb == NULL || (c->flags & COLLIDER_FLAG_IGNORE_PLAYER)) {
            continue;
        }
        for (t = 0; t < c->numTriangles; t++) {
            ColliderTriangle* tri = &c->triangleTable[t];
            f32 nx = tri->normal.x, ny = tri->normal.y, nz = tri->normal.z;
            f32 cx = (tri->v1->x + tri->v2->x + tri->v3->x) / 3.0f;
            f32 cy = (tri->v1->y + tri->v2->y + tri->v3->y) / 3.0f;
            f32 cz = (tri->v1->z + tri->v2->z + tri->v3->z) / 3.0f;
            f32 hx, hy, hz, hnx, hny, hnz;
            f32 depth = 10.0f;
            s32 hit;

            if (nx == 0.0f && ny == 0.0f && nz == 0.0f) {
                degenerate++;
                continue;
            }
            if (fabsf(ny) < 0.5f) {
                f32 dx = -nx, dz = -nz;
                unit_xz(&dx, &dz);
                walls++;
                hit = test_ray_colliders(COLLIDER_FLAG_IGNORE_PLAYER, cx - dx * 5.0f, cy, cz - dz * 5.0f, dx, 0.0f, dz,
                                         &hx, &hy, &hz, &depth, &hnx, &hny, &hnz);
                if (hit == i) {
                    wallHits++;
                } else if (hit >= 0) {
                    wallOther++;
                } else if (printed < 8) {
                    printed++;
                    rp("[collision] self-check MISS wall #%d (%s) tri%d centre (%.1f %.1f %.1f) n=(%.2f %.2f %.2f) oneSided=%d: ret %d\n",
                            i, collider_name(i), t, cx, cy, cz, nx, ny, nz, tri->oneSided, hit);
                }
            } else if (ny > 0.5f) {
                floors++;
                hit = test_ray_colliders(COLLIDER_FLAG_IGNORE_PLAYER, cx, cy + 5.0f, cz, 0.0f, -1.0f, 0.0f, &hx, &hy,
                                         &hz, &depth, &hnx, &hny, &hnz);
                if (hit == i) {
                    floorHits++;
                } else if (hit >= 0) {
                    floorOther++;
                } else if (printed < 8) {
                    printed++;
                    rp("[collision] self-check MISS floor #%d (%s) tri%d centre (%.1f %.1f %.1f) n=(%.2f %.2f %.2f) oneSided=%d: ret %d\n",
                            i, collider_name(i), t, cx, cy, cz, nx, ny, nz, tri->oneSided, hit);
                }
            }
        }
    }
    rp("[collision] self-check: walls %d hit / %d other / %d total, floors %d hit / %d other / %d total, degenerate %d\n",
            wallHits, wallOther, walls, floorHits, floorOther, floors, degenerate);
}

/* Called once per player update with the position the frame started from. If the
 * player's movement this frame passed through a solid wall triangle, log the event
 * and repeat the game's wall ray from the old position so the report shows what the
 * collision test returns for exactly this move. */
void port_testing_watch_player_walls(f32 prevX, f32 prevY, f32 prevZ) {
    static int sCooldown = 0;
    static int sLogged = 0;
    PlayerStatus* ps = &gPlayerStatus;
    CollisionStatus* cs = &gCollisionStatus;
    CollisionData* cd = &gCollisionData;
    f32 x0 = prevX, y0 = prevY + 10.01f, z0 = prevZ;
    f32 x1 = ps->pos.x, y1 = ps->pos.y + 10.01f, z1 = ps->pos.z;
    f32 mx = x1 - x0, my = y1 - y0, mz = z1 - z0;
    f32 moveLen = sqrtf(mx * mx + mz * mz);
    f32 bbMinX, bbMaxX, bbMinY, bbMaxY, bbMinZ, bbMaxZ;
    int i, t;

    if (get_game_mode() != GAME_MODE_WORLD || sLogged >= 24) {
        return;
    }
    if (sCooldown > 0) {
        sCooldown--;
        return;
    }
    // Scripts and map transitions move the player through walls legitimately.
    if ((ps->flags & (PS_FLAG_NO_STATIC_COLLISION | PS_FLAG_CUTSCENE_MOVEMENT)) || moveLen < 0.01f || moveLen > 60.0f) {
        return;
    }
    bbMinX = (x0 < x1 ? x0 : x1) - 1.0f; bbMaxX = (x0 > x1 ? x0 : x1) + 1.0f;
    bbMinY = (y0 < y1 ? y0 : y1) - 1.0f; bbMaxY = (y0 > y1 ? y0 : y1) + 1.0f;
    bbMinZ = (z0 < z1 ? z0 : z1) - 1.0f; bbMaxZ = (z0 > z1 ? z0 : z1) + 1.0f;

    for (i = 0; i < cd->numColliders; i++) {
        Collider* c = &cd->colliderList[i];
        if (c->numTriangles == 0 || c->aabb == NULL || (c->flags & COLLIDER_FLAG_IGNORE_PLAYER)) {
            continue;
        }
        if (bbMaxX < c->aabb->min.x || bbMinX > c->aabb->max.x || bbMaxZ < c->aabb->min.z ||
            bbMinZ > c->aabb->max.z || bbMaxY < c->aabb->min.y || bbMinY > c->aabb->max.y) {
            continue;
        }
        for (t = 0; t < c->numTriangles; t++) {
            ColliderTriangle* tri = &c->triangleTable[t];
            f32 nx = tri->normal.x, ny = tri->normal.y, nz = tri->normal.z;
            f32 d0, d1, k, px, py, pz;
            f32 ax, ay, az, bx, by, bz, cx, cy, cz;
            f32 e0, e1, e2;
            int crossed;
            int fromBehind = 0;

            if (fabsf(ny) >= 0.5f || (nx == 0.0f && ny == 0.0f && nz == 0.0f)) {
                continue;
            }
            d0 = nx * (x0 - tri->v1->x) + ny * (y0 - tri->v1->y) + nz * (z0 - tri->v1->z);
            d1 = nx * (x1 - tri->v1->x) + ny * (y1 - tri->v1->y) + nz * (z1 - tri->v1->z);
            crossed = (d0 >= 0.0f && d1 < 0.0f) ? 1 : ((d0 <= 0.0f && d1 > 0.0f) ? 2 : 0);
            if (crossed == 0 || d0 == d1) {
                continue;
            }
            // Passing through the back of a one-sided wall is how the original game behaves,
            // so say which case this is rather than reporting both as a collision failure.
            if (crossed == 2 && tri->oneSided) {
                fromBehind = 1;
            }
            k = d0 / (d0 - d1);
            px = x0 + mx * k; py = y0 + my * k; pz = z0 + mz * k;
            // inside test: the loader's normal is (v2 - v1) x (v3 - v1), so v1, v2, v3 wind
            // counter-clockwise around it
            ax = tri->v2->x - tri->v1->x; ay = tri->v2->y - tri->v1->y; az = tri->v2->z - tri->v1->z;
            bx = px - tri->v1->x; by = py - tri->v1->y; bz = pz - tri->v1->z;
            e0 = (ay * bz - az * by) * nx + (az * bx - ax * bz) * ny + (ax * by - ay * bx) * nz;
            ax = tri->v3->x - tri->v2->x; ay = tri->v3->y - tri->v2->y; az = tri->v3->z - tri->v2->z;
            bx = px - tri->v2->x; by = py - tri->v2->y; bz = pz - tri->v2->z;
            e1 = (ay * bz - az * by) * nx + (az * bx - ax * bz) * ny + (ax * by - ay * bx) * nz;
            ax = tri->v1->x - tri->v3->x; ay = tri->v1->y - tri->v3->y; az = tri->v1->z - tri->v3->z;
            bx = px - tri->v3->x; by = py - tri->v3->y; bz = pz - tri->v3->z;
            e2 = (ay * bz - az * by) * nx + (az * bx - ax * bz) * ny + (ax * by - ay * bx) * nz;
            if (e0 < -0.01f || e1 < -0.01f || e2 < -0.01f) {
                continue;
            }

            cx = mx; cz = mz;
            unit_xz(&cx, &cz);
            sLogged++;
            sCooldown = 30;
            rp("[collision] PLAYER CROSSED WALL #%d (%s) tri%d flags=%08X oneSided=%d%s n=(%.2f %.2f %.2f) at (%.1f %.1f %.1f)\n",
                    i, collider_name(i), t, (unsigned)c->flags, tri->oneSided,
                    fromBehind ? " FROM BEHIND (not solid from this side)" : "", nx, ny, nz, px, py, pz);
            rp("    move (%.2f %.2f %.2f) -> (%.2f %.2f %.2f) len %.2f, speed %.2f state %d flags %08X animFlags %08X\n",
                    prevX, prevY, prevZ, ps->pos.x, ps->pos.y, ps->pos.z, moveLen, ps->curSpeed, ps->actionState,
                    (unsigned)ps->flags, (unsigned)ps->animFlags);
            rp("    yaw target %.1f cur %.1f heading %.1f facing %.1f cam %.1f; curWall %d pushing %d floor %d; overlaps nesting %d timeInAir %d pushVel (%.2f %.2f %.2f)\n",
                    ps->targetYaw, ps->curYaw, ps->heading, ps->spriteFacingAngle, gCameras[gCurrentCameraID].curYaw,
                    cs->curWall, cs->pushingAgainstWall, cs->curFloor, ps->enableCollisionOverlapsCheck, ps->timeInAir,
                    ps->pushVel.x, ps->pushVel.y, ps->pushVel.z);
            rp("    map %s entry %d story %d partner %d; collider aabb (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n",
                    port_testing_current_map(), gGameStatusPtr->entryID, evt_get_variable(NULL, GB_StoryProgress),
                    gPlayerData.curPartner, c->aabb->min.x, c->aabb->min.y, c->aabb->min.z, c->aabb->max.x,
                    c->aabb->max.y, c->aabb->max.z);
            log_wall_ray("re-test from old pos, +10.01, len+13", x0, y0, z0, cx, cz, moveLen + 13.0f);
            log_wall_ray("re-test from old pos, +10.01, len 40", x0, y0, z0, cx, cz, 40.0f);
            log_wall_ray("re-test from old pos, +0.1", x0, prevY + 0.1f, z0, cx, cz, moveLen + 13.0f);
            log_wall_ray("re-test from old pos, +0.75*height", x0, prevY + ps->colliderHeight * 0.75f, z0, cx, cz,
                         moveLen + 13.0f);
            {
                f32 hx, hy, hz, hnx, hny, hnz;
                f32 depth = moveLen + 13.0f;
                s32 ent = test_ray_entities(x0, y0, z0, cx, 0.0f, cz, &hx, &hy, &hz, &depth, &hnx, &hny, &hnz);
                rp("    entity ray: %d, depth left %.2f\n", ent, depth);
            }
            {
                f32 x = prevX, y = prevY, z = prevZ;
                f32 yaw = atan2(0.0f, 0.0f, mx, mz);
                HitID hit = player_test_move_with_slipping(ps, &x, &y, &z, moveLen, yaw);
                rp("    player_test_move_with_slipping(len %.2f, yaw %.1f) from old pos: hit %d, new pos (%.2f %.2f)\n",
                        moveLen, yaw, hit, x, z);
            }
            return;
        }
    }
}

/* Log the colliders near the player (flags, bounding box, triangles), the entities,
 * the bound triggers and the result of wall probes in eight directions, for
 * collision bug reports. */
void port_testing_dump_collision(void) {
    CollisionData* cd = &gCollisionData;
    PlayerStatus* ps = &gPlayerStatus;
    f32 px = ps->pos.x;
    f32 py = ps->pos.y;
    f32 pz = ps->pos.z;
    int printed = 0;
    int i;

    int near = 0;
    int changed = 0;
    char nearList[600];
    int nearLen = 0;

    rp("[collision] dump around player (%.1f %.1f %.1f): %d colliders, ignore mask %08X\n", px, py, pz,
       cd->numColliders, (unsigned)COLLIDER_FLAG_IGNORE_PLAYER);
    rp("  yaw target %.1f cur %.1f heading %.1f facing %.1f cam %.1f; overlaps nesting %d timeInAir %d\n",
       ps->targetYaw, ps->curYaw, ps->heading, ps->spriteFacingAngle, gCameras[gCurrentCameraID].curYaw,
       ps->enableCollisionOverlapsCheck, ps->timeInAir);

    /* One line naming every collider within range, so a lost detail line is obvious. */
    nearList[0] = '\0';
    for (i = 0; i < cd->numColliders; i++) {
        Collider* c = &cd->colliderList[i];
        if (c->numTriangles == 0 || c->aabb == NULL || aabb_distance_xz(c->aabb, px, pz) > 120.0f) {
            continue;
        }
        near++;
        if (nearLen < (int)sizeof(nearList) - 24) {
            nearLen += snprintf(nearList + nearLen, sizeof(nearList) - (size_t)nearLen, " #%d(%s)", i,
                                collider_name(i));
        }
    }
    rp("  within 120 units: %d colliders:%s\n", near, nearList);

    /* Anything a script changed since the map loaded (flags, triangles, bounding box). */
    for (i = 0; i < cd->numColliders && i < sLoadStateCount; i++) {
        Collider* c = &cd->colliderList[i];
        ColliderSnapshot* snap = &sLoadState[i];
        int hasAabb = (c->numTriangles != 0 && c->aabb != NULL);
        int moved = hasAabb && snap->hasAabb &&
                    (c->aabb->min.x != snap->min[0] || c->aabb->min.y != snap->min[1] ||
                     c->aabb->min.z != snap->min[2] || c->aabb->max.x != snap->max[0] ||
                     c->aabb->max.y != snap->max[1] || c->aabb->max.z != snap->max[2]);
        if (c->flags == snap->flags && c->numTriangles == snap->numTriangles && hasAabb == snap->hasAabb && !moved) {
            continue;
        }
        changed++;
        if (changed <= 24) {
            rp("  changed since load: #%d %s flags %08X->%08X tris %d->%d aabb %d->%d", i, collider_name(i),
               (unsigned)snap->flags, (unsigned)c->flags, snap->numTriangles, c->numTriangles, snap->hasAabb, hasAabb);
            if (moved) {
                rp(" box (%.0f %.0f %.0f)-(%.0f %.0f %.0f) -> (%.0f %.0f %.0f)-(%.0f %.0f %.0f)", snap->min[0],
                   snap->min[1], snap->min[2], snap->max[0], snap->max[1], snap->max[2], c->aabb->min.x,
                   c->aabb->min.y, c->aabb->min.z, c->aabb->max.x, c->aabb->max.y, c->aabb->max.z);
            }
            rp("\n");
        }
    }
    rp("  %d colliders changed since the map loaded\n", changed);

    for (i = 0; i < cd->numColliders && printed < 16; i++) {
        Collider* c = &cd->colliderList[i];
        f32 d;
        int t;
        if (c->numTriangles == 0 || c->aabb == NULL) {
            continue;
        }
        d = aabb_distance_xz(c->aabb, px, pz);
        if (d > 120.0f) {
            continue;
        }
        rp("  #%d %s flags=%08X tris=%d aabb=(%.0f %.0f %.0f)-(%.0f %.0f %.0f) dist=%.1f\n",
                i, collider_name(i), (unsigned)c->flags, c->numTriangles,
                c->aabb->min.x, c->aabb->min.y, c->aabb->min.z, c->aabb->max.x, c->aabb->max.y, c->aabb->max.z, d);
        for (t = 0; t < c->numTriangles && t < 12; t++) {
            ColliderTriangle* tri = &c->triangleTable[t];
            rp("     tri%d v1=(%.0f %.0f %.0f) v2=(%.0f %.0f %.0f) v3=(%.0f %.0f %.0f) n=(%.2f %.2f %.2f) oneSided=%d\n",
                    t, tri->v1->x, tri->v1->y, tri->v1->z, tri->v2->x, tri->v2->y, tri->v2->z, tri->v3->x, tri->v3->y,
                    tri->v3->z, tri->normal.x, tri->normal.y, tri->normal.z, tri->oneSided);
        }
        printed++;
    }

    rp("  entities:");
    for (i = 0; i < MAX_ENTITIES; i++) {
        Entity* e = get_entity_by_index(i);
        if (e == NULL) {
            continue;
        }
        rp(" [%d type %d flags %08X alpha %d pos (%.0f %.0f %.0f) size %.1f aabb (%d %d %d)]", i, e->type,
                (unsigned)e->flags, e->alpha, e->pos.x, e->pos.y, e->pos.z, e->effectiveSize, e->aabb.x, e->aabb.y,
                e->aabb.z);
    }
    rp("\n  triggers:");
    if (gCurrentTriggerListPtr != NULL) {
        for (i = 0; i < MAX_TRIGGERS; i++) {
            Trigger* tr = (*gCurrentTriggerListPtr)[i];
            if (tr == NULL) {
                continue;
            }
            rp(" [%d flags %08X collider %ld prompt %d]", i, (unsigned)tr->flags,
                    (long)tr->location.colliderID, tr->hasPlayerInteractPrompt);
        }
    }
    rp("\n");

    rp("  wall probes from the player (26 units, +10 up), yaw:hit@depth:");
    for (i = 0; i < 8; i++) {
        f32 yaw = (f32)(i * 45);
        f32 rad = yaw * 3.14159265f / 180.0f;
        f32 dx = sinf(rad);
        f32 dz = -cosf(rad);
        f32 hx, hy, hz, nx, ny, nz;
        f32 depth = 26.0f;
        s32 hit = test_ray_colliders(COLLIDER_FLAG_IGNORE_PLAYER, px, py + 10.01f, pz, dx, 0.0f, dz, &hx, &hy, &hz,
                                     &depth, &nx, &ny, &nz);
        if (hit >= 0) {
            rp(" %d:%d(%s)@%.1f", (int)yaw, hit, collider_name(hit), depth);
        } else {
            rp(" %d:-", (int)yaw);
        }
    }
    rp("\n  movement probes (player_test_move_with_slipping, 2 units), yaw:hit:");
    if (get_game_mode() == GAME_MODE_WORLD) {
        for (i = 0; i < 8; i++) {
            f32 x = px, y = py, z = pz;
            HitID hit = player_test_move_with_slipping(ps, &x, &y, &z, 2.0f, (f32)(i * 45));
            if (hit >= 0) {
                rp(" %d:%d(%s)", i * 45, hit, hit < cd->numColliders ? collider_name(hit) : "entity");
            } else {
                rp(" %d:-", i * 45);
            }
        }
    }
    rp("\n  dump complete (%d of %d nearby colliders listed in full)\n", printed, near);
}

int port_testing_request_report(char* msg, size_t msgSize) {
    char status[1024];
    char path[512];
    const char* reportPath = port_get_data_path("papership_report.txt", path, sizeof(path));

    if (reportPath != NULL) {
        sReportFile = fopen(reportPath, "w");
    }
    // Put the live status into the log (and the report file) so it travels with the report.
    port_testing_status_text(status, sizeof(status));
    rp("[status] report requested\n%s", status);
    if (get_game_mode() == GAME_MODE_WORLD) {
        port_testing_dump_collision();
        port_testing_dump_models();
    }
    if (sReportFile != NULL) {
        fflush(sReportFile);
        fclose(sReportFile);
        sReportFile = NULL;
    }
    fflush(stdout);
    fflush(stderr);
#ifdef __ANDROID__
    // Let the pump thread write the log out before the dialog reads the file.
    port_android_log_sync();
    port_android_request_report();
    snprintf(msg, msgSize, "Report dialog requested (close this menu if it is hidden behind it).");
    return 1;
#else
    snprintf(msg, msgSize, "Wrote %s (on desktop the log itself is stderr).",
             reportPath != NULL ? reportPath : "(unknown)");
    return 0;
#endif
}
