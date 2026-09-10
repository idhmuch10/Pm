#include "PaperShipMenu.h"
#include "cvar_prefixes.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <imgui.h>
#include <fast/Fast3dWindow.h>
#include <SDL2/SDL.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <climits>
#include <cstring>
#include "testing_bridge.h"
#if defined(__ANDROID__)
#include "android/AndroidPort.h"
#endif

// CVar names
#define PS_CVAR_INTERNAL_RES       CVAR_SETTING("InternalResolution")
#define PS_CVAR_MSAA               CVAR_SETTING("MSAAValue")
#define PS_CVAR_VSYNC              CVAR_SETTING("VsyncEnabled")
#define PS_CVAR_TEXTURE_FILTER     CVAR_SETTING("TextureFilter")

static const char* sTextureFilterNames[] = { "Nearest", "Linear", "Three-Point" };
static bool sIsFakeFullscreen = false;
static int sSavedWindowX = 100, sSavedWindowY = 100;
static int sSavedWindowW = 640, sSavedWindowH = 480;

static SDL_Window* GetSDLWindow() {
    SDL_Window* wnd = SDL_GetGrabbedWindow();
    if (!wnd) wnd = SDL_GetWindowFromID(1);
    return wnd;
}

// Toggle "fullscreen" by creating a borderless 4:3 window filling the screen height
static void ToggleFullscreen43() {
    SDL_Window* wnd = GetSDLWindow();
    if (!wnd) return;

    if (!sIsFakeFullscreen) {
        // Save current window position and size
        SDL_GetWindowPosition(wnd, &sSavedWindowX, &sSavedWindowY);
        SDL_GetWindowSize(wnd, &sSavedWindowW, &sSavedWindowH);

        // Find the best 4:3 display mode for exclusive fullscreen
        int display = SDL_GetWindowDisplayIndex(wnd);
        if (display < 0) display = 0;

        SDL_DisplayMode bestMode = {};
        int numModes = SDL_GetNumDisplayModes(display);
        int bestArea = 0;

        for (int i = 0; i < numModes; i++) {
            SDL_DisplayMode mode;
            if (SDL_GetDisplayMode(display, i, &mode) == 0) {
                float aspect = (float)mode.w / (float)mode.h;
                // Accept modes close to 4:3 (1.333)
                if (aspect >= 1.3f && aspect <= 1.4f) {
                    int area = mode.w * mode.h;
                    if (area > bestArea) {
                        bestArea = area;
                        bestMode = mode;
                    }
                }
            }
        }

        if (bestArea > 0) {
            // Found a 4:3 mode — use exclusive fullscreen
            SDL_SetWindowDisplayMode(wnd, &bestMode);
            SDL_SetWindowFullscreen(wnd, SDL_WINDOW_FULLSCREEN);
        } else {
            // No 4:3 mode — use borderless window sized to 4:3
            SDL_Rect bounds;
            SDL_GetDisplayBounds(display, &bounds);
            int h = bounds.h;
            int w = (int)(h * (4.0f / 3.0f));
            if (w > bounds.w) { w = bounds.w; h = (int)(w * (3.0f / 4.0f)); }
            SDL_SetWindowBordered(wnd, SDL_FALSE);
            SDL_SetWindowSize(wnd, w, h);
            SDL_SetWindowPosition(wnd, bounds.x + (bounds.w - w) / 2, bounds.y + (bounds.h - h) / 2);
            SDL_RaiseWindow(wnd);
        }
        sIsFakeFullscreen = true;
    } else {
        // Restore windowed mode
        SDL_SetWindowFullscreen(wnd, 0);
        SDL_SetWindowBordered(wnd, SDL_TRUE);
        SDL_SetWindowSize(wnd, sSavedWindowW, sSavedWindowH);
        SDL_SetWindowPosition(wnd, sSavedWindowX, sSavedWindowY);
        sIsFakeFullscreen = false;
    }
}


