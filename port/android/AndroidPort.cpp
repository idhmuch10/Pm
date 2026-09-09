/**
 * AndroidPort.cpp - Android glue for PaperShip Mobile.
 *
 * Everything in this file is compiled only for Android. It
 *  - redirects stdout/stderr to logcat (tag "PaperShip"), since the port logs with fprintf,
 *  - blocks the SDL thread until MainActivity has copied the bundled data files and the
 *    user has imported a ROM (see port_android_wait_for_setup),
 *  - receives the on-screen controller state through JNI and merges it into the N64 pad
 *    that NuSystemShims.cpp hands to the game,
 *  - lets the Java side open/close the settings menu (back button, MENU overlay button).
 */
#ifdef __ANDROID__

#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <unwind.h>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

// libultraship's umbrella header must be seen before common.h is included with
// C linkage below (ultra64.h pulls it in), exactly like NuSystemShims.cpp does.
#include <libultraship.h>
#include <SDL2/SDL.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include "android/AndroidPort.h"
#include "port_paths.h"

extern "C" {
#include "common.h"
}

#define LOG_TAG "PaperShip"

// ---------------------------------------------------------------------------
// State written by the Java UI thread, read by the game thread
// ---------------------------------------------------------------------------
static std::atomic<bool> sSetupDone{ false };
static std::atomic<uint32_t> sTouchButtons{ 0 }; // N64 button mask (OSContPad.button bits)
static std::atomic<int> sTouchStickX{ 0 };       // -80..80, N64 stick range
static std::atomic<int> sTouchStickY{ 0 };
static std::atomic<int> sMenuToggleRequests{ 0 };

// Data directory (set by MainActivity.nativeSetDataDir before the game thread starts)
// and the file paths derived from it. Fixed-size buffers: the crash handler must not
// allocate or call into Java.
static char sDataDir[1024] = "";
static char sSessionLogPath[1100] = "";
static char sCrashLogPath[1100] = "";
static FILE* sSessionLog = nullptr;

// ---------------------------------------------------------------------------
// stdout/stderr -> logcat (+ a ring buffer of recent lines for crash reports)
// ---------------------------------------------------------------------------
static int sLogPipe[2] = { -1, -1 };
static std::mutex sRecentLogMutex;
static std::deque<std::string> sRecentLog;
static constexpr size_t kRecentLogLines = 250;

static void RememberLogLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(sRecentLogMutex);
    sRecentLog.push_back(line);
    while (sRecentLog.size() > kRecentLogLines) {
        sRecentLog.pop_front();
    }
    if (sSessionLog != nullptr) {
        // Flushed per line so the file survives a crash even if the handler cannot run.
        fputs(line.c_str(), sSessionLog);
        fputc('\n', sSessionLog);
        fflush(sSessionLog);
    }
}

static void* LogcatPumpThread(void*) {
    char buf[1024];
    std::string line;
    for (;;) {
        ssize_t n = read(sLogPipe[0], buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                __android_log_write(ANDROID_LOG_INFO, LOG_TAG, line.c_str());
                RememberLogLine(line);
                line.clear();
            } else {
                line.push_back(buf[i]);
                if (line.size() >= 4000) { // logcat truncates very long lines
                    __android_log_write(ANDROID_LOG_INFO, LOG_TAG, line.c_str());
                    RememberLogLine(line);
                    line.clear();
                }
            }
        }
    }
    return nullptr;
}

