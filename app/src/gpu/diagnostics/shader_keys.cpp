// The shader keys of the Jade engine and the materials behind them
// (shader_keys.h). The key classes share their first three methods: 820F7300
// returns the 64-bit key at +8, 820F71E8 its low byte (the category of the
// shader bank) and 820F72F8 sets it; the fourth or fifth, per class, writes the
// shader defines of its bits (REA/IDA, 2026-10-17). The constructors read the
// current render slot of the renderer (82C0F020: slot = base + 3208 * index at
// +6420), its state block (+3060, 224 bytes, created by 820FBCE0), the
// material the slot's state stack holds (820FFD30: flags at +0, texture ids as
// int16 at +4..+18) and the texture set of the block (+12, flags at +32/+36).
// The hooks run with the diagnostics only; otherwise they call the originals.
#include "gpu/diagnostics/shader_keys.h"
#include "gpu/diagnostics/diagnostics.h"
#include "gpu/common/guest_memory.h"
#include "gpu/common/hash.h"
#include "gpu/shaders/shaders.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>
#include <fmt/format.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>

extern "C" void __imp__sub_820F5A80(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_820F5B68(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_820FFD30(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82105648(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82107498(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_821039A0(PPCContext& __restrict, uint8_t*);

namespace gpu {
namespace {
constexpr uint32_t kRendererGlobal = 0x82C0F020, kSlotStride = 3208, kSlotIndex = 6420, kSlotBlock = 3060;
constexpr uint32_t kBlockBytes = 224, kMaterialBytes = 32, kTexsetBytes = 64;

struct Lookup {
  uint32_t lr = 0, vtable = 0, object = 0;
  uint64_t key = 0;
};
// One material capture: the bytes the constructor read, by category.
struct Material {
  uint32_t category = 0, lr = 0;
  std::vector<uint8_t> material, block, texset;
  // (VS key, PS key) -> count
  std::map<std::pair<uint64_t, uint64_t>, uint64_t> keys;
  uint64_t count = 0;
};
struct Keys {
  std::mutex mutex;
  // (vertex, caller, key class, key) -> (D3D shader object, lookups).
  std::map<std::tuple<bool, uint32_t, uint32_t, uint64_t>, std::pair<uint32_t, uint64_t>> lookups;
  // (VS caller, VS key class, VS key, PS key class, PS key) -> (VS object, PS object, pairs).
  std::map<std::tuple<uint32_t, uint32_t, uint64_t, uint32_t, uint64_t>, std::tuple<uint32_t, uint32_t, uint64_t>>
      pairs;
  // (category, FNV of the captured bytes) -> material.
  std::map<std::pair<uint32_t, uint64_t>, Material> materials;
  uint64_t calls[2] = {}, unpaired = 0, captures = 0, captures_without_keys = 0, captures_without_block = 0;
} keys;
// The last lookups of the thread: the PS lookup pairs with the VS one of the
// same caller (and consumes it); a constructor's capture takes the last VS and
// PS lookups whatever the pairing did.
thread_local Lookup last_vs, last_ps, capture_vs;
thread_local bool last_vs_valid = false, last_ps_valid = false, capture_vs_valid = false;

void Note(bool vertex, const Lookup& l) {
  std::lock_guard lock(keys.mutex);
  ++keys.calls[vertex];
  auto& lookup = keys.lookups[{vertex, l.lr, l.vtable, l.key}];
  lookup.first = l.object;
  ++lookup.second;
  if (vertex) {
    if (last_vs_valid) ++keys.unpaired;
    last_vs = capture_vs = l;
    last_vs_valid = capture_vs_valid = true;
    last_ps_valid = false;
    return;
  }
  last_ps = l;
  last_ps_valid = true;
  // A caller looks its PS up a few instructions after its VS.
  if (last_vs_valid && l.lr > last_vs.lr && l.lr - last_vs.lr < 0x200) {
    auto& pair = keys.pairs[{last_vs.lr, last_vs.vtable, last_vs.key, l.vtable, l.key}];
    pair = {last_vs.object, l.object, std::get<2>(pair) + 1};
  } else {
    ++keys.unpaired;
  }
  last_vs_valid = false;
}
uint64_t Get64(uint8_t* base, uint32_t a) { return uint64_t(Get32(base, a)) << 32 | Get32(base, a + 4); }
std::vector<uint8_t> GuestBytes(uint8_t* base, uint32_t address, uint32_t size) {
  std::vector<uint8_t> out;
  if (!address || address < 0x1000 || size_t(address) + size > 0xFFFFFFFFull) return out;
  out.assign(base + address, base + address + size);  // Big-endian bytes as the title keeps them.
  return out;
}
std::string Hex(const std::vector<uint8_t>& bytes) {
  std::string out;
  out.reserve(bytes.size() * 2);
  for (uint8_t b : bytes) out += fmt::format("{:02x}", b);
  return out;
}

// After a constructor of a big category ran on this thread: the material the
// slot's state stack holds (through the title's own accessor, with the
// volatile registers and the link register restored after), the slot's state
// block and the texture set, keyed by their bytes, with the keys the
// constructor looked up (the last lookups of the thread).
void NoteMaterial(PPCContext& ctx, uint8_t* base, uint32_t category, uint32_t lr) {
  const uint32_t renderer = Get32(base, kRendererGlobal);
  if (!renderer) return;
  const uint32_t slot = renderer + kSlotStride * Get32(base, renderer + kSlotIndex);
  const uint32_t block = Get32(base, slot + kSlotBlock);
  Material m;
  m.category = category;
  m.lr = lr;
  if (block) {
    const PPCRegister saved[] = {ctx.r3, ctx.r4, ctx.r5, ctx.r6, ctx.r7, ctx.r8, ctx.r9, ctx.r10, ctx.r11, ctx.r12};
    const uint64_t saved_lr = ctx.lr;
    const PPCCRRegister saved_cr[] = {ctx.cr0, ctx.cr1, ctx.cr5, ctx.cr6, ctx.cr7};
    ctx.r3.u32 = block;
    __imp__sub_820FFD30(ctx, base);
    const uint32_t material = ctx.r3.u32;
    ctx.r3 = saved[0]; ctx.r4 = saved[1]; ctx.r5 = saved[2]; ctx.r6 = saved[3]; ctx.r7 = saved[4];
    ctx.r8 = saved[5]; ctx.r9 = saved[6]; ctx.r10 = saved[7]; ctx.r11 = saved[8]; ctx.r12 = saved[9];
    ctx.lr = saved_lr;
    ctx.cr0 = saved_cr[0]; ctx.cr1 = saved_cr[1]; ctx.cr5 = saved_cr[2]; ctx.cr6 = saved_cr[3]; ctx.cr7 = saved_cr[4];
    m.material = GuestBytes(base, material, kMaterialBytes);
    m.block = GuestBytes(base, block, kBlockBytes);
    m.texset = GuestBytes(base, Get32(base, block + 12), kTexsetBytes);
  }
  uint64_t hash = Fnv64(reinterpret_cast<const uint8_t*>(&category), 4, 0xCBF29CE484222325ull);
  hash = Fnv64(reinterpret_cast<const uint8_t*>(m.material.data()), m.material.size(), hash);
  hash = Fnv64(reinterpret_cast<const uint8_t*>(m.block.data()), m.block.size(), hash);
  hash = Fnv64(reinterpret_cast<const uint8_t*>(m.texset.data()), m.texset.size(), hash);
  const bool with_keys = capture_vs_valid || last_ps_valid;
  const uint64_t vs = capture_vs_valid ? capture_vs.key : 0, ps = last_ps_valid ? last_ps.key : 0;
  capture_vs_valid = last_ps_valid = false;  // Each constructor call takes its own lookups.
  std::lock_guard lock(keys.mutex);
  ++keys.captures;
  if (!block) ++keys.captures_without_block;
  if (!with_keys) ++keys.captures_without_keys;
  auto [it, inserted] = keys.materials.try_emplace({category, hash});
  if (inserted) it->second = std::move(m);
  ++it->second.count;
  if (with_keys) ++it->second.keys[{vs, ps}];
}

// Before the lookup: the caller and the key object (r4); after it: the
// wrapper it returns (r3), whose +4 is the D3D shader object.
void NoteShaderKey(uint8_t* base, bool vertex, uint32_t lr, uint32_t key_object, uint32_t wrapper) {
  if (!key_object) return;
  Note(vertex, {lr, Get32(base, key_object), wrapper ? Get32(base, wrapper + 4) : 0, Get64(base, key_object + 8)});
}
}  // namespace

std::string RecordShaderKeys() {
  if (!Diagnostics()) return {};
  decltype(keys.lookups) lookups;
  decltype(keys.pairs) pairs;
  std::vector<std::pair<std::pair<uint32_t, uint64_t>, const Material*>> materials;
  uint64_t calls[2], unpaired, captures, without_keys, without_block;
  std::unique_lock lock(keys.mutex);  // Held while the materials are formatted (pointers into the map).
  lookups = keys.lookups;
  pairs = keys.pairs;
  for (const auto& [k, v] : keys.materials) materials.push_back({k, &v});
  calls[0] = keys.calls[0];
  calls[1] = keys.calls[1];
  unpaired = keys.unpaired;
  captures = keys.captures;
  without_keys = keys.captures_without_keys;
  without_block = keys.captures_without_block;
  std::string text = "{\"lookups\":[";
  bool first = true;
  for (const auto& [k, v] : lookups) {
    const auto& [vertex, lr, vtable, key] = k;
    text += fmt::format("{}{{\"stage\":\"{}\",\"lr\":\"{:08X}\",\"class\":\"{:08X}\",\"key\":\"{:016x}\",\"category\":{},"
                        "\"name\":\"{}\",\"object\":{},\"count\":{}}}", first ? "" : ",", vertex ? "vs" : "ps", lr,
                        vtable, key, key & 0xFF, ShaderNameOf(v.first, vertex), v.first, v.second);
    first = false;
  }
  text += "],\"pairs\":[";
  first = true;
  for (const auto& [k, v] : pairs) {
    const auto& [lr, vs_class, vs_key, ps_class, ps_key] = k;
    const auto& [vs_object, ps_object, count] = v;
    text += fmt::format("{}{{\"lr\":\"{:08X}\",\"vs_class\":\"{:08X}\",\"vs_key\":\"{:016x}\",\"vs\":\"{}\","
                        "\"ps_class\":\"{:08X}\",\"ps_key\":\"{:016x}\",\"ps\":\"{}\",\"count\":{}}}", first ? "" : ",", lr,
                        vs_class, vs_key, ShaderNameOf(vs_object, true), ps_class, ps_key,
                        ShaderNameOf(ps_object, false), count);
    first = false;
  }
  text += "],\"materials\":[";
  first = true;
  for (const auto& [k, m] : materials) {
    std::string pairs_text;
    for (const auto& [kk, count] : m->keys)
      pairs_text += fmt::format("{}{{\"vs_key\":\"{:016x}\",\"ps_key\":\"{:016x}\",\"count\":{}}}",
                                pairs_text.empty() ? "" : ",", kk.first, kk.second, count);
    text += fmt::format("{}{{\"category\":{},\"hash\":\"{:016x}\",\"lr\":\"{:08X}\",\"count\":{},\"material\":\"{}\","
                        "\"block\":\"{}\",\"texset\":\"{}\",\"keys\":[{}]}}", first ? "" : ",", m->category, k.second,
                        m->lr, m->count, Hex(m->material), Hex(m->block), Hex(m->texset), pairs_text);
    first = false;
  }
  text += "]}";
  lock.unlock();
  std::string file = "null";
  if (const char* dir = std::getenv("REX_NATIVE_FRAME_DIR"); dir && *dir) {
    std::string path = std::string(dir) + "/shader-keys.json";
    std::replace(path.begin(), path.end(), '\\', '/');
    if (std::FILE* out = std::fopen(path.c_str(), "wb")) {
      std::fwrite(text.data(), 1, text.size(), out);
      std::fclose(out);
      file = "\"" + path + "\"";
    }
  }
  return fmt::format("\"vs_lookups\":{},\"ps_lookups\":{},\"distinct\":{},\"pairs\":{},\"unpaired\":{},\"materials\":{},"
                     "\"captures\":{},\"captures_without_keys\":{},\"captures_without_block\":{},\"file\":{}",
                     calls[1], calls[0], lookups.size(), pairs.size(), unpaired, materials.size(), captures,
                     without_keys, without_block, file);
}
}  // namespace gpu

REX_HOOK_RAW(sub_820F5A80) {
  if (!gpu::Diagnostics()) return __imp__sub_820F5A80(ctx, base);
  const uint32_t lr = ctx.lr, key_object = ctx.r4.u32;
  __imp__sub_820F5A80(ctx, base);
  gpu::NoteShaderKey(base, true, lr, key_object, ctx.r3.u32);
}
REX_HOOK_RAW(sub_820F5B68) {
  if (!gpu::Diagnostics()) return __imp__sub_820F5B68(ctx, base);
  const uint32_t lr = ctx.lr, key_object = ctx.r4.u32;
  __imp__sub_820F5B68(ctx, base);
  gpu::NoteShaderKey(base, false, lr, key_object, ctx.r3.u32);
}
// The constructors of the big categories: the capture after each.
REX_HOOK_RAW(sub_82105648) {
  if (!gpu::Diagnostics()) return __imp__sub_82105648(ctx, base);
  const uint32_t lr = ctx.lr;
  __imp__sub_82105648(ctx, base);
  gpu::NoteMaterial(ctx, base, 0x5, lr);
}
REX_HOOK_RAW(sub_82107498) {
  if (!gpu::Diagnostics()) return __imp__sub_82107498(ctx, base);
  const uint32_t lr = ctx.lr;
  __imp__sub_82107498(ctx, base);
  gpu::NoteMaterial(ctx, base, 0xa, lr);
}
REX_HOOK_RAW(sub_821039A0) {
  if (!gpu::Diagnostics()) return __imp__sub_821039A0(ctx, base);
  const uint32_t lr = ctx.lr;
  __imp__sub_821039A0(ctx, base);
  gpu::NoteMaterial(ctx, base, 0xe, lr);
}
