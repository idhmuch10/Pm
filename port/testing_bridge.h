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

/* Multi-line live status: player position/state, collision ids, partner NPC, sprite. */
void port_testing_status_text(char* buf, size_t size);

/* Log the colliders near the player, the entities and triggers, and wall/movement probes
 * in eight directions (part of the report). */
void port_testing_dump_collision(void);

/* Log every map model with a script transform (doors, gates, cutscene props). */
void port_testing_dump_models(void);

/* Ray-test every wall and floor triangle of the loaded map against itself (map load). */
void port_testing_collision_selfcheck(void);

/* Per-frame watchdog: logs when the player's movement passed through a solid wall and
 * repeats the game's wall ray from the previous position. */
void port_testing_watch_player_walls(float prevX, float prevY, float prevZ);

/* Offer the current session log as a report (Android: the Copy / Save / Share dialog).
 * Returns non-zero when a dialog was requested; msg receives a status line either way. */
int port_testing_request_report(char* msg, size_t msgSize);


/* Party and badges: put a partner or a badge in the save without playing to the point
 * where the game would have given it, so a test can start from the state that matters. */
int port_testing_partner_count(void);
const char* port_testing_partner_name(int partnerID);
int port_testing_partner_in_party(int partnerID);
int port_testing_current_partner(void);
void port_testing_set_partner_in_party(int partnerID, int inParty);
/* Brings the partner out, which is what creates the world NPC. PARTNER_NONE (0) puts
 * the current one away. */
void port_testing_set_current_partner(int partnerID);

int port_testing_badge_count(void);
int port_testing_badges_held(void);
int port_testing_grant_all_badges(char* msg, size_t msgSize);
int port_testing_clear_badges(char* msg, size_t msgSize);
int port_testing_badge_points(void);
void port_testing_set_badge_points(int bp);

#ifdef __cplusplus
}
#endif
