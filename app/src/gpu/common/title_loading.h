// The title's loading screen. TitleLoading is true from the call that shows
// it to the return of the call that hides it (sub_82685460 and sub_82684C48,
// hooked in host/title_loading.cpp): the equivalent of the loading flag the
// recomps read to raise the priority of the pipeline compilation while the
// player waits anyway. Header only, so the backend reads it without depending
// on the host.
#pragma once

#include <atomic>
#include <cstdint>

namespace gpu {
inline std::atomic<bool> title_loading{false};
inline std::atomic<uint32_t> title_loadings{0};  // Loading screens shown since the start.
inline bool TitleLoading() { return title_loading.load(std::memory_order_relaxed); }
}  // namespace gpu
