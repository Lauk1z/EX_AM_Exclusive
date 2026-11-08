#pragma once

#include <bit>
#include <cstdint>
#include <cstring>

namespace ammod::asio {

struct SampleLayout {
    unsigned bytes{}, bits{};
    bool floating{}, bigEndian{}, rightAligned{};
};

inline SampleLayout Layout(long type) noexcept {
    const bool little = type >= 16;
    const long base = little ? type - 16 : type;
    switch (base) {
    case 0: return {2, 16, false, !little, false};
    case 1: return {3, 24, false, !little, false};
    case 2: return {4, 32, false, !little, false};
    case 3: return {4, 24, true, !little, false};
    case 4: return {8, 32, true, !little, false};
    case 8: return {4, 16, false, !little, true};
    case 11: return {4, 24, false, !little, true};
    default: return {};
    }
}

inline bool PackChannel(const std::int32_t* stereo, void* output, std::uint32_t frames,
                        unsigned channel, SampleLayout layout) noexcept {
    if (!stereo || !output || channel > 1 || layout.bytes == 0) return false;
    auto* destination = static_cast<unsigned char*>(output);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto sample = stereo[static_cast<std::size_t>(frame) * 2 + channel];
        std::uint64_t value{};
        if (layout.floating) {
            const double normalized = static_cast<double>(sample) * 0x1.0p-31;
            value = layout.bytes == 8 ? std::bit_cast<std::uint64_t>(normalized)
                : std::bit_cast<std::uint32_t>(static_cast<float>(normalized));
        } else if (layout.rightAligned) {
            value = static_cast<std::uint32_t>(sample >> (32 - layout.bits));
        } else {
            value = static_cast<std::uint32_t>(sample) >> (32 - layout.bits);
        }
        for (unsigned byte = 0; byte < layout.bytes; ++byte) {
            const auto shift = 8 * (layout.bigEndian ? layout.bytes - byte - 1 : byte);
            destination[static_cast<std::size_t>(frame) * layout.bytes + byte] =
                static_cast<unsigned char>(value >> shift);
        }
    }
    return true;
}

inline long SelectBufferFrames(long minimum, long maximum, long preferred,
                               long granularity, std::uint32_t requested) noexcept {
    if (minimum <= 0 || maximum < minimum || maximum > 1048576 ||
        preferred < minimum || preferred > maximum || granularity < -1) return 0;
    if (granularity == 0) return preferred;
    const auto target = static_cast<long>(requested);
    long best = preferred;
    std::uint64_t distance = UINT64_MAX;
    for (std::int64_t candidate = granularity == -1 ? 1 : minimum;
         candidate <= maximum;
         candidate = granularity == -1 ? candidate * 2 : candidate + granularity) {
        if (candidate < minimum) continue;
        const auto delta = static_cast<std::uint64_t>(candidate > target ? candidate - target : target - candidate);
        if (delta < distance) { best = static_cast<long>(candidate); distance = delta; }
    }
    return distance == UINT64_MAX ? 0 : best;
}

}
