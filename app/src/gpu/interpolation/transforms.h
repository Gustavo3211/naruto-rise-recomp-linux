#pragma once

// The transforms between two logic ticks (state interpolation, tick.h).
//
// In the iterations the gate decides, the view block of the main step draws
// the objects and the camera between the last two ticks instead of the last
// one. At the start of the view block (82684050, the view of the main step),
// in an iteration with a tick, the matrix of each object the last display
// mounted (825E2A08: flag 0x40000 without 0x200004), [object + 20] (16
// floats, position at +48..+56), becomes the current transform, and the
// previous current one the previous; so does the camera, the matrix at the
// start of the view's entry (the 172-byte entries of world + 48, the same
// layout), at the display's camera (82659B88, which builds the view's camera
// from it). Then, in every iteration of the gate, the transform at the tick's
// alpha (tick.h) is written over the live one (the objects before the view
// block, the camera before 82659B88) and the live one comes back after the
// delivery (8259FF40), unless something wrote the place meanwhile. The display copies the matrices into the frame's
// packet (P2 of the study), so nothing else reads them.
//
// Rows of the rotation are interpolated linearly and keep the interpolated
// length; the position linearly. Cuts, without interpolation until the next
// tick: an object whose matrix moved more than kCutDistance or turned more
// than ~60 degrees in one tick, or whose matrix pointer changed; the camera
// when it moved 10 units or turned 30 degrees in one tick (the thresholds of
// 8267A2A0) or while the game's cut counter 82C0EB34 is set.
//
// On with the gate of REX_NATIVE_STATE_INTERPOLATION=1;
// REX_NATIVE_INTERPOLATION_TRANSFORMS=0 leaves the frames between the ticks
// as the last tick (the gate alone). Main thread.
#include <cstdint>

namespace gpu::interpolation {
bool Enabled();
// The last view block: the matrices written (objects), the camera written,
// and the cuts of the last tick.
struct Applied {
  uint32_t objects = 0, object_cuts = 0;
  bool camera = false, camera_cut = false;
};
const Applied& LastApplied();
// A pointer the engine frees (820C0018, crash_dump.cpp), any thread: a freed
// object leaves the tracks before the next write.
void NoteFree(uint32_t pointer);
}  // namespace gpu::interpolation