// ---------------------------------------------------------------------------
// Testing tab: shortcuts to reach a scene again quickly. A true memory snapshot
// is not possible in a decomp port, so this uses the game's own save file
// (any position, not only save blocks) and the script API's map change
// (port/testing_bridge.c).
// ---------------------------------------------------------------------------
static char sTestingStatus[160] = "";
static int sWarpArea = -1;
static int sWarpMap = 0;
static int sWarpEntry = 0;
static int sStoryEdit = 0;
static bool sStoryEditInit = false;

static void DrawTestingTab() {
    ImGui::Spacing();
    ImGui::TextWrapped("Shortcuts for testing. Quick save stores the current position in the active save "
                       "slot (works anywhere, not only at save blocks); quick load re-enters the world from it. "
                       "Warp changes map like a door transition.");
    ImGui::Spacing();

    const bool inWorld = port_testing_in_world() != 0;
    if (inWorld) {
        ImGui::Text("Now: %s entry %d, story progress %d, slot %d", port_testing_current_map(),
                    port_testing_current_entry(), port_testing_story_progress(), port_testing_current_slot());
    } else {
        ImGui::Text("Not in the world right now (mode %d).", port_testing_game_mode());
    }
    ImGui::Spacing();

    ImGui::BeginDisabled(!inWorld);
    if (ImGui::Button("Quick save", ImVec2(-1, 0))) {
        port_testing_quick_save(sTestingStatus, sizeof(sTestingStatus));
    }
    if (ImGui::Button("Quick load", ImVec2(-1, 0))) {
        port_testing_quick_load(sTestingStatus, sizeof(sTestingStatus));
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Diagnostics");
    if (ImGui::Button("Save report (current log) now", ImVec2(-1, 0))) {
        port_testing_request_report(sTestingStatus, sizeof(sTestingStatus));
    }
    if (ImGui::CollapsingHeader("Live status (collision, partner, sprite)", ImGuiTreeNodeFlags_DefaultOpen)) {
        static char sLiveStatus[1024];
        port_testing_status_text(sLiveStatus, sizeof(sLiveStatus));
        ImGui::TextWrapped("%s", sLiveStatus);
    }

    ImGui::Separator();
    ImGui::Text("Presets");
    ImGui::BeginDisabled(!inWorld);
    if (ImGui::Button("Intro: Bowser confrontation (kkj_13)", ImVec2(-1, 0))) {
        port_testing_warp_by_name("kkj_13", 0, port_testing_story_intro(), sTestingStatus, sizeof(sTestingStatus));
    }
    if (ImGui::Button("Goomba Village (kmr_02)", ImVec2(-1, 0))) {
        port_testing_warp_by_name("kmr_02", 0, INT_MIN, sTestingStatus, sizeof(sTestingStatus));
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Warp");
    const int areaCount = port_testing_area_count();
    if (sWarpArea < 0) {
        sWarpArea = inWorld ? port_testing_current_area() : 0;
        sWarpMap = inWorld ? port_testing_current_map_index() : 0;
    }
    if (sWarpArea < 0 || sWarpArea >= areaCount) {
        sWarpArea = 0;
    }
    if (ImGui::BeginCombo("Area", port_testing_area_id(sWarpArea))) {
        for (int a = 0; a < areaCount; a++) {
            if (ImGui::Selectable(port_testing_area_id(a), a == sWarpArea)) {
                sWarpArea = a;
                sWarpMap = 0;
            }
        }
        ImGui::EndCombo();
    }
    const int mapCount = port_testing_map_count(sWarpArea);
    if (sWarpMap < 0 || sWarpMap >= mapCount) {
        sWarpMap = 0;
    }
    if (ImGui::BeginCombo("Map", port_testing_map_id(sWarpArea, sWarpMap))) {
        for (int m = 0; m < mapCount; m++) {
            if (ImGui::Selectable(port_testing_map_id(sWarpArea, m), m == sWarpMap)) {
                sWarpMap = m;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::InputInt("Entry", &sWarpEntry);
    if (sWarpEntry < 0) {
        sWarpEntry = 0;
    }
    ImGui::BeginDisabled(!inWorld);
    if (ImGui::Button("Warp", ImVec2(-1, 0))) {
        port_testing_warp(sWarpArea, sWarpMap, sWarpEntry, sTestingStatus, sizeof(sTestingStatus));
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Story progress (GB_StoryProgress)");
    if (!sStoryEditInit && inWorld) {
        sStoryEdit = port_testing_story_progress();
        sStoryEditInit = true;
    }
    ImGui::InputInt("Value", &sStoryEdit);
    ImGui::BeginDisabled(!inWorld);
    if (ImGui::Button("Apply story progress", ImVec2(-1, 0))) {
        port_testing_set_story_progress(sStoryEdit);
        snprintf(sTestingStatus, sizeof(sTestingStatus), "Story progress set to %d (takes effect on the next map load).",
                 sStoryEdit);
    }
    ImGui::EndDisabled();

    if (sTestingStatus[0] != '\0') {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", sTestingStatus);
    }
}

void PaperShipMenu::DrawElement() {
    auto window = Ship::Context::GetInstance()->GetWindow();

    ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));

    if (!ImGui::Begin("PaperShip Settings", &mIsVisible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    if (ImGui::BeginTabBar("SettingsTabs")) {
        // --- Graphics Tab ---
        if (ImGui::BeginTabItem("Graphics")) {
            ImGui::Spacing();
            ImGui::Text("Display");
            ImGui::Spacing();

            // VSync
            bool vsync = CVarGetInteger(PS_CVAR_VSYNC, 1) != 0;
            if (ImGui::Checkbox("VSync", &vsync)) {
                CVarSetInteger(PS_CVAR_VSYNC, vsync ? 1 : 0);
                CVarSave();
            }

            ImGui::Separator();
            ImGui::Text("Internal Resolution");
            ImGui::Spacing();

            // Resolution multiplier as discrete buttons
            float curRes = CVarGetFloat(PS_CVAR_INTERNAL_RES, 1.0f);
            static const float resOptions[] = { 2.0f, 4.0f, 8.0f };
            static const char* resLabels[] = { "2x", "4x", "8x" };

            for (int i = 0; i < 3; i++) {
                bool selected = (curRes >= resOptions[i] - 0.1f && curRes <= resOptions[i] + 0.1f);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                }
                if (ImGui::Button(resLabels[i], ImVec2(100, 0))) {
                    CVarSetFloat(PS_CVAR_INTERNAL_RES, resOptions[i]);
                    CVarSave();
                    window->SetResolutionMultiplier(resOptions[i]);
                    curRes = resOptions[i];
                }
                if (selected) {
                    ImGui::PopStyleColor();
                }
                if (i < 3) ImGui::SameLine();
            }

            ImGui::Spacing();

            // MSAA
            int msaa = CVarGetInteger(PS_CVAR_MSAA, 1);
            if (ImGui::SliderInt("Anti-Aliasing (MSAA)", &msaa, 1, 8)) {
                CVarSetInteger(PS_CVAR_MSAA, msaa);
                CVarSave();
                window->SetMsaaLevel(msaa);
            }

            // Texture filtering
            int texFilter = CVarGetInteger(PS_CVAR_TEXTURE_FILTER, 0);
            if (ImGui::Combo("Texture Filter", &texFilter, sTextureFilterNames, IM_ARRAYSIZE(sTextureFilterNames))) {
                CVarSetInteger(PS_CVAR_TEXTURE_FILTER, texFilter);
                CVarSave();
                auto fast3d = std::dynamic_pointer_cast<Fast::Fast3dWindow>(
                    Ship::Context::GetInstance()->GetWindow());
                if (fast3d) fast3d->SetTextureFilter((Fast::FilteringMode)texFilter);
            }

            ImGui::Separator();
            ImGui::Text("Window: %ux%u", window->GetWidth(), window->GetHeight());

            ImGui::EndTabItem();
        }

        // --- Audio Tab ---
        if (ImGui::BeginTabItem("Audio")) {
            ImGui::Spacing();

            int masterVol = CVarGetInteger("gSettings.Volume.Master", 100);
            if (ImGui::SliderInt("Master Volume", &masterVol, 0, 100, "%d%%")) {
                CVarSetInteger("gSettings.Volume.Master", masterVol);
                CVarSave();
            }

            ImGui::EndTabItem();
        }

        // --- Controls Tab ---
        if (ImGui::BeginTabItem("Controls")) {
            ImGui::Spacing();

            // Open the built-in Input Editor window
            {
                auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
                auto inputWin = gui->GetGuiWindow("Input Editor");
                bool inputOpen = inputWin && inputWin->IsVisible();
                if (ImGui::Button(inputOpen ? "Close Controller Configuration" : "Open Controller Configuration", ImVec2(-1, 0))) {
                    if (inputWin) inputWin->ToggleVisibility();
                }
            }

            ImGui::Spacing();

            bool controllerNav = CVarGetInteger(CVAR_SETTING("ControlNav"), 0) != 0;
            if (ImGui::Checkbox("Menu Controller Navigation", &controllerNav)) {
                CVarSetInteger(CVAR_SETTING("ControlNav"), controllerNav ? 1 : 0);
                CVarSave();
            }

            ImGui::EndTabItem();
        }

        // --- Testing Tab (PaperShip Mobile) ---
        if (ImGui::BeginTabItem("Testing")) {
            DrawTestingTab();
            ImGui::EndTabItem();
        }

        // --- Debug Tab ---
        if (ImGui::BeginTabItem("Debug")) {
            ImGui::Spacing();

            if (ImGui::Button("Open Stats Window")) {
                auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
                auto statsWin = gui->GetGuiWindow("Stats");
                if (statsWin) statsWin->ToggleVisibility();
            }

            if (ImGui::Button("Open GFX Debugger")) {
                auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
                auto gfxWin = gui->GetGuiWindow("GfxDebuggerWindow");
                if (gfxWin) gfxWin->ToggleVisibility();
            }

            if (ImGui::Button("Open Console")) {
                auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
                auto conWin = gui->GetGuiWindow("Console");
                if (conWin) conWin->ToggleVisibility();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Restart Game", ImVec2(-1, 0))) {
        // Re-launch the executable and exit current process
        SDL_Window* wnd = GetSDLWindow();
        if (wnd) SDL_SetWindowFullscreen(wnd, 0);
#if defined(__APPLE__)
        char path[1024];
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) == 0) {
            execl(path, path, nullptr);
        }
#elif defined(__linux__) && !defined(__ANDROID__)
        execl("/proc/self/exe", "/proc/self/exe", nullptr);
#endif
#if defined(__ANDROID__)
        // Android: the process exits (SDLActivity finishes); mark the session clean so the
        // next launch does not treat the restart as a crash.
        port_android_mark_clean_shutdown();
#endif
        exit(0);
    }
    if (ImGui::Button("Exit Game", ImVec2(-1, 0))) {
#if defined(__ANDROID__)
        port_android_mark_clean_shutdown();
#endif
        exit(0);
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// --- Setup ---

void PaperShipGui::SetupMenu() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    auto menu = std::make_shared<PaperShipMenu>(CVAR_WINDOW("Menu"), "PaperShip Settings");
    gui->SetMenu(menu);
    menu->Hide();

    // Force N64 4:3 display mode — the GUI draws the game framebuffer at 4:3
    // with pillarboxing in widescreen windows
    CVarSetInteger(CVAR_SETTING("LowResMode"), 1);

    // Apply saved internal resolution on startup (minimum 2x to avoid 1x rendering artifacts)
    float savedRes = CVarGetFloat(PS_CVAR_INTERNAL_RES, 2.0f);
    if (savedRes < 2.0f) savedRes = 2.0f;
    Ship::Context::GetInstance()->GetWindow()->SetResolutionMultiplier(savedRes);

}
