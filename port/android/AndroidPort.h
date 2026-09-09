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

/** Redirect stdout/stderr to logcat and to <data dir>/papership_log.txt. Call once, first thing in main(). */
void port_android_init(void);

/** Install the crash handler (alternate signal stack + report file) for the calling thread. */
void port_android_install_crash_handler(void);

/** Give the calling thread an alternate signal stack so the crash handler can run on stack overflow. */
void port_android_setup_thread_crash_stack(void);

/** Append the clean-shutdown marker to the session log (MainActivity checks it on the next launch). */
void port_android_mark_clean_shutdown(void);

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
