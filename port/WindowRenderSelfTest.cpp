/**
 * WindowRenderSelfTest.cpp - Draw a game window and check the pixels that come out.
 *
 * A window drawn by draw_box() is the most demanding thing the renderer does: two
 * tiles at once, two-cycle combining, and a fill whose opacity comes from the clamped
 * edge of the corner texture rather than from any colour the game sets. PaperShip is
 * developed against Metal, so the GL/GLES path had never had to get that right, and a
 * phone reported message boxes and the pause menu as see-through.
 *
 * The test draws a window with textures it builds itself over a solid background and
 * reads the frame back, both the way a message box on screen is drawn and the way one
 * that is still opening is, so a window that comes out transparent, unpainted or
 * misplaced fails in CI with neither a ROM nor a phone. The game runs it once at
 * startup too: a device that draws it wrong says so in its next report, which
 * separates the window textures arriving wrong from the renderer using them wrong.
 */
#include <libultraship.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Engine.h"
#include <fast/Fast3dWindow.h>
#include "fast/interpreter.h"
#include "fast/backends/gfx_rendering_api.h"

#if defined(USE_OPENGLES)
#include <GLES3/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengl.h>
#endif

extern "C" {
#include "common.h"
}

// The window box textures, normally filled from the ROM by port_load_ui_textures().
extern "C" u8 ui_box_bg_flat_png[];  // I4 16x1
extern "C" u8 ui_box_corners1_png[]; // four 16x16 IA8 corners, in TL TR BL BR order

// The window styles the game itself draws with (src/draw_box.c).
extern "C" WindowCorners gBoxCorners[];

namespace {

// WINDOW_STYLE_0 (message boxes, the item popup) draws gBoxBackground[1] tiled under
// gBoxCorners[0], with PM_CC_BOX1_OPAQUE / PM_CC_BOX1_CYC2.
constexpr s32 BOX_X = 40;
constexpr s32 BOX_Y = 40;
constexpr s32 BOX_W = 240;
constexpr s32 BOX_H = 120;

constexpr s32 CORNER_SIZE = 16;

// Solid background the window is drawn over. Green is 0 so a pixel's green channel
// reads back as the window's opacity: the box paints white, so green == 255 means
// fully opaque and green == 0 means the box never covered the background at all.
constexpr u8 BG_R = 255, BG_G = 0, BG_B = 255;

// A rounded corner in IA8, built so that the pixels it produces say which texel was
// sampled. Alpha is what the window's fill opacity is taken from, and the tile is only
// as big as the corner, so every part of the window away from a corner is whatever the
// tile's clamp gives back past its edge:
//
//   - outside the quarter circle:      alpha 0    (the rounded corner is cut away)
//   - the tile's inner corner texel:   alpha 15   (stretched over the window's middle)
//   - everywhere else inside:          alpha 8    (the edges, and what a tile that
//                                                  repeated instead of clamping shows)
//
// So the window's middle coming out fully opaque means both axes clamped, and half
// opaque means the tile repeated.
void build_corner_texture(void) {
    for (s32 corner = 0; corner < 4; corner++) {
        // Which way round the tile is: the rounded cut is on the box's outside.
        s32 flipX = (corner & 1) != 0;
        s32 flipY = (corner & 2) != 0;
        for (s32 y = 0; y < CORNER_SIZE; y++) {
            for (s32 x = 0; x < CORNER_SIZE; x++) {
                // Measured from the tile's inner corner, the one the clamp stretches.
                s32 dx = flipX ? x : (CORNER_SIZE - 1 - x);
                s32 dy = flipY ? y : (CORNER_SIZE - 1 - y);
                u8 alpha;
                if (dx * dx + dy * dy > (CORNER_SIZE - 1) * (CORNER_SIZE - 1)) {
                    alpha = 0;
                } else if (dx == 0 && dy == 0) {
                    alpha = 15;
                } else {
                    alpha = 8;
                }
                ui_box_corners1_png[corner * CORNER_SIZE * CORNER_SIZE + y * CORNER_SIZE + x] =
                    (u8)((15 << 4) | alpha);
            }
        }
    }
}

// What the game's own corner textures say a window's fill should look like: the window
// is opaque wherever the corner tile's inner texel is, because the tile's clamp is what
// covers everything between the corners. Printed before the test overwrites the
// texture, so a report from a device says whether the data arrived intact.
void report_game_corner_alpha(const char* name, const WindowCorners& corners) {
    if (corners.imgData == nullptr || corners.bitDepth != G_IM_SIZ_8b || corners.fmt != G_IM_FMT_IA) {
        return; // only IA8 corners keep their opacity in the alpha channel
    }
    const Vec2bu* sizes = (const Vec2bu*)&corners.size1;
    const u8* image = (const u8*)corners.imgData;
    s32 alpha[4];
    for (s32 corner = 0; corner < 4; corner++) {
        s32 width = sizes[corner].x;
        s32 height = sizes[corner].y;
        // The texel on the inside of the box, the one the clamp stretches inwards.
        s32 x = (corner & 1) ? 0 : width - 1;
        s32 y = (corner & 2) ? 0 : height - 1;
        alpha[corner] = image[y * width + x] & 0xF;
        image += width * height;
    }
    fprintf(stderr, "[selftest] window render: %s fill alpha is %d/%d/%d/%d of 15\n", name, alpha[0], alpha[1],
            alpha[2], alpha[3]);
}

struct Frame {
    s32 width;
    s32 height;
    u8* pixels; // RGBA8, row 0 at the top
};

// The rendered game image, read back from the framebuffer the interpreter drew it to.
bool capture_frame(Frame* out) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    if (wnd == nullptr) {
        fprintf(stderr, "[selftest] window render: no Fast3D window\n");
        return false;
    }
    auto interpreter = wnd->GetInterpreterWeak().lock();
    if (interpreter == nullptr) {
        fprintf(stderr, "[selftest] window render: no interpreter\n");
        return false;
    }

