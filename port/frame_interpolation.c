/**
 * frame_interpolation.c - draw the game's 30 fps at a higher rate.
 *
 * Paper Mario's logic is locked to 30 fps and everything about it -- animation
 * timers, physics, script waits -- counts frames, so the only way to a smoother
 * picture is to draw the frames in between rather than to run more of them.
 *
 * The renderer already supports this: Interpreter::Run() takes a set of matrices to
 * substitute for the ones the display list names, so the same display list can be
 * drawn again part-way between where things were last frame and where they are now.
 * What was missing is the matrices, which is what this works out.
 *
 * Everything that moves on screen is positioned by a matrix the game writes into the
 * frame's display context, and the game alternates between two of those, so last
 * frame's value of a matrix is the same slot in the other one. Pairing them by slot
 * is only right while the frame draws the same things in the same order: as soon as
 * the number of things changes, a slot holds something else and interpolating would
 * stretch one object across the screen towards another. Pairs too far apart to be the
 * same object a thirtieth of a second later are left alone, which just draws them
 * where they are -- what a 30 fps frame would have shown anyway.
 *
 * Written in C because the decomp headers define script macros (Set, Call, End) that
 * collide with C++ standard headers; Engine.cpp turns the result into the map the
 * renderer wants.
 */
#include "common.h"
#include "frame_interpolation.h"
#include <stdio.h>

extern DisplayContext D_80164000[2];
extern s32 gCurrentDisplayContextIndex;

// How far a matrix may move in one 30 fps frame and still be taken for the same object.
// Modelview matrices here are in camera space, where a fast object covers a few tens of
// units per frame and a camera cut covers everything at once.
#define MAX_TRANSLATION_STEP 300.0f
// The rotation and scale part is near 1 at its largest, so half of that is already a
// different orientation -- a sprite turning to face the other way, say.
#define MAX_LINEAR_STEP 0.5f

static s32 sPrevContextIndex = -1;
static s32 sPrevMatrixCount = 0;
static s32 sInterpolatedThisFrame = 0;
static s32 sLastInterpolated = 0;

static f32 port_absf(f32 value) {
    return value < 0.0f ? -value : value;
}

// Whether two matrices are close enough to be the same object one frame apart.
static s32 could_be_the_same_object(const f32 (*prev)[4], const f32 (*cur)[4]) {
    s32 row, col;
    f32 dx = cur[3][0] - prev[3][0];
    f32 dy = cur[3][1] - prev[3][1];
    f32 dz = cur[3][2] - prev[3][2];

    if (dx * dx + dy * dy + dz * dz > MAX_TRANSLATION_STEP * MAX_TRANSLATION_STEP) {
        return FALSE;
    }
    for (row = 0; row < 3; row++) {
        for (col = 0; col < 4; col++) {
            if (port_absf(cur[row][col] - prev[row][col]) > MAX_LINEAR_STEP) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

static int add_lerped(PortMtxLerp* out, int count, int maxOut, Mtx* addr, const Mtx* prev, const Mtx* cur,
                      f32 step) {
    s32 row, col;

    if (count >= maxOut) {
        return count;
    }
    if (!could_be_the_same_object(prev->mf, cur->mf)) {
        return count;
    }
    out[count].addr = addr;
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            f32 a = prev->mf[row][col];
            f32 b = cur->mf[row][col];
            out[count].m[row][col] = a + (b - a) * step;
        }
    }
    return count + 1;
}

int port_frame_interpolation_build(float step, PortMtxLerp* out, int maxOut) {
    DisplayContext* cur;
    DisplayContext* prev;
    s32 count = 0;
    s32 shared;
    s32 i;

    // The previous frame has to be the other display context, and has to have drawn.
    // After a skipped frame or a fresh boot there is nothing to interpolate from.
    if (sPrevContextIndex != (gCurrentDisplayContextIndex ^ 1) || sPrevMatrixCount == 0) {
        return 0;
    }

    cur = &D_80164000[gCurrentDisplayContextIndex];
    prev = &D_80164000[gCurrentDisplayContextIndex ^ 1];

    shared = gMatrixListPos < sPrevMatrixCount ? gMatrixListPos : sPrevMatrixCount;
    for (i = 0; i < shared; i++) {
        count = add_lerped(out, count, maxOut, &cur->matrixStack[i], &prev->matrixStack[i], &cur->matrixStack[i],
                           step);
    }

    // The cameras' projections, which change when one zooms rather than when it moves.
    for (i = 0; i < (s32)ARRAY_COUNT(cur->camPerspMatrix); i++) {
        count = add_lerped(out, count, maxOut, &cur->camPerspMatrix[i], &prev->camPerspMatrix[i],
                           &cur->camPerspMatrix[i], step);
    }

    sInterpolatedThisFrame = count;
    return count;
}

int port_frame_interpolation_last_count(void) {
    return sLastInterpolated;
}

void port_frame_interpolation_record(void) {
    // A frame that interpolated nothing reports nothing, so the count says what the
    // frame just drawn actually did rather than what some earlier one did.
    sLastInterpolated = sInterpolatedThisFrame;
    sInterpolatedThisFrame = 0;
    sPrevContextIndex = gCurrentDisplayContextIndex;
    sPrevMatrixCount = gMatrixListPos;
}

int port_frame_interpolation_max(void) {
    DisplayContext* context = &D_80164000[0];

    return (s32)ARRAY_COUNT(context->matrixStack) + (s32)ARRAY_COUNT(context->camPerspMatrix);
}

// ---------------------------------------------------------------------------
// Self-test (part of PAPERSHIP_SELFTEST, and needs no ROM)
//
// What this has to get right is which pairs of matrices are the same object a frame
// apart. Getting that wrong is not a crash, it is one object stretched across the
// screen for half a frame, which is hard to catch by looking. So: two frames built by
// hand, with one matrix that moved a little, one that jumped, one that turned around
// and one that did not move at all.
// ---------------------------------------------------------------------------

static void set_test_matrix(Mtx* m, f32 x, f32 y, f32 z, f32 facing) {
    s32 row, col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            m->mf[row][col] = row == col ? 1.0f : 0.0f;
        }
    }
    m->mf[0][0] = facing; // -1 is the same sprite turned to face the other way
    m->mf[3][0] = x;
    m->mf[3][1] = y;
    m->mf[3][2] = z;
}

