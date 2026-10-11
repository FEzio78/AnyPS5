#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace AgcDriver::GuestMemory {

inline std::uint64_t ChangedByteMask(const std::byte* current, const std::byte* original, std::size_t bytes) {
    std::uint64_t mask = 0;
    std::size_t at = 0;
#if defined(__SSE2__) || defined(_M_X64)
    for (; at + 16 <= bytes; at += 16) {
        const auto a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(current + at));
        const auto b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(original + at));
        const auto changed = static_cast<std::uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(a, b))) ^ 0xffffu;
        mask |= static_cast<std::uint64_t>(changed) << at;
    }
#endif
    for (; at < bytes; ++at) if (current[at] != original[at]) mask |= std::uint64_t{1} << at;
    return mask;
}

template <class Store>
void ForEachChangedRun(std::span<const std::byte> current, std::span<const std::byte> original, Store&& store) {
    const auto size = current.size();
    std::size_t pending = size;
    const auto finish = [&](std::size_t end) {
        if (pending != size) store(pending, end - pending);
        pending = size;
    };
    for (std::size_t at = 0; at < size;) {
        const auto blockBytes = std::min<std::size_t>(256, size - at);
        if (std::memcmp(current.data() + at, original.data() + at, blockBytes) == 0) {
            finish(at);
            at += blockBytes;
            continue;
        }
        const auto end = at + blockBytes;
        while (at < end) {
            const auto bytes = std::min<std::size_t>(64, end - at);
            auto mask = ChangedByteMask(current.data() + at, original.data() + at, bytes);
            std::size_t cursor = 0;
            while (mask != 0) {
                const auto first = static_cast<std::size_t>(std::countr_zero(mask));
                if (first > cursor) finish(at + cursor);
                if (pending == size) pending = at + first;
                const auto length = static_cast<std::size_t>(std::countr_one(mask >> first));
                cursor = first + length;
                mask = cursor == 64 ? 0 : mask & (~std::uint64_t{0} << cursor);
            }
            if (cursor < bytes) finish(at + cursor);
            at += bytes;
        }
    }
    finish(size);
}

}
