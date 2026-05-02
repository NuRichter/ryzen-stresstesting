#!/usr/bin/env bash
# scripts/arch_setup.sh
# sets up build deps on Arch Linux for ryzen-socket-breach
set -euo pipefail

echo ":: installing deps..."
sudo pacman -Sy --needed \
    base-devel cmake ninja \
    ncurses \
    linux-headers \
    ryzenadj

# allow perf counters without root (optional)
echo ":: relaxing perf_event_paranoid (session only)..."
sudo sysctl -w kernel.perf_event_paranoid=0

echo ":: building..."
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build -j"$(nproc)"

echo ""
echo ":: built -> ./build/rsb"
echo ":: run:    sudo ./build/rsb -t 60 --ppt"
echo "::         ./build/rsb -t 30 --no-tui"
