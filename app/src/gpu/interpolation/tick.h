#pragma once

// The logic tick of the engine at a free frame rate (state interpolation).
//
// The main loop (82684920) calls, once per presented frame, the map of the
// HUD (82677D20), the pending world change (826843D8) and the main step
// (82684518): the time update 826ACFC0, the services, the scripts, the
// world 826BCAD0 and the work thread of the effects (82650610), then the view
// block (the view 82684050 / 8259E908, the display 8265A058, the effects of
// the view 826AC2E8 and the delivery 8259FF40), the sound and the end of the
// frame (8267DDC0). With the fixed step on (time structure +72, the open world
// at 2/60 s, the fight at 1/60 s), the logic runs at the period of +76, as on
// the console: the clock owes the real time to the game and a loop iteration
// is a tick when a period is due. An iteration without a tick runs only the
// view block of the original (no time update, no sound, no logic), so the
// frames between two ticks show the last tick again; the transforms are
// interpolated over them (later increments). Without the fixed step, or when
// the period is not longer than the frame (the fight at 60), every iteration
// is a tick.
//
// REX_NATIVE_STATE_INTERPOLATION=1 turns the mode on. The own limiter then
// paces at REX_NATIVE_FRAME_RATE (60 by default) and the time step stays the
// title's (no rewrite of +76). F8 switches the gate off and on at run time:
// off, the frame is the 1/60 s simulation of unlock_fps (every iteration a
// tick, +76 rewritten). Changes and every 300th iteration are recorded as
// native-d3d-tick (diagnostics). Main thread.
//
// The time of the view block: the display and the effects of the view advance
// some state by the engine's dt (+56 in microseconds, +64 scaled, +68 raw):
// the passes of 8259BA90 under the display, the step dust among them. The
// engine's time is updated only in the ticks, so between them the view block
// would see the tick's dt in every frame, twice per tick at 60 Hz. In the
// iterations the gate decides, from the view block's start (82684050) to the
// delivery (8259FF40), the three fields are scaled to the advance of the
// image's time (ticks + alpha) since the last view block, in periods: half
// the tick's dt per frame at 60 Hz, a quarter at 120 Hz; then the tick's
// values come back. The effects' work thread gets its dt as an argument
// (826AB438). REX_NATIVE_INTERPOLATION_VIEW_TIME=0 leaves the tick's dt;
// =tick gives the tick's dt in the ticks and 0 between them (a probe).
#include <cstdint>

namespace gpu::tick {
// The mode is on (REX_NATIVE_STATE_INTERPOLATION=1), whatever F8 says.
bool Enabled();
// On and not switched off by F8: the limiter paces at the frame rate and the
// engine's time step is not rewritten.
bool Owning();
// REX_NATIVE_FRAME_RATE (30 to 240), 60 by default.
uint32_t FrameRate();
// The current loop iteration.
struct Frame {
  uint64_t index = 0;     // Loop iterations of the native mode.
  bool gate = false;      // The gate decides (fixed step longer than the frame).
  bool tick = true;       // The logic runs in this iteration.
  float period = 0.0f;    // The tick period (+76), seconds.
  float alpha = 0.0f;     // This frame between the last two ticks, 0 to 1.
  uint64_t ticks = 0;     // Ticks of the gated iterations.
  uint64_t skipped = 0;   // Gated iterations without a tick.
};
const Frame& Current();
// Inside the main step of an iteration the gate decides.
bool InGatedStep();
// The label of the window title; empty when the mode is off.
const char* Label();
// The view block's start (82684050) and the delivery's end (8259FF40), main
// thread: the engine's dt of the view block (above).
void BeginView(uint8_t* base);
void EndView(uint8_t* base);
}  // namespace gpu::tick
