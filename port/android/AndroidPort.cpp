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
#include <pthread.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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

// ---------------------------------------------------------------------------
// stdout/stderr -> logcat
// ---------------------------------------------------------------------------
static int sLogPipe[2] = { -1, -1 };

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
                line.clear();
            } else {
                line.push_back(buf[i]);
                if (line.size() >= 4000) { // logcat truncates very long lines
                    __android_log_write(ANDROID_LOG_INFO, LOG_TAG, line.c_str());
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
