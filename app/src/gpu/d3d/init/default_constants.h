#pragma once
#include <array>
#include <cstdint>

namespace gpu {
struct DefaultConstant { uint32_t address, bytes; std::array<uint8_t,32> data; };
// Bytes of the XEX image (read with IDA). Verify the current image bytes
// before any live projection.
inline constexpr std::array<DefaultConstant,7> kDefaultConstants{{
    {0x8208DB90,4,{0x41,0x80,0,0}}, {0x8208C6A4,4,{0,0,0,0}},
    {0x8208DB34,4,{0x41,0,0,0}}, {0x82082728,4,{0x3B,0x80,0x80,0x81}},
    {0x8208DB38,4,{0x42,0,0,0}}, {0x8208DF04,4,{0xC1,0,0,0}},
    {0x8208B3C0,32,{0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,2,
                   0,0,0,3,0,0,0,3,0,0,0,3,0,0,0,4}}}};
} // namespace gpu
