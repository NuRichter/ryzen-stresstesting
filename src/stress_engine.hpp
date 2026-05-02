#pragma once
// stress_engine.hpp
// dispatches CPU stress workloads via BinaryStack, pinned per-core

#include "binary_stack.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace rsb {

enum class StressType : uint8_t {
    Integer   = 0,   // ALU hammering -- mul/div chains
    FloatAVX  = 1,   // YMM FMA blast
    CacheL1   = 2,   // 32 KB ping-pong
    CacheL2   = 3,   // 512 KB stride
    Branch    = 4,   // branch predictor torture
    Combined  = 5,   // all of the above interleaved
};

struct SocketConfig {
    int      logical_cores   = 12;  // 9600X: 6C/12T
    int      queue_depth     = 512;
    uint32_t duration_sec    = 30;
    bool     push_ppt        = false; // requires ryzenadj + root
    uint32_t ppt_limit_mw    = 95000; // 95W -- above stock 65W PPT
    uint32_t tdc_limit_ma    = 75000;
    uint32_t edc_limit_ma    = 95000;
};

using ProgressCb = std::function<void(int core, uint64_t ops, double temp_c)>;

class StressEngine {
public:
    explicit StressEngine(SocketConfig cfg);
    ~StressEngine();

    // non-copyable
    StressEngine(const StressEngine&)            = delete;
    StressEngine& operator=(const StressEngine&) = delete;

    void start(ProgressCb cb = nullptr);
    void stop();
    void wait();

    [[nodiscard]] uint64_t total_ops()   const noexcept;
    [[nodiscard]] bool     is_running()  const noexcept;

private:
    void worker_loop(int core_id, BinaryStack& stack);
    void dispatcher_loop(BinaryStack& stack);
    void apply_power_limits();

    static void pin_to_core(int core_id);

    SocketConfig              cfg_;
    ProgressCb                progress_cb_;
    std::vector<std::thread>  workers_;
    std::thread               dispatcher_;
    std::atomic<bool>         running_{false};
    std::atomic<uint64_t>     total_ops_{0};
    std::vector<std::atomic<uint64_t>> per_core_ops_;
};

// workload kernels -- in stress_engine.cpp
void kernel_integer (uint32_t seed, uint16_t iters) noexcept;
void kernel_float_avx(uint32_t seed, uint16_t iters) noexcept;
void kernel_cache_l1(uint32_t seed, uint16_t iters) noexcept;
void kernel_cache_l2(uint32_t seed, uint16_t iters) noexcept;
void kernel_branch  (uint32_t seed, uint16_t iters) noexcept;

} // namespace rsb
