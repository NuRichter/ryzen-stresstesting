// main.cpp
// ryzen-socket-breach -- 9600X AM5 stress tester
// Arch Linux only. run as root if --ppt is used.

#include "binary_stack.hpp"
#include "perf_counter.hpp"
#include "stress_engine.hpp"
#include "thermal.hpp"
#include "tui.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static volatile sig_atomic_t g_quit = 0;

static void on_signal(int) { g_quit = 1; }

static void usage(const char* argv0) {
    std::cerr
        << "usage: " << argv0 << " [options]\n"
        << "  -t <sec>    duration (default: 30)\n"
        << "  -c <n>      logical cores (default: 12)\n"
        << "  -q <n>      stack queue depth (default: 512)\n"
        << "  --ppt       push PPT/TDC/EDC via ryzenadj (root required)\n"
        << "  --no-tui    plain stdout mode\n"
        << "  -h          this help\n";
}

int main(int argc, char** argv) {
    rsb::SocketConfig cfg;
    cfg.logical_cores  = 12;
    cfg.queue_depth    = 512;
    cfg.duration_sec   = 30;
    cfg.push_ppt       = false;
    bool use_tui = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { usage(argv[0]); return 0; }
        else if (arg == "-t" && i + 1 < argc) cfg.duration_sec   = std::stoul(argv[++i]);
        else if (arg == "-c" && i + 1 < argc) cfg.logical_cores  = std::stoi(argv[++i]);
        else if (arg == "-q" && i + 1 < argc) cfg.queue_depth    = std::stoi(argv[++i]);
        else if (arg == "--ppt")              cfg.push_ppt       = true;
        else if (arg == "--no-tui")           use_tui            = false;
        else { std::cerr << "unknown arg: " << arg << '\n'; usage(argv[0]); return 1; }
    }

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    // perf counters
    std::vector<rsb::PerfCounter> perf;
    try {
        perf = rsb::open_all_cores(cfg.logical_cores);
        for (auto& p : perf) { p.reset(); p.enable(); }
    } catch (const std::exception& e) {
        std::cerr << "[warn] perf counters unavailable: " << e.what() << '\n';
    }

    rsb::StressEngine engine(cfg);
    std::vector<uint64_t> per_core_snap(cfg.logical_cores, 0);

    engine.start(nullptr);

    auto t_start = std::chrono::steady_clock::now();

    if (use_tui) {
        rsb::Tui tui;
        rsb::TuiState state;
        state.n_cores       = cfg.logical_cores;
        state.duration_sec  = cfg.duration_sec;
        state.per_core_ops.resize(cfg.logical_cores, 0);

        while (!g_quit && engine.is_running()) {
            int ch = getch();
            if (ch == 'q' || ch == 'Q') break;

            auto thermal = rsb::Thermal::read_all();
            state.tdie       = thermal.tdie;
            state.tctl       = thermal.tctl;
            state.socket_w   = thermal.socket_w;
            state.total_ops  = engine.total_ops();
            state.elapsed_sec = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - t_start).count());
            state.done = !engine.is_running();

            if (!perf.empty()) {
                state.perf.clear();
                for (auto& p : perf) state.perf.push_back(p.read());
            }

            tui.draw(state);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        // final draw
        rsb::TuiState final_state = {};
        final_state.total_ops    = engine.total_ops();
        final_state.elapsed_sec  = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - t_start).count());
        final_state.done         = true;
        final_state.n_cores      = cfg.logical_cores;
        final_state.duration_sec = cfg.duration_sec;
        final_state.per_core_ops.resize(cfg.logical_cores, 0);
        tui.draw(final_state);
        std::this_thread::sleep_for(std::chrono::seconds(2));

    } else {
        // plain mode
        while (!g_quit && engine.is_running()) {
            auto thermal = rsb::Thermal::read_all();
            uint64_t ops = engine.total_ops();
            uint64_t sec = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - t_start).count());
            std::printf("[%3lus] ops=%-8llu  Tdie=%.1f C  Tctl=%.1f C  PKG=%.1f W\n",
                        (unsigned long)sec,
                        (unsigned long long)ops,
                        thermal.tdie, thermal.tctl, thermal.socket_w);
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    engine.stop();
    engine.wait();

    for (auto& p : perf) p.disable();

    std::printf("\ndone. total ops: %llu\n",
                (unsigned long long)engine.total_ops());
    return 0;
}
