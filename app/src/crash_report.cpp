// narutorise - ReXGlue Recompiled Project
//
// Crash report of a guest fault and the probe of the object link chain
// (crash_report.h).
//
// The guest stack: a function saves its return address at -8 of the caller's
// r1 and links the frames with the back chain at [r1]. From the r1 of the
// faulting thread, the caller's r1 is [r1] and the return address that frame
// saved is [[r1] - 8]; the walk stops at the thread's entry (back chain 0) or
// at anything that is not a frame above the last one. Equal addresses in a row
// are counted (a recursion shows as one address times its depth).

#include "crash_report.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include <fmt/format.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

extern "C" void __imp__sub_8266AE68(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_8264C358(PPCContext& __restrict, uint8_t*);

namespace narutorise::crash_report {
namespace {
constexpr uint64_t kGuestArena = 0x100000000ull;  // The virtual base and the 4 GB after it.
constexpr uint32_t kMaxFrames = 65536;
constexpr uint32_t kMaxRuns = 96;
constexpr uint32_t kMaxReports = 4;
constexpr uint32_t kChainMax = 64;
// The dlmalloc state of the game's main heap (the one of the known heap
// corruption): smallmap +0, treemap +4, dvsize +8, topsize +12, least_addr +16,
// dv +20, top +24, the 32 small bins from +36 (8 bytes each: the head is a
// pseudo chunk whose fd is at +8 and bk at +12), the segment list from +444
// (base, size, next, flags).
constexpr uint32_t kHeapState = 0x82701E04;
constexpr uint32_t kSmallBins = 32;
constexpr uint32_t kMaxSegments = 32;

std::atomic<uint32_t> reports{0};
thread_local bool reporting = false;

// A guarded big-endian read of guest memory: false where there is no memory.
bool Read32(const uint8_t* base, uint32_t address, uint32_t* out) {
  __try {
    *out = _byteswap_ulong(*reinterpret_cast<const uint32_t*>(base + address));
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

struct Run {
  uint32_t address, count;
};

// The game's heap at the fault: the dlmalloc state, its segments and the
// small bins that hold something, with the fault placed among them. The
// allocator only checks a bin pointer against least_addr; a failed check
// traps (type 22), keeps the chunk in the bin and hands it out again, and a
// later pointer above least_addr is written through (fd at +8, bk at +12).
// So a fault at +12 of a garbage pointer names the bin whose head holds it.
void HeapReport(const uint8_t* base, uint32_t fault) {
  uint32_t smallmap = 0, treemap = 0, dvsize = 0, topsize = 0, least = 0, dv = 0, top = 0;
  if (!Read32(base, kHeapState, &smallmap) || !Read32(base, kHeapState + 4, &treemap) ||
      !Read32(base, kHeapState + 8, &dvsize) || !Read32(base, kHeapState + 12, &topsize) ||
      !Read32(base, kHeapState + 16, &least) || !Read32(base, kHeapState + 20, &dv) ||
      !Read32(base, kHeapState + 24, &top)) {
    REXLOG_ERROR("[narutorise] crash report: heap state 0x{:08X} unreadable", kHeapState);
    return;
  }
  std::string segments;
  bool inside = false;
  uint32_t segment = kHeapState + 444;
  for (uint32_t i = 0; i < kMaxSegments && segment; ++i) {
    uint32_t sbase = 0, size = 0, next = 0, flags = 0;
    if (!Read32(base, segment, &sbase) || !Read32(base, segment + 4, &size) || !Read32(base, segment + 8, &next) ||
        !Read32(base, segment + 12, &flags))
      break;
    if (!sbase && !size) break;
    const bool holds = fault >= sbase && fault < sbase + size;
    inside = inside || holds;
    segments += fmt::format("{}{:08X}..{:08X}/{:X}{}", i ? " " : "", sbase, sbase + size, flags, holds ? "<-fault" : "");
    segment = next;
  }
  REXLOG_ERROR(
      "[narutorise] crash report: heap 0x{:08X}: smallmap 0x{:08X} treemap 0x{:08X} least_addr 0x{:08X} dv 0x{:08X} "
      "({}) top 0x{:08X} ({}); fault {} the segments: {}",
      kHeapState, smallmap, treemap, least, dv, dvsize, top, topsize, inside ? "inside" : "outside", segments);
  std::string bins;
  for (uint32_t index = 0; index < kSmallBins; ++index) {
    const uint32_t head = kHeapState + 36 + index * 8;
    uint32_t fd = 0, bk = 0;
    if (!Read32(base, head + 8, &fd) || !Read32(base, head + 12, &bk)) break;
    if (fd == head && bk == head) continue;
    const bool names = fd == fault - 12 || fd == fault - 8 || bk == fault - 12 || bk == fault - 8;
    bins += fmt::format("{}{}:{:08X}/{:08X}{}", bins.empty() ? "" : " ", index * 8, fd, bk, names ? "<-fault" : "");
  }
  REXLOG_ERROR("[narutorise] crash report: small bins (size:fd/bk): {}", bins.empty() ? "none" : bins);
  for (uint32_t offset : {8u, 12u}) {
    const uint32_t chunk = fault - offset;
    uint32_t foot = 0, head = 0, fd = 0, bk = 0;
    if (Read32(base, chunk, &foot) && Read32(base, chunk + 4, &head) && Read32(base, chunk + 8, &fd) &&
        Read32(base, chunk + 12, &bk))
      REXLOG_ERROR("[narutorise] crash report: chunk at fault-{} 0x{:08X}: prev_foot 0x{:08X} head 0x{:08X} fd 0x{:08X} "
                   "bk 0x{:08X}",
                   offset, chunk, foot, head, fd, bk);
    else
      REXLOG_ERROR("[narutorise] crash report: chunk at fault-{} 0x{:08X}: no memory", offset, chunk);
  }
}

void Report(const char* kind, uint32_t code, uint64_t host_fault) {
  auto* state = rex::runtime::ThreadState::Get();
  if (!state || !state->context() || !state->memory()) {
    REXLOG_ERROR("[narutorise] crash report: {} (code 0x{:08X}) on a host thread without a guest context", kind,
                 code);
    return;
  }
  PPCContext* ctx = state->context();
  const uint8_t* base = state->memory()->virtual_membase();
  const uint64_t host_base = reinterpret_cast<uint64_t>(base);
  const std::string fault = host_fault >= host_base && host_fault < host_base + kGuestArena
                                ? fmt::format("guest 0x{:08X}", uint32_t(host_fault - host_base))
                                : fmt::format("host 0x{:016X}", host_fault);
  // The thread's stack from its PCR (r13): +0x70 the base (high), +0x74 the end (low).
  uint32_t stack_base = 0, stack_end = 0;
  Read32(base, ctx->r13.u32 + 0x70, &stack_base);
  Read32(base, ctx->r13.u32 + 0x74, &stack_end);
  REXLOG_ERROR(
      "[narutorise] crash report: {} of {} (code 0x{:08X}) on guest thread 0x{:X}: r1 0x{:08X}, lr 0x{:08X}, "
      "r3 0x{:08X}, r4 0x{:08X}, stack 0x{:08X}..0x{:08X}",
      kind, fault, code, state->thread_id(), ctx->r1.u32, uint32_t(ctx->lr), ctx->r3.u32, ctx->r4.u32, stack_end,
      stack_base);
  Run runs[kMaxRuns]{};
  uint32_t run_count = 0, frames = 0, beyond = 0;
  auto add = [&](uint32_t address) {
    ++frames;
    if (run_count && runs[run_count - 1].address == address) {
      ++runs[run_count - 1].count;
      return;
    }
    if (run_count < kMaxRuns) runs[run_count++] = {address, 1};
    else ++beyond;
  };
  add(uint32_t(ctx->lr));  // The return address of the function running.
  uint32_t sp = ctx->r1.u32;
  for (uint32_t i = 0; i < kMaxFrames; ++i) {
    uint32_t next = 0, address = 0;
    if (!Read32(base, sp, &next) || next <= sp || next - sp > 0x100000 || (next & 7)) break;
    if (!Read32(base, next - 8, &address)) break;
    add(address);
    sp = next;
  }
  std::string text;
  for (uint32_t i = 0; i < run_count; ++i) {
    if (!text.empty()) text += ' ';
    text += runs[i].count > 1 ? fmt::format("{:08X}x{}", runs[i].address, runs[i].count)
                              : fmt::format("{:08X}", runs[i].address);
  }
  if (beyond) text += fmt::format(" ...+{}", beyond);
  REXLOG_ERROR("[narutorise] crash report: {} return addresses from the top (lr, then the saved ones): {}", frames,
               text);
  if (host_fault >= host_base && host_fault < host_base + kGuestArena)
    HeapReport(base, uint32_t(host_fault - host_base));
  rex::FlushLogging();
}

// After the SDK's handler (first in the order), which resumes the faults it
// handles (the write watch, the MMIO windows) and leaves the rest to the
// search, so the report sees only those. A fault of the report itself passes.
LONG WINAPI Handler(EXCEPTION_POINTERS* pointers) {
  if (!pointers || !pointers->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
  const char* kind = nullptr;
  switch (record.ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
      kind = record.ExceptionInformation[0] == 1 ? "write" : record.ExceptionInformation[0] == 0 ? "read" : "access";
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      kind = "illegal instruction";
      break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      kind = "integer division by zero";
      break;
    default:  // A host stack overflow has no stack for the report.
      return EXCEPTION_CONTINUE_SEARCH;
  }
  if (reporting || reports.fetch_add(1) >= kMaxReports) return EXCEPTION_CONTINUE_SEARCH;
  reporting = true;
  Report(kind, record.ExceptionCode,
         record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ? uint64_t(record.ExceptionInformation[1]) : 0);
  reporting = false;
  return EXCEPTION_CONTINUE_SEARCH;
}

// The chain a transform update follows (8266AE68): an object not yet activated
// (bit 0 of its flags clear) with the link flag 0x400000 recurses into the
// object at [[object + 28] + 8] + 4 before anything else, with the same test
// there. A cycle in that chain never ends (the stack overflow of the known
// problems). Walks it first, up to kChainMax objects; true on a repeat or a
// chain that long, with the objects and their flags in `text`.
bool LinkCycle(const uint8_t* base, uint32_t object, std::string* text) {
  uint32_t chain[kChainMax], flags[kChainMax];
  uint32_t n = 0, current = object;
  while (current) {
    uint32_t repeat = n;
    for (uint32_t i = 0; i < n; ++i)
      if (chain[i] == current) repeat = i;
    if (repeat < n || n == kChainMax) {
      for (uint32_t i = 0; i < n; ++i) *text += fmt::format("{}{:08X}({:08X})", i ? " -> " : "", chain[i], flags[i]);
      *text += repeat < n ? fmt::format(" -> {:08X} again", current) : fmt::format(" -> {:08X} ...", current);
      return true;
    }
    uint32_t f = 0, extension = 0, link = 0, next = 0;
    if (!Read32(base, current, &f)) return false;
    chain[n] = current;
    flags[n++] = f;
    if ((f & 1) || !(f & 0x400000)) return false;
    if (!Read32(base, current + 28, &extension) || !extension) return false;
    if (!Read32(base, extension + 8, &link) || !link) return false;
    if (!Read32(base, link + 4, &next)) return false;
    current = next;
  }
  return false;
}
}  // namespace

void Install() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  AddVectoredExceptionHandler(0, Handler);  // Last.
}
}  // namespace narutorise::crash_report

REX_HOOK_RAW(sub_8266AE68) {
  std::string text;
  if (narutorise::crash_report::LinkCycle(base, ctx.r3.u32, &text)) {
    static std::atomic<uint32_t> cycles{0};
    const uint32_t n = cycles.fetch_add(1) + 1;
    if (n <= 16) REXLOG_ERROR("[narutorise] object link cycle at 8266AE68 (#{}), update skipped: {}", n, text);
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8266AE68(ctx, base);
}

// The world loads, with the world's id: to place a crash in the return from a fight.
REX_HOOK_RAW(sub_8264C358) {
  REXLOG_INFO("[narutorise] Loading world: 0x{:08X}", ctx.r4.u32);
  __imp__sub_8264C358(ctx, base);
}
