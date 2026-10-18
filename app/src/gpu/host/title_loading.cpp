// What the title is loading (common/title_loading.h): its loading screen and
// its worlds.
//
// The screen is an object with its own thread (sub_82685AB8, a loop that
// lives the whole session and ticks the "Menu/LoadingNoLoc" movie while a load
// runs). sub_82685460 shows it: it wakes the thread and returns once the thread
// took over; sub_82684C48 hides it: it stops the thread's loop and waits for it
// (REA/IDA, 2026-10-17; callers: the world change, the menu flows and the
// fights). The hooks bracket the two with the flag and, in the native mode,
// record each loading with its Swaps and duration.
//
// sub_8264B3B0 is the loader of a world file (".wow") the title's loader pump
// (sub_8255AE00) calls with the file's key (r3) for every world requested:
// the world change by script (sub_8264C358, "Loading world: 0x%08X", through
// sub_82665F18) and the loads of a save and of a fight, which do not pass
// there (increment323 recorded none of them). It reads the file, builds the
// world and queues its object list and grids. The hook notes the key before
// the load, so the pipelines of the first draws of the world are recorded with
// it, and posts the pipelines the list knows for that world to the asynchronous
// threads (shaders/pipelines.h), which compile at the highest priority while
// the loading screen is up. A world is some sixty .wow files loaded in a burst
// (increment324: 66 at the boot, 51 for the village, 17 for an arena), so the
// key that identifies the world is the last one of its burst: the pipelines
// are attributed to it, and its load posts them.
#include "gpu/common/title_loading.h"
#include "gpu/host/services.h"
#include "gpu/d3d/d3d.h"
#include "gpu/d3d/frame.h"
#include "gpu/shaders/pipelines.h"
#include <chrono>
#include <fmt/format.h>
#include <rex/hook.h>

extern "C" void __imp__sub_82685460(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82684C48(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_8264B3B0(PPCContext& __restrict, uint8_t*);

namespace {
std::atomic<uint64_t> loading_swap_begin{0};
std::atomic<int64_t> loading_began_ns{0};
}  // namespace

REX_HOOK_RAW(sub_82685460) {
  gpu::title_loadings.fetch_add(1, std::memory_order_relaxed);
  loading_swap_begin.store(gpu::swaps.load(std::memory_order_relaxed), std::memory_order_relaxed);
  loading_began_ns.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
  gpu::title_loading.store(true, std::memory_order_relaxed);
  __imp__sub_82685460(ctx, base);
}

REX_HOOK_RAW(sub_82684C48) {
  __imp__sub_82684C48(ctx, base);
  const bool was_loading = gpu::title_loading.exchange(false, std::memory_order_relaxed);
  if (!was_loading || !gpu::Active()) return;
  const auto began = std::chrono::steady_clock::time_point(
      std::chrono::steady_clock::duration(loading_began_ns.load(std::memory_order_relaxed)));
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began);
  gpu::host::Record("native-d3d-title-loading", fmt::format("\"number\":{},\"ms\":{},\"swap_begin\":{},\"swap_end\":{}",
                                                            gpu::title_loadings.load(std::memory_order_relaxed), ms.count(),
                                                            loading_swap_begin.load(std::memory_order_relaxed),
                                                            gpu::swaps.load(std::memory_order_relaxed)));
}

REX_HOOK_RAW(sub_8264B3B0) {
  const uint32_t key = ctx.r3.u32;
  if (key == 0 || key == 0xFFFFFFFFu) return __imp__sub_8264B3B0(ctx, base);
  gpu::world_key.store(key, std::memory_order_relaxed);
  const uint32_t number = gpu::world_loads.fetch_add(1, std::memory_order_relaxed) + 1;
  const bool active = gpu::Active();
  const uint64_t swap = gpu::swaps.load(std::memory_order_relaxed);
  const auto began = std::chrono::steady_clock::now();
  const gpu::WorldPrefetch prefetch = active ? gpu::PrefetchWorldPipelines(key) : gpu::WorldPrefetch{};
  __imp__sub_8264B3B0(ctx, base);
  // A world is some sixty files (its sectors); only the ones the list knows
  // are recorded. The pipelines by world are in pipelines.json.
  if (!active || !prefetch.listed) return;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began);
  gpu::host::Record("native-d3d-world-loading",
                    fmt::format("\"number\":{},\"key\":\"{:08x}\",\"loaded\":{},\"ms\":{},\"swap\":{},"
                                "\"title_loading\":{},\"listed\":{},\"posted\":{},\"present\":{},\"unresolved\":{}",
                                number, key, ctx.r3.u32 != 0, ms.count(), swap, gpu::TitleLoading(), prefetch.listed,
                                prefetch.posted, prefetch.present, prefetch.unresolved));
}
