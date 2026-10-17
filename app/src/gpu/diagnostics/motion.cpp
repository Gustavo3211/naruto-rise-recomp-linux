// The motion record of the frames the title builds (motion.h).
#include "gpu/diagnostics/motion.h"
#include "gpu/diagnostics/diagnostics.h"
#include "gpu/interpolation/tick.h"
#include "gpu/common/guest_memory.h"
#include "gpu/host/services.h"
#include "gpu/textures/formats.h"
#include <windows.h>
#include <fmt/format.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

extern "C" void __imp__sub_8265A058(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_8267A2A0(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_825E2A08(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_8259FF40(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_820FC008(PPCContext& __restrict, uint8_t*);

namespace gpu::motion {
namespace {
constexpr uint32_t kCutCounter = 0x82C0EB34;
enum Stage { kLoop, kDisplayIn, kDisplayOut, kDelivered, kListsIn, kListsOut, kStages };
constexpr const char* kStageNames[kStages] = {"loop", "display_in", "display_out", "delivered", "lists_in",
                                              "lists_out"};
constexpr size_t kRangesMax = 256;
constexpr uint32_t kRangeBytesMax = 64 * 1024;

int enabled = -1;
float probe_units = 0.0f;
std::FILE* file = nullptr;
int64_t frequency = 0, start = 0;
// Main thread.
uint64_t displays = 0, cameras = 0, main_steps = 0, mounts = 0;
uint64_t last_displays = 0, last_cameras = 0, last_main_steps = 0;
int64_t last_record = 0;
bool in_display = false;
uint32_t mounted = 0, mounted_outside = 0;
std::unordered_set<uint32_t> live_set;
// The vertex textures, by physical address (translation, main and render threads).
struct Range {
  uint32_t bytes = 0, format = 0;
  uint64_t hash = 0, seen = 0;
  int stage = -1;
};
std::mutex ranges_mutex;
std::map<uint32_t, Range> ranges;
uint64_t notes = 0, distinct = 0, hashed_ranges = 0;
uint64_t changes[2][kStages][kStages]{};  // [tick][from][to]
std::atomic<uint64_t> loop_index{0};
std::atomic<bool> iteration_tick{true};

uint64_t Hash(const uint8_t* data, size_t size) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < size; ++i) h = (h ^ data[i]) * 0x100000001B3ull;
  return h;
}

void StageHash(uint8_t* base, Stage stage) {
  std::vector<uint8_t> buffer;
  std::lock_guard lock(ranges_mutex);
  const int tick = iteration_tick.load() ? 1 : 0;
  for (auto& [address, r] : ranges) {
    buffer.resize(r.bytes);
    if (!ReadPhysical(base, address, r.bytes, buffer.data())) continue;
    const uint64_t h = Hash(buffer.data(), buffer.size());
    if (r.stage >= 0 && h != r.hash) ++changes[tick][r.stage][stage];
    r.hash = h;
    r.stage = stage;
    ++hashed_ranges;
  }
}

// The live list of a world and its objects with dynamics.
std::vector<uint32_t> LiveObjects(uint8_t* base, uint32_t world) {
  std::vector<uint32_t> out;
  if (!world) return out;
  const uint32_t first = Get32(base, world + 240), last = Get32(base, world + 228) - 8;
  if (!first || last < first || last - first > 8 * 65536) return out;
  for (uint32_t entry = first; entry <= last; entry += 8) {
    const uint32_t object = Get32(base, entry);
    if (object && (object & 3) != 1) out.push_back(object);
  }
  return out;
}
bool HasDynamics(uint8_t* base, uint32_t object) {
  if (!(Get32(base, object + 4) & 0x00040000)) return false;
  const uint32_t extension = Get32(base, object + 28);
  const uint32_t dynamics = extension ? Get32(base, extension + 4) : 0;
  return dynamics && (Get32(base, dynamics) & 0x80000000);
}
float Finite(float f) { return std::isfinite(f) ? f : 0.0f; }

int64_t Microseconds() {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  if (!frequency) {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    frequency = f.QuadPart;
    start = now.QuadPart;
  }
  return (now.QuadPart - start) * 1000000 / frequency;
}

void Display(PPCContext& ctx, uint8_t* base) {
  if (!Enabled()) {
    __imp__sub_8265A058(ctx, base);
    return;
  }
  const uint32_t world = ctx.r3.u32, view = ctx.r4.u32;
  const auto& f = tick::Current();
  ++displays;
  StageHash(base, kDisplayIn);
  const std::vector<uint32_t> live = LiveObjects(base, world);
  live_set.clear();
  live_set.insert(live.begin(), live.end());
  struct Moving { uint32_t object, matrix; float x, y, z, a, b, c; };
  std::vector<Moving> moving;
  for (const uint32_t object : live) {
    if (moving.size() >= 32 || !HasDynamics(base, object)) continue;
    const uint32_t m = Get32(base, object + 20);
    if (!m) continue;
    moving.push_back({object, m, Finite(GetFloat(base, m + 48)), Finite(GetFloat(base, m + 52)),
                      Finite(GetFloat(base, m + 56)), Finite(GetFloat(base, m)), Finite(GetFloat(base, m + 4)),
                      Finite(GetFloat(base, m + 8))});
  }
  // The matrix probe: raised during the display of an iteration without a tick.
  std::vector<std::pair<uint32_t, float>> raised;
  if (probe_units != 0.0f && f.gate && !f.tick)
    for (const auto& o : moving) {
      if (std::any_of(raised.begin(), raised.end(), [&](const auto& r) { return r.first == o.matrix + 52; }))
        continue;
      raised.emplace_back(o.matrix + 52, GetFloat(base, o.matrix + 52));
      PutFloat(base, o.matrix + 52, raised.back().second + probe_units);
    }
  mounted = mounted_outside = 0;
  in_display = true;
  __imp__sub_8265A058(ctx, base);
  in_display = false;
  for (const auto& [address, value] : raised) PutFloat(base, address, value);
  StageHash(base, kDisplayOut);
  if (!file) return;
  const int64_t us = Microseconds();
  const uint32_t cut = Get32(base, kCutCounter);
  std::string rows = fmt::format("{},{},{},{},{},{:.4f},{},F,0,{},{},{},{},{},0\n", displays, us, f.index, int(f.gate),
                                 int(f.tick), f.alpha, cut, live.size(), moving.size(), mounted, mounted_outside,
                                 raised.size());
  if (view)
    rows += fmt::format("{},{},{},{},{},{:.4f},{},C,{:08X},{:.5f},{:.5f},{:.5f},{:.5f},{:.5f},{:.5f}\n", displays, us,
                        f.index, int(f.gate), int(f.tick), f.alpha, cut, view, Finite(GetFloat(base, view + 476)),
                        Finite(GetFloat(base, view + 480)), Finite(GetFloat(base, view + 484)),
                        Finite(GetFloat(base, view + 460)), Finite(GetFloat(base, view + 464)),
                        Finite(GetFloat(base, view + 468)));
  for (const auto& o : moving)
    rows += fmt::format("{},{},{},{},{},{:.4f},{},O,{:08X},{:.5f},{:.5f},{:.5f},{:.5f},{:.5f},{:.5f}\n", displays, us,
                        f.index, int(f.gate), int(f.tick), f.alpha, cut, o.object, o.x, o.y, o.z, o.a, o.b, o.c);
  std::fputs(rows.c_str(), file);
  if (displays % 60 == 0) std::fflush(file);
}

void Record(uint8_t* base) {
  const int64_t us = Microseconds();
  const double seconds = last_record ? double(us - last_record) * 1e-6 : 0.0;
  std::string pairs[2];
  size_t range_count = 0;
  uint64_t note_count = 0, distinct_count = 0, hashed = 0;
  std::string formats;
  {
    std::lock_guard lock(ranges_mutex);
    for (int t = 0; t < 2; ++t)
      for (int a = 0; a < kStages; ++a)
        for (int b = 0; b < kStages; ++b)
          if (changes[t][a][b])
            pairs[t] += fmt::format("{}\"{}-{}\":{}", pairs[t].empty() ? "" : ",", kStageNames[a], kStageNames[b],
                                    changes[t][a][b]);
    range_count = ranges.size();
    note_count = notes;
    distinct_count = distinct;
    hashed = hashed_ranges;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> kinds;  // (format, bytes) -> ranges
    for (const auto& [address, r] : ranges) ++kinds[{r.format, r.bytes}];
    for (const auto& [k, n] : kinds)
      formats += fmt::format("{}[{},{},{}]", formats.empty() ? "" : ",", k.first, k.second, n);
  }
  const uint32_t time = Get32(base, 0x82C0F080);
  const uint32_t context = Get32(base, 0x82C0F008);
  host::Record("native-d3d-motion", fmt::format(
      "\"main_steps\":{},\"displays\":{},\"cameras\":{},\"seconds\":{:.3f},\"main_steps_per_s\":{:.3f},"
      "\"displays_per_s\":{:.3f},\"cameras_per_s\":{:.3f},\"fixed\":{},\"period\":{:.6f},\"title_interval\":{},"
      "\"tick\":{{\"index\":{},\"gate\":{},\"ticks\":{},\"skipped\":{}}},\"mounts\":{},\"probe_units\":{},"
      "\"vertex_textures\":{{\"ranges\":{},\"notes\":{},\"distinct\":{},\"hashed\":{},\"format_bytes_count\":[{}],"
      "\"changes_tick\":{{{}}},\"changes_without_tick\":{{{}}}}}",
      main_steps, displays, cameras, seconds, seconds > 0 ? double(main_steps - last_main_steps) / seconds : 0.0,
      seconds > 0 ? double(displays - last_displays) / seconds : 0.0,
      seconds > 0 ? double(cameras - last_cameras) / seconds : 0.0, time ? Get32(base, time + 72) : 0,
      time ? Finite(GetFloat(base, time + 76)) : 0.0f, context ? Get32(base, context + 448) : 0,
      tick::Current().index, tick::Current().gate, tick::Current().ticks, tick::Current().skipped, mounts, probe_units,
      range_count, note_count, distinct_count, hashed, formats, pairs[1], pairs[0]));
  last_record = us;
  last_displays = displays;
  last_cameras = cameras;
  last_main_steps = main_steps;
}
}  // namespace

bool Enabled() {
  if (enabled < 0) {
    const char* text = std::getenv("REX_NATIVE_MOTION_RECORD");
    enabled = Diagnostics() && text && std::string_view(text) == "1" ? 1 : 0;
    if (enabled) {
      if (const char* probe = std::getenv("REX_NATIVE_MATRIX_PROBE"); probe && *probe)
        probe_units = std::strtof(probe, nullptr);
      if (const char* dir = std::getenv("REX_NATIVE_FRAME_DIR"); dir && *dir) {
        file = std::fopen(fmt::format("{}/motion.csv", dir).c_str(), "w");
        if (file) std::fputs("display,us,iteration,gate,tick,alpha,cut,kind,id,x,y,z,a,b,c\n", file);
      }
    }
  }
  return enabled == 1;
}

void LoopStart(uint8_t* base) {
  if (!Enabled()) return;
  iteration_tick = tick::Current().tick;
  const uint64_t index = ++loop_index;
  {
    std::lock_guard lock(ranges_mutex);
    for (auto it = ranges.begin(); it != ranges.end();)
      it = index - it->second.seen > 300 ? ranges.erase(it) : std::next(it);
  }
  StageHash(base, kLoop);
}

void MainStepDone(uint8_t* base) {
  if (!Enabled()) return;
  if (++main_steps % 120 == 0) Record(base);
}

void NoteVertexTexture(const std::array<uint32_t, 6>& d) {
  const uint32_t address = d[1] & 0xFFFFF000u, format = d[1] & 0x3F, dimension = (d[5] >> 9) & 3;
  const GuestFormat* guest = FindGuestFormat(format);
  if (!address || !guest || guest->block != 1) return;
  const uint64_t texels = dimension == 0 ? (d[2] & 0xFFFFFF) + 1ull
                                         : ((d[2] & 0x1FFF) + 1ull) * (((d[2] >> 13) & 0x1FFF) + 1ull);
  const uint32_t bytes = uint32_t(std::min<uint64_t>(texels * guest->bytes_per_block, kRangeBytesMax));
  std::lock_guard lock(ranges_mutex);
  ++notes;
  auto it = ranges.find(address);
  if (it == ranges.end()) {
    if (ranges.size() >= kRangesMax) return;
    ++distinct;
    it = ranges.emplace(address, Range{bytes, format}).first;
  }
  it->second.bytes = bytes;
  it->second.seen = loop_index.load();
}
}  // namespace gpu::motion

using namespace gpu;
using namespace gpu::motion;
REX_HOOK_RAW(sub_8265A058) { Display(ctx, base); }
REX_HOOK_RAW(sub_8267A2A0) {
  if (Enabled()) ++cameras;
  __imp__sub_8267A2A0(ctx, base);
}
REX_HOOK_RAW(sub_825E2A08) {
  if (Enabled() && in_display) {
    const uint32_t holder = ctx.r3.u32, count = Get32(base, holder + 8), array = Get32(base, holder + 4);
    for (uint32_t i = 0; array && i < count && i < 65536; ++i) {
      const uint32_t object = Get32(base, array + 4 * i);
      if (!object) continue;
      const uint32_t flags = Get32(base, object + 4);
      if (!(flags & 0x00040000) || (flags & 0x00200004)) continue;
      ++mounted;
      ++mounts;
      if (!live_set.count(object)) ++mounted_outside;
    }
  }
  __imp__sub_825E2A08(ctx, base);
}
REX_HOOK_RAW(sub_8259FF40) {
  __imp__sub_8259FF40(ctx, base);
  if (Enabled()) StageHash(base, kDelivered);
}
REX_HOOK_RAW(sub_820FC008) {
  if (Enabled()) StageHash(base, kListsIn);
  __imp__sub_820FC008(ctx, base);
  if (Enabled()) StageHash(base, kListsOut);
}