    GLuint texture = (GLuint)(uintptr_t)interpreter->mRapi->GetFramebufferTextureId(interpreter->mGameFb);
    if (texture == 0) {
        fprintf(stderr, "[selftest] window render: the game renders straight to the window, cannot read it back\n");
        return false;
    }

    s32 width = (s32)interpreter->mCurDimensions.width;
    s32 height = (s32)interpreter->mCurDimensions.height;
    // The geometry the frame was rendered with. A device that draws the game at the
    // wrong size, or into a viewport that does not match the framebuffer it renders to,
    // shows it here: the game's own 320x240 has to end up as all of that framebuffer.
    static bool reported = false;
    if (!reported) {
        reported = true;
        fprintf(stderr,
                "[selftest] window render: window %ux%u, game %dx%d (x%.2f), native %ux%u, viewport %dx%d at %d,%d, "
                "to framebuffer %d\n",
                interpreter->mGfxCurrentWindowDimensions.width, interpreter->mGfxCurrentWindowDimensions.height, width,
                height, interpreter->mCurDimensions.internal_mul, interpreter->mNativeDimensions.width,
                interpreter->mNativeDimensions.height, (s32)interpreter->mGameWindowViewport.width,
                (s32)interpreter->mGameWindowViewport.height, (s32)interpreter->mGameWindowViewport.x,
                (s32)interpreter->mGameWindowViewport.y, (s32)interpreter->mRendersToFb);
    }
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "[selftest] window render: framebuffer is %dx%d\n", width, height);
        return false;
    }

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[selftest] window render: framebuffer incomplete (0x%X)\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        return false;
    }

    u8* pixels = (u8*)malloc((size_t)width * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "[selftest] window render: glReadPixels failed (0x%X)\n", error);
        free(pixels);
        return false;
    }

    // The game framebuffer is drawn bottom-up, the same way glReadPixels reads it, so
    // row 0 is already the top of the picture.
    out->width = width;
    out->height = height;
    out->pixels = pixels;
    return true;
}

// Sample the frame at a point given in the game's own 320x240 screen coordinates.
void sample(const Frame& frame, s32 screenX, s32 screenY, u8 rgba[4]) {
    s32 x = screenX * frame.width / SCREEN_WIDTH;
    s32 y = screenY * frame.height / SCREEN_HEIGHT;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x >= frame.width) {
        x = frame.width - 1;
    }
    if (y >= frame.height) {
        y = frame.height - 1;
    }
    memcpy(rgba, frame.pixels + ((size_t)y * frame.width + x) * 4, 4);
}

