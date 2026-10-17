// The logic tick of the engine at a free frame rate (tick.h).
#include "gpu/interpolation/tick.h"
#include "gpu/interpolation/transforms.h"
#include "gpu/common/guest_memory.h"
#include "gpu/d3d/d3d.h"
#include "gpu/diagnostics/diagnostics.h"
#include "gpu/diagnostics/motion.h"
#include "gpu/host/services.h"
#include <windows.h>
#include <fmt/format.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

extern "C" void __imp__sub_82684518(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82677D20(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_826843D8(PPCContext& __restrict, uint8_t*);
extern "C" void sub_82684050(PPCContext& __restrict, uint8_t*);
extern "C" void sub_8259E908(PPCContext& __restrict, uint8_t*);
extern "C" void sub_8265A058(PPCContext& __restrict, uint8_t*);
extern "C" void sub_826AC2E8(PPCContext& __restrict, uint8_t*);
extern "C" void sub_8259FF40(PPCContext& __restrict, uint8_t*);

namespace gpu::tick {
namespace {
constexpr uint32_t kTimePointer = 0x82C0F080;
// The view block of 82684518: +8 the world, +12 the view.
constexpr uint32_t kViews = 0x82709C34;
constexpr uint32_t kViewsOff = 0x82C0EBB4;   // Set while a world is replaced: no view.
constexpr uint32_t kWorldEffects = 0x82C0EB04;  // The argument of 826AC2E8.

int enabled = -1;
uint32_t rate = 0;
bool switched_off = false, f8_down = false;
// Main thread.
Frame frame;
bool decided = false, started = false, in_step = false;
uint32_t frames_since_tick = 0;
int64_t frequency = 0, last = 0;
double debt = 0;  // Real time owed to the game, seconds.
uint64_t capped = 0;  // Iterations whose debt reached two periods (the game behind the real time).
int64_t window_start = 0;
uint64_t window_ticks = 0, window_iterations = 0;

void Record(const char* reason, int64_t now) {
  if (!Diagnostics()) return;
  const double seconds = window_start && frequency ? double(now - window_start) / double(frequency) : 0.0;
  host::Record("native-d3d-tick", fmt::format(
      "\"reason\":\"{}\",\"iteration\":{},\"owning\":{},\"gate\":{},\"tick\":{},\"period\":{:.6f},\"alpha\":{:.4f},"
      "\"frame_rate\":{},\"ticks\":{},\"skipped\":{},\"capped\":{},\"debt_ms\":{:.3f},\"window_s\":{:.3f},"
      "\"window_ticks_per_s\":{:.3f},\"window_iterations_per_s\":{:.3f}",
      reason, frame.index, Owning(), frame.gate, frame.tick, frame.period, frame.alpha, FrameRate(), frame.ticks,
      frame.skipped, capped, debt * 1e3, seconds, seconds > 0 ? double(window_ticks) / seconds : 0.0,
      seconds > 0 ? double(window_iterations) / seconds : 0.0));
  window_start = now;
  window_ticks = window_iterations = 0;
}

// Once per loop iteration, by the first of its three calls.
void Decide(uint8_t* base) {
  if (decided) return;
  decided = true;
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  if (!frequency) {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    frequency = f.QuadPart;
    window_start = now.QuadPart;
  }
  const bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
  const bool toggled = f8 && !f8_down;
  f8_down = f8;
  if (toggled) switched_off = !switched_off;
  ++frame.index;
  const uint32_t time = Get32(base, kTimePointer);
  const bool fixed = time && Get32(base, time + 72);
  const float period = time ? GetFloat(base, time + 76) : 0.0f;
  const double frame_period = 1.0 / double(FrameRate());
  const bool gate = Owning() && fixed && std::isfinite(period) && period > frame_period * 1.25 && period <= 0.25f;
  const bool changed = gate != frame.gate || (gate && std::fabs(period - frame.period) > 1e-6f);
  frame.gate = gate;
  frame.period = gate ? period : 0.0f;
  if (!gate) {
    frame.tick = true;
    frame.alpha = 1.0f;
    started = false;
    debt = 0;
    frames_since_tick = 0;
  } else {
    // A new run of the gate starts with a tick; then the real time since the
    // previous iteration is owed. A tick is due within half a frame of its
    // period, so the frames at a steady rate alternate without jitter.
    debt = started ? debt + double(now.QuadPart - last) / double(frequency) : double(period);
    started = true;
    if (debt > 2.0 * period) {
      debt = 2.0 * period;
      ++capped;
    }
    frame.tick = debt >= double(period) - frame_period * 0.5;
    if (frame.tick) {
      debt -= period;
      ++frame.ticks;
      frames_since_tick = 0;
    } else {
      ++frame.skipped;
      ++frames_since_tick;
    }
    // The frames are presented at the limiter's steady rate, not when their
    // iteration starts (the delivery waits for the render thread): the alpha
    // follows the frames since the tick, and the real time only when a frame
    // is more than half a frame off its slot (a slow frame).
    const double real = debt / double(period);
    const double paced = double(frames_since_tick) * frame_period / double(period);
    const double alpha = std::fabs(paced - real) <= 0.5 * frame_period / double(period) ? paced : real;
    frame.alpha = float(std::clamp(alpha, 0.0, 1.0));
  }
  last = now.QuadPart;
  ++window_iterations;
  if (frame.tick) ++window_ticks;
  if (toggled || changed || frame.index % 300 == 0)
    Record(toggled ? "f8" : changed ? "mode" : "periodic", now.QuadPart);
  motion::LoopStart(base);  // REX_NATIVE_MOTION_RECORD.
}

// The view block of 82684518 (8268479C to 82684868) without the sound
// (82668A78 advances by the dt; 82532328 and 8227AC30 after the delivery):
// the arguments and stores of the original, in its order.
void ViewBlock(PPCContext& ctx, uint8_t* base) {
  const uint32_t view = Get32(base, kViews + 12);
  if (!view || Get32(base, kViewsOff)) return;
  auto call = [&](void (*fn)(PPCContext& __restrict, uint8_t*), uint32_t r3, uint32_t r4) {
    ctx.r3.u64 = r3;
    ctx.r4.u64 = r4;
    fn(ctx, base);
  };
  call(sub_82684050, view, 0);
  call(sub_8259E908, view, 0);
  const uint32_t world = Get32(base, kViews + 8);
  Put32(base, view + 2248, world);
  if (world) {
    Put32(base, Get32(base, world + 48) + 144, view);
    call(sub_8265A058, Get32(base, view + 2248), view);
  }
  call(sub_826AC2E8, Get32(base, kWorldEffects), view);
  call(sub_8259FF40, view, 1);
}

bool Gating() { return gpu::Active() && Enabled(); }
}  // namespace

bool Enabled() {
  if (enabled < 0) {
    const char* text = std::getenv("REX_NATIVE_STATE_INTERPOLATION");
    enabled = text && std::string_view(text) == "1" ? 1 : 0;
  }
  return enabled == 1;
}

bool Owning() { return Enabled() && !switched_off; }

uint32_t FrameRate() {
  if (!rate) {
    const char* text = std::getenv("REX_NATIVE_FRAME_RATE");
    const int n = text ? std::atoi(text) : 0;
    rate = n >= 30 && n <= 240 ? uint32_t(n) : 60u;
  }
  return rate;
}

const Frame& Current() { return frame; }

bool InGatedStep() { return in_step; }

const char* Label() {
  if (!Enabled()) return "";
  if (switched_off) return "F8: 1/60 s";
  if (!frame.gate) return "F8: ticks (fora)";
  return interpolation::Enabled() ? "F8: interpolado" : "F8: ticks";
}

// The three calls of the main loop.
void MapStep(PPCContext& ctx, uint8_t* base) {
  if (Gating()) {
    Decide(base);
    if (!frame.tick) return;
  }
  __imp__sub_82677D20(ctx, base);
}

void WorldChangeStep(PPCContext& ctx, uint8_t* base) {
  if (Gating()) {
    Decide(base);
    if (!frame.tick) return;
  }
  __imp__sub_826843D8(ctx, base);
}

void MainStep(PPCContext& ctx, uint8_t* base) {
  if (!Gating()) {
    if (gpu::Active()) motion::LoopStart(base);
    __imp__sub_82684518(ctx, base);
    if (gpu::Active()) motion::MainStepDone(base);
    return;
  }
  Decide(base);
  in_step = frame.gate;
  if (frame.tick) __imp__sub_82684518(ctx, base);
  else ViewBlock(ctx, base);
  in_step = false;
  decided = false;
  motion::MainStepDone(base);
}
}  // namespace gpu::tick

REX_HOOK_RAW(sub_82677D20) { gpu::tick::MapStep(ctx, base); }
REX_HOOK_RAW(sub_826843D8) { gpu::tick::WorldChangeStep(ctx, base); }
REX_HOOK_RAW(sub_82684518) { gpu::tick::MainStep(ctx, base); }
