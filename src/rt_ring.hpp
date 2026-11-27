#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace vac3 {

// Single-producer/single-consumer fixed-capacity ring. The producer never
// waits for the consumer and both sides avoid locks and dynamic allocation.
template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
public:
    std::size_t size() const noexcept {
        const auto h = head_.load(std::memory_order_acquire);
        const auto t = tail_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(h - t);
    }

    std::size_t free_space() const noexcept { return Capacity - size(); }

    std::size_t write(const T* src, std::size_t count) noexcept {
        if (!src || count == 0) return 0;
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_acquire);
        const auto used = static_cast<std::size_t>(h - t);
        const auto n = std::min(count, Capacity - std::min(used, Capacity));
        for (std::size_t i = 0; i < n; ++i) data_[(h + i) & (Capacity - 1)] = src[i];
        head_.store(h + n, std::memory_order_release);
        return n;
    }

    std::size_t read(T* dst, std::size_t count) noexcept {
        if (!dst || count == 0) return 0;
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);
        const auto n = std::min(count, static_cast<std::size_t>(h - t));
        for (std::size_t i = 0; i < n; ++i) dst[i] = data_[(t + i) & (Capacity - 1)];
        tail_.store(t + n, std::memory_order_release);
        return n;
    }

private:
    alignas(64) std::array<T, Capacity> data_{};
    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
};

// The AC-3 worker publishes complete 6144-byte IEC 61937 bursts. The PipeWire
// realtime callback copies one burst into its private current buffer, so a slot
// can be safely reused as soon as pop() returns.
template <std::size_t BurstBytes, std::size_t SlotCount>
class BurstRing {
    static_assert((SlotCount & (SlotCount - 1)) == 0, "SlotCount must be a power of two");
public:
    bool push(const std::uint8_t* burst) noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_acquire);
        if (h - t >= SlotCount) return false;
        std::memcpy(slots_[h & (SlotCount - 1)].data(), burst, BurstBytes);
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    bool pop(std::uint8_t* burst) noexcept {
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);
        if (t == h) return false;
        std::memcpy(burst, slots_[t & (SlotCount - 1)].data(), BurstBytes);
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    std::size_t size() const noexcept {
        return static_cast<std::size_t>(head_.load(std::memory_order_acquire) -
                                        tail_.load(std::memory_order_acquire));
    }

private:
    alignas(64) std::array<std::array<std::uint8_t, BurstBytes>, SlotCount> slots_{};
    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
};

} // namespace vac3
