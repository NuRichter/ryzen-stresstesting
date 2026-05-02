// stress_engine.cpp
#include "stress_engine.hpp"
#include "thermal.hpp"

#include <immintrin.h>
#include <pthread.h>
#include <sched.h>

#include <chrono>
#include <cstring>
#include <random>
#include <stdexcept>

#if HAVE_RYZENADJ
#  include <ryzenadj.h>
#endif

namespace rsb {

// ---------------------------------------------------------------------------
// Kernels
// ---------------------------------------------------------------------------

void kernel_integer(uint32_t seed, uint16_t iters) noexcept {
    uint64_t a = seed, b = seed ^ 0xDEADBEEF'CAFEF00D;
    const uint64_t n = static_cast<uint64_t>(iters) * 1024;
    for (uint64_t i = 0; i < n; ++i) {
        a = a * 6364136223846793005ULL + 1442695040888963407ULL;
        b ^= __builtin_bswap64(a);
        b  = (b << 17) | (b >> 47);
        a += b * 0xBF58476D1CE4E5B9ULL;
    }
    // prevent dead-code elimination
    asm volatile("" : : "r"(a), "r"(b) : "memory");
}

void kernel_float_avx(uint32_t seed, uint16_t iters) noexcept {
#ifdef __AVX2__
    float s = static_cast<float>(seed) * 1e-9f + 1.0f;
    __m256 acc0 = _mm256_set1_ps(s);
    __m256 acc1 = _mm256_set1_ps(s * 1.00001f);
    __m256 acc2 = _mm256_set1_ps(s * 1.00002f);
    __m256 acc3 = _mm256_set1_ps(s * 1.00003f);
    const __m256 mul = _mm256_set1_ps(1.0000001f);
    const __m256 add = _mm256_set1_ps(0.0000001f);
    const uint64_t n = static_cast<uint64_t>(iters) * 256;
    for (uint64_t i = 0; i < n; ++i) {
        acc0 = _mm256_fmadd_ps(acc0, mul, add);
        acc1 = _mm256_fmadd_ps(acc1, mul, add);
        acc2 = _mm256_fmadd_ps(acc2, mul, add);
        acc3 = _mm256_fmadd_ps(acc3, mul, add);
    }
    asm volatile("" : : "x"(acc0), "x"(acc1), "x"(acc2), "x"(acc3) : "memory");
#else
    kernel_integer(seed, iters); // fallback
#endif
}

void kernel_cache_l1(uint32_t seed, uint16_t iters) noexcept {
    alignas(64) uint64_t buf[512]; // 4 KB -- fits in L1
    std::memset(buf, 0, sizeof(buf));
    buf[0] = seed;
    const uint64_t n = static_cast<uint64_t>(iters) * 512;
    uint64_t idx = seed & 511;
    for (uint64_t i = 0; i < n; ++i) {
        buf[idx] ^= buf[(idx + 7) & 511] + i;
        idx = buf[idx] & 511;
    }
    asm volatile("" : : "r"(buf[0]) : "memory");
}

void kernel_cache_l2(uint32_t seed, uint16_t iters) noexcept {
    alignas(64) static thread_local uint64_t buf[65536]; // 512 KB
    buf[0] = seed;
    const uint64_t mask = 65535;
    const uint64_t n    = static_cast<uint64_t>(iters) * 1024;
    uint64_t idx = seed & mask;
    for (uint64_t i = 0; i < n; ++i) {
        buf[idx] ^= buf[(idx + 31) & mask] * 0x9E3779B97F4A7C15ULL;
        idx = buf[idx] & mask;
    }
    asm volatile("" : : "r"(buf[0]) : "memory");
}

void kernel_branch(uint32_t seed, uint16_t iters) noexcept {
    uint64_t x = seed, hits = 0;
    const uint64_t n = static_cast<uint64_t>(iters) * 1024;
    for (uint64_t i = 0; i < n; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        // hard-to-predict branches
        if ((x & 0xFF) < 128) hits += x & 1;
        else                  hits -= x & 1;
        if (__builtin_popcountll(x) & 1) x *= 0xBF58476D1CE4E5B9ULL;
    }
    asm volatile("" : : "r"(hits) : "memory");
}

static void dispatch_workload(const WorkItem& item) noexcept {
    switch (static_cast<StressType>(item.type)) {
        case StressType::Integer:  kernel_integer(item.seed, item.iter);  break;
        case StressType::FloatAVX: kernel_float_avx(item.seed, item.iter); break;
        case StressType::CacheL1:  kernel_cache_l1(item.seed, item.iter); break;
        case StressType::CacheL2:  kernel_cache_l2(item.seed, item.iter); break;
        case StressType::Branch:   kernel_branch(item.seed, item.iter);   break;
        case StressType::Combined:
            kernel_integer(item.seed, item.iter);
            kernel_float_avx(item.seed, item.iter);
            kernel_cache_l1(item.seed, item.iter);
            kernel_branch(item.seed, item.iter);
            break;
    }
}

// ---------------------------------------------------------------------------
// StressEngine
// ---------------------------------------------------------------------------

StressEngine::StressEngine(SocketConfig cfg)
    : cfg_(std::move(cfg))
    , per_core_ops_(cfg_.logical_cores)
{
    for (auto& a : per_core_ops_) a.store(0, std::memory_order_relaxed);
}

StressEngine::~StressEngine() { stop(); wait(); }

void StressEngine::pin_to_core(int core_id) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0)
        throw std::runtime_error("pthread_setaffinity_np failed for core "
                                 + std::to_string(core_id));
}

