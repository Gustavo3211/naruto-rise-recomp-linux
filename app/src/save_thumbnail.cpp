// narutorise - ReXGlue Recompiled Project
//
// Race on the save thumbnail.
//
// The render task of the capture (sub_8211D878, this[14] = the thumbnail
// structure) runs D3DXSaveTextureToFileInMemory on the reduced capture and
// calls the compression sub_82583C28(thumb): free(thumb[3]),
// thumb[3] = malloc(...), and zlib writes the thumbnail into thumb[3] + 4; at
// the end, thumb[4] = 1 (ready). The thread that writes the save reads
// thumb[3]/thumb[2] and, when done, calls sub_82583D28(thumb), which frees
// thumb[1] and thumb[3] and clears them.
//
// On an autosave the task runs again over the same thumbnail, ready and not
// yet consumed, while the save is being written: the second compression frees
// and reallocates thumb[3] (dlmalloc returns the same chunk) and the save
// writer frees that chunk in the middle of zlib, which keeps writing into it
// and corrupts the game's heap (memcpy <- deflate <- compress <- sub_82583C28
// <- sub_8211D878, about 1 ms apart). With a ready, unconsumed thumbnail
// (thumb[4] = 1 and thumb[3] != 0) the capture is not redone: the save keeps
// the thumbnail it is already writing, and the task returns as if the capture
// had finished (the two fields the function clears on entry and the return 1
// of its success path). Once the save writer frees the thumbnail
// (thumb[3] = 0), the capture runs again.

#include <atomic>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>

#include "generated/default/narutorise_init.h"

namespace {

extern "C" void __imp__sub_8211D878(PPCContext& __restrict, uint8_t*);

std::atomic<uint32_t> skipped_captures{0};

}  // namespace

REX_HOOK_RAW(sub_8211D878) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t thumb = REX_LOAD_U32(self + 56);
  if (thumb && REX_LOAD_U32(thumb + 16) == 1 && REX_LOAD_U32(thumb + 12) != 0) {
    REX_STORE_U32(self + 8, 0);
    REX_STORE_U32(self + 60, 0);
    ctx.r3.u64 = 1;
    const uint32_t count = skipped_captures.fetch_add(1) + 1;
    if (count <= 8)
      REXLOG_INFO("[narutorise] save thumbnail capture skipped: thumbnail {:08X} still in use (#{})", thumb,
                  count);
    return;
  }
  __imp__sub_8211D878(ctx, base);
}
