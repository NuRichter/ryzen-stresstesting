// perf_counter.cpp
#include "perf_counter.hpp"

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace rsb {

static long perf_event_open(perf_event_attr* attr, pid_t pid, int cpu,
                             int group_fd, unsigned long flags) {
    return syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

int PerfCounter::open_event(uint32_t type, uint64_t config, int group_fd) {
    perf_event_attr attr{};
    attr.type           = type;
    attr.size           = sizeof(attr);
    attr.config         = config;
    attr.disabled       = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv     = 1;
    if (group_fd >= 0) attr.read_format = PERF_FORMAT_GROUP;

    long fd = perf_event_open(&attr, -1, cpu_, group_fd, PERF_FLAG_FD_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("perf_event_open: " + std::string(strerror(errno))
                                 + " (try: sudo sysctl kernel.perf_event_paranoid=0)");
    return static_cast<int>(fd);
}

PerfCounter::PerfCounter(int cpu) : cpu_(cpu) {
    using P = perf_event_attr;
    // open group leader first
    fds_[0] = open_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,       -1);
    fds_[1] = open_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,     fds_[0]);
    fds_[2] = open_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES, fds_[0]);
    fds_[3] = open_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES,     fds_[0]);
    fds_[4] = open_event(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES,    fds_[0]);
}

PerfCounter::~PerfCounter() {
    for (int fd : fds_) if (fd >= 0) close(fd);
}

void PerfCounter::reset() {
    ioctl(fds_[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
}

void PerfCounter::enable() {
    ioctl(fds_[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
}

void PerfCounter::disable() {
    ioctl(fds_[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
}

PerfSample PerfCounter::read() {
    PerfSample s{};
    ::read(fds_[0], &s.cycles,        sizeof(uint64_t));
    ::read(fds_[1], &s.instructions,  sizeof(uint64_t));
    ::read(fds_[2], &s.cache_refs,    sizeof(uint64_t));
    ::read(fds_[3], &s.cache_misses,  sizeof(uint64_t));
    ::read(fds_[4], &s.branch_misses, sizeof(uint64_t));
    return s;
}

double PerfSample::ipc() const noexcept {
    if (cycles == 0) return 0.0;
    return static_cast<double>(instructions) / static_cast<double>(cycles);
}

double PerfSample::miss_rate() const noexcept {
    if (cache_refs == 0) return 0.0;
    return 100.0 * static_cast<double>(cache_misses)
                 / static_cast<double>(cache_refs);
}

std::vector<PerfCounter> open_all_cores(int n_cores) {
    std::vector<PerfCounter> counters;
    counters.reserve(n_cores);
    for (int i = 0; i < n_cores; ++i)
        counters.emplace_back(i);
    return counters;
}

} // namespace rsb