// PAPERSHIP_SELFTEST_DUMP=<path>: write the frame as a binary PPM to look at. The case
// number goes before the extension, since there is more than one.
void dump_frame(const Frame& frame, const char* path, s32 caseIndex) {
    char name[512];
    const char* dot = strrchr(path, '.');
    if (dot != nullptr) {
        snprintf(name, sizeof(name), "%.*s%d%s", (int)(dot - path), path, caseIndex, dot);
    } else {
        snprintf(name, sizeof(name), "%s%d", path, caseIndex);
    }
    FILE* file = fopen(name, "wb");
    if (file == nullptr) {
        fprintf(stderr, "[selftest] window render: cannot write %s\n", name);
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", frame.width, frame.height);
    for (s32 i = 0; i < frame.width * frame.height; i++) {
        fwrite(frame.pixels + (size_t)i * 4, 1, 3, file);
    }
    fclose(file);
    fprintf(stderr, "[selftest] window render: wrote %s (%dx%d)\n", name, frame.width, frame.height);
}

s32 abs_diff(s32 a, s32 b) {
    return a > b ? a - b : b - a;
}

bool is_background(const u8 rgba[4]) {
    return rgba[0] > 200 && rgba[1] < 40 && rgba[2] > 200;
}

// The window's extent in the game's screen coordinates, as the rectangle of everything
// the window painted over the background.
bool measure_window(const Frame& frame, s32* left, s32* top, s32* right, s32* bottom) {
    s32 minX = frame.width, minY = frame.height, maxX = -1, maxY = -1;
    for (s32 y = 0; y < frame.height; y++) {
        const u8* row = frame.pixels + (size_t)y * frame.width * 4;
        for (s32 x = 0; x < frame.width; x++) {
            if (!is_background(row + (size_t)x * 4)) {
                if (x < minX) {
                    minX = x;
                }
                if (x > maxX) {
                    maxX = x;
                }
                if (y < minY) {
                    minY = y;
                }
                if (y > maxY) {
                    maxY = y;
                }
            }
        }
    }
    if (maxX < 0) {
        return false;
    }
    *left = minX * SCREEN_WIDTH / frame.width;
    *right = maxX * SCREEN_WIDTH / frame.width;
    *top = minY * SCREEN_HEIGHT / frame.height;
    *bottom = maxY * SCREEN_HEIGHT / frame.height;
    return true;
}

s32 check(const char* label, const char* what, bool ok, const char* detail) {
    fprintf(stderr, "[selftest] window render: %s %-26s %s%s%s\n", label, what, ok ? "ok" : "FAILED",
            detail ? " - " : "", detail ? detail : "");
    return ok ? 0 : 1;
}

// One window, drawn and inspected. `flags` is what draw_box() is given: a message box
// already on screen is drawn as rectangles, one that is still opening goes through the
// rotate/scale path, which sends the same window through the 3D pipeline instead.
s32 run_case(const char* label, s32 flags, bool wrapFirst, s32 caseIndex) {
    Gfx* pos = gDisplayContext->mainGfx;
    gMatrixListPos = 0;

    if (wrapFirst) {
        // Draw the corner texture wrapped before the window clamps it. The renderer
        // keeps one uploaded copy per texture and only changes its wrap mode when it
        // notices the mode changed, so a window drawn after something that wrapped the
        // same texture is the case where that bookkeeping has to be right. In the game
        // there is always a frame's worth of drawing before a window.
        gDPPipeSync(pos++);
        gDPSetCycleType(pos++, G_CYC_1CYCLE);
        gDPSetTexturePersp(pos++, G_TP_NONE);
        gDPSetScissor(pos++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        gDPSetRenderMode(pos++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
        gDPSetCombineMode(pos++, G_CC_MODULATEIA, G_CC_MODULATEIA);
        gDPSetPrimColor(pos++, 0, 0, 255, 255, 255, 255);
        gSPTexture(pos++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPLoadTextureTile(pos++, ui_box_corners1_png, G_IM_FMT_IA, G_IM_SIZ_8b, CORNER_SIZE, CORNER_SIZE, 0, 0,
                           CORNER_SIZE - 1, CORNER_SIZE - 1, 0, G_TX_WRAP, G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
        gSPScisTextureRectangle(pos++, 0, 0, SCREEN_WIDTH * 4, SCREEN_HEIGHT * 4, G_TX_RENDERTILE, 0, 0, 0x400,
                                0x400);
        gDPPipeSync(pos++);
    }

    // A solid background to draw the window over. A fill rectangle would be simpler,
    // but the interpreter skips those until the game has set a colour image, which only
    // happens once the game itself boots; a primitive-coloured rectangle needs no state.
    gDPPipeSync(pos++);
    gDPSetCycleType(pos++, G_CYC_1CYCLE);
    gDPSetTexturePersp(pos++, G_TP_NONE);
    gDPSetScissor(pos++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPSetRenderMode(pos++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    // draw_box() inherits these, and the game sets them before drawing a window. Alpha
    // dithering in particular turns a partly transparent window into a coin flip per
    // pixel, which would make the opacity readings meaningless.
    gDPSetAlphaCompare(pos++, G_AC_NONE);
    gDPSetAlphaDither(pos++, G_AD_DISABLE);
    gDPSetColorDither(pos++, G_CD_DISABLE);
    gDPSetCombineMode(pos++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(pos++, 0, 0, BG_R, BG_G, BG_B, 255);
    gSPScisTextureRectangle(pos++, 0, 0, SCREEN_WIDTH * 4, SCREEN_HEIGHT * 4, G_TX_RENDERTILE, 0, 0, 0x400, 0x400);
    gDPPipeSync(pos++);

    WindowStyle style;
    style.defaultStyleID = WINDOW_STYLE_0;
    gMainGfxPos = pos;
    draw_box(flags, style, BOX_X, BOX_Y, 0, BOX_W, BOX_H, 255, 0, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, nullptr, nullptr,
             nullptr, SCREEN_WIDTH, SCREEN_HEIGHT, nullptr);
    pos = gMainGfxPos;
    if (pos >= gDisplayContext->mainGfx + ARRAY_COUNT(gDisplayContext->mainGfx)) {
        fprintf(stderr, "[selftest] window render: display list overflow\n");
        abort();
    }
    gSPEndDisplayList(pos++);

    GameEngine::ProcessGfxCommands(gDisplayContext->mainGfx);

    Frame frame = {};
    if (!capture_frame(&frame)) {
        // Not a rendering failure: nothing could be read back to judge.
        return 0;
    }

    const char* dump = getenv("PAPERSHIP_SELFTEST_DUMP");
    if (dump != nullptr && dump[0] != '\0') {
        dump_frame(frame, dump, caseIndex);
    }

    char detail[224];
    s32 failures = 0;

    u8 screenCorner[4][4];
    sample(frame, 2, 2, screenCorner[0]);
    sample(frame, SCREEN_WIDTH - 3, 2, screenCorner[1]);
    sample(frame, 2, SCREEN_HEIGHT - 3, screenCorner[2]);
    sample(frame, SCREEN_WIDTH - 3, SCREEN_HEIGHT - 3, screenCorner[3]);
    bool background = is_background(screenCorner[0]) && is_background(screenCorner[1]) &&
                      is_background(screenCorner[2]) && is_background(screenCorner[3]);
    snprintf(detail, sizeof(detail), "the screen's corners are (%d,%d,%d) (%d,%d,%d) (%d,%d,%d) (%d,%d,%d)",
             screenCorner[0][0], screenCorner[0][1], screenCorner[0][2], screenCorner[1][0], screenCorner[1][1],
             screenCorner[1][2], screenCorner[2][0], screenCorner[2][1], screenCorner[2][2], screenCorner[3][0],
             screenCorner[3][1], screenCorner[3][2]);
    failures += check(label, "background painted", background, detail);

    s32 left, top, right, bottom;
    if (!measure_window(frame, &left, &top, &right, &bottom)) {
        failures += check(label, "window drawn", false, "nothing was drawn over the background");
        free(frame.pixels);
        return failures;
    }

    snprintf(detail, sizeof(detail), "%d,%d to %d,%d, asked for %d,%d to %d,%d", left, top, right, bottom, BOX_X,
             BOX_Y, BOX_X + BOX_W - 1, BOX_Y + BOX_H - 1);
    failures += check(label, "window is where it was put",
                      abs_diff(left, BOX_X) <= 2 && abs_diff(top, BOX_Y) <= 2 &&
                          abs_diff(right, BOX_X + BOX_W - 1) <= 2 && abs_diff(bottom, BOX_Y + BOX_H - 1) <= 2,
                      detail);

    u8 middle[4], insideTop[4], insideLeft[4], boxCorner[4];
    sample(frame, BOX_X + BOX_W / 2, BOX_Y + BOX_H / 2, middle);
    sample(frame, BOX_X + BOX_W / 4, BOX_Y + 4, insideTop);
    sample(frame, BOX_X + 4, BOX_Y + BOX_H / 4, insideLeft);
    sample(frame, BOX_X + 1, BOX_Y + 1, boxCorner);

    // The middle of the window is nowhere near a corner tile, so every one of its pixels
    // is the tile's inner texel held by the clamp on both axes. That texel is the only
    // opaque one: a see-through window means the clamp did not happen.
    snprintf(detail, sizeof(detail), "the middle is (%d,%d,%d), %d%% opaque", middle[0], middle[1], middle[2],
             middle[1] * 100 / 255);
    failures += check(label, "window fill is opaque", middle[1] > 245, detail);

    // Along an edge only one axis is past the tile, which lands on a half-opaque texel.
    // Fully opaque or fully transparent there means the edge sampled the wrong texel.
    snprintf(detail, sizeof(detail), "under the top edge is (%d,%d,%d)", insideTop[0], insideTop[1], insideTop[2]);
    failures += check(label, "top edge clamps sideways", insideTop[1] > 90 && insideTop[1] < 190, detail);

    snprintf(detail, sizeof(detail), "inside the left edge is (%d,%d,%d)", insideLeft[0], insideLeft[1],
             insideLeft[2]);
    failures += check(label, "left edge clamps downwards", insideLeft[1] > 90 && insideLeft[1] < 190, detail);

    // The corner is rounded, so the pixel in the very corner of the window's rectangle
    // is outside the window and must be left alone.
    snprintf(detail, sizeof(detail), "the window's corner pixel is (%d,%d,%d)", boxCorner[0], boxCorner[1],
             boxCorner[2]);
    failures += check(label, "corner is rounded", is_background(boxCorner), detail);

    free(frame.pixels);
    return failures;
}

} // namespace

extern "C" int port_window_selftest_run(void) {
    report_game_corner_alpha("message box corners", gBoxCorners[0]);
    report_game_corner_alpha("pause menu corners", gBoxCorners[3]);

    // The test needs textures whose contents it knows, and the game is about to use the
    // real ones, so borrow the arrays and put them back afterwards.
    u8 savedBackground[8];
    u8 savedCorners[4 * CORNER_SIZE * CORNER_SIZE];
    memcpy(savedBackground, ui_box_bg_flat_png, sizeof(savedBackground));
    memcpy(savedCorners, ui_box_corners1_png, sizeof(savedCorners));
    memset(ui_box_bg_flat_png, 0xFF, sizeof(savedBackground)); // I4 16x1, every texel white
    build_corner_texture();

    // draw_box() builds its display list at gMainGfxPos and the rotate/scale path also
    // pushes matrices, so give it the buffers a frame of the game itself would have.
    DisplayContext* savedContext = gDisplayContext;
    Gfx* savedGfxPos = gMainGfxPos;
    u16 savedMatrixPos = gMatrixListPos;
    gDisplayContext = &D_80164000[0];

    s32 failures = 0;
    failures += run_case("on screen:  ", 0, false, 0);
    failures += run_case("opening:    ", DRAW_FLAG_ROTSCALE, false, 1);
    failures += run_case("after wrap: ", 0, true, 2);

    gDisplayContext = savedContext;
    gMainGfxPos = savedGfxPos;
    gMatrixListPos = savedMatrixPos;
    memcpy(ui_box_bg_flat_png, savedBackground, sizeof(savedBackground));
    memcpy(ui_box_corners1_png, savedCorners, sizeof(savedCorners));
    gfx_texture_cache_clear(); // the borrowed textures may still be uploaded

    fprintf(stderr, "[selftest] window render: %d check(s) failed\n", failures);
    return failures;
}
