// tui.cpp
#include "tui.hpp"

#include <curses.h>
#include <cmath>
#include <cstdio>

namespace rsb {

Tui::Tui() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_GREEN,   -1);
        init_pair(2, COLOR_YELLOW,  -1);
        init_pair(3, COLOR_RED,     -1);
        init_pair(4, COLOR_CYAN,    -1);
        init_pair(5, COLOR_MAGENTA, -1);
        init_pair(6, COLOR_WHITE,   -1);
    }
}

Tui::~Tui() { endwin(); }

static int temp_color(double t) {
    if (t < 65.0) return 1; // green
    if (t < 80.0) return 2; // yellow
    return 3;               // red
}

static int ops_bar(uint64_t ops, uint64_t max_ops, int width) {
    if (max_ops == 0) return 0;
    return static_cast<int>(static_cast<double>(ops)
                            / static_cast<double>(max_ops) * width);
}

void Tui::draw(const TuiState& s) {
    erase();
    draw_header(s);
    draw_cores(s, 5);
    draw_footer(s, 5 + s.n_cores + 2);
    refresh();
}

void Tui::draw_header(const TuiState& s) {
    attron(COLOR_PAIR(5) | A_BOLD);
    mvprintw(0, 0, " ryzen-socket-breach // 9600X AM5 stress");
    attroff(COLOR_PAIR(5) | A_BOLD);

    attron(COLOR_PAIR(4));
    mvprintw(1, 0, " Tdie: ");
    attroff(COLOR_PAIR(4));
    attron(COLOR_PAIR(temp_color(s.tdie)) | A_BOLD);
    printw("%.1f C", s.tdie);
    attroff(COLOR_PAIR(temp_color(s.tdie)) | A_BOLD);

    attron(COLOR_PAIR(4));
    printw("  Tctl: ");
    attroff(COLOR_PAIR(4));
    printw("%.1f C", s.tctl);

    if (s.socket_w > 0.0) {
        attron(COLOR_PAIR(4));
        printw("  PKG: ");
        attroff(COLOR_PAIR(4));
        attron(COLOR_PAIR(temp_color(s.socket_w / 2.0)) | A_BOLD);
        printw("%.1f W", s.socket_w);
        attroff(A_BOLD);
    }

    mvprintw(2, 0, " Ops: %llu  Elapsed: %llu/%llu s",
             (unsigned long long)s.total_ops,
             (unsigned long long)s.elapsed_sec,
             (unsigned long long)s.duration_sec);

    mvhline(3, 0, ACS_HLINE, COLS);
    attron(COLOR_PAIR(6) | A_BOLD);
    mvprintw(4, 0, " %-4s  %-8s  %-6s  %-6s  %-5s  %s",
             "CPU", "ops", "IPC", "miss%", "bmiss", "load");
    attroff(COLOR_PAIR(6) | A_BOLD);
}

void Tui::draw_cores(const TuiState& s, int row) {
    uint64_t max_ops = 1;
    for (auto v : s.per_core_ops) if (v > max_ops) max_ops = v;

    for (int c = 0; c < s.n_cores && c < (int)s.per_core_ops.size(); ++c) {
        uint64_t ops = s.per_core_ops[c];
        double ipc  = (c < (int)s.perf.size()) ? s.perf[c].ipc()       : 0.0;
        double miss = (c < (int)s.perf.size()) ? s.perf[c].miss_rate() : 0.0;
        uint64_t bm = (c < (int)s.perf.size()) ? s.perf[c].branch_misses : 0;

        mvprintw(row + c, 0, " C%-2d  %-8llu  %-6.2f  %-6.2f  %-5llu  ",
                 c,
                 (unsigned long long)ops,
                 ipc, miss,
                 (unsigned long long)bm);

        int bar_w = COLS - 46;
        if (bar_w < 4) bar_w = 4;
        int filled = ops_bar(ops, max_ops, bar_w);

        attron(COLOR_PAIR(1));
        for (int i = 0; i < filled; ++i) addch('#');
        attroff(COLOR_PAIR(1));
        for (int i = filled; i < bar_w; ++i) addch('.');
    }
}

void Tui::draw_footer(const TuiState& s, int row) {
    mvhline(row, 0, ACS_HLINE, COLS);
    if (s.done) {
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(row + 1, 0, " done. total ops: %llu  avg %.1f ops/s",
                 (unsigned long long)s.total_ops,
                 s.elapsed_sec > 0
                     ? static_cast<double>(s.total_ops) / s.elapsed_sec
                     : 0.0);
        attroff(COLOR_PAIR(1) | A_BOLD);
    } else {
        mvprintw(row + 1, 0, " q = quit early");
    }
}

} // namespace rsb
