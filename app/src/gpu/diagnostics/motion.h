#pragma once

// The motion record of the frames the title builds (state interpolation).
// At each display (8265A058, r3 the world, r4 the view), with the loop
// iteration of tick.h (tick, alpha) and the camera cut counter 82C0EB34 (the
// camera 8267A2A0 sets 4 when the camera moved 10 units or turned 30 degrees,
// minus one per call), REX_NATIVE_FRAME_DIR/motion.csv gets:
// - F: the frame: the camera flags of transforms.h in the id (1 written, 4
//   cut), objects of the world's live list (+240 to +228 -
//   8, 8-byte entries), those with dynamics, the objects the display mounted
//   (825E2A08: flag 0x40000 without 0x200004), how many of them are outside
//   the live list, the objects of the matrix probe, the tick period, the
//   matrices interpolated and the object cuts of the tick;
// - C: the camera of the view, position +476..+484 and direction +460..+468;
// - O: each object with dynamics (flag 0x40000 at +4, dynamics [[object + 28]
//   + 4] with 0x80000000): the position of its matrix, [object + 20] + 48..56,
//   and its first row (+0..+8).
// And, every 120 main steps, native-d3d-motion with:
// - the calls of the main step, the display and the camera since the last
//   record;
// - the vertex textures of the draws (the bone palettes, noted by
//   BindTexture): their bytes are hashed at the start of the loop iteration,
//   at the entry and exit of the display, after the delivery (8259FF40) and at
//   the entry and exit of the render thread's lists (820FC008); each change
//   counts for the stage pair it fell between, apart for the iterations with
//   and without a tick;
// - REX_NATIVE_MATRIX_PROBE=<units>: in the iterations without a tick, the
//   objects with dynamics go up by <units> ([object + 20] + 52) during the
//   display and back right after it, so the image shows the raise only when
//   the frame's packet copies the matrices at the display;
// - the stage probe: the words of the view and of the camera's copy
//   (82C0F1F8) that change between the view block's start, the display's
//   entry, the camera's entry and exit (8267A2A0), the display's exit and the
//   delivery, with and without a tick, and the view's and camera's functions.
// REX_NATIVE_MOTION_RECORD=1, with the diagnostics, turns it on.
#include <array>
#include <cstdint>

namespace gpu::motion {
bool Enabled();
// The start of a loop iteration (tick.h, main thread).
void LoopStart(uint8_t* base);
// A main step ran (tick.h, main thread, after it).
void MainStepDone(uint8_t* base);
// The start of the view block (82684050, transforms.cpp), before the
// interpolation writes: a stage of the probe.
void ViewStart(uint8_t* base, uint32_t view);
// After the delivery (8259FF40, transforms.cpp).
void Delivered(uint8_t* base);
// An object the display mounts (825E2A08, transforms.cpp).
void NoteMounted(uint32_t object);
// A vertex texture bound by a draw (BindTexture, translation thread).
void NoteVertexTexture(const std::array<uint32_t, 6>& fetch);
}  // namespace gpu::motion