void StressEngine::apply_power_limits() {
#if HAVE_RYZENADJ
    ryzenadj_t* adj = init_ryzenadj();
    if (!adj) return;
    set_stapm_limit(adj, cfg_.ppt_limit_mw);
    set_slow_limit(adj,  cfg_.ppt_limit_mw);
    set_fast_limit(adj,  cfg_.ppt_limit_mw);
    set_tctl_temp(adj,   95);
    cleanup_ryzenadj(adj);
#endif
}

void StressEngine::start(ProgressCb cb) {
    if (running_.load()) return;
    progress_cb_ = std::move(cb);
    running_.store(true, std::memory_order_release);

    if (cfg_.push_ppt) apply_power_limits();

    auto stack = std::make_shared<BinaryStack>(cfg_.queue_depth);

    for (int c = 0; c < cfg_.logical_cores; ++c) {
        workers_.emplace_back([this, c, stack] {
            worker_loop(c, *stack);
        });
    }

    dispatcher_ = std::thread([this, stack] {
        dispatcher_loop(*stack);
    });
}

void StressEngine::stop() {
    running_.store(false, std::memory_order_release);
}

void StressEngine::wait() {
    if (dispatcher_.joinable()) dispatcher_.join();
    for (auto& t : workers_) if (t.joinable()) t.join();
    workers_.clear();
}

void StressEngine::worker_loop(int core_id, BinaryStack& stack) {
    pin_to_core(core_id);

    std::mt19937_64 rng(core_id ^ 0xFEEDC0DE);
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::seconds(cfg_.duration_sec);

    while (running_.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline)
    {
        if (auto item = stack.pop()) {
            dispatch_workload(*item);
            per_core_ops_[core_id].fetch_add(1, std::memory_order_relaxed);
            total_ops_.fetch_add(1, std::memory_order_relaxed);

            if (progress_cb_) {
                double temp = Thermal::read_tdie();
                progress_cb_(core_id,
                             per_core_ops_[core_id].load(std::memory_order_relaxed),
                             temp);
            }
        } else {
            sched_yield();
        }
    }
    running_.store(false, std::memory_order_release);
}

void StressEngine::dispatcher_loop(BinaryStack& stack) {
    const int n_types = 6;
    std::mt19937 rng(0xC0FFEE42);
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::seconds(cfg_.duration_sec + 2);

    while (running_.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline)
    {
        if (stack.empty()) {
            for (int c = 0; c < cfg_.logical_cores; ++c) {
                WorkItem item{
                    .type = static_cast<uint8_t>(rng() % n_types),
                    .core = static_cast<uint8_t>(c),
                    .iter = static_cast<uint16_t>(16 + rng() % 48),
                    .seed = rng(),
                };
                stack.push(item);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
}

uint64_t StressEngine::total_ops()  const noexcept { return total_ops_.load(); }
bool     StressEngine::is_running() const noexcept { return running_.load(); }

} // namespace rsb
