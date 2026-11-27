#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vac3 {

inline constexpr std::size_t kIecBurstBytes = 6144; // 1536 stereo S16 frames
inline constexpr std::uint16_t kIecAc3DataType = 0x0001;
inline constexpr std::uint16_t kIecPauseDataType = 0x0003;

inline void put_le16(std::uint8_t* dst, std::uint16_t value) noexcept {
    dst[0] = static_cast<std::uint8_t>(value & 0xff);
    dst[1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

// Build an IEC 61937 AC-3 burst. IEC 61937 payload words are byte-swapped
// relative to the raw FFmpeg AC-3 packet on little-endian PCM carriers.
inline bool make_ac3_burst(const std::uint8_t* encoded, std::size_t encodedBytes,
                           std::uint8_t* burst, std::size_t burstBytes = kIecBurstBytes) noexcept {
    if (!encoded || !burst || burstBytes != kIecBurstBytes || encodedBytes + 8 > burstBytes ||
        encodedBytes > 8191) return false;

    std::memset(burst, 0, burstBytes);
    put_le16(burst + 0, 0xF872);
    put_le16(burst + 2, 0x4E1F);
    put_le16(burst + 4, kIecAc3DataType);
    put_le16(burst + 6, static_cast<std::uint16_t>(encodedBytes * 8));

    std::size_t i = 0;
    for (; i + 1 < encodedBytes; i += 2) {
        burst[8 + i] = encoded[i + 1];
        burst[8 + i + 1] = encoded[i];
    }
    if (i < encodedBytes) {
        burst[8 + i] = 0;
        burst[8 + i + 1] = encoded[i];
    }
    return true;
}

inline void make_pause_burst(std::uint8_t* burst) noexcept {
    std::memset(burst, 0, kIecBurstBytes);
    put_le16(burst + 0, 0xF872);
    put_le16(burst + 2, 0x4E1F);
    put_le16(burst + 4, kIecPauseDataType);
    put_le16(burst + 6, 0);
}

} // namespace vac3
