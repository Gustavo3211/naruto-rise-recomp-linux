// Guest functions of the graphics library that the native mode replaces or
// guards: the internals of the device creation (the D3D boundary projects
// them), the interrupt dispatch, the memory dispatcher of the creation, the
// last-error helper and the legacy consumers of the command ring.
#include "gpu/host/internal.h"
#include "gpu/common/guest_memory.h"
#include "gpu/d3d/d3d.h"
#include <array>
#include <fmt/format.h>
#include <rex/hook.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>
#include <rex/system/xmemory.h>

using namespace gpu::host;

extern "C" void __imp__sub_8212FA80(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_8217DDB0(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_821480D0(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_82131428(PPCContext& __restrict, uint8_t*);

// Device creation (8213ECE8) internals: the D3D boundary replaces them
// (gpu::Device*); outside the native mode the original runs.
REX_HOOK_RAW(sub_8212FA80) {
  if (gpu::Active()) {
    gpu::DeviceArena(ctx, base);
    return;
  }
  __imp__sub_8212FA80(ctx, base);
}

REX_HOOK_RAW(sub_821480D0) {
  if (gpu::Active()) {
    gpu::DeviceWorkers(ctx, base);
    return;
  }
  __imp__sub_821480D0(ctx, base);
}

REX_HOOK_RAW(sub_82131428) {
  if (gpu::Active()) {
    gpu::DeviceCache(ctx, base);
    return;
  }
  __imp__sub_82131428(ctx, base);
}

extern "C" void __imp__sub_821483D0(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_821483D0) {
  if (gpu::Active()) {
    gpu::DeviceInitializer(ctx, base);
    return;
  }
  __imp__sub_821483D0(ctx, base);
}

extern "C" void __imp__sub_8213E8F0(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8213E8F0) {
  if (gpu::Active()) {
    gpu::DeviceVideo(ctx, base);
    return;
  }
  __imp__sub_8213E8F0(ctx, base);
}

extern "C" void __imp__sub_8213E610(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8213E610) {
  if (gpu::Active()) {
    gpu::DeviceDefaults(ctx, base);
    return;
  }
  __imp__sub_8213E610(ctx, base);
}

// A registered interrupt handler is not an admitted guest dispatch: the own
// Swap paces itself and no own interrupt is delivered. In particular, the
// legacy scratch-backed callback sentinel is not a receipt.
extern "C" void __imp__sub_8212F950(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8212F950) {
  if (enabled.load())
    Stop("2F950-guest-interrupt-dispatch-uncovered", &ctx, ctx.r3.u32, ctx.r4.u32, 88);
  __imp__sub_8212F950(ctx, base);
}
// The memory dispatcher at the natural caller of the device creation (LR
// 8213EE0C); its other uses are unchanged, including explicit owner free calls.
REX_HOOK_RAW(sub_8217DDB0) {
  if (gpu::Active() && ctx.lr == 0x8213EE0C) {
    gpu::DeviceMemory(ctx, base);
    return;
  }
  __imp__sub_8217DDB0(ctx, base); // Earlier memory paths retain original behavior.
}

// DD18 -> 80568 reaches this tail helper on allocation failure. F558 may
// mutate KTHREAD.last_error; restoring PPCContext cannot undo that store.
// Stop before its effects; this inner frame cannot prove joins or cleanup.
extern "C" void __imp__sub_8217F550(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8217F550) {
  if (enabled.load()) {
    // 80568's allocation failure is the guarded inner frame. This same pure
    // CPU helper is also used to restore per-thread last_error on normal paths.
    if (ctx.lr == 0x821805D4)
      Stop("native-allocation-error-helper-uncovered", &ctx, ctx.r3.u32, ctx.lr, 88);
    auto* ts = rex::runtime::ThreadState::Get();
    auto* thread = rex::system::XThread::GetCurrentThread();
    const uint32_t pcr = ctx.r13.u32, lr = ctx.lr, value = ctx.r3.u32;
    std::array<uint8_t, 4> raw_dpc{}, raw_thread{}, raw_error{};
    if (!ts || !thread || ts->context() != &ctx || ts->memory()->virtual_membase() != base ||
        pcr != thread->pcr_ptr() ||
        !Read(ts->memory(), pcr + 336, raw_dpc.data(), 4) ||
        !Read(ts->memory(), pcr + 256, raw_thread.data(), 4) ||
        gpu::BigEndianWord(raw_thread.data()) != thread->guest_object() ||
        !Read(ts->memory(), thread->guest_object() + 352, raw_error.data(), 4))
      Stop("cpu-last-error-context-uncovered", &ctx, 0, 0, 88);
    const uint32_t before = gpu::BigEndianWord(raw_error.data()), dpc = gpu::BigEndianWord(raw_dpc.data());
    __imp__sub_8217F550(ctx, base); // Preserve original CPU write and volatile ABI effects.
    std::array<uint8_t, 4> after{};
    const bool valid = ctx.lr == lr && ctx.r13.u32 == pcr &&
        Read(ts->memory(), thread->guest_object() + 352, after.data(), 4) &&
        gpu::BigEndianWord(after.data()) == (dpc ? before : value);
    if (KeepRecord(last_error_records)) Record("native-cpu-last-error-preserved", fmt::format(
        "\"lr\":{},\"guest_thread\":{},\"cpu\":{},\"pcr\":{},\"kthread\":{},"
        "\"code\":{},\"dpc_active\":{},\"before\":{},\"after\":{},\"original_executed\":true,\"effect_verified\":{}",
        lr, thread->thread_id(), thread->active_cpu(), pcr, thread->guest_object(),
        value, dpc, before, gpu::BigEndianWord(after.data()), valid));
    if (!valid) Stop("cpu-last-error-effect-unproved", &ctx, 0, 0, 88);
    return;
  }
  __imp__sub_8217F550(ctx, base);
}

#define GUARDED_LEGACY(name) \
  extern "C" void __imp__##name(PPCContext& __restrict, uint8_t*); \
  REX_HOOK_RAW(name) { \
    if (enabled.load()) Stop("legacy-consumer-" #name, &ctx); \
    __imp__##name(ctx, base); \
  }
GUARDED_LEGACY(sub_8212EE38)
GUARDED_LEGACY(sub_8212EF28)
GUARDED_LEGACY(sub_8212ED70)
extern "C" void __imp__sub_8212F608(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8212F608) {
  if (gpu::Active()) {
    gpu::RingReserve(ctx, base);
    return;
  }
  __imp__sub_8212F608(ctx,base);
}
GUARDED_LEGACY(sub_8212F530)
// 8212F7E0 only writes packets at its cursor argument; inside an own discard
// scope of the D3D boundary those packets reach no consumer.
extern "C" void __imp__sub_8212F7E0(PPCContext& __restrict, uint8_t*);
REX_HOOK_RAW(sub_8212F7E0) {
  if (enabled.load() && !gpu::AdmitDiscardLeaf(ctx)) Stop("legacy-consumer-sub_8212F7E0", &ctx);
  __imp__sub_8212F7E0(ctx, base);
}
GUARDED_LEGACY(sub_82146C48)
GUARDED_LEGACY(sub_82147C98)
GUARDED_LEGACY(sub_82147890)
GUARDED_LEGACY(sub_821476E8)
// F950 uses the explicit interrupt-dispatch guard above.
GUARDED_LEGACY(sub_82131140)
GUARDED_LEGACY(sub_82131DD8)
GUARDED_LEGACY(sub_8213EF10)
#undef GUARDED_LEGACY
