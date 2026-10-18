#pragma once

// The shader keys of the Jade engine and the materials behind them
// (diagnostics, shader_keys.cpp). A draw looks its shaders up by key: 820F5A80
// (VS) and 820F5B68 (PS) take a key object (vtable at +0, the 64-bit key at
// +8, its low byte the category) and return the shader wrapper (+4: the D3D
// shader object). The keys of the three big categories are built by one
// constructor each (0x5: 82105648, 0xa: 82107498, 0xe: 821039A0, dispatched
// from the table 821025F0 fills) out of the material of the current render
// slot and its render-state block. With the diagnostics on, each lookup is
// noted with its caller, a PS lookup right after a VS lookup of the same caller
// on the same thread makes a pair, and each constructor call captures the
// material record, the state block and the texture set with the keys it
// produced: the input of the prediction at loading (phase 3).
#include <string>

namespace gpu {
// Writes shader-keys.json into REX_NATIVE_FRAME_DIR (the lookups, the pairs
// and the materials, with the cache names of their shaders) and returns the
// summary as JSON fields; empty without the diagnostics.
std::string RecordShaderKeys();
}  // namespace gpu