extern "C" void port_android_init(void) {
    static bool sInitialized = false;
    if (sInitialized) {
        return;
    }
    sInitialized = true;

    if (sDataDir[0] != '\0') {
        snprintf(sSessionLogPath, sizeof(sSessionLogPath), "%s/papership_log.txt", sDataDir);
        snprintf(sCrashLogPath, sizeof(sCrashLogPath), "%s/papership_crash.log", sDataDir);
        sSessionLog = fopen(sSessionLogPath, "w");
        if (sSessionLog == nullptr) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "Could not open session log %s", sSessionLogPath);
        }
    }

    if (pipe(sLogPipe) == 0) {
        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        dup2(sLogPipe[1], STDOUT_FILENO);
        dup2(sLogPipe[1], STDERR_FILENO);

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_t thread;
        pthread_create(&thread, &attr, LogcatPumpThread, nullptr);
        pthread_attr_destroy(&attr);
    }
    __android_log_write(ANDROID_LOG_INFO, LOG_TAG, "PaperShip native library initialized");
}

// ---------------------------------------------------------------------------
// Crash diagnostics
// ---------------------------------------------------------------------------
struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code UnwindCallback(struct _Unwind_Context* context, void* arg) {
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc != 0) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        }
        *state->current++ = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

extern "C" void port_android_write_backtrace(void* filePtr) {
    FILE* file = static_cast<FILE*>(filePtr);
    void* frames[64];
    BacktraceState state = { frames, frames + 64 };
    _Unwind_Backtrace(UnwindCallback, &state);
    size_t count = state.current - frames;

    if (file != nullptr) {
        fprintf(file, "Backtrace (%zu frames):\n", count);
    }
    for (size_t i = 0; i < count; i++) {
        Dl_info info;
        char line[512];
        if (dladdr(frames[i], &info) != 0 && info.dli_fname != nullptr) {
            uintptr_t offset = reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(info.dli_fbase);
            const char* file_name = strrchr(info.dli_fname, '/');
            file_name = file_name ? file_name + 1 : info.dli_fname;
            if (info.dli_sname != nullptr) {
                uintptr_t symOffset =
                    reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(info.dli_saddr);
                snprintf(line, sizeof(line), "  #%02zu pc %08lx %s (%s+%lu)", i, (unsigned long)offset, file_name,
                         info.dli_sname, (unsigned long)symOffset);
            } else {
                snprintf(line, sizeof(line), "  #%02zu pc %08lx %s", i, (unsigned long)offset, file_name);
            }
        } else {
            snprintf(line, sizeof(line), "  #%02zu pc %p", i, frames[i]);
        }
        __android_log_write(ANDROID_LOG_ERROR, LOG_TAG, line);
        if (file != nullptr) {
            fprintf(file, "%s\n", line);
        }
    }
}

extern "C" void port_android_dump_recent_log(void* filePtr) {
    FILE* file = static_cast<FILE*>(filePtr);
    if (file == nullptr) {
        return;
    }
    // Called from a signal handler: never block on the mutex.
    if (!sRecentLogMutex.try_lock()) {
        fprintf(file, "(recent log unavailable: log mutex busy)\n");
        return;
    }
    fprintf(file, "\nLast %zu log lines before the crash:\n", sRecentLog.size());
    for (const auto& line : sRecentLog) {
        fprintf(file, "%s\n", line.c_str());
    }
    sRecentLogMutex.unlock();
}

// ---------------------------------------------------------------------------
// Crash handler: alternate stack, no allocation, no Java, then the system tombstone
// ---------------------------------------------------------------------------
static void CrashWrite(int fd, const char* text) {
    if (fd >= 0 && text != nullptr) {
        size_t len = strlen(text);
        while (len > 0) {
            ssize_t n = write(fd, text, len);
            if (n <= 0) {
                break;
            }
            text += n;
            len -= (size_t)n;
        }
    }
}

extern "C" void port_android_setup_thread_crash_stack(void) {
    static thread_local void* sAltStack = nullptr;
    if (sAltStack != nullptr) {
        return;
    }
    const size_t size = 256 * 1024;
    sAltStack = malloc(size);
    if (sAltStack == nullptr) {
        return;
    }
    stack_t ss;
    ss.ss_sp = sAltStack;
    ss.ss_size = size;
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);
}

extern "C" void worker_dump_last(void);

