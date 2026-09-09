/**
 * testing_bridge.c - Testing shortcuts backed by the game's own save/load and
 * map-change code paths (see testing_bridge.h).
 */
#include "common.h"
#include "game_modes.h"
#include "fio.h"
#include "map.h"
#include "testing_bridge.h"
#include <limits.h>
#include <stdio.h>

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
