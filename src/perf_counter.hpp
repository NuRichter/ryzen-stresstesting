#pragma once
// perf_counter.hpp
// thin wrapper over perf_event_open(2) for per-core HW counters

#include <cstdint>
#include <string_view>
#include <vector>

namespace rsb {

enum class PerfEvent : uint32_t {
    Cycles         = 0,
    Instructions   = 1,
    CacheRefs      = 2,
    CacheMisses    = 3,
    BranchMisses   = 4,
};

struct PerfSample {
    uint64_t cycles;
    uint64_t instructions;
    uint64_t cache_refs;
    uint64_t cache_misses;
    uint64_t branch_misses;

    [[nodiscard]] double ipc()       const noexcept;
    [[nodiscard]] double miss_rate() const noexcept; // cache miss %
};

class PerfCounter {
public:
    explicit PerfCounter(int cpu); // -1 = any
    ~PerfCounter();

    PerfCounter(const PerfCounter&)            = delete;
    PerfCounter& operator=(const PerfCounter&) = delete;

    void   reset();
    void   enable();
    void   disable();
    PerfSample read();

private:
    int open_event(uint32_t type, uint64_t config, int group_fd);

    int cpu_;
    int fds_[5]{-1, -1, -1, -1, -1};
};

// convenience: open counters for all logical CPUs
std::vector<PerfCounter> open_all_cores(int n_cores);

} // namespace rsb
