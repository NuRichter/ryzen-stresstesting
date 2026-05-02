#pragma once
// binary_stack.hpp
// lock-free binary stack -- stores workload descriptors as packed 64-bit words

#include <atomic>
#include <bit>
#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

namespace rsb {

// Workload descriptor packed into 64 bits:
//   [63:56] type   (8 bit)  -- StressType enum
//   [55:48] core   (8 bit)  -- target logical CPU
//   [47:32] iter   (16 bit) -- inner loop count  x1024
//   [31: 0] seed   (32 bit) -- PRNG seed for data pattern
struct WorkItem {
    uint8_t  type;
    uint8_t  core;
    uint16_t iter;
    uint32_t seed;

    [[nodiscard]] constexpr uint64_t pack() const noexcept {
        return (static_cast<uint64_t>(type) << 56)
             | (static_cast<uint64_t>(core) << 48)
             | (static_cast<uint64_t>(iter) << 32)
             | static_cast<uint64_t>(seed);
    }

    [[nodiscard]] static constexpr WorkItem unpack(uint64_t w) noexcept {
        return {
            .type = static_cast<uint8_t>(w >> 56),
            .core = static_cast<uint8_t>((w >> 48) & 0xFF),
            .iter = static_cast<uint16_t>((w >> 32) & 0xFFFF),
            .seed = static_cast<uint32_t>(w & 0xFFFFFFFF),
        };
    }
};

// Michael-Scott inspired lock-free stack over a fixed node pool.
// Avoids heap allocation after init -- all nodes pre-allocated.
class BinaryStack {
public:
    explicit BinaryStack(std::size_t capacity)
        : pool_(capacity), free_head_(0)
    {
        // chain pool into free list
        for (std::size_t i = 0; i + 1 < capacity; ++i)
            pool_[i].next.store(static_cast<int64_t>(i + 1), std::memory_order_relaxed);
        pool_[capacity - 1].next.store(-1, std::memory_order_relaxed);
        top_.store(-1, std::memory_order_relaxed);
    }

    // returns false if pool exhausted
    bool push(WorkItem item) noexcept {
        int64_t idx = alloc_node();
        if (idx < 0) return false;

        pool_[idx].value.store(item.pack(), std::memory_order_relaxed);

        int64_t old_top = top_.load(std::memory_order_relaxed);
        do {
            pool_[idx].next.store(old_top, std::memory_order_relaxed);
        } while (!top_.compare_exchange_weak(
            old_top, idx,
            std::memory_order_release,
            std::memory_order_relaxed));

        return true;
    }

    std::optional<WorkItem> pop() noexcept {
        int64_t idx = top_.load(std::memory_order_acquire);
        while (idx >= 0) {
            int64_t next = pool_[idx].next.load(std::memory_order_relaxed);
            if (top_.compare_exchange_weak(
                    idx, next,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                WorkItem item = WorkItem::unpack(
                    pool_[idx].value.load(std::memory_order_relaxed));
                free_node(idx);
                return item;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool empty() const noexcept {
        return top_.load(std::memory_order_acquire) < 0;
    }

private:
    struct Node {
        alignas(64) std::atomic<uint64_t> value{0};
        std::atomic<int64_t>             next{-1};
    };

    std::vector<Node>    pool_;
    std::atomic<int64_t> top_;
    std::atomic<int64_t> free_head_;

    int64_t alloc_node() noexcept {
        int64_t idx = free_head_.load(std::memory_order_acquire);
        while (idx >= 0) {
            int64_t next = pool_[idx].next.load(std::memory_order_relaxed);
            if (free_head_.compare_exchange_weak(
                    idx, next,
                    std::memory_order_acquire,
                    std::memory_order_relaxed))
                return idx;
        }
        return -1;
    }

    void free_node(int64_t idx) noexcept {
        int64_t head = free_head_.load(std::memory_order_relaxed);
        do {
            pool_[idx].next.store(head, std::memory_order_relaxed);
        } while (!free_head_.compare_exchange_weak(
            head, idx,
            std::memory_order_release,
            std::memory_order_relaxed));
    }
};

} // namespace rsb
