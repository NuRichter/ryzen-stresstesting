#pragma once
// tui.hpp
// ncurses real-time dashboard for rsb

#include "perf_counter.hpp"
#include "thermal.hpp"
#include <cstdint>
#include <vector>

namespace rsb {

struct TuiState {
    double   tdie          = 0.0;
    double   tctl          = 0.0;
    double   socket_w      = 0.0;
    uint64_t total_ops     = 0;
    uint64_t elapsed_sec   = 0;
    uint64_t duration_sec  = 0;
    int      n_cores       = 12;
    std::vector<uint64_t> per_core_ops;
    std::vector<PerfSample> perf;
    bool     done          = false;
};

class Tui {
public:
    Tui();
    ~Tui();

    void draw(const TuiState& state);

private:
    void draw_header(const TuiState& s);
    void draw_cores (const TuiState& s, int row);
    void draw_footer(const TuiState& s, int row);
};

} // namespace rsb