static s32 find_lerped(const PortMtxLerp* lerped, s32 count, const Mtx* addr) {
    s32 i;

    for (i = 0; i < count; i++) {
        if (lerped[i].addr == (const void*)addr) {
            return i;
        }
    }
    return -1;
}

// `detail` says what went wrong, so it is only worth printing when something did.
static s32 check_interp(const char* what, s32 ok, const char* detail) {
    fprintf(stderr, "[selftest] interpolation: %-38s %s%s%s\n", what, ok ? "ok" : "FAILED", ok ? "" : " - ",
            ok ? "" : detail);
    return ok ? 0 : 1;
}

int port_frame_interpolation_selftest(void) {
    PortMtxLerp lerped[8];
    DisplayContext* first = &D_80164000[0];
    DisplayContext* second = &D_80164000[1];
    s32 savedIndex = gCurrentDisplayContextIndex;
    u16 savedMatrixPos = gMatrixListPos;
    s32 savedPrevIndex = sPrevContextIndex;
    s32 savedPrevCount = sPrevMatrixCount;
    s32 failures = 0;
    s32 count;
    s32 slot;
    char detail[128];

    // Nothing to interpolate from before a frame has been drawn.
    sPrevContextIndex = -1;
    sPrevMatrixCount = 0;
    gCurrentDisplayContextIndex = 0;
    gMatrixListPos = 4;
    count = port_frame_interpolation_build(0.5f, lerped, (s32)ARRAY_COUNT(lerped));
    snprintf(detail, sizeof(detail), "produced %d matrices", count);
    failures += check_interp("first frame interpolates nothing", count == 0, detail);

    // A frame, then the next one a thirtieth of a second later.
    set_test_matrix(&first->matrixStack[0], 0.0f, 0.0f, 0.0f, 1.0f);    // walks
    set_test_matrix(&first->matrixStack[1], 0.0f, 0.0f, 0.0f, 1.0f);    // replaced by something else
    set_test_matrix(&first->matrixStack[2], 50.0f, 0.0f, 0.0f, 1.0f);   // turns around
    set_test_matrix(&first->matrixStack[3], 10.0f, 20.0f, 30.0f, 1.0f); // stands still
    port_frame_interpolation_record();

    gCurrentDisplayContextIndex = 1;
    gMatrixListPos = 4;
    set_test_matrix(&second->matrixStack[0], 20.0f, 0.0f, 0.0f, 1.0f);
    set_test_matrix(&second->matrixStack[1], 900.0f, 0.0f, 0.0f, 1.0f);
    set_test_matrix(&second->matrixStack[2], 50.0f, 0.0f, 0.0f, -1.0f);
    set_test_matrix(&second->matrixStack[3], 10.0f, 20.0f, 30.0f, 1.0f);
    count = port_frame_interpolation_build(0.5f, lerped, (s32)ARRAY_COUNT(lerped));

    slot = find_lerped(lerped, count, &second->matrixStack[0]);
    if (slot < 0) {
        failures += check_interp("something that walked is drawn between", FALSE, "it was left where it was");
    } else {
        f32 x = lerped[slot].m[3][0];
        snprintf(detail, sizeof(detail), "halfway from 0 to 20 is %.1f", x);
        failures += check_interp("something that walked is drawn between", x > 9.9f && x < 10.1f, detail);
    }

    slot = find_lerped(lerped, count, &second->matrixStack[1]);
    failures += check_interp("a slot that jumped 900 units is left", slot < 0, "it was drawn moving there");

    slot = find_lerped(lerped, count, &second->matrixStack[2]);
    failures += check_interp("something facing the other way is left", slot < 0, "it was drawn turning inside out");

    slot = find_lerped(lerped, count, &second->matrixStack[3]);
    failures += check_interp("something that did not move is kept", slot >= 0, "it was dropped");

    gCurrentDisplayContextIndex = savedIndex;
    gMatrixListPos = savedMatrixPos;
    sPrevContextIndex = savedPrevIndex;
    sPrevMatrixCount = savedPrevCount;
    fprintf(stderr, "[selftest] interpolation: %d check(s) failed\n", failures);
    return failures;
}
