// narutorise - ReXGlue Recompiled Project
//
// Race on the save thumbnail (save_thumbnail.cpp).

#pragma once

#include <cstdint>

namespace narutorise::save_thumbnail {
// Every free of the engine (820C0018, the memory in r4; engine_free.cpp): a
// requested thumbnail freed before its capture is not captured.
void NoteFree(uint32_t pointer);
}  // namespace narutorise::save_thumbnail
