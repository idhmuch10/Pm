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
    n = append_status(buf, size, n, "Mode %d, map %s entry %d\n", get_game_mode(), port_testing_current_map(),
                      gGameStatusPtr->entryID);
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

/* The port keeps the map's name table as the raw N64 array: big-endian 32-bit
 * addresses (0x80210000-based) into the shape buffer gMapShapeData. */
static const char* collider_name(int index) {
    MapSettings* settings = get_current_map_settings();
    const u8* base = (const u8*)&gMapShapeData;
    const u8* table;
    u32 n64Addr;
    u32 offset;
    const char* name;
    int i;

    if (settings == NULL || settings->colliderNameList == NULL || index < 0 || index >= gCollisionData.numColliders) {
        return "?";
    }
    table = (const u8*)settings->colliderNameList + (size_t)index * 4;
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

/* Log the colliders near the player (flags, bounding box, first triangles) and the
 * result of horizontal wall probes in eight directions, for collision bug reports. */
void port_testing_dump_collision(void) {
    CollisionData* cd = &gCollisionData;
    PlayerStatus* ps = &gPlayerStatus;
    f32 px = ps->pos.x;
    f32 py = ps->pos.y;
    f32 pz = ps->pos.z;
    int printed = 0;
    int i;

    fprintf(stderr, "[collision] dump around player (%.1f %.1f %.1f): %d colliders, ignore mask %08X\n", px, py, pz,
            cd->numColliders, (unsigned)COLLIDER_FLAG_IGNORE_PLAYER);
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
        fprintf(stderr, "  #%d %s flags=%08X tris=%d verts=%d parentModel=%d aabb=(%.0f %.0f %.0f)-(%.0f %.0f %.0f) dist=%.1f\n",
                i, collider_name(i), (unsigned)c->flags, c->numTriangles, c->numVertices, c->parentModelIndex,
                c->aabb->min.x, c->aabb->min.y, c->aabb->min.z, c->aabb->max.x, c->aabb->max.y, c->aabb->max.z, d);
        for (t = 0; t < c->numTriangles && t < 4; t++) {
            ColliderTriangle* tri = &c->triangleTable[t];
            fprintf(stderr, "     tri%d v1=(%.0f %.0f %.0f) v2=(%.0f %.0f %.0f) v3=(%.0f %.0f %.0f) n=(%.2f %.2f %.2f) oneSided=%d\n",
                    t, tri->v1->x, tri->v1->y, tri->v1->z, tri->v2->x, tri->v2->y, tri->v2->z, tri->v3->x, tri->v3->y,
                    tri->v3->z, tri->normal.x, tri->normal.y, tri->normal.z, tri->oneSided);
        }
        printed++;
    }

    fprintf(stderr, "  wall probes from the player (26 units, +10 up), yaw:hit@depth:");
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
            fprintf(stderr, " %d:%d(%s)@%.1f", (int)yaw, hit, collider_name(hit), depth);
        } else {
            fprintf(stderr, " %d:-", (int)yaw);
        }
    }
    fprintf(stderr, "\n");
}

int port_testing_request_report(char* msg, size_t msgSize) {
    char status[1024];

    // Put the live status into the log so it travels with the report.
    port_testing_status_text(status, sizeof(status));
    fprintf(stderr, "[status] report requested\n%s", status);
    if (get_game_mode() == GAME_MODE_WORLD) {
        port_testing_dump_collision();
    }
    fflush(stdout);
    fflush(stderr);
#ifdef __ANDROID__
    port_android_request_report();
    snprintf(msg, msgSize, "Report dialog requested (close this menu if it is hidden behind it).");
    return 1;
#else
    {
        char path[512];
        const char* logPath = port_get_data_path("papership_log.txt", path, sizeof(path));
        snprintf(msg, msgSize, "On desktop the log is stderr; data directory file would be %s.",
                 logPath != NULL ? logPath : "(unknown)");
    }
    return 0;
#endif
}
