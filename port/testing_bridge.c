/**
 * testing_bridge.c - Testing shortcuts backed by the game's own save/load and
 * map-change code paths (see testing_bridge.h).
 */
#include "common.h"
#include "game_modes.h"
#include "fio.h"
#include "map.h"
#include "testing_bridge.h"
#include "port_paths.h"
#include <limits.h>
#include <stdio.h>
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

int port_testing_request_report(char* msg, size_t msgSize) {
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
