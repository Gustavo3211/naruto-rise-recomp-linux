// The transforms between two logic ticks (transforms.h).
#include "gpu/interpolation/transforms.h"
#include "gpu/interpolation/tick.h"
#include "gpu/common/guest_memory.h"
#include "gpu/d3d/d3d.h"
#include "gpu/diagnostics/motion.h"
#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" void __imp__sub_82684050(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_8259FF40(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_825E2A08(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82659B88(PPCContext& __restrict, uint8_t*);

namespace gpu::interpolation {
namespace {
constexpr uint32_t kCutCounter = 0x82C0EB34;
constexpr float kCutDistance = 4.0f;    // Units in one tick (~120 units/s; walking is ~7).
constexpr float kCutCosine = 0.5f;      // A row turning more than 60 degrees in one tick.
constexpr float kCameraCutDistance = 10.0f, kCameraCutCosine = 0.866f;  // 8267A2A0.
// The camera: the matrix at the start of the view's entry (the 172-byte
// entries of world + 48), written by the logic in the tick. The display builds
// the view's camera from it (82659B88 -> 82584F58: the world matrix at view +
// 428, the view matrix at +496, the projection and the planes at +1084).
constexpr size_t kCameraFloats = 16;

using Matrix = std::array<float, 16>;
using Camera = std::array<float, kCameraFloats>;
struct Track {
  uint32_t matrix = 0;
  uint64_t tick = 0;
  bool valid = false;
  Matrix previous{}, current{};
};
struct CameraTrack {
  uint32_t entry = 0;
  uint64_t tick = 0;
  bool valid = false, cut = false;
  Camera previous{}, current{};
};
// A place written over the live transform, put back after the delivery.
struct Written {
  uint32_t address = 0, count = 0;
  std::array<float, kCameraFloats> value{}, live{};
};

int enabled = -1;
std::unordered_map<uint32_t, Track> tracks;  // By object.
std::vector<uint32_t> mounted, candidates;   // Objects of the current and the last display.
bool collecting = false;
CameraTrack camera;
std::vector<Written> written;
Camera camera_written{};
bool camera_pending = false, camera_entry_done = false;
uint32_t camera_written_entry = 0;
Applied applied;
// The pointers the engine freed (820C0018, any thread) since the last view
// block: an object freed by the tick (the deferred destruction at the end of
// the iteration, 82659060) leaves the candidates and the tracks before
// anything is written.
std::atomic<bool> watching{false};
std::mutex freed_mutex;
std::vector<uint32_t> freed;
bool freed_overflow = false;
constexpr size_t kFreedMax = 1 << 20;

void ReadFloats(uint8_t* base, uint32_t address, float* out, size_t count) {
  for (size_t i = 0; i < count; ++i) out[i] = GetFloat(base, address + uint32_t(4 * i));
}
bool Finite(const float* v, size_t count) {
  return std::all_of(v, v + count, [](float f) { return std::isfinite(f); });
}
float Length3(const float* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
float Cosine3(const float* a, const float* b) {
  const float la = Length3(a), lb = Length3(b);
  if (la <= 1e-6f || lb <= 1e-6f) return 1.0f;
  return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
}
float Distance3(const float* a, const float* b) {
  const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  return Length3(d);
}
// A direction row between two: linear, with the length interpolated.
void BlendRow(const float* a, const float* b, float t, float* out) {
  for (int i = 0; i < 3; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
  const float length = Length3(out), target = Length3(a) + (Length3(b) - Length3(a)) * t;
  if (length > 1e-6f)
    for (int i = 0; i < 3; ++i) out[i] *= target / length;
}
void BlendMatrix(const float* a, const float* b, float t, float* out) {
  for (int row = 0; row < 3; ++row) {
    BlendRow(a + 4 * row, b + 4 * row, t, out + 4 * row);
    out[4 * row + 3] = a[4 * row + 3] + (b[4 * row + 3] - a[4 * row + 3]) * t;
  }
  for (int i = 12; i < 16; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
}
bool MatrixCut(const Matrix& a, const Matrix& b) {
  if (Distance3(a.data() + 12, b.data() + 12) > kCutDistance) return true;
  for (int row = 0; row < 3; ++row)
    if (Cosine3(a.data() + 4 * row, b.data() + 4 * row) < kCutCosine) return true;
  return false;
}
void ReadCamera(uint8_t* base, uint32_t entry, Camera& out) { ReadFloats(base, entry, out.data(), kCameraFloats); }
void WriteCamera(uint8_t* base, uint32_t entry, const Camera& c) {
  for (uint32_t i = 0; i < kCameraFloats; ++i) PutFloat(base, entry + 4 * i, c[i]);
}
// The position (row 3) and the rotation rows.
bool CameraCut(const Camera& a, const Camera& b) {
  if (Distance3(a.data() + 12, b.data() + 12) >= kCameraCutDistance) return true;
  for (int row = 0; row < 3; ++row)
    if (Cosine3(a.data() + 4 * row, b.data() + 4 * row) < kCameraCutCosine) return true;
  return false;
}
// The camera at the tick: the current one becomes the previous.
void TickCamera(CameraTrack& track, uint32_t entry, const Camera& live, uint64_t tick) {
  const bool continuing = track.entry == entry && track.tick + 1 == tick;
  track.previous = continuing ? track.current : live;
  track.current = live;
  track.cut = continuing && CameraCut(track.previous, track.current);
  track.valid = continuing && !track.cut && Finite(live.data(), kCameraFloats);
  track.entry = entry;
  track.tick = tick;
}
// Writes the camera of the track at alpha; false when it does not apply.
bool ApplyCamera(uint8_t* base, CameraTrack& track, uint32_t entry, uint64_t tick, float alpha) {
  if (!track.valid || track.entry != entry || track.tick != tick || Get32(base, kCutCounter)) return false;
  if (track.previous == track.current) return false;
  Camera live;
  ReadCamera(base, entry, live);
  if (live != track.current) {  // Something else wrote it since the tick.
    track.valid = false;
    return false;
  }
  BlendMatrix(track.previous.data(), track.current.data(), alpha, camera_written.data());
  WriteCamera(base, entry, camera_written);
  camera_pending = true;
  camera_written_entry = entry;
  return true;
}

bool Active() { return gpu::Active() && Enabled() && tick::InGatedStep(); }

// The start of the view block (82684050, r3 the view).
void ViewStart(uint8_t* base) {
  candidates.swap(mounted);
  mounted.clear();
  collecting = Active();
  applied = {};
  if (!collecting) {
    tracks.clear();
    camera = {};
    watching = false;
    std::lock_guard lock(freed_mutex);
    freed.clear();
    return;
  }
  watching = true;
  {
    std::vector<uint32_t> gone;
    bool overflow = false;
    {
      std::lock_guard lock(freed_mutex);
      gone.swap(freed);
      overflow = freed_overflow;
      freed_overflow = false;
    }
    if (overflow) {
      tracks.clear();
      candidates.clear();
    } else if (!gone.empty()) {
      const std::unordered_set<uint32_t> set(gone.begin(), gone.end());
      for (auto it = tracks.begin(); it != tracks.end();)
        it = set.count(it->first) || set.count(it->second.matrix) ? tracks.erase(it) : std::next(it);
      std::erase_if(candidates, [&](uint32_t object) { return set.count(object) != 0; });
    }
  }
  const auto& f = tick::Current();
  const uint64_t tick = f.ticks;
  if (f.tick) {
    std::unordered_set<uint32_t> seen;
    for (const uint32_t object : candidates) {
      if (!seen.insert(object).second) continue;
      const uint32_t m = Get32(base, object + 20);
      if (!m) continue;
      Matrix live;
      ReadFloats(base, m, live.data(), 16);
      Track& t = tracks[object];
      const bool continuing = t.matrix == m && t.tick + 1 == tick;
      t.previous = continuing ? t.current : live;
      t.current = live;
      const bool cut = continuing && MatrixCut(t.previous, t.current);
      if (cut) ++applied.object_cuts;
      t.valid = continuing && !cut && Finite(live.data(), 16);
      t.matrix = m;
      t.tick = tick;
    }
    for (auto it = tracks.begin(); it != tracks.end();) it = it->second.tick + 2 < tick ? tracks.erase(it) : std::next(it);
  }
  const float alpha = f.alpha;
  std::unordered_set<uint32_t> matrices;
  for (auto& [object, t] : tracks) {
    if (!t.valid || t.tick != tick || t.previous == t.current || !matrices.insert(t.matrix).second) continue;
    Matrix live;
    ReadFloats(base, t.matrix, live.data(), 16);
    if (live != t.current) {  // Written outside the logic since the tick.
      t.valid = false;
      continue;
    }
    Written w;
    w.address = t.matrix;
    w.count = 16;
    BlendMatrix(t.previous.data(), t.current.data(), alpha, w.value.data());
    std::copy(t.current.begin(), t.current.end(), w.live.begin());
    for (uint32_t i = 0; i < 16; ++i) PutFloat(base, w.address + 4 * i, w.value[i]);
    written.push_back(w);
    ++applied.objects;
  }
  camera_entry_done = false;
}

// The display's camera (82659B88, r3 the view's entry), before it builds the
// view's camera from the entry: once per display (the display calls it again
// at its end, over the same entry).
void CameraEntry(uint8_t* base, uint32_t entry) {
  if (!collecting || !entry || camera_entry_done) return;
  camera_entry_done = true;
  const auto& f = tick::Current();
  if (f.tick) {
    Camera live;
    ReadCamera(base, entry, live);
    TickCamera(camera, entry, live, f.ticks);
    applied.camera_cut = camera.cut;
  }
  applied.camera = ApplyCamera(base, camera, entry, f.ticks, f.alpha);
}

// After the delivery: the live transforms come back where nothing else wrote.
void Restore(uint8_t* base) {
  for (const auto& w : written) {
    bool ours = true;
    for (uint32_t i = 0; i < w.count && ours; ++i) ours = GetFloat(base, w.address + 4 * i) == w.value[i];
    if (ours)
      for (uint32_t i = 0; i < w.count; ++i) PutFloat(base, w.address + 4 * i, w.live[i]);
  }
  written.clear();
  if (camera_pending) {
    Camera live;
    ReadCamera(base, camera_written_entry, live);
    if (live == camera_written) WriteCamera(base, camera_written_entry, camera.current);
    camera_pending = false;
  }
  collecting = false;
}
}  // namespace

bool Enabled() {
  if (enabled < 0) {
    const char* text = std::getenv("REX_NATIVE_INTERPOLATION_TRANSFORMS");
    enabled = tick::Enabled() && !(text && std::string_view(text) == "0") ? 1 : 0;
  }
  return enabled == 1;
}

const Applied& LastApplied() { return applied; }

void NoteFree(uint32_t pointer) {
  if (!watching.load(std::memory_order_relaxed)) return;
  std::lock_guard lock(freed_mutex);
  if (freed.size() < kFreedMax) freed.push_back(pointer);
  else freed_overflow = true;
}
}  // namespace gpu::interpolation

using namespace gpu;
REX_HOOK_RAW(sub_82684050) {
  motion::ViewStart(base, ctx.r3.u32);  // The view probe, before the writes.
  gpu::interpolation::ViewStart(base);
  __imp__sub_82684050(ctx, base);
}
REX_HOOK_RAW(sub_8259FF40) {
  __imp__sub_8259FF40(ctx, base);
  gpu::interpolation::Restore(base);
  motion::Delivered(base);
}
REX_HOOK_RAW(sub_825E2A08) {
  using namespace gpu::interpolation;
  if (collecting || motion::Enabled()) {
    const uint32_t holder = ctx.r3.u32, count = Get32(base, holder + 8), array = Get32(base, holder + 4);
    for (uint32_t i = 0; array && i < count && i < 65536; ++i) {
      const uint32_t object = Get32(base, array + 4 * i);
      if (!object) continue;
      const uint32_t flags = Get32(base, object + 4);
      if (!(flags & 0x00040000) || (flags & 0x00200004)) continue;
      if (collecting) mounted.push_back(object);
      motion::NoteMounted(object);
    }
  }
  __imp__sub_825E2A08(ctx, base);
}
REX_HOOK_RAW(sub_82659B88) {
  gpu::interpolation::CameraEntry(base, ctx.r3.u32);
  __imp__sub_82659B88(ctx, base);
}
