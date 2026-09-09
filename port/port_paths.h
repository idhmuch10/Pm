#pragma once
/**
 * port_paths.h - Platform-independent helpers for locating the port's data files.
 *
 * Implemented in port/NuSystemShims.cpp. Usable from both C and C++.
 */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build "<app data directory>/<filename>" into `out` and return it.
 * Desktop: next to the executable (or $SHIP_HOME); Android: the app's private
 * external files directory (Android/data/com.papership.mobile/files).
 */
const char* port_get_data_path(const char* filename, char* out, size_t outSize);

/** Force the ROM search to try `path` first (Android file picker, launchers). */
void port_set_rom_path(const char* path);

/** Non-zero once a ROM image has been loaded into memory. */
int port_rom_is_loaded(void);

#ifdef __cplusplus
}
#endif
