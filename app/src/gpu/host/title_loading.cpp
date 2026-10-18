// The title's loading screen (common/title_loading.h). The screen is an
// object with its own thread (sub_82685AB8, a loop that lives the whole
// session and ticks the "Menu/LoadingNoLoc" movie while a load runs).
// sub_82685460 shows it: it wakes the thread and returns once the thread took
// over; sub_82684C48 hides it: it stops the thread's loop and waits for it
// (REA/IDA, 2026-10-17; callers: the world change, the menu flows and the
// fights). The hooks bracket the two with the flag and, in the native mode,
// record each loading with its Swaps and duration.
#include "gpu/common/title_loading.h"
#include "gpu/host/services.h"
#include "gpu/d3d/d3d.h"
#include "gpu/d3d/frame.h"
#include <chrono>
#include <fmt/format.h>
#include <rex/hook.h>

extern "C" void __imp__sub_82685460(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82684C48(PPCContext& __restrict, uint8_t*);

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
