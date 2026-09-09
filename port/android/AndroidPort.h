#pragma once
/**
 * AndroidPort.h - Android glue for PaperShip Mobile.
 *
 * Only compiled into the Android build (port/android/AndroidPort.cpp is wrapped
 * in #ifdef __ANDROID__). The Java side of these calls lives in
 * android/app/src/main/java/com/papership/mobile/MainActivity.java.
 */
#ifdef __cplusplus
extern "C" {
#endif

/** Redirect stdout/stderr to logcat. Call once, first thing in main(). */
void port_android_init(void);

/** Block until MainActivity has copied the data files and imported a ROM. */
void port_android_wait_for_setup(void);

/** Run once per frame on the game thread (menu toggle requests from Java). */
void port_android_pre_frame(void);

/** Merge the on-screen controller state into the polled pads (OSContPad[MAXCONTROLLERS]). */
void port_android_merge_input(void* pads);

/** Crash diagnostics: native backtrace and the most recent log lines (also sent to logcat). */
void port_android_write_backtrace(void* file);
void port_android_dump_recent_log(void* file);

#ifdef __cplusplus
}
#endif
