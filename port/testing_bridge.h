#pragma once
/**
 * testing_bridge.h - Game-state helpers for the settings menu's Testing tab.
 *
 * Implemented in C (port/testing_bridge.c) so the decomp headers, whose script
 * macros (End, Set, Call, ...) clash with C++ code such as ImGui::End(), never
 * enter the C++ menu file.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int port_testing_in_world(void);
int port_testing_game_mode(void);
const char* port_testing_current_map(void);
int port_testing_current_entry(void);
int port_testing_current_slot(void);
int port_testing_story_progress(void);
void port_testing_set_story_progress(int value);

int port_testing_area_count(void);
const char* port_testing_area_id(int area);
int port_testing_map_count(int area);
const char* port_testing_map_id(int area, int map);
int port_testing_current_area(void);
int port_testing_current_map_index(void);

/* Each returns non-zero on success and writes a status message into msg. */
int port_testing_quick_save(char* msg, size_t msgSize);
int port_testing_quick_load(char* msg, size_t msgSize);
int port_testing_warp(int area, int map, int entry, char* msg, size_t msgSize);
/* story: INT_MIN keeps the current story progress. */
int port_testing_warp_by_name(const char* mapName, int entry, int story, char* msg, size_t msgSize);

/* Story progress value of the intro (before the first Bowser fight). */
int port_testing_story_intro(void);

#ifdef __cplusplus
}
#endif
