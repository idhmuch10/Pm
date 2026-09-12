/**
 * ShaderSelfTest.cpp - Compile every shader variant the game can request.
 *
 * Run the port with PAPERSHIP_SHADER_SELFTEST=1: instead of booting the game, a
 * triangle is drawn with every gDPSetCombineMode() pair found in the sources
 * (port/shader_selftest_cases.inc) under a matrix of render modes, alpha-compare
 * modes, fog and cycle types. libultraship compiles a GLSL (or GLSL ES on
 * Android/GLES builds) program for each distinct variant and logs failures, so
 * this catches shaders a stricter GPU compiler rejects without needing a ROM.
 * The process exits with the number of failed programs.
 */
#include <libultraship.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Engine.h"

extern "C" {
#include "common.h"
}

namespace Fast {
extern int gGfxOglShaderFailures;  // gfx_opengl.cpp
extern int gGfxOglShaderCreations; // gfx_opengl.cpp
}
using Fast::gGfxOglShaderCreations;
using Fast::gGfxOglShaderFailures;

namespace {

// A tiny RGBA16 texture (content irrelevant; the interpreter only needs valid memory).
u16 sTexture[32 * 32];

Vtx sVerts[3] = {
    { { { -50, -50, -200 }, 0, { 0, 0 }, { 255, 0, 0, 255 } } },
    { { { 50, -50, -200 }, 0, { 31 << 5, 0 }, { 0, 255, 0, 255 } } },
    { { { 0, 50, -200 }, 0, { 0, 31 << 5 }, { 0, 0, 255, 128 } } },
};

// GBI_FLOATS: the interpreter memcpy()s a Mtx as float[4][4]. A scale-only matrix
// is symmetric, so the row/column convention does not matter here.
float sProjMtx[4][4] = { { 0.01f, 0, 0, 0 }, { 0, 0.01f, 0, 0 }, { 0, 0, 0.001f, 0 }, { 0, 0, 0, 1 } };
float sIdentityMtx[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
Vp sViewport = { { { 160 * 4, 120 * 4, 511, 0 }, { 160 * 4, 120 * 4, 511, 0 } } };

Gfx sLoadTextures[] = {
    gsDPLoadTextureBlock(sTexture, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, G_TX_WRAP, G_TX_WRAP, 5, 5, G_TX_NOLOD,
                         G_TX_NOLOD),
    gsDPLoadMultiBlock(sTexture, 256, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, G_TX_WRAP, G_TX_WRAP, 5, 5,
                       G_TX_NOLOD, G_TX_NOLOD),
    gsSPEndDisplayList(),
};

struct CombineCase {
    const char* name;
    Gfx cmd;
};

// gsDPSetCombineLERP takes the 16 already-expanded combiner arguments (a and b are
// expanded before substitution, so the 2-argument gsDPSetCombineMode cannot be used).
#define SELFTEST_ENTRY(name, a, b) { name, gsDPSetCombineLERP(a, b) },
#include "shader_selftest_cases.inc"
const CombineCase sCombineCases[] = { SHADER_SELFTEST_CASES(SELFTEST_ENTRY) };
#undef SELFTEST_ENTRY

struct RenderModeCase {
    const char* name;
    u32 c0;
    u32 c1;
};

const RenderModeCase sRenderModes[] = {
    { "AA_ZB_OPA_SURF", G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2 },
    { "AA_ZB_XLU_SURF", G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2 },
    { "AA_ZB_TEX_EDGE", G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2 },
    { "AA_TEX_EDGE", G_RM_AA_TEX_EDGE, G_RM_AA_TEX_EDGE2 },
    { "CLD_SURF", G_RM_CLD_SURF, G_RM_CLD_SURF2 },
    { "ZB_XLU_DECAL", G_RM_ZB_XLU_DECAL, G_RM_ZB_XLU_DECAL2 },
    { "FOG_SHADE_A+AA_ZB_XLU_SURF2", G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2 },
    { "XLU_SURF", G_RM_XLU_SURF, G_RM_XLU_SURF2 },
};

const u32 sAlphaCompare[] = { G_AC_NONE, G_AC_THRESHOLD, G_AC_DITHER };
const char* const sAlphaCompareNames[] = { "AC_NONE", "AC_THRESHOLD", "AC_DITHER" };

} // namespace

extern "C" int port_shader_selftest_run(void) {
    const int numCombine = (int)(sizeof(sCombineCases) / sizeof(sCombineCases[0]));
    const int numRender = (int)(sizeof(sRenderModes) / sizeof(sRenderModes[0]));
    int frames = 0;
    int variants = 0;
    fprintf(stderr, "[selftest] %d combiner pairs x %d render modes x 3 alpha compares x fog x cycle type\n",
            numCombine, numRender);

    // One frame per combiner pair; every render/alpha/fog/cycle variant is drawn in it.
    static Gfx dl[4096];
    for (int ci = 0; ci < numCombine; ci++) {
        int n = 0;
        dl[n++] = (Gfx)gsDPPipeSync();
        dl[n++] = (Gfx)gsSPMatrix((Mtx*)sProjMtx, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        dl[n++] = (Gfx)gsSPMatrix((Mtx*)sIdentityMtx, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        dl[n++] = (Gfx)gsSPViewport(&sViewport);
        dl[n++] = (Gfx)gsDPSetScissor(G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        dl[n++] = (Gfx)gsSPClearGeometryMode(G_CULL_BOTH | G_LIGHTING | G_FOG);
        dl[n++] = (Gfx)gsSPSetGeometryMode(G_SHADE | G_SHADING_SMOOTH);
        dl[n++] = (Gfx)gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        dl[n++] = (Gfx)gsDPSetTextureFilter(G_TF_BILERP);
        dl[n++] = (Gfx)gsDPSetPrimColor(0, 0, 255, 255, 255, 200);
        dl[n++] = (Gfx)gsDPSetEnvColor(128, 128, 128, 255);
        dl[n++] = (Gfx)gsDPSetFogColor(0, 0, 0, 255);
        dl[n++] = (Gfx)gsSPFogPosition(900, 1000);
        dl[n++] = (Gfx)gsSPDisplayList(sLoadTextures);
        dl[n++] = sCombineCases[ci].cmd;
        dl[n++] = (Gfx)gsSPVertex(sVerts, 3, 0);
        for (int ri = 0; ri < numRender; ri++) {
            for (int ai = 0; ai < 3; ai++) {
                for (int fog = 0; fog < 2; fog++) {
                    for (int cyc = 0; cyc < 2; cyc++) {
                        dl[n++] = (Gfx)gsDPPipeSync();
                        dl[n++] = (Gfx)gsDPSetCycleType(cyc ? G_CYC_2CYCLE : G_CYC_1CYCLE);
                        if (fog) {
                            dl[n++] = (Gfx)gsSPSetGeometryMode(G_FOG);
                        } else {
                            dl[n++] = (Gfx)gsSPClearGeometryMode(G_FOG);
                        }
                        dl[n++] = (Gfx)gsDPSetAlphaCompare(sAlphaCompare[ai]);
                        dl[n++] = (Gfx)gsDPSetRenderMode(sRenderModes[ri].c0, sRenderModes[ri].c1);
                        dl[n++] = (Gfx)gsSP1Triangle(0, 1, 2, 0);
                        variants++;
                    }
                }
            }
        }
        dl[n++] = (Gfx)gsDPPipeSync();
        dl[n++] = (Gfx)gsSPEndDisplayList();
        if (n > (int)(sizeof(dl) / sizeof(dl[0]))) {
            fprintf(stderr, "[selftest] display list overflow (%d commands)\n", n);
            abort();
        }

        int before = gGfxOglShaderFailures;
        int createdBefore = gGfxOglShaderCreations;
        GameEngine::ProcessGfxCommands(dl);
        frames++;
        if (gGfxOglShaderCreations != createdBefore) {
            // Names the combiner behind the [GfxOGL] new shader lines just above, so a
            // shader id seen in a device log can be traced back to its combine mode.
            fprintf(stderr, "[selftest] the %d shader(s) above are (%s)\n", gGfxOglShaderCreations - createdBefore,
                    sCombineCases[ci].name);
        }
        if (gGfxOglShaderFailures != before) {
            fprintf(stderr, "[selftest] %d FAILURE(s) with combiner (%s); see the [GfxOGL] lines above\n",
                    gGfxOglShaderFailures - before, sCombineCases[ci].name);
        }
    }
    fprintf(stderr, "[selftest] rendered %d frames / %d draw variants, %d shader program failure(s)\n", frames,
            variants, gGfxOglShaderFailures);
    return gGfxOglShaderFailures;
}