static void CrashHandler(int sig, siginfo_t* info, void*) {
    const char* name = (sig == SIGSEGV) ? "SIGSEGV"
                       : (sig == SIGBUS)  ? "SIGBUS"
                       : (sig == SIGABRT) ? "SIGABRT"
                       : (sig == SIGILL)  ? "SIGILL"
                       : (sig == SIGFPE)  ? "SIGFPE"
                       : (sig == SIGTRAP) ? "SIGTRAP"
                                          : "signal";
    char header[512];
    snprintf(header, sizeof(header), "PaperShip Mobile crash: %s (code %d, fault address %p)\n", name,
             info ? info->si_code : 0, info ? info->si_addr : nullptr);
    __android_log_write(ANDROID_LOG_ERROR, LOG_TAG, header);

    int fd = -1;
    if (sCrashLogPath[0] != '\0') {
        fd = open(sCrashLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    CrashWrite(fd, header);

    // Backtrace through the C library's unwinder; dladdr gives function names because
    // the game's functions are exported from libPaperShip.so.
    void* frames[64];
    BacktraceState state = { frames, frames + 64 };
    _Unwind_Backtrace(UnwindCallback, &state);
    size_t count = state.current - frames;
    CrashWrite(fd, "Backtrace:\n");
    for (size_t i = 0; i < count; i++) {
        Dl_info dlinfo;
        char line[512];
        if (dladdr(frames[i], &dlinfo) != 0 && dlinfo.dli_fname != nullptr) {
            uintptr_t offset = reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(dlinfo.dli_fbase);
            const char* file = strrchr(dlinfo.dli_fname, '/');
            file = file ? file + 1 : dlinfo.dli_fname;
            if (dlinfo.dli_sname != nullptr) {
                uintptr_t symOffset =
                    reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(dlinfo.dli_saddr);
                snprintf(line, sizeof(line), "  #%02zu pc %08lx %s (%s+%lu)\n", i, (unsigned long)offset, file,
                         dlinfo.dli_sname, (unsigned long)symOffset);
            } else {
                snprintf(line, sizeof(line), "  #%02zu pc %08lx %s\n", i, (unsigned long)offset, file);
            }
        } else {
            snprintf(line, sizeof(line), "  #%02zu pc %p\n", i, frames[i]);
        }
        __android_log_write(ANDROID_LOG_ERROR, LOG_TAG, line);
        CrashWrite(fd, line);
    }

    // Recent log lines (never block on the mutex from a signal handler).
    if (sRecentLogMutex.try_lock()) {
        CrashWrite(fd, "\nLast log lines before the crash:\n");
        for (const auto& l : sRecentLog) {
            CrashWrite(fd, l.c_str());
            CrashWrite(fd, "\n");
        }
        sRecentLogMutex.unlock();
    }
    if (fd >= 0) {
        close(fd);
    }
    if (sSessionLog != nullptr) {
        fflush(sSessionLog);
    }

    // Hand the signal back to the system so debuggerd writes a tombstone
    // (`adb logcat -s DEBUG`), then die.
    signal(sig, SIG_DFL);
    raise(sig);
    _exit(1);
}

extern "C" void port_android_install_crash_handler(void) {
    port_android_setup_thread_crash_stack();
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = CrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND | SA_NODEFER;
    const int signals[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE, SIGTRAP };
    for (int s : signals) {
        sigaction(s, &sa, nullptr);
    }
}

extern "C" void port_android_mark_clean_shutdown(void) {
    if (sSessionLog != nullptr) {
        fputs("=== PaperShip clean shutdown ===\n", sSessionLog);
        fflush(sSessionLog);
    }
}

extern "C" void port_android_wait_for_setup(void) {
    if (sSetupDone.load()) {
        return;
    }
    __android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Waiting for MainActivity to finish setup (data files + ROM)...");
    while (!sSetupDone.load()) {
        SDL_Delay(50);
    }
    __android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Setup complete, booting the game");
}

static std::shared_ptr<Ship::Gui> GetGui() {
    auto context = Ship::Context::GetInstance();
    if (context == nullptr) {
        return nullptr;
    }
    auto window = context->GetWindow();
    if (window == nullptr) {
        return nullptr;
    }
    return window->GetGui();
}

extern "C" void port_android_pre_frame(void) {
    int requests = sMenuToggleRequests.exchange(0);
    if (requests <= 0 || (requests & 1) == 0) {
        return;
    }
    auto gui = GetGui();
    if (gui == nullptr) {
        return;
    }
    auto menu = gui->GetMenu();
    if (menu != nullptr) {
        menu->ToggleVisibility();
    }
}

extern "C" void port_android_merge_input(void* padsPtr) {
    OSContPad* pads = static_cast<OSContPad*>(padsPtr);
    uint32_t buttons = sTouchButtons.load();
    int stickX = sTouchStickX.load();
    int stickY = sTouchStickY.load();
    if (buttons == 0 && stickX == 0 && stickY == 0) {
        return;
    }
    // While the settings menu is open the overlay is hidden and touches go to ImGui.
    auto gui = GetGui();
    if (gui != nullptr && gui->GetMenuOrMenubarVisible()) {
        return;
    }
    pads[0].button |= (u16)(buttons & 0xFFFF);
    if (stickX != 0 || stickY != 0) {
        pads[0].stick_x = (s8)stickX;
        pads[0].stick_y = (s8)stickY;
    }
}

// ---------------------------------------------------------------------------
// JNI entry points (com.papership.mobile.MainActivity)
// ---------------------------------------------------------------------------
extern "C" {

JNIEXPORT void JNICALL Java_com_papership_mobile_MainActivity_nativeSetDataDir(JNIEnv* env, jclass, jstring dir) {
    if (dir != nullptr) {
        const char* path = env->GetStringUTFChars(dir, nullptr);
        if (path != nullptr) {
            snprintf(sDataDir, sizeof(sDataDir), "%s", path);
            env->ReleaseStringUTFChars(dir, path);
        }
    }
}

JNIEXPORT void JNICALL Java_com_papership_mobile_MainActivity_nativeSetupDone(JNIEnv* env, jclass, jstring romPath) {
    if (romPath != nullptr) {
        const char* path = env->GetStringUTFChars(romPath, nullptr);
        if (path != nullptr) {
            port_set_rom_path(path);
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "ROM path: %s", path);
            env->ReleaseStringUTFChars(romPath, path);
        }
    }
    sSetupDone.store(true);
}

JNIEXPORT void JNICALL Java_com_papership_mobile_MainActivity_nativeSetTouchButton(JNIEnv*, jclass, jint mask,
                                                                                    jboolean down) {
    uint32_t bits = (uint32_t)mask & 0xFFFF;
    if (down) {
        sTouchButtons.fetch_or(bits);
    } else {
        sTouchButtons.fetch_and(~bits);
    }
}

JNIEXPORT void JNICALL Java_com_papership_mobile_MainActivity_nativeSetTouchStick(JNIEnv*, jclass, jfloat x, jfloat y) {
    // x/y are in [-1, 1] with +y pointing up, like the N64 stick.
    auto clamp = [](float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); };
    sTouchStickX.store((int)lroundf(clamp(x) * 80.0f));
    sTouchStickY.store((int)lroundf(clamp(y) * 80.0f));
}

JNIEXPORT void JNICALL Java_com_papership_mobile_MainActivity_nativeToggleMenu(JNIEnv*, jclass) {
    sMenuToggleRequests.fetch_add(1);
}

JNIEXPORT jboolean JNICALL Java_com_papership_mobile_MainActivity_nativeIsMenuVisible(JNIEnv*, jclass) {
    if (!sSetupDone.load()) {
        return JNI_FALSE;
    }
    auto gui = GetGui();
    return (gui != nullptr && gui->GetMenuOrMenubarVisible()) ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"

#endif // __ANDROID__
