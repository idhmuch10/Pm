#pragma once
/**
 * frame_interpolation.h - the matrices for a frame drawn between two game frames.
 *
 * See port/frame_interpolation.c. Engine.cpp asks for the matrices part-way between
 * the last game frame and this one and hands them to the renderer as substitutes for
 * the ones the display list names.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortMtxLerp {
    void* addr;  /* the Mtx the display list names */
    float m[4][4]; /* what to draw with instead */
} PortMtxLerp;

/* Fill out with the matrices to substitute, `step` of the way from the previous game
 * frame to this one, and return how many. Zero means draw the frame as it is: there is
 * no previous frame to interpolate from, or nothing moved in a way worth interpolating.
 * Call before the frame is drawn and before port_frame_interpolation_record(). */
int port_frame_interpolation_build(float step, PortMtxLerp* out, int maxOut);

/* Remember this frame as the one to interpolate from next time. Call once per frame,
 * after it has been drawn. */
void port_frame_interpolation_record(void);

/* How many matrices the last frame carried between game frames. Zero while the picture
 * is drawn at the game's own rate, or on a frame nothing could be interpolated from. */
int port_frame_interpolation_last_count(void);

/* How many entries port_frame_interpolation_build() can ever produce. */
int port_frame_interpolation_max(void);

/* Check which pairs of matrices are taken for the same object a frame apart, on two
 * frames built by hand. Returns the number of checks that failed. */
int port_frame_interpolation_selftest(void);

#ifdef __cplusplus
}
#endif
